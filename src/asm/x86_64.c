/* src/asm/x86_64.c: x86-64 instruction encoding for the COBALT assembler.
 *
 * Covers the instructions the compiler and the runtime use. Where x86 offers
 * several encodings the choice matches nasm's default (optimizing) output, so
 * objects can be compared byte for byte: the shortest immediate form, the
 * accumulator short forms, mov r64, imm as mov r32, imm32 when the value fits,
 * shift by 1 without an immediate, and short jumps when they reach. */

#include "asm_internal.h"
#include <string.h>
#include <strings.h>

enum {
    M_ADD, M_OR, M_ADC, M_SBB, M_AND, M_SUB, M_XOR, M_CMP,   // ALU: index is /n
    M_ROL, M_ROR, M_RCL, M_RCR, M_SHL, M_SHR, M_SAL, M_SAR,  // shifts: index - M_ROL is /n
    M_MOV, M_LEA, M_TEST, M_MOVZX, M_MOVSX, M_MOVSXD,
    M_INC, M_DEC, M_NOT, M_NEG, M_MUL, M_IMUL, M_DIV, M_IDIV,
    M_PUSH, M_POP, M_CALL, M_JMP, M_BT, M_XCHG, M_BSWAP,
    M_RET, M_LEAVE, M_CQO, M_CDQ, M_CDQE, M_SYSCALL, M_CLD, M_STD, M_NOP,
    M_MOVSB, M_MOVSQ, M_STOSB, M_STOSQ, M_HLT, M_CMPSB, M_SCASB,
    M_FIXED_COUNT
};
#define M_JCC    100                // + condition code
#define M_SETCC  200
#define M_CMOVCC 300

static const char *const fixed_names[M_FIXED_COUNT] = {
    "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp",
    "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar",
    "mov", "lea", "test", "movzx", "movsx", "movsxd",
    "inc", "dec", "not", "neg", "mul", "imul", "div", "idiv",
    "push", "pop", "call", "jmp", "bt", "xchg", "bswap",
    "ret", "leave", "cqo", "cdq", "cdqe", "syscall", "cld", "std", "nop",
    "movsb", "movsq", "stosb", "stosq", "hlt", "cmpsb", "scasb",
};

static const struct { const char *name; int cc; } conditions[] = {
    {"o", 0}, {"no", 1}, {"b", 2}, {"c", 2}, {"nae", 2}, {"ae", 3}, {"nb", 3}, {"nc", 3},
    {"e", 4}, {"z", 4}, {"ne", 5}, {"nz", 5}, {"be", 6}, {"na", 6}, {"a", 7}, {"nbe", 7},
    {"s", 8}, {"ns", 9}, {"p", 10}, {"pe", 10}, {"np", 11}, {"po", 11},
    {"l", 12}, {"nge", 12}, {"ge", 13}, {"nl", 13}, {"le", 14}, {"ng", 14}, {"g", 15}, {"nle", 15},
};

static int condition(const char *s, size_t len) {
    for (size_t k = 0; k < sizeof(conditions) / sizeof(conditions[0]); k++)
        if (strlen(conditions[k].name) == len && strncasecmp(conditions[k].name, s, len) == 0)
            return conditions[k].cc;
    return -1;
}

int x86_mnemonic(const char *name, size_t len) {
    for (int m = 0; m < M_FIXED_COUNT; m++)
        if (strlen(fixed_names[m]) == len && strncasecmp(fixed_names[m], name, len) == 0) return m;
    int cc;
    if (len > 1 && (name[0] == 'j' || name[0] == 'J') && (cc = condition(name + 1, len - 1)) >= 0)
        return M_JCC + cc;
    if (len > 3 && strncasecmp(name, "set", 3) == 0 && (cc = condition(name + 3, len - 3)) >= 0)
        return M_SETCC + cc;
    if (len > 4 && strncasecmp(name, "cmov", 4) == 0 && (cc = condition(name + 4, len - 4)) >= 0)
        return M_CMOVCC + cc;
    return -1;
}

