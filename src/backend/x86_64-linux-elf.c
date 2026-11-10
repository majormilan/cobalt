/* src/backend/x86_64-linux-elf.c */
#include "backend.h"
#include "ir.h"
#include "promote.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>

// Write a NUL-terminated data literal as a list of byte values, which is safe
// for any character (NASM strings would need escaping for quotes)
static void emit_bytes(FILE *f, const char *s) {
    fprintf(f, "db ");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) fprintf(f, "%u, ", *p);
    fprintf(f, "0\n");
}

// Assembly label for a COBOL name or IR label. COBOL names are
// case-insensitive and may clash with assembler reserved words (DS, AX, ...),
// so labels are upper-cased and prefixed with "c_"; '-' becomes '_'.
static char *sanitize_label_name(const char *name) {
    if (!name) return NULL;
    char *sanitized = malloc(strlen(name) + 3);
    if (!sanitized) return NULL;
    char *d = sanitized;
    if (name[0] == '$') {
        // Compiler-generated label: "$if_end_3" becomes "L_if_end_3"
        sprintf(sanitized, "L_%s", name + 1);
        return sanitized;
    }
    if (name[0] == '%') {
        // Record storage "%r3+12" becomes "D_3+12"; a file control block
        // "%f2" becomes "F_2"
        sprintf(sanitized, "%c_%s", name[1] == 'f' ? 'F' : 'D', name + 2);
        return sanitized;
    }
    *d++ = 'c';
    *d++ = '_';
    for (const char *p = name; *p; p++) *d++ = (*p == '-') ? '_' : (char)toupper((unsigned char)*p);
    *d = '\0';
    return sanitized;
}

// Every routine in the runtime contract (runtime/routines.def)
static const char *const runtime_routines[] = {
#define RUNTIME_ROUTINE(name) #name,
#include "routines.def"
#undef RUNTIME_ROUTINE
};

// Stack and register discipline of generated code (see runtime/ABI.md):
// - Procedures are laid out in source order and entered by jumps. Each has a
//   return slot R_<n>: PERFORM saves the slot of the range's last procedure,
//   points it at the return label and jumps to the first procedure; the end
//   of every procedure jumps through its slot when it is set.
// - rsp is 16-byte aligned everywhere (_start is entered aligned and every
//   push uses a 16-byte slot), so every runtime call is made aligned.
// - rbx holds the PERFORM ... TIMES counter. Each loop saves the previous
//   value in a stack slot, so loops nest.
// - The numeric stack machine (IR_NUM_*) keeps values on the machine stack in
//   16-byte slots, so rsp stays aligned while values are pending.
// - cobrt_tgt_err collects errors (overflow, division by zero) while one
//   receiver's value is computed; cobrt_stmt_err collects size errors of the
//   whole statement for ON SIZE ERROR.

// Load the address of a field operand into reg. Operands are static
// ("%r3+12", literal labels) or held in an address slot ("*A_5[+offset]").
static void emit_addr(FILE *f, const char *reg, const char *name) {
    if (name[0] == '*') {
        char *rest;
        long slot = strtol(name + 3, &rest, 10);    // after "*A_"
        fprintf(f, "    mov %s, [rel A_%ld]\n", reg, slot);
        if (*rest) fprintf(f, "    add %s, 0%s\n", reg, rest);
        return;
    }
    char *label = sanitize_label_name(name);
    fprintf(f, "    lea %s, [%s]\n", reg, label);
    free(label);
}

// Load a field length into reg; -(k+1) means the length is in slot N_<k>
static void emit_size(FILE *f, const char *reg, int size) {
    if (size >= 0) fprintf(f, "    mov %s, %d\n", reg, size);
    else fprintf(f, "    mov %s, [rel N_%d]\n", reg, -size - 1);
}

// Numeric stack: push rax / pop into reg, one 16-byte slot per value
static void emit_push(FILE *f) {
    fprintf(f, "    sub rsp, 16\n");
    fprintf(f, "    mov [rsp], rax\n");
}

static void emit_pop(FILE *f, const char *reg) {
    fprintf(f, "    mov %s, [rsp]\n", reg);
    fprintf(f, "    add rsp, 16\n");
}

// ── Binary items inline (runtime/ABI.md section 5) ──────────────────────────
// COMP-5 (native) and COMP (big-endian) items of 2, 4 or 8 bytes are loaded
// and stored without a runtime call. A store checks that the value fits the
// item (its PICTURE digits for COMP, its bytes for COMP-5 and for COMP with
// IR_NUM_NOTRUNC) and otherwise falls back to runtime_num_store, which
// truncates and reports the size error.

static bool inline_binary(const IrInstr *i) {
    return (i->usage_type == USAGE_COMP || i->usage_type == USAGE_COMP5) &&
           (i->size == 2 || i->size == 4 || i->size == 8);
}

// Register names by width: [0] 64-bit, [1] 32-bit, [2] 16-bit
typedef struct { const char *q, *d, *w; } RegName;

// COMP-3 items of 1 to 10 bytes go through the lean packed routines
static bool inline_packed(const IrInstr *i) {
    return i->usage_type == USAGE_COMP3 && i->size >= 1 && i->size <= 10;
}

// R = the item at mem
static void emit_binary_load(FILE *f, const IrInstr *i, const char *mem, RegName r) {
    bool sign = i->flags & IR_NUM_SIGNED;
    bool big = i->usage_type == USAGE_COMP;
    switch (i->size) {
        case 8:
            fprintf(f, "    mov %s, %s\n", r.q, mem);
            if (big) fprintf(f, "    bswap %s\n", r.q);
            break;
        case 4:
            if (!big) {
                if (sign) fprintf(f, "    movsxd %s, dword %s\n", r.q, mem);
                else fprintf(f, "    mov %s, %s\n", r.d, mem);
                break;
            }
            fprintf(f, "    mov %s, %s\n", r.d, mem);
            fprintf(f, "    bswap %s\n", r.d);
            if (sign) fprintf(f, "    movsxd %s, %s\n", r.q, r.d);
            break;
        case 2:
            if (!big) {
                fprintf(f, "    %s %s, word %s\n", sign ? "movsx" : "movzx", sign ? r.q : r.d, mem);
                break;
            }
            fprintf(f, "    movzx eax, word %s\n", mem);
            fprintf(f, "    rol ax, 8\n");
            fprintf(f, "    %s %s, ax\n", sign ? "movsx" : "movzx", sign ? r.q : r.d);
            break;
    }
}

// Largest magnitude a value can have: values are signed 64-bit, so anything
// at or above 2^63 means "unknown"
#define BOUND_UNKNOWN (1ULL << 63)

static unsigned long long bound_add(unsigned long long a, unsigned long long b) {
    return (a >= BOUND_UNKNOWN || b >= BOUND_UNKNOWN || a + b >= BOUND_UNKNOWN) ? BOUND_UNKNOWN : a + b;
}

static unsigned long long bound_mul(unsigned long long a, unsigned long long b) {
    if (a >= BOUND_UNKNOWN || b >= BOUND_UNKNOWN) return BOUND_UNKNOWN;
    unsigned __int128 p = (unsigned __int128)a * b;
    return p >= BOUND_UNKNOWN ? BOUND_UNKNOWN : (unsigned long long)p;
}

static unsigned long long pow10_ull(int n) {
    unsigned long long p = 1;
    for (int k = 0; k < n && k < 19; k++) p *= 10;
    return n > 19 ? BOUND_UNKNOWN : p;
}

// Store V at mem when it fits; else jump to slow. bound is the largest
// magnitude V can have, nonneg tells that V is known not to be negative: a
// value that provably fits needs no check.
static void emit_binary_check(FILE *f, const IrInstr *i, RegName v, const char *slow,
                              unsigned long long bound, bool nonneg) {
    bool sign = i->flags & IR_NUM_SIGNED;
    bool big = i->usage_type == USAGE_COMP;
    if (big && !(i->flags & IR_NUM_NOTRUNC) && i->digits >= 1 && i->digits <= 18) {
        // limited by the PICTURE: |value| < 10^digits (unsigned: 0 <= value)
        long long limit = (long long)pow10_ull(i->digits);
        if (sign && bound < (unsigned long long)limit) {
            // fits
        } else if (sign) {
            fprintf(f, "    mov rax, %s\n", v.q);
            fprintf(f, "    neg rax\n");
            fprintf(f, "    cmovs rax, %s\n", v.q);
            fprintf(f, "    mov rcx, %lld\n", limit);
            fprintf(f, "    cmp rax, rcx\n");
            fprintf(f, "    jae %s\n", slow);
        } else if (!(nonneg && bound < (unsigned long long)limit)) {
            fprintf(f, "    mov rcx, %lld\n", limit);
            fprintf(f, "    cmp %s, rcx\n", v.q);
            fprintf(f, "    jae %s\n", slow);
        }
    } else if (sign && i->size < 8) {
        // limited by the bytes: the value survives sign extension
        if (bound >= (i->size == 4 ? 0x80000000ULL : 0x8000ULL)) {
            if (i->size == 4) fprintf(f, "    movsxd rax, %s\n", v.d);
            else fprintf(f, "    movsx rax, %s\n", v.w);
            fprintf(f, "    cmp rax, %s\n", v.q);
            fprintf(f, "    jne %s\n", slow);
        }
    } else if (!sign) {
        bool fits = nonneg && (i->size == 8 || bound < (i->size == 4 ? 0x100000000ULL : 0x10000ULL));
        if (fits) {
            // fits
        } else if (i->size == 8) {
            fprintf(f, "    test %s, %s\n", v.q, v.q);
            fprintf(f, "    js %s\n", slow);
        } else {
            if (i->size == 4) fprintf(f, "    mov eax, %s\n", v.d);
            else fprintf(f, "    movzx eax, %s\n", v.w);
            fprintf(f, "    cmp rax, %s\n", v.q);
            fprintf(f, "    jne %s\n", slow);
        }
    }
}

