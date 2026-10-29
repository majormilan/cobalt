/* src/asm/asm_internal.h: data shared by the assembler's parts */
#ifndef COBALT_ASM_INTERNAL_H
#define COBALT_ASM_INTERNAL_H

#include "asm.h"
#include "util.h"
#include <stddef.h>
#include <stdint.h>

// ── Expressions: constant + symbol (+ registers inside memory operands) ─────

#define NO_SYM (-1)
#define NO_REG (-1)

typedef struct {
    int64_t value;
    int sym;                    // symbol added to value, or NO_SYM
    int regs[2], coef[2];       // registers with their factors (memory only)
    int nregs;
    int neg_sym;                // symbol subtracted (sym - neg_sym: a label difference), or NO_SYM
} Expr;

#define EXPR_CONST(v) ((Expr){ (v), NO_SYM, {0, 0}, {0, 0}, 0, NO_SYM })

// ── Operands and statements ──────────────────────────────────────────────────

enum { OP_REG, OP_IMM, OP_MEM };

typedef struct {
    int kind;
    int size;                   // bytes: register size or BYTE/WORD/DWORD/QWORD; 0 unknown
    int reg;                    // OP_REG: 0..15
    bool needs_rex;             // spl bpl sil dil
    bool high8;                 // ah ch dh bh
    Expr expr;                  // OP_IMM: the value; OP_MEM: the displacement
    int base, index, scale;     // OP_MEM (NO_REG when absent)
    bool rip;                   // OP_MEM: [rel ...]
} Operand;

enum { ST_INSTR, ST_DATA, ST_RESERVE, ST_ALIGN, ST_LABEL };

typedef struct {
    int width;                  // 1, 2, 4 or 8 bytes per item
    Expr *items;                // numbers or symbol addresses
    char **strings;             // per item: string bytes (NULL for a number)
    size_t *string_lens;
    int count;
} DataList;

typedef struct {
    int kind;
    int section;
    int line;
    const char *file;
    int64_t times;              // TIMES count (1 without)
    // ST_INSTR
    int mnemonic;               // index into the mnemonic table (x86_64.c)
    int prefix;                 // 0, or 0xF3 for REP
    int nops;
    Operand ops[3];
    bool long_jump;             // jump encoded with a 32-bit displacement
    bool has_difference;        // an operand holds a label difference (known after layout)
    // ST_DATA
    DataList data;
    // ST_RESERVE / ST_ALIGN / ST_LABEL
    int64_t amount;             // bytes to reserve; alignment; the label's symbol
    // layout
    uint64_t offset;            // in its section
    uint64_t size;              // bytes (all TIMES repetitions)
} Stmt;

// ── Sections, symbols, relocations ───────────────────────────────────────────

#define R_X86_64_64    1
#define R_X86_64_PC32  2
#define R_X86_64_32    10
#define R_X86_64_32S   11

typedef struct {
    uint64_t offset;
    int sym;                    // symbol index
    int type;
    int64_t addend;
} Reloc;

typedef struct {
    char *name;
    bool nobits;                // .bss: size only
    bool exec, write;
    uint64_t align;
    Bytes data;
    uint64_t size;
    Reloc *relocs;
    int nrelocs, crelocs;
} Section;

typedef struct {
    char *name;
    int section;                // -1 when not defined here
    int stmt;                   // defining statement (its offset gives the value)
    uint64_t value;
    bool defined, global, external, referenced;
    bool is_equ;                // an EQU: stands for equ_value
    Expr equ_value;
} Symbol;

struct AsmObject {
    Section *sections;
    int nsections;
    Symbol *symbols;
    int nsymbols, csymbols;
    NameMap symbol_names;
    char *source_name;
    char **included;            // NULL-terminated
};

// ── Encoding (x86_64.c) ──────────────────────────────────────────────────────

// A field of an encoded instruction that needs a symbol's address
enum { FIX_ABS32S, FIX_ABS32, FIX_ABS64, FIX_PC32, FIX_PC8 };

typedef struct {
    int at;                     // offset of the field in the instruction
    int kind;
    Expr expr;
    int64_t pc_adjust;          // PC-relative: bytes from the field to the instruction's end
} Fixup;

#define MAX_INSTR 16
#define MAX_FIXUPS 2

int x86_mnemonic(const char *name, size_t len);        // -1 if unknown
bool x86_is_jump(int mnemonic);                         // jmp / jcc with a label
int x86_branch_size(int mnemonic, bool long_jump);      // bytes of a jmp / jcc to a label
// Encode one instruction into out; returns its length or -1 with *error set.
int x86_encode(const Stmt *st, uint8_t *out, Fixup *fixups, int *nfixups, const char **error);

#endif