bool x86_is_jump(int m) {
    return m == M_JMP || (m >= M_JCC && m < M_JCC + 16);
}

int x86_branch_size(int m, bool long_jump) {
    if (!long_jump) return 2;
    return m == M_JMP ? 5 : 6;
}

// ── Output helpers ───────────────────────────────────────────────────────────

typedef struct {
    uint8_t *out;
    int len;
    Fixup *fix;
    int nfix;
    const char *err;
} Enc;

static void put(Enc *e, uint8_t v) {
    if (e->len < MAX_INSTR) e->out[e->len] = v;
    e->len++;
}

static void fail(Enc *e, const char *msg) {
    if (!e->err) e->err = msg;
}

static void add_fixup(Enc *e, int kind, const Expr *x, int64_t pc_adjust) {
    if (e->nfix >= MAX_FIXUPS) { fail(e, "too many symbol references in one instruction"); return; }
    e->fix[e->nfix++] = (Fixup){ e->len, kind, *x, pc_adjust };
}

// Little-endian value of size bytes; a symbol gets a fixup of kind
static void put_value(Enc *e, const Expr *x, int size, int kind, int64_t pc_adjust) {
    if (x->sym != NO_SYM) add_fixup(e, kind, x, pc_adjust);
    uint64_t v = (uint64_t)x->value;
    for (int k = 0; k < size; k++) put(e, (uint8_t)(v >> (8 * k)));
}

static bool fits8(int64_t v) { return v >= -128 && v <= 127; }
static bool fits32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

// The value as the signed number an immediate of size bytes holds
static int64_t sized(int64_t v, int size) {
    switch (size) {
        case 1: return (int8_t)v;
        case 2: return (int16_t)v;
        case 4: return (int32_t)v;
        default: return v;
    }
}

static bool is_reg(const Operand *o) { return o->kind == OP_REG; }
static bool is_mem(const Operand *o) { return o->kind == OP_MEM; }
static bool is_imm(const Operand *o) { return o->kind == OP_IMM; }
static bool is_rm(const Operand *o) { return o->kind == OP_REG || o->kind == OP_MEM; }