// Write V (known to fit) to the binary item at mem
static void emit_binary_write(FILE *f, const IrInstr *i, RegName v, const char *mem) {
    bool big = i->usage_type == USAGE_COMP;
    switch (i->size) {
        case 8:
            if (big) {
                fprintf(f, "    mov rax, %s\n", v.q);
                fprintf(f, "    bswap rax\n");
                fprintf(f, "    mov %s, rax\n", mem);
            } else {
                fprintf(f, "    mov %s, %s\n", mem, v.q);
            }
            break;
        case 4:
            if (big) {
                fprintf(f, "    mov eax, %s\n", v.d);
                fprintf(f, "    bswap eax\n");
                fprintf(f, "    mov %s, eax\n", mem);
            } else {
                fprintf(f, "    mov %s, %s\n", mem, v.d);
            }
            break;
        case 2:
            if (big) {
                fprintf(f, "    mov eax, %s\n", v.d);
                fprintf(f, "    rol ax, 8\n");
                fprintf(f, "    mov %s, ax\n", mem);
            } else {
                fprintf(f, "    mov %s, %s\n", mem, v.w);
            }
            break;
    }
}

static void emit_binary_store(FILE *f, const IrInstr *i, RegName v, const char *mem,
                              const char *slow, unsigned long long bound, bool nonneg) {
    emit_binary_check(f, i, v, slow, bound, nonneg);
    emit_binary_write(f, i, v, mem);
}

// Memory operand of a field: "[D_3+12]", or through its address slot
// ("[rdi+12]" after loading rdi)
static void field_mem(FILE *f, const char *name, char *out, size_t n) {
    if (name[0] == '*') {
        char *rest;
        long slot = strtol(name + 3, &rest, 10);
        fprintf(f, "    mov rdi, [rel A_%ld]\n", slot);
        snprintf(out, n, "[rdi%s]", *rest ? rest : "");
        return;
    }
    char *label = sanitize_label_name(name);
    snprintf(out, n, "[%s]", label);
    free(label);
}

// ── Virtual numeric stack ───────────────────────────────────────────────────
// The IR's numeric stack machine is executed at compile time as far as
// possible: the top entries of the stack are constants or live in the
// callee-saved registers r12 to r15 (which survive runtime calls), and only
// entries below them are on the machine stack in 16-byte slots. Before any
// instruction the virtual stack does not handle (labels, jumps, other
// statements), all entries are flushed to the machine stack, so the stack
// discipline of runtime/ABI.md holds at every label.
//
// Each entry carries a bound on its magnitude, which removes overflow checks
// that can never fire (S9(9) + S9(17) cannot overflow 64 bits).
//
// The error bytes are tracked too: while cobrt_tgt_err is known to be zero,
// stores neither test nor clear it.

#define VS_NREGS 4
static const RegName vs_regs[VS_NREGS + 5] = {
    {"r12", "r12d", "r12w"}, {"r13", "r13d", "r13w"},
    {"r14", "r14d", "r14w"}, {"r15", "r15d", "r15w"},
    {"r9", "r9d", "r9w"},   // scratch, not allocated
    // the registers of promoted fields: an entry may name one while the
    // field is not stored (it is copied out first)
    {"rbp", "ebp", "bp"}, {"rbx", "ebx", "bx"}, {"r15", "r15d", "r15w"}, {"r14", "r14d", "r14w"},
};
#define VS_R9 VS_NREGS
#define VS_PROMO0 (VS_NREGS + 1)

static FILE *cold;              // out-of-line code, emitted after the program
static int ovf_store = -1;      // store whose overflow block an addition jumps to
static bool skip_neg;           // the next IR_NUM_NEG was folded into a subtraction

typedef struct {
    bool is_const;
    int reg;                    // index into vs_regs when not constant
    long long imm;              // the constant
    unsigned long long bound;   // |value| <= bound (BOUND_UNKNOWN: anything)
    bool nonneg;                // value >= 0 known
} VEntry;

#define VS_MAX 64
static VEntry vs[VS_MAX];
static int vs_n;
static bool vs_reserved[VS_NREGS];  // held by promoted fields
static bool tgt_zero;           // cobrt_tgt_err is known to be 0
static bool val_zero;           // cobrt_val_err is known to be 0

static void vs_push_entry(FILE *f, const VEntry *e) {
    if (e->is_const) {
        fprintf(f, "    mov rax, %lld\n", e->imm);
        emit_push(f);
    } else {
        fprintf(f, "    sub rsp, 16\n");
        fprintf(f, "    mov [rsp], %s\n", vs_regs[e->reg].q);
    }
}

// Move the bottom entry to the machine stack
static void vs_spill_bottom(FILE *f) {
    vs_push_entry(f, &vs[0]);
    memmove(vs, vs + 1, (size_t)(vs_n - 1) * sizeof(VEntry));
    vs_n--;
}

static void vs_flush(FILE *f) {
    for (int k = 0; k < vs_n; k++) vs_push_entry(f, &vs[k]);
    vs_n = 0;
}

static int vs_avoid = -1;       // a popped entry's register vs_alloc must not reuse

static bool vs_reg_used(int r) {
    for (int k = 0; k < vs_n; k++)
        if (!vs[k].is_const && vs[k].reg == r) return true;
    return false;
}

// A free register, spilling the bottom entries when all are in use
static int vs_alloc(FILE *f) {
    for (;;) {
        for (int r = 0; r < VS_NREGS; r++)
            if (!vs_reserved[r] && r != vs_avoid && !vs_reg_used(r)) return r;
        vs_spill_bottom(f);
    }
}

// A register of the pool for an entry that will be changed in place
// (constants and promoted registers are copied); avoid: a register to keep
static int vs_own(FILE *f, const VEntry *e, int avoid) {
    if (!e->is_const && e->reg < VS_NREGS) return e->reg;
    vs_avoid = avoid;
    int r = vs_alloc(f);
    vs_avoid = -1;
    if (e->is_const) fprintf(f, "    mov %s, %lld\n", vs_regs[r].q, e->imm);
    else fprintf(f, "    mov %s, %s\n", vs_regs[r].q, vs_regs[e->reg].q);
    return r;
}

// Copy out every entry that names register reg (before reg changes)
static void vs_unalias(FILE *f, int reg) {
    for (;;) {
        int k = 0;
        while (k < vs_n && (vs[k].is_const || vs[k].reg != reg)) k++;
        if (k == vs_n) return;
        int r = vs_alloc(f);
        // the allocation may have spilled entries: find it again
        for (k = 0; k < vs_n && (vs[k].is_const || vs[k].reg != reg); k++) {}
        if (k == vs_n) return;
        fprintf(f, "    mov %s, %s\n", vs_regs[r].q, vs_regs[reg].q);
        vs[k].reg = r;
    }
}

static void vs_push_reg(FILE *f, int r, unsigned long long bound, bool nonneg) {
    if (vs_n == VS_MAX) vs_spill_bottom(f);
    vs[vs_n++] = (VEntry){ .is_const = false, .reg = r, .bound = bound, .nonneg = nonneg };
}

static void vs_push_const(FILE *f, long long imm) {
    if (vs_n == VS_MAX) vs_spill_bottom(f);
    unsigned long long mag = imm < 0 ? 0ULL - (unsigned long long)imm : (unsigned long long)imm;
    vs[vs_n++] = (VEntry){ .is_const = true, .imm = imm, .bound = mag, .nonneg = imm >= 0 };
}

// Make sure the top n entries are virtual, moving entries from the machine
// stack into registers below the virtual ones
static void vs_ensure(FILE *f, int n) {
    while (vs_n < n) {
        int r = vs_alloc(f);
        fprintf(f, "    mov %s, [rsp]\n", vs_regs[r].q);
        fprintf(f, "    add rsp, 16\n");
        memmove(vs + 1, vs, (size_t)vs_n * sizeof(VEntry));
        vs[0] = (VEntry){ .is_const = false, .reg = r, .bound = BOUND_UNKNOWN, .nonneg = false };
        vs_n++;
    }
}

static VEntry vs_pop(void) {
    return vs[--vs_n];
}

// Load an entry into a named register
static void vs_to(FILE *f, const VEntry *e, const char *reg) {
    if (e->is_const) fprintf(f, "    mov %s, %lld\n", reg, e->imm);
    else fprintf(f, "    mov %s, %s\n", reg, vs_regs[e->reg].q);
}

static bool fits_imm32(long long v) {
    return v >= -2147483648LL && v <= 2147483647LL;
}

// Operand text for the second operand of an ALU instruction: a register or
// a 32-bit immediate; larger constants go through rcx
static const char *vs_operand(FILE *f, const VEntry *e, char *buf, size_t n) {
    if (!e->is_const) return vs_regs[e->reg].q;
    if (fits_imm32(e->imm)) {
        snprintf(buf, n, "%lld", e->imm);
        return buf;
    }
    fprintf(f, "    mov rcx, %lld\n", e->imm);
    return "rcx";
}