// Instruction prefixes, REX, opcode and ModRM / SIB / displacement.
// reg_field: a register number, or an opcode extension (/n) when regop is NULL.
// imm_after: bytes of immediate that follow (for RIP-relative fixups).
static void emit_modrm_op(Enc *e, int opsize, const uint8_t *opcode, int oplen,
                          int reg_field, const Operand *regop, const Operand *rm, int imm_after) {
    if (opsize == 2) put(e, 0x66);
    int rex = 0;
    if (opsize == 8) rex |= 8;
    if (reg_field >= 8) rex |= 4;
    bool force_rex = (regop && regop->needs_rex) || (is_reg(rm) && rm->needs_rex);
    bool high8 = (regop && regop->high8) || (is_reg(rm) && rm->high8);
    if (is_reg(rm)) {
        if (rm->reg >= 8) rex |= 1;
    } else {
        if (rm->base != NO_REG && rm->base >= 8) rex |= 1;
        if (rm->index != NO_REG && rm->index >= 8) rex |= 2;
    }
    if (rex || force_rex) {
        if (high8) fail(e, "ah, bh, ch or dh cannot be used with a REX prefix");
        put(e, (uint8_t)(0x40 | rex));
    }
    for (int k = 0; k < oplen; k++) put(e, opcode[k]);

    int r = (reg_field & 7) << 3;
    if (is_reg(rm)) {
        put(e, (uint8_t)(0xC0 | r | (rm->reg & 7)));
        return;
    }
    const Expr *d = &rm->expr;
    bool sym = d->sym != NO_SYM;
    if (rm->rip) {
        put(e, (uint8_t)(0x05 | r));
        put_value(e, d, 4, FIX_PC32, 4 + imm_after);
        return;
    }
    if (rm->base == NO_REG && rm->index == NO_REG) {
        // absolute [disp32] through a SIB byte (mod 00 rm 101 is RIP-relative)
        put(e, (uint8_t)(0x04 | r));
        put(e, 0x25);
        put_value(e, d, 4, FIX_ABS32S, 0);
        return;
    }
    if (rm->index != NO_REG && (rm->index & 15) == 4) fail(e, "rsp cannot be an index register");
    int scale_bits = rm->scale == 8 ? 3 : rm->scale == 4 ? 2 : rm->scale == 2 ? 1 : 0;
    if (rm->base == NO_REG) {
        // [index*scale + disp32]
        put(e, (uint8_t)(0x04 | r));
        put(e, (uint8_t)((scale_bits << 6) | ((rm->index & 7) << 3) | 5));
        put_value(e, d, 4, FIX_ABS32S, 0);
        return;
    }
    int mod;
    if (!sym && d->value == 0 && (rm->base & 7) != 5) mod = 0;
    else if (!sym && fits8(d->value)) mod = 1;
    else mod = 2;
    bool sib = rm->index != NO_REG || (rm->base & 7) == 4;
    put(e, (uint8_t)((mod << 6) | r | (sib ? 4 : (rm->base & 7))));
    if (sib) {
        int index = rm->index != NO_REG ? (rm->index & 7) : 4;
        put(e, (uint8_t)((scale_bits << 6) | (index << 3) | (rm->base & 7)));
    }
    if (mod == 1) put(e, (uint8_t)d->value);
    else if (mod == 2) put_value(e, d, 4, FIX_ABS32S, 0);
}

static void op1(Enc *e, int opsize, uint8_t opcode, int reg_field, const Operand *regop,
                const Operand *rm, int imm_after) {
    emit_modrm_op(e, opsize, &opcode, 1, reg_field, regop, rm, imm_after);
}

static void op2(Enc *e, int opsize, uint8_t b0, uint8_t b1, int reg_field, const Operand *regop,
                const Operand *rm, int imm_after) {
    uint8_t opcode[2] = { b0, b1 };
    emit_modrm_op(e, opsize, opcode, 2, reg_field, regop, rm, imm_after);
}

// REX for an instruction with the register in the opcode's low bits
static void rex_opreg(Enc *e, int opsize, const Operand *reg) {
    if (opsize == 2) put(e, 0x66);
    int rex = (opsize == 8 ? 8 : 0) | (reg->reg >= 8 ? 1 : 0);
    if (rex || reg->needs_rex) put(e, (uint8_t)(0x40 | rex));
}

// Operand size of a two-operand instruction (from a register, else the memory size)
static int operand_size(Enc *e, const Operand *a, const Operand *b) {
    int size = 0;
    if (is_reg(a)) size = a->size;
    else if (b && is_reg(b)) size = b->size;
    if (!size && is_mem(a)) size = a->size;
    if (!size && b && is_mem(b)) size = b->size;
    if (is_mem(a) && a->size && size && a->size != size) fail(e, "mismatch in operand sizes");
    if (b && is_mem(b) && b->size && size && b->size != size) fail(e, "mismatch in operand sizes");
    if (!size) fail(e, "operation size not specified");
    return size;
}

// Immediate operand of an instruction of size bytes (at most 4: sign-extended)
static void put_imm(Enc *e, const Expr *x, int size) {
    int n = size > 4 ? 4 : size;
    int kind = size == 8 ? FIX_ABS32S : FIX_ABS32;
    if (n < 4 && x->sym != NO_SYM) fail(e, "a symbol address does not fit a small immediate");
    put_value(e, x, n, kind, 0);
}

// ── Instruction groups ───────────────────────────────────────────────────────

static void enc_alu(Enc *e, int n, const Operand *d, const Operand *s) {
    int size = operand_size(e, d, is_imm(s) ? NULL : s);
    if (is_imm(s)) {
        if (!is_rm(d)) { fail(e, "invalid operands"); return; }
        if (size == 1) {
            if (is_reg(d) && d->reg == 0 && !d->high8) {
                put(e, (uint8_t)(n * 8 + 4));
                put_value(e, &s->expr, 1, FIX_ABS32, 0);
            } else {
                op1(e, 1, 0x80, n, NULL, d, 1);
                put_value(e, &s->expr, 1, FIX_ABS32, 0);
            }
            return;
        }
        int64_t v = sized(s->expr.value, size);
        if (s->expr.sym == NO_SYM && fits8(v)) {
            op1(e, size, 0x83, n, NULL, d, 1);
            Expr x = EXPR_CONST(v);
            put_value(e, &x, 1, FIX_ABS32, 0);
            return;
        }
        int isize = size == 2 ? 2 : 4;
        if (is_reg(d) && d->reg == 0) {
            if (size == 2) put(e, 0x66);
            if (size == 8) put(e, 0x48);
            put(e, (uint8_t)(n * 8 + 5));
        } else {
            op1(e, size, 0x81, n, NULL, d, isize);
        }
        put_imm(e, &s->expr, size == 2 ? 2 : size);
        return;
    }
    int w = size == 1 ? 0 : 1;
    if (is_reg(s) && is_rm(d)) op1(e, size, (uint8_t)(n * 8 + w), s->reg, s, d, 0);
    else if (is_reg(d) && is_mem(s)) op1(e, size, (uint8_t)(n * 8 + 2 + w), d->reg, d, s, 0);
    else fail(e, "invalid operands");
}

static void enc_mov(Enc *e, const Operand *d, const Operand *s) {
    if (is_imm(s)) {
        if (is_reg(d)) {
            int size = d->size;
            const Expr *x = &s->expr;
            if (size == 8) {
                if (x->sym != NO_SYM) {
                    rex_opreg(e, 8, d);
                    put(e, (uint8_t)(0xB8 + (d->reg & 7)));
                    put_value(e, x, 8, FIX_ABS64, 0);
                } else if (x->value >= 0 && x->value <= 0xFFFFFFFFLL) {
                    rex_opreg(e, 4, d);                         // zero-extends
                    put(e, (uint8_t)(0xB8 + (d->reg & 7)));
                    put_value(e, x, 4, FIX_ABS32, 0);
                } else if (fits32(x->value)) {
                    op1(e, 8, 0xC7, 0, NULL, d, 4);
                    put_value(e, x, 4, FIX_ABS32S, 0);
                } else {
                    rex_opreg(e, 8, d);
                    put(e, (uint8_t)(0xB8 + (d->reg & 7)));
                    put_value(e, x, 8, FIX_ABS64, 0);
                }
                return;
            }
            rex_opreg(e, size, d);
            put(e, (uint8_t)((size == 1 ? 0xB0 : 0xB8) + (d->reg & 7)));
            put_imm(e, x, size);
            return;
        }
        int size = operand_size(e, d, NULL);
        int isize = size == 1 ? 1 : size == 2 ? 2 : 4;
        op1(e, size, size == 1 ? 0xC6 : 0xC7, 0, NULL, d, isize);
        put_imm(e, &s->expr, size);
        return;
    }
    int size = operand_size(e, d, s);
    int w = size == 1 ? 0 : 1;
    if (is_reg(s) && is_rm(d)) op1(e, size, (uint8_t)(0x88 + w), s->reg, s, d, 0);
    else if (is_reg(d) && is_mem(s)) op1(e, size, (uint8_t)(0x8A + w), d->reg, d, s, 0);
    else fail(e, "invalid operands");
}