// ── Register promotion (src/ir/promote.c) ───────────────────────────────────
// In a promoted loop, up to four numeric fields live in callee-saved
// registers (rbp, rbx, r15, r14; the virtual stack keeps r12 and r13 and
// whatever is left). Before the loop head they are loaded; a store in the
// loop checks that the value fits the field and only sets the register and
// the field's dirty byte; a value that does not fit takes the normal store
// path to memory and is loaded again. Every jump out of the loop goes
// through an exit block that writes the dirty fields back. rbx may hold a
// PERFORM TIMES counter, so it is saved on the stack around the loop.

#define PROMO_NREGS 4
static const RegName promo_regs[PROMO_NREGS] = {
    {"rbp", "ebp", "bp"}, {"rbx", "ebx", "bx"}, {"r15", "r15d", "r15w"}, {"r14", "r14d", "r14w"},
};
static const int promo_pool[PROMO_NREGS] = { -1, -1, 3, 2 };   // virtual stack register taken

static bool promo_accept(const IrInstr *load, const IrInstr *store) {
    bool digits_ok = !store || (store->digits >= 1 && store->digits <= 18);
    if (inline_binary(load)) return true;
    if (inline_packed(load)) return digits_ok;
    if (load->usage_type == USAGE_DISPLAY && load->size >= 1 && load->size <= 18) return digits_ok;
    return false;
}

// Largest magnitude a load of the field can give
static unsigned long long field_bound(const IrInstr *i) {
    if (inline_binary(i))
        return i->size == 8 ? BOUND_UNKNOWN : (1ULL << (i->size * 8 - ((i->flags & IR_NUM_SIGNED) ? 1 : 0)));
    // DISPLAY: at most one digit per byte (anything else counts as 0);
    // COMP-3: two nibbles per byte but the sign, each at most 15 for
    // invalid data, so below 2 * 10^nibbles
    if (i->size > 0 && i->usage_type == USAGE_DISPLAY && i->size <= 18) return pow10_ull(i->size) - 1;
    if (i->size > 0 && i->usage_type == USAGE_COMP3 && 2 * i->size - 1 <= 18) return 2 * pow10_ull(2 * i->size - 1);
    return BOUND_UNKNOWN;
}

// dst = the field's value in memory
static void field_load_to(FILE *f, const IrInstr *i, RegName dst) {
    if (inline_binary(i)) {
        char mem[96];
        field_mem(f, i->var_name, mem, sizeof(mem));
        emit_binary_load(f, i, mem, dst);
        return;
    }
    emit_addr(f, "rdi", i->var_name);
    if (inline_packed(i)) {
        fprintf(f, "    mov esi, %d\n", i->size);
        fprintf(f, "    call runtime_packed_load\n");
    } else {
        emit_size(f, "rsi", i->size);
        fprintf(f, "    mov rdx, %d\n", i->usage_type);
        fprintf(f, "    mov rcx, %d\n", i->flags);
        fprintf(f, "    call runtime_num_load\n");
    }
    fprintf(f, "    mov %s, rax\n", dst.q);
}

// Write src, known to fit, to the field (st: a store into it, for digits)
static void field_write_from(FILE *f, const IrInstr *st, RegName src) {
    if (inline_binary(st)) {
        char mem[96];
        field_mem(f, st->var_name, mem, sizeof(mem));
        emit_binary_write(f, st, src, mem);
        return;
    }
    emit_addr(f, "rdi", st->var_name);
    if (inline_packed(st)) {
        fprintf(f, "    mov esi, %d\n", st->size);
        fprintf(f, "    mov rdx, %s\n", src.q);
        fprintf(f, "    mov ecx, %d\n", st->flags);
        fprintf(f, "    call runtime_packed_store\n");
        return;
    }
    fprintf(f, "    mov r9, %s\n", src.q);
    emit_size(f, "rsi", st->size);
    fprintf(f, "    mov rdx, %d\n", st->digits);
    fprintf(f, "    mov rcx, %d\n", st->usage_type);
    fprintf(f, "    mov r8, %d\n", st->flags & ~IR_NUM_KEEP);
    fprintf(f, "    call runtime_num_store\n");
}

// Jump to slow unless V fits the field of store st exactly (the value a
// load gives back)
static void field_fits_check(FILE *f, const IrInstr *st, RegName v, const char *slow,
                             unsigned long long bound, bool nonneg) {
    if (inline_binary(st)) {
        emit_binary_check(f, st, v, slow, bound, nonneg);
        return;
    }
    long long limit = (long long)pow10_ull(st->digits);
    if (st->flags & IR_NUM_SIGNED) {
        if (bound < (unsigned long long)limit) return;
        fprintf(f, "    mov rax, %s\n", v.q);
        fprintf(f, "    neg rax\n");
        fprintf(f, "    cmovs rax, %s\n", v.q);
        fprintf(f, "    mov rcx, %lld\n", limit);
        fprintf(f, "    cmp rax, rcx\n");
        fprintf(f, "    jae %s\n", slow);
    } else {
        if (nonneg && bound < (unsigned long long)limit) return;
        fprintf(f, "    mov rcx, %lld\n", limit);
        fprintf(f, "    cmp %s, rcx\n", v.q);
        fprintf(f, "    jae %s\n", slow);   // negative values too: stored as their magnitude
    }
}

// Overflow while computing the value of store L: a size error, and the
// receiver keeps its value
static void emit_ovf_block(int L) {
    fprintf(cold, "L_store_ovf_%d:\n", L);
    fprintf(cold, "    or byte [rel cobrt_stmt_err], 1\n");
    fprintf(cold, "    jmp L_store_done_%d\n", L);
}

// The general store: runtime_num_store of entry v into the field of st
static void emit_num_store_call(FILE *f, const IrInstr *st, const VEntry *v) {
    vs_to(f, v, "r9");
    emit_addr(f, "rdi", st->var_name);
    emit_size(f, "rsi", st->size);
    fprintf(f, "    mov rdx, %d\n", st->digits);
    fprintf(f, "    mov rcx, %d\n", st->usage_type);
    fprintf(f, "    mov r8, %d\n", st->flags);
    fprintf(f, "    call runtime_num_store\n");
}

typedef struct {
    const PromoteLoop *loop;    // NULL: no promoted loop being emitted
    int id;                     // loop number, for labels
    int nf;                     // promoted fields
    const char **inside;        // labels in the loop (the head included)
    int ninside;
    const char **exits;         // jump targets outside the loop
    int nexits;
    bool head_tgt, head_val;    // error bytes known zero at the head
    // Loop rotation: a head test "a cmp b" on promoted fields and
    // constants is repeated at the bottom instead of jumping back to it
    const IrInstr *test;        // the head's IR_NUM_CMP_JUMP_IF_FALSE, or NULL
    char test_a[32], test_b[32];
    const char *test_op;
} Promo;

static Promo promo;

static int promo_field(const char *var_name) {
    if (!promo.loop || !var_name) return -1;
    for (int k = 0; k < promo.nf; k++)
        if (strcmp(promo.loop->fields[k].var_name, var_name) == 0) return k;
    return -1;
}

// A binary field written back from its register gets the same bytes as
// were loaded, so it needs no dirty byte; decimal fields may hold
// non-canonical data (a space, sign F) that only an actual store replaces
static bool promo_dirty(int k) {
    return promo.loop->fields[k].store && !inline_binary(promo.loop->fields[k].load);
}

static bool promo_uses_rbx(void) {
    return promo.nf > 1;
}

static const char *true_jump(const char *cmp_op) {
    if (!cmp_op || strcmp(cmp_op, "=") == 0) return "je";
    if (strcmp(cmp_op, "<>") == 0) return "jne";
    if (strcmp(cmp_op, ">")  == 0) return "jg";
    if (strcmp(cmp_op, "<")  == 0) return "jl";
    if (strcmp(cmp_op, ">=") == 0) return "jge";
    if (strcmp(cmp_op, "<=") == 0) return "jle";
    return "je";
}

// Operand text of a head test operand: a promoted register or a 32-bit
// constant
static bool rotation_operand(const IrInstr *i, char *out, size_t n, bool *is_const) {
    if (i->op == IR_NUM_CONST && fits_imm32(i->imm)) {
        snprintf(out, n, "%lld", i->imm);
        *is_const = true;
        return true;
    }
    int k = i->op == IR_NUM_LOAD ? promo_field(i->var_name) : -1;
    if (k < 0) return false;
    snprintf(out, n, "%s", promo_regs[k].q);
    *is_const = false;
    return true;
}

// Whether the loop starts with LOAD/CONST, LOAD/CONST, CMP_JUMP_IF_FALSE
// leaving the loop, on promoted fields: then it is rotated
static void promo_rotation_plan(void) {
    const IrInstr *a = promo.loop->head->next;
    const IrInstr *b = a ? a->next : NULL;
    const IrInstr *c = b ? b->next : NULL;
    promo.test = NULL;
    if (!c || c->op != IR_NUM_CMP_JUMP_IF_FALSE || !c->jump_target) return;
    for (int k = 0; k < promo.ninside; k++)
        if (strcmp(promo.inside[k], c->jump_target) == 0) return;
    bool ca, cb;
    if (!rotation_operand(a, promo.test_a, sizeof(promo.test_a), &ca) ||
        !rotation_operand(b, promo.test_b, sizeof(promo.test_b), &cb) || (ca && cb)) return;
    const char *op = c->cmp_op;
    if (ca) {
        // compare the other way round
        char t[32];
        memcpy(t, promo.test_a, sizeof(t));
        memcpy(promo.test_a, promo.test_b, sizeof(t));
        memcpy(promo.test_b, t, sizeof(t));
        if (!op || !strcmp(op, "=") || !strcmp(op, "<>")) {}
        else if (!strcmp(op, ">")) op = "<";
        else if (!strcmp(op, "<")) op = ">";
        else if (!strcmp(op, ">=")) op = "<=";
        else if (!strcmp(op, "<=")) op = ">=";
    }
    promo.test_op = op;
    promo.test = c;
}

// Before the loop head: load the fields, clear their dirty bytes
static void promo_begin(FILE *f, const PromoteLoop *loop, int id) {
    // The error bytes only carry information within one statement, and the
    // loop head is between statements: they are cleared before the loop and
    // again before the back jump when needed, so the head knows them as zero
    if (!tgt_zero) fprintf(f, "    mov byte [rel cobrt_tgt_err], 0\n");
    if (!val_zero) fprintf(f, "    mov byte [rel cobrt_val_err], 0\n");
    tgt_zero = val_zero = true;
    promo = (Promo){ .loop = loop, .id = id, .nf = loop->nfields < PROMO_NREGS ? loop->nfields : PROMO_NREGS,
                     .head_tgt = true, .head_val = true };
    for (const IrInstr *i = loop->head; i; i = i->next) {
        if (i->op == IR_LABEL) {
            promo.inside = realloc(promo.inside, sizeof(char *) * (size_t)(promo.ninside + 1));
            promo.inside[promo.ninside++] = i->var_name;
        }
        if (i == loop->back) break;
    }
    promo_rotation_plan();
    fprintf(f, "    ; loop with %d field(s) in registers\n", promo.nf);
    if (promo_uses_rbx()) {
        fprintf(f, "    sub rsp, 16\n");
        fprintf(f, "    mov [rsp], rbx\n");
    }
    for (int k = 0; k < promo.nf; k++) {
        if (promo_pool[k] >= 0) vs_reserved[promo_pool[k]] = true;
        field_load_to(f, loop->fields[k].load, promo_regs[k]);
        if (promo_dirty(k)) fprintf(f, "    mov byte [rel L_dirty_%d_%d], 0\n", id, k);
    }
}

// The label a jump in the loop goes to: an exit block for targets outside
static const char *promo_target(const char *target, char *buf, size_t n) {
    for (int k = 0; k < promo.ninside; k++)
        if (strcmp(promo.inside[k], target) == 0) return NULL;
    int e = 0;
    while (e < promo.nexits && strcmp(promo.exits[e], target) != 0) e++;
    if (e == promo.nexits) {
        promo.exits = realloc(promo.exits, sizeof(char *) * (size_t)(promo.nexits + 1));
        promo.exits[promo.nexits++] = target;
    }
    snprintf(buf, n, "L_loop_exit_%d_%d", promo.id, e);
    return buf;
}

// After the back jump: the exit blocks
static void promo_end(FILE *f) {
    for (int e = 0; e < promo.nexits; e++) {
        fprintf(f, "L_loop_exit_%d_%d:\n", promo.id, e);
        for (int k = 0; k < promo.nf; k++) {
            const PromotedField *pf = &promo.loop->fields[k];
            if (!pf->store) continue;
            if (promo_dirty(k)) {
                fprintf(f, "    cmp byte [rel L_dirty_%d_%d], 0\n", promo.id, k);
                fprintf(f, "    je L_loop_clean_%d_%d_%d\n", promo.id, e, k);
            }
            field_write_from(f, pf->store, promo_regs[k]);
            if (promo_dirty(k)) fprintf(f, "L_loop_clean_%d_%d_%d:\n", promo.id, e, k);
        }
        if (promo_uses_rbx()) {
            fprintf(f, "    mov rbx, [rsp]\n");
            fprintf(f, "    add rsp, 16\n");
        }
        char *t = sanitize_label_name(promo.exits[e]);
        fprintf(f, "    jmp %s\n", t);
        free(t);
    }
    for (int r = 0; r < VS_NREGS; r++) vs_reserved[r] = false;
    free(promo.inside);
    free(promo.exits);
    promo = (Promo){ 0 };
}

// Opcodes the virtual stack executes; any other one flushes it first
static bool vs_handles(IrOpcode op) {
    switch (op) {
        case IR_NUM_LOAD: case IR_NUM_CONST: case IR_NUM_RESCALE:
        case IR_NUM_ADD: case IR_NUM_SUB: case IR_NUM_MUL: case IR_NUM_DIV:
        case IR_NUM_NEG: case IR_NUM_DUP: case IR_NUM_DROP: case IR_NUM_SWAP:
        case IR_ERR_SAVE: case IR_ERR_RESTORE: case IR_NUM_STORE:
        case IR_NUM_CMP_JUMP_IF_FALSE: case IR_SIZE_ERR_CLEAR: case IR_NUM_DEEDIT:
        case IR_DATA_VAR: case IR_DATA_LIT: case IR_DATA_BSS: case IR_DATA_BYTES:
        case IR_DATA_FILE:
            return true;
        default:
            return false;
    }
}

// Opcodes after which the error bytes are still as the compiler tracked
// them (no other control flow joins, nothing writes them)
static bool keeps_err_state(IrOpcode op) {
    switch (op) {
        case IR_CALL_DISPLAY_STR: case IR_CALL_DISPLAY_VAR_STR: case IR_EMIT_NEWLINE:
        case IR_CALL_DISPLAY_FILL: case IR_CALL_MOVE_ALPHA: case IR_CALL_MOVE_NUM:
        case IR_CALL_MOVE_FILL: case IR_ADDR_SET: case IR_ADDR_ADD: case IR_LEN_SET:
        case IR_NUM_TO_TEXT: case IR_NUM_TRUNC: case IR_JUMP_IF_NO_SIZE_ERR:
        case IR_DATA_VAR: case IR_DATA_LIT: case IR_DATA_BSS: case IR_DATA_BYTES:
        case IR_DATA_FILE: case IR_CALL_REPLICATE: case IR_CALL_EDIT_ALNUM:
            return true;
        default:
            return false;
    }
}

// Inverted conditional jump: taken when (a cmp_op b) is false
static const char *false_jump(const char *cmp_op) {
    if (!cmp_op || strcmp(cmp_op, "=") == 0) return "jne";
    if (strcmp(cmp_op, "<>") == 0) return "je";
    if (strcmp(cmp_op, ">")  == 0) return "jle";
    if (strcmp(cmp_op, "<")  == 0) return "jge";
    if (strcmp(cmp_op, ">=") == 0) return "jl";
    if (strcmp(cmp_op, "<=") == 0) return "jg";
    return "jne";
}