static void enc_test(Enc *e, const Operand *a, const Operand *b) {
    if (is_imm(a)) { const Operand *t = a; a = b; b = t; }
    if (is_mem(b) && is_reg(a)) { const Operand *t = a; a = b; b = t; }
    int size = operand_size(e, a, is_imm(b) ? NULL : b);
    if (is_imm(b)) {
        if (is_reg(a) && a->reg == 0 && !a->high8) {
            if (size == 2) put(e, 0x66);
            if (size == 8) put(e, 0x48);
            put(e, size == 1 ? 0xA8 : 0xA9);
        } else {
            op1(e, size, size == 1 ? 0xF6 : 0xF7, 0, NULL, a, size == 1 ? 1 : size == 2 ? 2 : 4);
        }
        put_imm(e, &b->expr, size);
        return;
    }
    if (!is_reg(b)) { fail(e, "invalid operands"); return; }
    op1(e, size, size == 1 ? 0x84 : 0x85, b->reg, b, a, 0);
}

static void enc_shift(Enc *e, int n, const Operand *d, const Operand *c) {
    int size = operand_size(e, d, NULL);
    int w = size == 1 ? 0 : 1;
    if (is_reg(c)) {
        if (c->reg != 1 || c->size != 1) { fail(e, "a shift count register must be cl"); return; }
        op1(e, size, (uint8_t)(0xD2 + w), n, NULL, d, 0);
    } else if (is_imm(c) && c->expr.sym == NO_SYM && c->expr.value == 1) {
        op1(e, size, (uint8_t)(0xD0 + w), n, NULL, d, 0);
    } else if (is_imm(c)) {
        op1(e, size, (uint8_t)(0xC0 + w), n, NULL, d, 1);
        put_value(e, &c->expr, 1, FIX_ABS32, 0);
    } else {
        fail(e, "invalid shift count");
    }
}

// Branch to a label: short or near as layout decided
static void enc_branch(Enc *e, const Stmt *st, int short_op, int near0, int near1) {
    const Expr *x = &st->ops[0].expr;
    if (!st->long_jump) {
        put(e, (uint8_t)short_op);
        put_value(e, x, 1, FIX_PC8, 1);
        if (x->sym == NO_SYM) fail(e, "a jump needs a label");
        return;
    }
    if (near0 >= 0) put(e, (uint8_t)near0);
    put(e, (uint8_t)near1);
    put_value(e, x, 4, FIX_PC32, 4);
    if (x->sym == NO_SYM) fail(e, "a jump needs a label");
}

int x86_encode(const Stmt *st, uint8_t *out, Fixup *fixups, int *nfixups, const char **error) {
    Enc e = { out, 0, fixups, 0, NULL };
    const Operand *a = st->nops > 0 ? &st->ops[0] : NULL;
    const Operand *b = st->nops > 1 ? &st->ops[1] : NULL;
    const Operand *c = st->nops > 2 ? &st->ops[2] : NULL;
    int m = st->mnemonic;
    if (st->prefix) put(&e, (uint8_t)st->prefix);

    #define NEED(n) do { if (st->nops != (n)) { fail(&e, "wrong number of operands"); goto done; } } while (0)
    if (m <= M_CMP) {
        NEED(2);
        enc_alu(&e, m, a, b);
    } else if (m >= M_ROL && m <= M_SAR) {
        NEED(2);
        static const int ext[] = { 0, 1, 2, 3, 4, 5, 4, 7 };
        enc_shift(&e, ext[m - M_ROL], a, b);
    } else if (m >= M_JCC && m < M_JCC + 16) {
        NEED(1);
        enc_branch(&e, st, 0x70 + (m - M_JCC), 0x0F, 0x80 + (m - M_JCC));
    } else if (m >= M_SETCC && m < M_SETCC + 16) {
        NEED(1);
        if (operand_size(&e, a, NULL) != 1) fail(&e, "setcc needs a byte operand");
        op2(&e, 1, 0x0F, (uint8_t)(0x90 + (m - M_SETCC)), 0, NULL, a, 0);
    } else if (m >= M_CMOVCC && m < M_CMOVCC + 16) {
        NEED(2);
        if (!is_reg(a) || !is_rm(b)) fail(&e, "invalid operands");
        else op2(&e, operand_size(&e, a, b), 0x0F, (uint8_t)(0x40 + (m - M_CMOVCC)), a->reg, a, b, 0);
    } else switch (m) {
        case M_MOV:
            NEED(2);
            enc_mov(&e, a, b);
            break;
        case M_LEA:
            NEED(2);
            if (!is_reg(a) || !is_mem(b)) { fail(&e, "invalid operands"); break; }
            op1(&e, a->size, 0x8D, a->reg, a, b, 0);
            break;
        case M_TEST:
            NEED(2);
            enc_test(&e, a, b);
            break;
        case M_MOVZX:
        case M_MOVSX: {
            NEED(2);
            if (!is_reg(a) || !is_rm(b)) { fail(&e, "invalid operands"); break; }
            int from = b->size;
            if (from != 1 && from != 2) { fail(&e, "movzx / movsx need a byte or word source"); break; }
            uint8_t opc = (uint8_t)((m == M_MOVZX ? 0xB6 : 0xBE) + (from == 2 ? 1 : 0));
            op2(&e, a->size, 0x0F, opc, a->reg, a, b, 0);
            break;
        }
        case M_MOVSXD:
            NEED(2);
            if (!is_reg(a) || !is_rm(b)) { fail(&e, "invalid operands"); break; }
            op1(&e, 8, 0x63, a->reg, a, b, 0);
            break;
        case M_INC:
        case M_DEC: {
            NEED(1);
            int size = operand_size(&e, a, NULL);
            op1(&e, size, size == 1 ? 0xFE : 0xFF, m == M_INC ? 0 : 1, NULL, a, 0);
            break;
        }
        case M_NOT: case M_NEG: case M_MUL: case M_DIV: case M_IDIV: {
            NEED(1);
            static const int ext[] = { [M_NOT] = 2, [M_NEG] = 3, [M_MUL] = 4, [M_DIV] = 6, [M_IDIV] = 7 };
            int size = operand_size(&e, a, NULL);
            op1(&e, size, size == 1 ? 0xF6 : 0xF7, ext[m], NULL, a, 0);
            break;
        }
        case M_IMUL:
            if (st->nops == 1) {
                int size = operand_size(&e, a, NULL);
                op1(&e, size, size == 1 ? 0xF6 : 0xF7, 5, NULL, a, 0);
            } else if (st->nops == 2 && is_reg(a) && is_rm(b)) {
                op2(&e, operand_size(&e, a, b), 0x0F, 0xAF, a->reg, a, b, 0);
            } else if (st->nops == 3 && is_reg(a) && is_rm(b) && is_imm(c)) {
                int size = operand_size(&e, a, b);
                int64_t v = sized(c->expr.value, size);
                if (c->expr.sym == NO_SYM && fits8(v)) {
                    op1(&e, size, 0x6B, a->reg, a, b, 1);
                    Expr x = EXPR_CONST(v);
                    put_value(&e, &x, 1, FIX_ABS32, 0);
                } else {
                    op1(&e, size, 0x69, a->reg, a, b, size == 2 ? 2 : 4);
                    put_imm(&e, &c->expr, size);
                }
            } else {
                fail(&e, "invalid operands");
            }
            break;
        case M_PUSH:
        case M_POP:
            NEED(1);
            if (is_reg(a)) {
                if (a->size != 8) { fail(&e, "push / pop need a 64-bit register"); break; }
                if (a->reg >= 8) put(&e, 0x41);
                put(&e, (uint8_t)((m == M_PUSH ? 0x50 : 0x58) + (a->reg & 7)));
            } else if (is_mem(a)) {
                op1(&e, 4, m == M_PUSH ? 0xFF : 0x8F, m == M_PUSH ? 6 : 0, NULL, a, 0);
            } else if (m == M_PUSH && a->expr.sym == NO_SYM && fits8(a->expr.value)) {
                put(&e, 0x6A);
                put(&e, (uint8_t)a->expr.value);
            } else if (m == M_PUSH) {
                put(&e, 0x68);
                put_value(&e, &a->expr, 4, FIX_ABS32S, 0);
            } else {
                fail(&e, "invalid operand");
            }
            break;
        case M_CALL:
            NEED(1);
            if (is_imm(a)) {
                put(&e, 0xE8);
                put_value(&e, &a->expr, 4, FIX_PC32, 4);
            } else {
                op1(&e, 4, 0xFF, 2, NULL, a, 0);            // 64-bit by default
            }
            break;
        case M_JMP:
            NEED(1);
            if (is_imm(a)) enc_branch(&e, st, 0xEB, -1, 0xE9);
            else op1(&e, 4, 0xFF, 4, NULL, a, 0);
            break;
        case M_BT:
            NEED(2);
            if (is_imm(b)) {
                op2(&e, operand_size(&e, a, NULL), 0x0F, 0xBA, 4, NULL, a, 1);
                put_value(&e, &b->expr, 1, FIX_ABS32, 0);
            } else if (is_reg(b)) {
                op2(&e, operand_size(&e, a, b), 0x0F, 0xA3, b->reg, b, a, 0);
            } else {
                fail(&e, "invalid operands");
            }
            break;
        case M_XCHG:
            NEED(2);
            if (is_mem(a)) { const Operand *t = a; a = b; b = t; }
            if (!is_reg(a)) { fail(&e, "invalid operands"); break; }
            {
                int size = operand_size(&e, a, b);
                op1(&e, size, size == 1 ? 0x86 : 0x87, a->reg, a, b, 0);
            }
            break;
        case M_BSWAP:
            NEED(1);
            if (!is_reg(a) || (a->size != 4 && a->size != 8)) { fail(&e, "bswap needs a 32 or 64-bit register"); break; }
            rex_opreg(&e, a->size, a);
            put(&e, 0x0F);
            put(&e, (uint8_t)(0xC8 + (a->reg & 7)));
            break;
        case M_RET:     NEED(0); put(&e, 0xC3); break;
        case M_LEAVE:   NEED(0); put(&e, 0xC9); break;
        case M_CQO:     NEED(0); put(&e, 0x48); put(&e, 0x99); break;
        case M_CDQ:     NEED(0); put(&e, 0x99); break;
        case M_CDQE:    NEED(0); put(&e, 0x48); put(&e, 0x98); break;
        case M_SYSCALL: NEED(0); put(&e, 0x0F); put(&e, 0x05); break;
        case M_CLD:     NEED(0); put(&e, 0xFC); break;
        case M_STD:     NEED(0); put(&e, 0xFD); break;
        case M_NOP:     NEED(0); put(&e, 0x90); break;
        case M_HLT:     NEED(0); put(&e, 0xF4); break;
        case M_CMPSB:   NEED(0); put(&e, 0xA6); break;
        case M_SCASB:   NEED(0); put(&e, 0xAE); break;
        case M_MOVSB:   NEED(0); put(&e, 0xA4); break;
        case M_MOVSQ:   NEED(0); put(&e, 0x48); put(&e, 0xA5); break;
        case M_STOSB:   NEED(0); put(&e, 0xAA); break;
        case M_STOSQ:   NEED(0); put(&e, 0x48); put(&e, 0xAB); break;
        default:
            fail(&e, "unsupported instruction");
    }
    #undef NEED
done:
    if (e.len > MAX_INSTR) fail(&e, "instruction too long");
    *nfixups = e.nfix;
    *error = e.err;
    return e.err ? -1 : e.len;
}