int x86_64_linux_emit(IrProgram *ir, FILE *f, const TargetInfo *target) {
    int perform_counter = 0; // unique label suffix for PERFORM return points
    int store_counter = 0;   // unique label suffix for numeric stores

    fprintf(f, ";; COBALT generated x86-64 Linux assembly\n\n");
    PromotePlan plan = promote_plan(ir, promo_accept);
    IrFlow *flow = ir_flow_new(ir);
    int pos = -1;                   // position of the instruction being emitted
    char *cold_buf = NULL;
    size_t cold_len = 0;
    cold = open_memstream(&cold_buf, &cold_len);
    if (!cold) return 1;
    ovf_store = -1;
    skip_neg = false;

    for (size_t r = 0; r < sizeof(runtime_routines) / sizeof(runtime_routines[0]); r++) {
        fprintf(f, "extern %s\n", runtime_routines[r]);
    }
    fprintf(f, "\n"); // Add a single newline for readability if preferred

    fprintf(f, "section .bss\n");
    IrInstr *i = ir->head;
    while (i) {
        if (i->op == IR_DATA_VAR && i->var_name) {
            char *sanitized_name = sanitize_label_name(i->var_name);
            fprintf(f, "%s: resb %d\n", sanitized_name, i->size);
            free(sanitized_name);
        }
        i = i->next;
    }
    // One return slot per procedure
    int num_procs = 0;
    for (i = ir->head; i; i = i->next)
        if (i->op == IR_PROC_ENTRY && i->imm + 1 > num_procs) num_procs = (int)i->imm + 1;
    for (int p = 0; p < num_procs; p++) fprintf(f, "R_%d: resq 1\n", p);
    // Address and length slots of field references computed at run time
    int num_slots = 0;
    for (i = ir->head; i; i = i->next)
        if ((i->op == IR_ADDR_SET || i->op == IR_LEN_SET || i->op == IR_UNSTRING_SLOTS) && i->imm + 1 > num_slots)
            num_slots = (int)i->imm + 1;
    for (int k = 0; k < num_slots; k++) fprintf(f, "A_%d: resq 1\nN_%d: resq 1\n", k, k);
    for (i = ir->head; i; i = i->next) {
        if (i->op != IR_DATA_BSS) continue;
        char *label = sanitize_label_name(i->var_name);
        fprintf(f, "%s: resb %d\n", label, i->size > 0 ? i->size : 8);
        free(label);
    }
    for (int p = 0; p < plan.nloops; p++)
        for (int k = 0; k < plan.loops[p].nfields && k < PROMO_NREGS; k++)
            if (plan.loops[p].fields[k].store) fprintf(f, "L_dirty_%d_%d: resb 1\n", p, k);
    fprintf(f, "cobrt_io_status: resq 1\n");
    fprintf(f, "L_numtext: resb 40\n");
    fprintf(f, "L_numtext2: resb 40\n");
    fprintf(f, "cobrt_tgt_err: resb 1\n");
    fprintf(f, "cobrt_val_err: resb 1\n");
    fprintf(f, "cobrt_stmt_err: resb 1\n");
    fprintf(f, "\n");

    fprintf(f, "section .data\n");
    i = ir->head;
    while (i) {
        if (i->op == IR_DATA_LIT && i->lit_name && i->content) {
            char *sanitized_name = sanitize_label_name(i->lit_name);
            fprintf(f, "%s: ", sanitized_name);
            emit_bytes(f, i->content);
            free(sanitized_name);
        }
        i = i->next;
    }
    for (i = ir->head; i; i = i->next) {
        if (i->op != IR_DATA_BYTES) continue;
        char *label = sanitize_label_name(i->var_name);
        fprintf(f, "%s: db ", label);
        for (int k = 0; k < i->size; k++)
            fprintf(f, "%u%s", (unsigned char)i->content[k], k + 1 < i->size ? ", " : "\n");
        free(label);
    }

    // Target slots of alterable GO TO statements: an unaltered GO TO without
    // a procedure ends the run
    {
        static bool slot_done[1024];
        memset(slot_done, 0, sizeof(slot_done));
        for (i = ir->head; i; i = i->next) {
            if (i->op != IR_GOTO_ALTERABLE || i->imm < 0 || i->imm >= 1024 || slot_done[i->imm]) continue;
            slot_done[i->imm] = true;
            if (i->proc_end >= 0) fprintf(f, "align 8\nG_%lld: dq P_%d\n", i->imm, i->proc_end);
            else fprintf(f, "align 8\nG_%lld: dq runtime_exit\n", i->imm);
        }
    }

    // File control blocks (runtime/ABI.md section 7)
    for (i = ir->head; i; i = i->next) {
        if (i->op != IR_DATA_FILE) continue;
        char *fcb = sanitize_label_name(i->var_name);
        char *name = sanitize_label_name(i->lit_name);
        char *status = i->content ? sanitize_label_name(i->content) : NULL;
        fprintf(f, "align 8\n%s: dq 0, 0, %d, %d, %s, %s, 0, 0\n",
                fcb, i->size, i->flags, name, status ? status : "0");
        free(fcb);
        free(name);
        free(status);
    }
    fprintf(f, "\n");

    fprintf(f, "section .text\n");
    fprintf(f, "global %s\n", target->entry_symbol);
    fprintf(f, "%s:\n", target->entry_symbol);
    fprintf(f, "    mov rdi, rsp\n");  // entry stack: argc, argv, environment
    fprintf(f, "    mov rsi, %d\n", ir->runtime_flags);
    fprintf(f, "    call runtime_init\n\n");
    vs_n = 0;
    tgt_zero = val_zero = true;     // .bss starts zeroed

    i = ir->head;

    while (i) {
        char *sanitized_var_name = NULL;
        char *sanitized_lit_name = NULL;
        char *sanitized_jump = NULL;
        pos++;

        if (i->var_name) sanitized_var_name = sanitize_label_name(i->var_name);
        if (i->lit_name) sanitized_lit_name = sanitize_label_name(i->lit_name);
        if (i->jump_target) sanitized_jump = sanitize_label_name(i->jump_target);
        if (!vs_handles(i->op)) vs_flush(f);
        if (promo.loop && i == promo.loop->back) {
            if (promo.head_tgt && !tgt_zero) fprintf(f, "    mov byte [rel cobrt_tgt_err], 0\n");
            if (promo.head_val && !val_zero) fprintf(f, "    mov byte [rel cobrt_val_err], 0\n");
        }
        if (promo.loop && i->jump_target && i != promo.loop->back) {
            char buf[64];
            const char *t = promo_target(i->jump_target, buf, sizeof(buf));
            if (t) {
                free(sanitized_jump);
                sanitized_jump = strdup(t);
            }
        }

        switch (i->op) {
            case IR_CALL_DISPLAY_STR:
                emit_addr(f, "rdi", i->lit_name);
                fprintf(f, "    call runtime_display_string\n");
                break;
            case IR_CALL_DISPLAY_FILL:
                fprintf(f, "    mov rdi, %d\n", i->pad_char);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_display_fill\n");
                break;
            case IR_CALL_DISPLAY_VAR_STR:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_display_var_string\n");
                break;
            case IR_EMIT_NEWLINE:
                fprintf(f, "    call runtime_display_newline\n\n");
                break;
            case IR_CALL_MOVE_ALPHA:
            case IR_CALL_MOVE_NUM:
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                emit_size(f, "rdx", i->lit_len);
                emit_size(f, "rcx", i->size);
                fprintf(f, "    mov r8, %d\n", i->flags);  // JUSTIFIED RIGHT
                fprintf(f, "    call runtime_move_%s\n\n",
                        i->op == IR_CALL_MOVE_NUM ? "numeric" : "alphanum");
                break;
            case IR_CALL_MOVE_FILL:
                if (i->lit_name) {
                    emit_addr(f, "rdi", i->var_name);
                    emit_size(f, "rsi", i->size);
                    emit_addr(f, "rdx", i->lit_name);
                    fprintf(f, "    mov rcx, %d\n", i->lit_len);
                    fprintf(f, "    call runtime_move_pattern\n\n");
                    break;
                }
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    mov rsi, %d\n", i->pad_char);
                emit_size(f, "rdx", i->size);
                fprintf(f, "    mov rcx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_move_fill\n\n");
                break;
            case IR_CALL_ACCEPT:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    xor edx, edx\n");
                fprintf(f, "    xor ecx, ecx\n");
                fprintf(f, "    mov r8, %d\n", (int)(unsigned char)i->pad_char);
                fprintf(f, "    call runtime_accept\n");
                break;
            case IR_CALL_ACCEPT_DATE:
                fprintf(f, "    lea rdi, [rel L_numtext]\n");
                fprintf(f, "    mov rsi, %lld\n", i->imm);
                fprintf(f, "    call runtime_accept_date\n");
                break;
            case IR_CALL_ACCEPT_NUM:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->digits);
                fprintf(f, "    mov rcx, %d\n", i->decimal_places);
                fprintf(f, "    mov r8, %d\n", i->usage_type);
                fprintf(f, "    mov r9, %d\n", i->flags);
                fprintf(f, "    call runtime_accept_numeric\n");
                break;
            case IR_LABEL:
                for (int p = 0; p < plan.nloops && !promo.loop; p++)
                    if (plan.loops[p].head == i) promo_begin(f, &plan.loops[p], p);
                fprintf(f, "\n%s:\n", sanitized_var_name);
                break;
            case IR_PROC_ENTRY:
                fprintf(f, "\n; %s\nP_%lld:\n", i->var_name, i->imm);
                break;
            case IR_PROC_EXIT:
                // Return to the PERFORM waiting for the end of this procedure
                fprintf(f, "    mov rax, [rel R_%lld]\n", i->imm);
                fprintf(f, "    test rax, rax\n");
                fprintf(f, "    jz L_proc_end_%lld\n", i->imm);
                fprintf(f, "    jmp rax\n");
                fprintf(f, "L_proc_end_%lld:\n", i->imm);
                break;
            case IR_PERFORM:
                fprintf(f, "    mov rax, [rel R_%d]\n", i->proc_end);
                fprintf(f, "    sub rsp, 16\n");
                fprintf(f, "    mov [rsp], rax\n");
                fprintf(f, "    lea rax, [rel L_perform_%d]\n", perform_counter);
                fprintf(f, "    mov [rel R_%d], rax\n", i->proc_end);
                fprintf(f, "    jmp P_%lld\n", i->imm);
                fprintf(f, "L_perform_%d:\n", perform_counter);
                fprintf(f, "    mov rax, [rsp]\n");
                fprintf(f, "    add rsp, 16\n");
                fprintf(f, "    mov [rel R_%d], rax\n\n", i->proc_end);
                perform_counter++;
                break;
            case IR_GOTO:
                fprintf(f, "    jmp P_%lld\n\n", i->imm);
                break;
            case IR_GOTO_ALTERABLE:
                fprintf(f, "    jmp [rel G_%lld]\n\n", i->imm);
                break;
            case IR_ALTER:
                fprintf(f, "    lea rax, [rel P_%d]\n", i->proc_end);
                fprintf(f, "    mov [rel G_%lld], rax\n\n", i->imm);
                break;
            case IR_TIMES_BEGIN:
                if (i->flags) emit_pop(f, "rax");
                else fprintf(f, "    mov rax, %lld\n", i->imm);
                fprintf(f, "    sub rsp, 16\n");
                fprintf(f, "    mov [rsp], rbx\n"); // save the outer counter
                fprintf(f, "    mov rbx, rax\n");
                fprintf(f, "    test rbx, rbx\n");
                fprintf(f, "    jle %s\n", sanitized_jump);
                fprintf(f, "%s:\n", sanitized_var_name);
                break;
            case IR_TIMES_END:
                fprintf(f, "    dec rbx\n");
                fprintf(f, "    jnz %s\n", sanitized_var_name);
                fprintf(f, "%s:\n", sanitized_jump);
                fprintf(f, "    mov rbx, [rsp]\n");
                fprintf(f, "    add rsp, 16\n\n");
                break;
            case IR_CALL_EXIT:
                fprintf(f, "    call runtime_exit\n");
                break;
            case IR_JUMP:
                if (promo.loop && i == promo.loop->back && promo.test) {
                    // rotated loop: the head's test, then on to the exit
                    char buf[64];
                    fprintf(f, "    cmp %s, %s\n", promo.test_a, promo.test_b);
                    fprintf(f, "    %s L_loop_body_%d\n", true_jump(promo.test_op), promo.id);
                    fprintf(f, "    jmp %s\n\n", promo_target(promo.test->jump_target, buf, sizeof(buf)));
                    break;
                }
                fprintf(f, "    jmp %s\n\n", sanitized_jump);
                break;
            case IR_NUM_LOAD: {
                int pk = promo_field(i->var_name);
                if (pk >= 0) {
                    vs_push_reg(f, VS_PROMO0 + pk, field_bound(i), !(i->flags & IR_NUM_SIGNED));
                    break;
                }
                if (inline_binary(i)) {
                    char mem[96];
                    field_mem(f, i->var_name, mem, sizeof(mem));
                    int r = vs_alloc(f);
                    emit_binary_load(f, i, mem, vs_regs[r]);
                    vs_push_reg(f, r, field_bound(i), !(i->flags & IR_NUM_SIGNED));
                    break;
                }
                emit_addr(f, "rdi", i->var_name);
                if (inline_packed(i)) {
                    fprintf(f, "    mov esi, %d\n", i->size);
                    fprintf(f, "    call runtime_packed_load\n");
                } else {
                    emit_size(f, "rsi", i->size);
                    fprintf(f, "    mov rdx, %d\n", i->usage_type);
                    fprintf(f, "    mov rcx, %d\n", i->flags);
                    fprintf(f, "    call runtime_num_load\n");
                }
                int r = vs_alloc(f);
                fprintf(f, "    mov %s, rax\n", vs_regs[r].q);
                unsigned long long bound = field_bound(i);
                vs_push_reg(f, r, bound, !(i->flags & IR_NUM_SIGNED));
                break;
            }
            case IR_NUM_CONST:
                vs_push_const(f, i->imm);
                break;
            case IR_NUM_RESCALE: {
                vs_ensure(f, 1);
                VEntry *e = &vs[vs_n - 1];
                if (i->imm == 0) break;
                if (i->imm > 0 && i->imm <= 18) {
                    unsigned long long p = pow10_ull((int)i->imm);
                    unsigned long long nb = bound_mul(e->bound, p);
                    if (nb < BOUND_UNKNOWN) {
                        // cannot overflow: multiply inline
                        if (e->is_const) {
                            e->imm *= (long long)p;
                            e->bound = nb;
                            break;
                        }
                        VEntry v = vs_pop();
                        int r = vs_own(f, &v, -1);
                        if (fits_imm32((long long)p)) {
                            fprintf(f, "    imul %s, %s, %lld\n", vs_regs[r].q, vs_regs[v.reg].q, (long long)p);
                        } else {
                            fprintf(f, "    mov rcx, %lld\n", (long long)p);
                            fprintf(f, "    imul %s, rcx\n", vs_regs[r].q);
                        }
                        vs_push_reg(f, r, nb, v.nonneg);
                        break;
                    }
                }
                VEntry v = vs_pop();
                vs_to(f, &v, "rdi");
                fprintf(f, "    mov rsi, %lld\n", i->imm);
                fprintf(f, "    mov rdx, %d\n", i->flags);
                fprintf(f, "    call runtime_num_rescale\n");
                fprintf(f, "    or [rel cobrt_tgt_err], dl\n");
                tgt_zero = false;
                int r = vs_alloc(f);
                fprintf(f, "    mov %s, rax\n", vs_regs[r].q);
                unsigned long long nb = BOUND_UNKNOWN;
                if (i->imm < 0) nb = v.bound >= BOUND_UNKNOWN ? BOUND_UNKNOWN
                                   : v.bound / pow10_ull((int)-i->imm) + 1;
                vs_push_reg(f, r, nb, v.nonneg);
                break;
            }
            case IR_NUM_ADD:
            case IR_NUM_SUB: {
                vs_ensure(f, 2);
                VEntry b = vs_pop();
                VEntry a = vs_pop();
                bool add = i->op == IR_NUM_ADD;
                long long folded;
                if (a.is_const && b.is_const &&
                    !(add ? __builtin_add_overflow(a.imm, b.imm, &folded)
                          : __builtin_sub_overflow(a.imm, b.imm, &folded))) {
                    vs_push_const(f, folded);
                    break;
                }
                unsigned long long nb = bound_add(a.bound, b.bound);
                const IrInstr *n = i->next;
                if (!add && n && n->op == IR_NUM_NEG) {
                    // SUBTRACT b FROM c is lowered as -(b - c): compute c - b
                    VEntry t = a; a = b; b = t;
                    skip_neg = true;
                    n = n->next;
                }
                if (add && (a.is_const || a.reg >= VS_NREGS) && !b.is_const && b.reg < VS_NREGS) {
                    // commutative: add into b's register
                    VEntry t = a; a = b; b = t;
                }
                int r = vs_own(f, &a, b.is_const ? -1 : b.reg);
                char buf[32];
                fprintf(f, "    %s %s, %s\n", add ? "add" : "sub", vs_regs[r].q, vs_operand(f, &b, buf, sizeof(buf)));
                if (nb >= BOUND_UNKNOWN) {
                    if (tgt_zero && n && n->op == IR_NUM_STORE && !n->lit_name) {
                        // the store that follows takes the result: overflow
                        // goes straight to its size error block
                        fprintf(f, "    jo L_store_ovf_%d\n", store_counter);
                        ovf_store = store_counter;
                    } else {
                        fprintf(f, "    seto dl\n");
                        fprintf(f, "    or [rel cobrt_tgt_err], dl\n");
                        tgt_zero = false;
                    }
                }
                vs_push_reg(f, r, nb, add && a.nonneg && b.nonneg);
                break;
            }
            case IR_NUM_MUL:
            case IR_NUM_DIV: {
                vs_ensure(f, 2);
                VEntry b = vs_pop();
                VEntry a = vs_pop();
                unsigned long long nb = bound_mul(a.bound, b.bound);
                if (i->op == IR_NUM_MUL && i->imm == 0 && nb < BOUND_UNKNOWN) {
                    // cannot overflow: multiply inline
                    if (a.is_const && b.is_const) {
                        vs_push_const(f, a.imm * b.imm);
                        break;
                    }
                    if (a.is_const) { VEntry t = a; a = b; b = t; }
                    if (b.is_const && fits_imm32(b.imm)) {
                        int r = (a.reg < VS_NREGS) ? a.reg : vs_own(f, &(VEntry){ .is_const = true, .imm = 0 }, -1);
                        fprintf(f, "    imul %s, %s, %lld\n", vs_regs[r].q, vs_regs[a.reg].q, b.imm);
                        vs_push_reg(f, r, nb, a.nonneg && b.nonneg);
                        break;
                    }
                    if (a.reg >= VS_NREGS && !b.is_const && b.reg < VS_NREGS) { VEntry t = a; a = b; b = t; }
                    int r = vs_own(f, &a, b.is_const ? -1 : b.reg);
                    char buf[32];
                    fprintf(f, "    imul %s, %s\n", vs_regs[r].q, vs_operand(f, &b, buf, sizeof(buf)));
                    vs_push_reg(f, r, nb, a.nonneg && b.nonneg);
                    break;
                }
                vs_to(f, &b, "rsi");
                vs_to(f, &a, "rdi");
                fprintf(f, "    mov rdx, %lld\n", i->imm);
                fprintf(f, "    call runtime_num_%s\n", i->op == IR_NUM_MUL ? "mul" : "div");
                fprintf(f, "    or [rel cobrt_tgt_err], dl\n");
                tgt_zero = false;
                int r = vs_alloc(f);
                fprintf(f, "    mov %s, rax\n", vs_regs[r].q);
                vs_push_reg(f, r, BOUND_UNKNOWN, false);
                break;
            }
            case IR_NUM_POW:
                emit_pop(f, "rdx");
                emit_pop(f, "rdi");
                fprintf(f, "    mov rsi, %d\n", i->digits);
                fprintf(f, "    mov rcx, %d\n", i->size);
                fprintf(f, "    mov r8, %lld\n", i->imm);
                fprintf(f, "    call runtime_num_pow\n");
                fprintf(f, "    or [rel cobrt_tgt_err], dl\n");
                emit_push(f);
                break;
            case IR_NUM_DEEDIT: {
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    call runtime_deedit\n");
                int r = vs_alloc(f);
                fprintf(f, "    mov %s, rax\n", vs_regs[r].q);
                vs_push_reg(f, r, BOUND_UNKNOWN, false);
                break;
            }
            case IR_CALL_EDIT_ALNUM:
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                emit_size(f, "rdx", i->lit_len);
                emit_addr(f, "rcx", i->content);
                emit_size(f, "r8", i->size);
                fprintf(f, "    call runtime_edit_alnum\n\n");
                break;
            case IR_NUM_TO_TEXT:
                emit_pop(f, "rdx");
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    mov rsi, %lld\n", i->imm);
                fprintf(f, "    call runtime_num_to_digits\n");
                break;
            case IR_NUM_TRUNC: {
                // keep the low-order imm digits (the remainder keeps the sign)
                long long p = 1;
                for (long long k = 0; k < i->imm; k++) p *= 10;
                fprintf(f, "    mov rax, [rsp]\n");
                fprintf(f, "    cqo\n");
                fprintf(f, "    mov rcx, %lld\n", p);
                fprintf(f, "    idiv rcx\n");
                fprintf(f, "    mov [rsp], rdx\n");
                break;
            }
            case IR_NUM_DUP: {
                vs_ensure(f, 1);
                VEntry e = vs[vs_n - 1];
                if (e.is_const) {
                    vs_push_const(f, e.imm);
                    break;
                }
                if (e.reg >= VS_NREGS) {
                    // a promoted register: name it again
                    vs_push_reg(f, e.reg, e.bound, e.nonneg);
                    break;
                }
                int r = vs_alloc(f);
                // the allocation may have spilled the entry being copied
                fprintf(f, "    mov %s, %s\n", vs_regs[r].q, vs_regs[e.reg].q);
                vs_push_reg(f, r, e.bound, e.nonneg);
                break;
            }
            case IR_NUM_DROP:
                if (vs_n > 0) vs_n--;
                else fprintf(f, "    add rsp, 16\n");
                break;
            case IR_NUM_SWAP: {
                vs_ensure(f, 2);
                VEntry t = vs[vs_n - 1];
                vs[vs_n - 1] = vs[vs_n - 2];
                vs[vs_n - 2] = t;
                break;
            }
            case IR_ERR_SAVE:
                if (tgt_zero) {
                    if (!val_zero) fprintf(f, "    mov byte [rel cobrt_val_err], 0\n");
                    val_zero = true;
                    break;
                }
                fprintf(f, "    mov al, [rel cobrt_tgt_err]\n");
                fprintf(f, "    mov [rel cobrt_val_err], al\n");
                val_zero = false;
                break;
            case IR_ERR_RESTORE:
                if (val_zero) {
                    if (!tgt_zero) fprintf(f, "    mov byte [rel cobrt_tgt_err], 0\n");
                    tgt_zero = true;
                    break;
                }
                fprintf(f, "    mov al, [rel cobrt_val_err]\n");
                fprintf(f, "    mov [rel cobrt_tgt_err], al\n");
                tgt_zero = false;
                break;
            case IR_NUM_NEG: {
                if (skip_neg) {
                    skip_neg = false;   // done by the subtraction before
                    break;
                }
                vs_ensure(f, 1);
                VEntry e = vs_pop();
                if (e.is_const && e.imm != INT64_MIN) {
                    vs_push_const(f, -e.imm);
                    break;
                }
                int r = vs_own(f, &e, -1);
                fprintf(f, "    neg %s\n", vs_regs[r].q);
                vs_push_reg(f, r, e.is_const ? BOUND_UNKNOWN : e.bound, false);
                break;
            }
            case IR_NUM_STORE: {
                vs_ensure(f, 1);
                int pk = i->lit_name ? -1 : promo_field(i->var_name);
                VEntry v = vs_pop();
                if (pk >= 0) vs_unalias(f, VS_PROMO0 + pk);
                int L = store_counter++;
                bool known = tgt_zero;      // no error while computing the value
                bool ovf = ovf_store == L;  // an addition jumps to L_store_ovf on overflow
                ovf_store = -1;
                const char *err = known ? "cobrt_stmt_err" : "cobrt_tgt_err";
                // An error while computing the value: leave the receiver unchanged
                if (!known) {
                    fprintf(f, "    cmp byte [rel cobrt_tgt_err], 0\n");
                    fprintf(f, "    jne L_store_%d\n", L);
                }
                RegName vr = v.is_const ? vs_regs[VS_R9] : vs_regs[v.reg];
                bool packed = inline_packed(i) && i->digits >= 1 && i->digits <= 18;
                if (i->lit_name || (pk < 0 && !inline_binary(i) && !packed)) {
                    // No fast path: the runtime stores (or edits) the value
                    if (i->lit_name) {
                        vs_to(f, &v, "rcx");
                        emit_addr(f, "rdi", i->var_name);
                        emit_addr(f, "rsi", i->lit_name);
                        fprintf(f, "    mov rdx, %d\n", i->size);
                        fprintf(f, "    mov r8, %d\n", i->flags);
                        fprintf(f, "    call runtime_edit_numeric\n");
                    } else {
                        emit_num_store_call(f, i, &v);
                    }
                    fprintf(f, "    or [rel %s], al\n", err);
                    if (!known) {
                        fprintf(f, "L_store_%d:\n", L);
                        fprintf(f, "    mov al, [rel cobrt_tgt_err]\n");
                        fprintf(f, "    or [rel cobrt_stmt_err], al\n");
                        fprintf(f, "    mov byte [rel cobrt_tgt_err], 0\n");
                    }
                    if (ovf) emit_ovf_block(L);
                    fprintf(f, "L_store_done_%d:\n\n", L);
                    tgt_zero = true;
                    break;
                }
                // Fast path inline, the rest out of line (cold): a value
                // that does not fit goes through runtime_num_store, which
                // truncates and reports the size error
                char slow[32];
                snprintf(slow, sizeof(slow), "L_store_slow_%d", L);
                if (v.is_const) fprintf(f, "    mov r9, %lld\n", v.imm);
                fprintf(cold, "%s:\n", slow);
                if (pk >= 0) {
                    // Promoted field: only the register changes
                    RegName pr = promo_regs[pk];
                    bool dirty = promo_dirty(pk);
                    field_fits_check(f, i, vr, slow, v.bound, v.nonneg);
                    fprintf(f, "    mov %s, %s\n", pr.q, vr.q);
                    if (dirty) fprintf(f, "    mov byte [rel L_dirty_%d_%d], 1\n", promo.id, pk);
                    // memory first gets the register's value (ON SIZE ERROR
                    // may keep it), then the normal store, then the register
                    // is loaded again
                    if (dirty) {
                        fprintf(cold, "    cmp byte [rel L_dirty_%d_%d], 0\n", promo.id, pk);
                        fprintf(cold, "    je L_store_clean_%d\n", L);
                    }
                    field_write_from(cold, i, pr);
                    if (dirty) fprintf(cold, "L_store_clean_%d:\n", L);
                    emit_num_store_call(cold, i, &v);
                    fprintf(cold, "    or [rel %s], al\n", err);
                    field_load_to(cold, i, pr);
                    if (dirty) fprintf(cold, "    mov byte [rel L_dirty_%d_%d], 0\n", promo.id, pk);
                } else {
                    if (inline_binary(i)) {
                        char mem[96];
                        field_mem(f, i->var_name, mem, sizeof(mem));
                        emit_binary_store(f, i, vr, mem, slow, v.bound, v.nonneg);
                    } else {
                        // fits the PICTURE: the packed store routine
                        long long limit = (long long)pow10_ull(i->digits);
                        if (v.bound >= (unsigned long long)limit) {
                            fprintf(f, "    mov rax, %s\n", vr.q);
                            fprintf(f, "    neg rax\n");
                            fprintf(f, "    cmovs rax, %s\n", vr.q);
                            fprintf(f, "    mov rcx, %lld\n", limit);
                            fprintf(f, "    cmp rax, rcx\n");
                            fprintf(f, "    jae %s\n", slow);
                        }
                        emit_addr(f, "rdi", i->var_name);
                        fprintf(f, "    mov esi, %d\n", i->size);
                        fprintf(f, "    mov rdx, %s\n", vr.q);
                        fprintf(f, "    mov ecx, %d\n", i->flags);
                        fprintf(f, "    call runtime_packed_store\n");
                    }
                    emit_num_store_call(cold, i, &v);
                    fprintf(cold, "    or [rel %s], al\n", err);
                }
                if (known) {
                    fprintf(cold, "    jmp L_store_done_%d\n", L);
                } else {
                    fprintf(cold, "L_store_%d:\n", L);
                    fprintf(cold, "    mov al, [rel cobrt_tgt_err]\n");
                    fprintf(cold, "    or [rel cobrt_stmt_err], al\n");
                    fprintf(cold, "    mov byte [rel cobrt_tgt_err], 0\n");
                    fprintf(cold, "    jmp L_store_done_%d\n", L);
                }
                if (ovf) emit_ovf_block(L);
                fprintf(f, "L_store_done_%d:\n\n", L);
                tgt_zero = true;
                break;
            }
            case IR_NUM_CMP_JUMP_IF_FALSE: {
                vs_ensure(f, 2);
                VEntry b = vs_pop();
                VEntry a = vs_pop();
                vs_flush(f);
                const char *op = i->cmp_op;
                if (a.is_const && !b.is_const) {
                    // compare the other way round
                    VEntry t = a; a = b; b = t;
                    if (!op || !strcmp(op, "=") || !strcmp(op, "<>")) {}
                    else if (!strcmp(op, ">")) op = "<";
                    else if (!strcmp(op, "<")) op = ">";
                    else if (!strcmp(op, ">=")) op = "<=";
                    else if (!strcmp(op, "<=")) op = ">=";
                }
                const char *ra;
                if (a.is_const) {
                    fprintf(f, "    mov rax, %lld\n", a.imm);
                    ra = "rax";
                } else {
                    ra = vs_regs[a.reg].q;
                }
                char buf[32];
                fprintf(f, "    cmp %s, %s\n", ra, vs_operand(f, &b, buf, sizeof(buf)));
                fprintf(f, "    %s %s\n\n", false_jump(op), sanitized_jump);
                break;
            }
            case IR_ALNUM_CMP_JUMP_IF_FALSE:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                emit_addr(f, "rdx", i->lit_name);
                emit_size(f, "rcx", i->lit_len);
                fprintf(f, "    call runtime_compare_alnum\n");
                fprintf(f, "    cmp rax, 0\n");
                fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                break;
            case IR_FILL_CMP_JUMP_IF_FALSE:
                if (i->lit_name) {
                    emit_addr(f, "rdi", i->var_name);
                    emit_size(f, "rsi", i->size);
                    emit_addr(f, "rdx", i->lit_name);
                    fprintf(f, "    mov rcx, %d\n", i->lit_len);
                    fprintf(f, "    call runtime_compare_pattern\n");
                    fprintf(f, "    cmp rax, 0\n");
                    fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                    break;
                }
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", (int)(unsigned char)i->pad_char);
                fprintf(f, "    call runtime_compare_fill\n");
                fprintf(f, "    cmp rax, 0\n");
                fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                break;
            case IR_CLASS_JUMP:
                if (i->lit_name) {
                    emit_addr(f, "rcx", i->lit_name);
                    fprintf(f, "    mov r8, %d\n", i->lit_len);
                }
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %lld\n", i->imm);
                fprintf(f, "    call runtime_class_test\n");
                fprintf(f, "    test rax, rax\n");
                fprintf(f, "    %s %s\n\n", i->flags ? "jnz" : "jz", sanitized_jump);
                break;
            case IR_ADDR_SET:
                emit_addr(f, "rax", i->var_name);
                fprintf(f, "    mov [rel A_%lld], rax\n", i->imm);
                break;
            case IR_ADDR_ADD:
                emit_pop(f, "rax");
                fprintf(f, "    dec rax\n");
                fprintf(f, "    imul rax, rax, %d\n", i->size);
                fprintf(f, "    add [rel A_%lld], rax\n", i->imm);
                break;
            case IR_LEN_SET:
                emit_pop(f, "rax");
                fprintf(f, "    mov [rel N_%lld], rax\n", i->imm);
                break;
            case IR_CALL_REPLICATE:
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %lld\n", i->imm);
                fprintf(f, "    call runtime_replicate\n");
                break;
            case IR_FILE_OPEN:
            case IR_FILE_CLOSE:
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    mov rsi, %lld\n", i->imm);
                fprintf(f, "    call runtime_file_%s\n", i->op == IR_FILE_OPEN ? "open" : "close");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_FILE_READ:
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                fprintf(f, "    call runtime_file_read\n");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_FILE_WRITE:
                if (i->flags) emit_pop(f, "r8");
                else fprintf(f, "    mov r8, %d\n", i->digits);
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                emit_size(f, "rdx", i->size);
                fprintf(f, "    mov rcx, %lld\n", i->imm);
                fprintf(f, "    call runtime_file_write\n");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_FILE_REWRITE:
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                emit_size(f, "rdx", i->size);
                fprintf(f, "    call runtime_file_rewrite\n");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_IO_JUMP:
                fprintf(f, "    mov rax, [rel cobrt_io_status]\n");
                fprintf(f, "    cmp rax, %lld\n", i->imm);
                fprintf(f, "    jl L_io_%d\n", store_counter);
                fprintf(f, "    cmp rax, %d\n", i->size);
                fprintf(f, "    jle %s\n", sanitized_jump);
                fprintf(f, "L_io_%d:\n", store_counter);
                store_counter++;
                break;
            case IR_STORE_QWORD:
                if (i->flags == 1) emit_addr(f, "rax", i->lit_name);
                else if (i->flags == 2) emit_size(f, "rax", i->size);
                emit_addr(f, "rdi", i->var_name);
                if (i->flags == 0) fprintf(f, "    mov qword [rdi], %lld\n", i->imm);
                else fprintf(f, "    mov [rdi], rax\n");
                break;
            case IR_CALL_INSPECT:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                emit_addr(f, "rdx", i->lit_name);
                fprintf(f, "    mov rcx, %lld\n", i->imm);
                emit_addr(f, "r8", i->content);
                fprintf(f, "    call runtime_inspect\n\n");
                break;
            case IR_SWITCH_JUMP:
                fprintf(f, "    mov rdi, %lld\n", i->imm);
                fprintf(f, "    call runtime_switch_status\n");
                fprintf(f, "    cmp rax, %d\n", i->flags);
                fprintf(f, "    je %s\n\n", sanitized_jump);
                break;
            case IR_CALL_STRING:
                emit_pop(f, "r8");
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                emit_addr(f, "rdx", i->lit_name);
                fprintf(f, "    mov rcx, %lld\n", i->imm);
                fprintf(f, "    call runtime_string\n");
                fprintf(f, "    mov [rel cobrt_stmt_err], dl\n");
                emit_push(f);
                break;
            // UNSTRING state block: runtime/ABI.md section 11
            case IR_CALL_UNSTRING_START:
                emit_pop(f, "rsi");
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    call runtime_unstring_start\n");
                emit_addr(f, "rcx", i->var_name);
                fprintf(f, "    xor rax, 1\n");
                fprintf(f, "    mov [rcx + 72], rax\n");     // overflow when out of range
                fprintf(f, "    test rax, rax\n");
                fprintf(f, "    jnz %s\n", sanitized_jump);
                break;
            case IR_CALL_UNSTRING_NEXT:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    call runtime_unstring_next\n");
                fprintf(f, "    test rax, rax\n");
                fprintf(f, "    jz %s\n", sanitized_jump);
                break;
            case IR_UNSTRING_SLOTS:
                emit_addr(f, "rcx", i->var_name);
                fprintf(f, "    mov rax, [rcx + %d]\n", i->flags ? 56 : 40);
                fprintf(f, "    mov [rel A_%lld], rax\n", i->imm);
                fprintf(f, "    mov rax, [rcx + %d]\n", i->flags ? 64 : 48);
                fprintf(f, "    mov [rel N_%lld], rax\n", i->imm);
                break;
            case IR_UNSTRING_END:
                emit_addr(f, "rcx", i->var_name);
                fprintf(f, "    mov rax, [rcx + 32]\n");
                fprintf(f, "    cmp rax, [rcx + 8]\n");
                fprintf(f, "    setb al\n");
                fprintf(f, "    movzx eax, al\n");
                fprintf(f, "    or [rcx + 72], rax\n");
                break;
            case IR_UNSTRING_OVERFLOW:
                emit_addr(f, "rcx", i->var_name);
                fprintf(f, "    mov al, [rcx + 72]\n");
                fprintf(f, "    mov [rel cobrt_stmt_err], al\n");
                break;
            case IR_SWITCH_SET:
                fprintf(f, "    mov rdi, %lld\n", i->imm);
                fprintf(f, "    mov rsi, %d\n", i->flags);
                fprintf(f, "    call runtime_switch_set\n\n");
                break;
            case IR_CALL_SET_COLLATION:
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    call runtime_set_collation\n");
                break;
            case IR_SIZE_ERR_CLEAR:
                if (!tgt_zero) fprintf(f, "    mov byte [rel cobrt_tgt_err], 0\n");
                tgt_zero = true;
                // no ON SIZE ERROR reads it: no need to clear it
                if (!ir_size_err_dead_after(flow, pos))
                    fprintf(f, "    mov byte [rel cobrt_stmt_err], 0\n");
                break;
            case IR_JUMP_IF_NO_SIZE_ERR:
                fprintf(f, "    cmp byte [rel cobrt_stmt_err], 0\n");
                fprintf(f, "    je %s\n", sanitized_jump);
                break;
            default:
                // Handle other IR opcodes if necessary, or do nothing for IR_DATA_VAR/IR_DATA_LIT as they are handled in .bss/.data sections
                break;
        }
        if (!vs_handles(i->op) && !keeps_err_state(i->op)) tgt_zero = val_zero = false;
        if (promo.loop && i == promo.test) fprintf(f, "L_loop_body_%d:\n", promo.id);
        if (promo.loop && i == promo.loop->head) {
            tgt_zero = promo.head_tgt;
            val_zero = promo.head_val;
        }
        if (promo.loop && i == promo.loop->back) promo_end(f);
        free(sanitized_var_name);
        free(sanitized_lit_name);
        free(sanitized_jump);
        i = i->next;
    }

    promote_free(&plan);
    ir_flow_free(flow);
    // Out-of-line paths; control never falls into them
    fprintf(f, "\n    call runtime_exit\n\n; out-of-line paths\n");
    fclose(cold);
    cold = NULL;
    fwrite(cold_buf, 1, cold_len, f);
    free(cold_buf);
    return ferror(f) ? 1 : 0;
}

