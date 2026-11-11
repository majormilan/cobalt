/* src/asm/asm.c: the COBALT assembler's driver.
 *
 * Preprocesses (%define, %include, %ifdef / %ifndef / %else / %endif,
 * %error), parses NASM-style lines into statements, lays the sections out
 * (shrinking jumps to their short form where they reach, repeated until
 * nothing changes) and emits section contents and relocations. The supported
 * syntax is described in docs/assembler.md. */

#include "asm_internal.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ── Assembler state ──────────────────────────────────────────────────────────

typedef struct {
    AsmObject *obj;
    const AsmOptions *opts;
    Stmt *stmts;
    int nstmts, cstmts;
    int section;                // current section
    NameMap defines;            // %define name -> index into define_values
    char **define_values;
    int ndefines;
    int npositions;             // hidden labels made for $
    char last_label[256];       // for local labels (.name)
    bool default_rel;
    int errors;
    const char *file;           // position for messages
    int line;
    int nincluded;
    // %macro definitions: name, number of parameters, body (lines)
    struct { char *name; int nargs; char *body; } *macros;
    int nmacros, cmacros;
    int expansions;             // for the %%labels of each expansion
} Asm;

static void error_at(Asm *as, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: error: ", as->file, as->line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    as->errors++;
}

static int section_index(Asm *as, const char *name, size_t len) {
    AsmObject *o = as->obj;
    for (int s = 0; s < o->nsections; s++)
        if (strlen(o->sections[s].name) == len && strncmp(o->sections[s].name, name, len) == 0) return s;
    o->sections = realloc(o->sections, (size_t)(o->nsections + 1) * sizeof(Section));
    Section *sec = &o->sections[o->nsections];
    memset(sec, 0, sizeof(*sec));
    sec->name = strndup(name, len);
    // nasm's ELF defaults for the standard names
    if (strcmp(sec->name, ".text") == 0) { sec->exec = true; sec->align = 16; }
    else if (strcmp(sec->name, ".data") == 0) { sec->write = true; sec->align = 4; }
    else if (strcmp(sec->name, ".bss") == 0) { sec->write = true; sec->nobits = true; sec->align = 4; }
    else if (strcmp(sec->name, ".rodata") == 0) { sec->align = 4; }
    else sec->align = 1;
    return o->nsections++;
}

static int symbol_index(Asm *as, const char *name, size_t len) {
    AsmObject *o = as->obj;
    int k = namemap_get(&o->symbol_names, name, len);
    if (k >= 0) return k;
    if (o->nsymbols == o->csymbols) {
        o->csymbols = o->csymbols ? o->csymbols * 2 : 256;
        o->symbols = realloc(o->symbols, (size_t)o->csymbols * sizeof(Symbol));
    }
    Symbol *sym = &o->symbols[o->nsymbols];
    memset(sym, 0, sizeof(*sym));
    sym->name = strndup(name, len);
    sym->section = -1;
    namemap_put(&o->symbol_names, name, len, o->nsymbols);
    return o->nsymbols++;
}

// A label name as written: a local label (.x) belongs to the last plain label
static int label_symbol(Asm *as, const char *name, size_t len) {
    if (name[0] == '.' && !(len > 1 && name[1] == '.')) {
        char full[512];
        int n = snprintf(full, sizeof(full), "%s%.*s", as->last_label, (int)len, name);
        return symbol_index(as, full, (size_t)n);
    }
    return symbol_index(as, name, len);
}

// A statement at the current position. Statements are built in a local
// copy and appended when complete: parsing an operand can append a label ($).
static Stmt stmt_here(Asm *as, int kind) {
    Stmt st;
    memset(&st, 0, sizeof(st));
    for (int k = 0; k < 3; k++) st.ops[k].expr = EXPR_CONST(0);
    st.kind = kind;
    st.section = as->section;
    st.line = as->line;
    st.file = as->file;
    st.times = 1;
    return st;
}

static void push_stmt(Asm *as, const Stmt *st) {
    if (as->nstmts == as->cstmts) {
        as->cstmts = as->cstmts ? as->cstmts * 2 : 1024;
        as->stmts = realloc(as->stmts, (size_t)as->cstmts * sizeof(Stmt));
    }
    as->stmts[as->nstmts++] = *st;
}

// ── Lexical helpers ──────────────────────────────────────────────────────────

static bool ident_start(char c) { return isalpha((unsigned char)c) || c == '_' || c == '.' || c == '$' || c == '?' || c == '@'; }
static bool ident_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$' || c == '?' || c == '@' || c == '#'; }

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// p[0..len) equals the lower-case word w, ignoring case
// (inline, so strlen of a literal w is a constant: most calls end at the
// length test)
static inline bool word_is(const char *p, size_t len, const char *w) {
    if (len != strlen(w)) return false;
    for (size_t k = 0; k < len; k++)
        if (tolower((unsigned char)p[k]) != w[k]) return false;
    return true;
}

// ── Registers ────────────────────────────────────────────────────────────────

typedef struct { const char *name; int num, size; bool needs_rex, high8; } RegInfo;

static const RegInfo registers[] = {
    {"st0",0,10,false,false}, {"st1",1,10,false,false}, {"st2",2,10,false,false}, {"st3",3,10,false,false},
    {"st4",4,10,false,false}, {"st5",5,10,false,false}, {"st6",6,10,false,false}, {"st7",7,10,false,false},
    {"rax",0,8,false,false}, {"rcx",1,8,false,false}, {"rdx",2,8,false,false}, {"rbx",3,8,false,false}, {"rsp",4,8,false,false}, {"rbp",5,8,false,false}, {"rsi",6,8,false,false}, {"rdi",7,8,false,false},
    {"r8",8,8,false,false}, {"r9",9,8,false,false}, {"r10",10,8,false,false}, {"r11",11,8,false,false}, {"r12",12,8,false,false}, {"r13",13,8,false,false}, {"r14",14,8,false,false}, {"r15",15,8,false,false},
    {"eax",0,4,false,false}, {"ecx",1,4,false,false}, {"edx",2,4,false,false}, {"ebx",3,4,false,false}, {"esp",4,4,false,false}, {"ebp",5,4,false,false}, {"esi",6,4,false,false}, {"edi",7,4,false,false},
    {"r8d",8,4,false,false}, {"r9d",9,4,false,false}, {"r10d",10,4,false,false}, {"r11d",11,4,false,false}, {"r12d",12,4,false,false}, {"r13d",13,4,false,false}, {"r14d",14,4,false,false}, {"r15d",15,4,false,false},
    {"ax",0,2,false,false}, {"cx",1,2,false,false}, {"dx",2,2,false,false}, {"bx",3,2,false,false}, {"sp",4,2,false,false}, {"bp",5,2,false,false}, {"si",6,2,false,false}, {"di",7,2,false,false},
    {"r8w",8,2,false,false}, {"r9w",9,2,false,false}, {"r10w",10,2,false,false}, {"r11w",11,2,false,false}, {"r12w",12,2,false,false}, {"r13w",13,2,false,false}, {"r14w",14,2,false,false}, {"r15w",15,2,false,false},
    {"al",0,1,false,false}, {"cl",1,1,false,false}, {"dl",2,1,false,false}, {"bl",3,1,false,false}, {"spl",4,1,true,false}, {"bpl",5,1,true,false}, {"sil",6,1,true,false}, {"dil",7,1,true,false},
    {"r8b",8,1,false,false}, {"r9b",9,1,false,false}, {"r10b",10,1,false,false}, {"r11b",11,1,false,false}, {"r12b",12,1,false,false}, {"r13b",13,1,false,false}, {"r14b",14,1,false,false}, {"r15b",15,1,false,false},
    {"ah",4,1,false,true}, {"ch",5,1,false,true}, {"dh",6,1,false,true}, {"bh",7,1,false,true},
};

// ── Reserved words ───────────────────────────────────────────────────────────
// Registers, directives and mnemonics, found with one hash lookup of the
// lower-cased word. Mnemonics are added the first time a word is looked up.

enum { W_REGISTER = 1, W_DIRECTIVE, W_MNEMONIC, W_PLAIN };     // value: kind << 16 | index

static NameMap word_table;

static const char *const directives[] = {
    "section", "segment", "global", "extern", "align", "alignb", "default", "bits", "incbin",
    "times", "rep", "repe", "repz", "repne", "repnz", "db", "dw", "dd", "dq",
    "resb", "resw", "resd", "resq", "equ", NULL };

static int lookup_word(const char *p, size_t len, int *index) {
    char lower[24];
    if (len == 0 || len >= sizeof(lower)) return W_PLAIN;
    for (size_t k = 0; k < len; k++) lower[k] = (char)tolower((unsigned char)p[k]);
    if (!word_table.cap) {
        for (size_t k = 0; k < sizeof(registers) / sizeof(registers[0]); k++)
            namemap_put(&word_table, registers[k].name, strlen(registers[k].name), W_REGISTER << 16 | (int)k);
        for (int k = 0; directives[k]; k++)
            namemap_put(&word_table, directives[k], strlen(directives[k]), W_DIRECTIVE << 16 | k);
    }
    int v = namemap_get(&word_table, lower, len);
    if (v < 0) {
        int m = x86_mnemonic(lower, len);
        v = m >= 0 ? (W_MNEMONIC << 16 | m) : (W_PLAIN << 16);
        namemap_put(&word_table, lower, len, v);
    }
    *index = v & 0xFFFF;
    return v >> 16;
}

static const RegInfo *find_register(const char *p, size_t len) {
    int k;
    return lookup_word(p, len, &k) == W_REGISTER ? &registers[k] : NULL;
}

static int find_mnemonic(const char *p, size_t len) {
    int k;
    return lookup_word(p, len, &k) == W_MNEMONIC ? k : -1;
}

// ── Expressions ──────────────────────────────────────────────────────────────

typedef struct {
    Asm *as;
    const char *p;
    bool allow_regs;
    bool ok;
} ExprParser;

static Expr const_expr(int64_t v) { return EXPR_CONST(v); }

static bool is_const(const Expr *e) { return e->sym == NO_SYM && e->neg_sym == NO_SYM && e->nregs == 0; }

static void define_label(Asm *as, const char *name, size_t len);

// $: a hidden label (..@ names are not written to the object) at the
// current position
static int position_symbol(Asm *as) {
    char name[32];
    int n = snprintf(name, sizeof(name), "..@pos%d", as->npositions++);
    define_label(as, name, (size_t)n);
    return namemap_get(&as->obj->symbol_names, name, (size_t)n);
}

static Expr parse_or(ExprParser *ep);

static void expr_fail(ExprParser *ep, const char *msg) {
    if (ep->ok) error_at(ep->as, "%s", msg);
    ep->ok = false;
}

static bool parse_number(const char *p, size_t len, int64_t *out) {
    char buf[80];
    size_t n = 0;
    for (size_t k = 0; k < len && n < sizeof(buf) - 1; k++) if (p[k] != '_') buf[n++] = p[k];
    buf[n] = '\0';
    char *end;
    if (n > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        *out = (int64_t)strtoull(buf + 2, &end, 16);
    } else if (n > 2 && buf[0] == '0' && strchr("oOqQ", buf[1])) {
        *out = (int64_t)strtoull(buf + 2, &end, 8);
    } else if (n > 1 && strchr("oOqQ", buf[n - 1])) {
        buf[n - 1] = '\0';
        *out = (int64_t)strtoull(buf, &end, 8);
    } else if (n > 1 && (buf[n - 1] == 'h' || buf[n - 1] == 'H')) {
        buf[n - 1] = '\0';
        *out = (int64_t)strtoull(buf, &end, 16);
    } else if (n > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
        *out = (int64_t)strtoull(buf + 2, &end, 2);
    } else {
        *out = (int64_t)strtoull(buf, &end, 10);
    }
    return *end == '\0';
}

static Expr parse_primary(ExprParser *ep) {
    ep->p = skip_ws(ep->p);
    const char *p = ep->p;
    if (*p == '(') {
        ep->p++;
        Expr e = parse_or(ep);
        ep->p = skip_ws(ep->p);
        if (*ep->p != ')') { expr_fail(ep, "expected ')'"); return e; }
        ep->p++;
        return e;
    }
    if (*p == '-' || *p == '+' || *p == '~') {
        ep->p++;
        Expr e = parse_primary(ep);
        if (*p == '+') return e;
        if (*p == '-' && e.nregs == 0 && (e.sym != NO_SYM || e.neg_sym != NO_SYM)) {
            int t = e.sym; e.sym = e.neg_sym; e.neg_sym = t;      // -(a - b) = b - a
            e.value = -e.value;
            return e;
        }
        if (!is_const(&e)) { expr_fail(ep, "invalid operand of a unary operator"); return e; }
        e.value = *p == '-' ? -e.value : ~e.value;
        return e;
    }
    if (*p == '\'' || *p == '"' || *p == '`') {
        // A character constant: its bytes, little-endian
        char q = *p;
        const char *s = p + 1;
        int64_t v = 0;
        int n = 0;
        while (*s && *s != q) {
            if (n < 8) v |= (int64_t)(uint8_t)*s << (8 * n);
            n++;
            s++;
        }
        if (*s != q) { expr_fail(ep, "unterminated character constant"); return const_expr(0); }
        ep->p = s + 1;
        return const_expr(v);
    }
    if (*p == '$' && !ident_char(p[1])) {
        ep->p++;
        Expr e = const_expr(0);
        e.sym = position_symbol(ep->as);
        return e;
    }
    if (isdigit((unsigned char)*p)) {
        const char *s = p;
        while (isalnum((unsigned char)*s) || *s == '_') s++;
        int64_t v;
        if (!parse_number(p, (size_t)(s - p), &v)) { expr_fail(ep, "invalid number"); return const_expr(0); }
        ep->p = s;
        return const_expr(v);
    }
    if (ident_start(*p)) {
        const char *s = p;
        while (ident_char(*s)) s++;
        size_t len = (size_t)(s - p);
        ep->p = s;
        const RegInfo *r = find_register(p, len);
        if (r) {
            if (!ep->allow_regs || r->size != 8) { expr_fail(ep, "a register cannot be used here"); return const_expr(0); }
            Expr e = const_expr(0);
            e.regs[0] = r->num;
            e.coef[0] = 1;
            e.nregs = 1;
            return e;
        }
        Expr e = const_expr(0);
        e.sym = label_symbol(ep->as, p, len);
        Symbol *sym = &ep->as->obj->symbols[e.sym];
        if (sym->is_equ) return sym->equ_value;     // defined above: its value
        sym->referenced = true;                     // a label, or an EQU further down
        return e;
    }
    expr_fail(ep, "expected an expression");
    return const_expr(0);
}

static Expr parse_mul(ExprParser *ep) {
    Expr l = parse_primary(ep);
    for (;;) {
        ep->p = skip_ws(ep->p);
        char op = *ep->p;
        if (op != '*' && op != '/' && op != '%') return l;
        ep->p++;
        Expr r = parse_primary(ep);
        if (op == '*' && (is_const(&l) || is_const(&r))) {
            Expr *k = is_const(&l) ? &l : &r, *x = is_const(&l) ? &r : &l;
            if ((x->sym != NO_SYM || x->neg_sym != NO_SYM) && k->value != 1) {
                expr_fail(ep, "a symbol cannot be multiplied");
                return l;
            }
            Expr e = *x;
            e.value = x->value * k->value;
            for (int i = 0; i < e.nregs; i++) e.coef[i] *= (int)k->value;
            l = e;
        } else if (is_const(&l) && is_const(&r)) {
            if (r.value == 0) { expr_fail(ep, "division by zero"); return l; }
            l.value = op == '/' ? l.value / r.value : l.value % r.value;
        } else {
            expr_fail(ep, "invalid operands of * / %");
        }
    }
}

static Expr parse_add(ExprParser *ep) {
    Expr l = parse_mul(ep);
    for (;;) {
        ep->p = skip_ws(ep->p);
        char op = *ep->p;
        if (op != '+' && op != '-') return l;
        ep->p++;
        Expr r = parse_mul(ep);
        int sign = op == '+' ? 1 : -1;
        l.value += sign * r.value;
        // at most one symbol added and one subtracted (a label difference)
        int plus = sign > 0 ? r.sym : r.neg_sym, minus = sign > 0 ? r.neg_sym : r.sym;
        if (plus != NO_SYM) {
            if (l.sym != NO_SYM) { expr_fail(ep, "only one symbol can be added"); return l; }
            l.sym = plus;
        }
        if (minus != NO_SYM) {
            if (l.neg_sym != NO_SYM) { expr_fail(ep, "only one symbol can be subtracted"); return l; }
            l.neg_sym = minus;
        }
        for (int i = 0; i < r.nregs; i++) {
            int j;
            for (j = 0; j < l.nregs && l.regs[j] != r.regs[i]; j++) {}
            if (j == l.nregs) {
                if (l.nregs == 2) { expr_fail(ep, "too many registers in an address"); return l; }
                l.regs[l.nregs] = r.regs[i];
                l.coef[l.nregs++] = 0;
            }
            l.coef[j] += sign * r.coef[i];
        }
    }
}

static Expr parse_shift(ExprParser *ep) {
    Expr l = parse_add(ep);
    for (;;) {
        ep->p = skip_ws(ep->p);
        if (!((ep->p[0] == '<' && ep->p[1] == '<') || (ep->p[0] == '>' && ep->p[1] == '>'))) return l;
        bool left = ep->p[0] == '<';
        ep->p += 2;
        Expr r = parse_add(ep);
        if (!is_const(&l) || !is_const(&r)) { expr_fail(ep, "invalid operands of a shift"); return l; }
        l.value = left ? (int64_t)((uint64_t)l.value << r.value) : (int64_t)((uint64_t)l.value >> r.value);
    }
}

static Expr parse_or(ExprParser *ep) {
    Expr l = parse_shift(ep);
    for (;;) {
        ep->p = skip_ws(ep->p);
        char op = *ep->p;
        if (op != '|' && op != '&' && op != '^') return l;
        ep->p++;
        Expr r = parse_shift(ep);
        if (!is_const(&l) || !is_const(&r)) { expr_fail(ep, "invalid operands of a bitwise operator"); return l; }
        l.value = op == '|' ? (l.value | r.value) : op == '&' ? (l.value & r.value) : (l.value ^ r.value);
    }
}

// Parse a whole expression from text [p, end)
static bool eval(Asm *as, const char *p, const char *end, bool allow_regs, Expr *out) {
    char buf[1024];
    size_t len = (size_t)(end - p);
    if (len >= sizeof(buf)) { error_at(as, "expression too long"); return false; }
    memcpy(buf, p, len);
    buf[len] = '\0';
    ExprParser ep = { as, buf, allow_regs, true };
    *out = parse_or(&ep);
    if (ep.ok && *skip_ws(ep.p)) expr_fail(&ep, "unexpected characters after an expression");
    return ep.ok;
}

// ── Operands ─────────────────────────────────────────────────────────────────

static const char *trim_end(const char *p, const char *end) {
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
    return end;
}

static bool parse_operand(Asm *as, const char *p, const char *end, Operand *op) {
    memset(op, 0, sizeof(*op));
    op->expr = EXPR_CONST(0);
    op->base = op->index = NO_REG;
    p = skip_ws(p);
    end = trim_end(p, end);
    // size keyword, and jump distance keywords
    for (;;) {
        const char *s = p;
        while (s < end && isalpha((unsigned char)*s)) s++;
        size_t len = (size_t)(s - p);
        int size = word_is(p, len, "byte") ? 1 : word_is(p, len, "word") ? 2 :
                   word_is(p, len, "dword") ? 4 : word_is(p, len, "qword") ? 8 : 0;
        if (size && (s == end || *s == ' ' || *s == '\t' || *s == '[')) {
            op->size = size;
            p = skip_ws(s);
            continue;
        }
        if ((word_is(p, len, "short") || word_is(p, len, "near") || word_is(p, len, "strict")) &&
            s < end && (*s == ' ' || *s == '\t')) {
            p = skip_ws(s);
            continue;
        }
        break;
    }
    if (p >= end) { error_at(as, "expected an operand"); return false; }
    if (*p == '[') {
        if (end[-1] != ']') { error_at(as, "expected ']'"); return false; }
        const char *in = skip_ws(p + 1), *in_end = end - 1;
        op->kind = OP_MEM;
        op->rip = as->default_rel;
        const char *s = in;
        while (s < in_end && isalpha((unsigned char)*s)) s++;
        if (word_is(in, (size_t)(s - in), "rel") && s < in_end && (*s == ' ' || *s == '\t')) {
            op->rip = true;
            in = skip_ws(s);
        } else if (word_is(in, (size_t)(s - in), "abs") && s < in_end && (*s == ' ' || *s == '\t')) {
            op->rip = false;
            in = skip_ws(s);
        }
        Expr e;
        if (!eval(as, in, in_end, true, &e)) return false;
        // like nasm: reg*3, *5, *9 are reg + reg*2, *4, *8, and a lone reg*2
        // is reg + reg (no 32-bit displacement needed)
        int live = 0;
        for (int i = 0; i < e.nregs; i++) if (e.coef[i]) live++;
        if (live == 1) {
            int i = e.coef[0] ? 0 : 1, c = e.coef[i];
            if (c == 3 || c == 5 || c == 9 || c == 2) {
                op->base = e.regs[i];
                op->index = e.regs[i];
                op->scale = c - 1;
                e.coef[i] = 0;
            }
        }
        for (int i = 0; i < e.nregs; i++) {
            int c = e.coef[i];
            if (c == 0) continue;
            if (c == 1 && op->base == NO_REG) {
                op->base = e.regs[i];
            } else if ((c == 1 || c == 2 || c == 4 || c == 8) && op->index == NO_REG) {
                op->index = e.regs[i];
                op->scale = c;
            } else {
                error_at(as, "invalid address");
                return false;
            }
        }
        if (op->index != NO_REG && op->scale == 1 && op->index == 4 && op->base != NO_REG && op->base != 4) {
            int t = op->base; op->base = op->index; op->index = t;     // [x + rsp]: rsp as the base
        }
        if (op->base != NO_REG || op->index != NO_REG) op->rip = false;
        e.nregs = 0;
        op->expr = e;
        return true;
    }
    const char *s = p;
    while (s < end && ident_char(*s)) s++;
    if (s == end) {
        const RegInfo *r = find_register(p, (size_t)(end - p));
        if (r) {
            op->kind = OP_REG;
            op->reg = r->num;
            op->size = r->size;
            op->needs_rex = r->needs_rex;
            op->high8 = r->high8;
            return true;
        }
    }
    op->kind = OP_IMM;
    return eval(as, p, end, false, &op->expr);
}

// Split [p, end) at top-level commas (outside brackets, parentheses and quotes)
static int split_commas(const char *p, const char *end, const char **starts, const char **ends, int max) {
    int n = 0, depth = 0;
    char quote = 0;
    const char *start = p;
    for (const char *s = p; s < end; s++) {
        if (quote) { if (*s == quote) quote = 0; continue; }
        if (*s == '\'' || *s == '"' || *s == '`') quote = *s;
        else if (*s == '[' || *s == '(') depth++;
        else if (*s == ']' || *s == ')') depth--;
        else if (*s == ',' && depth == 0) {
            if (n < max) { starts[n] = start; ends[n] = s; }
            n++;
            start = s + 1;
        }
    }
    if (skip_ws(start) < end || n > 0) {
        if (n < max) { starts[n] = start; ends[n] = end; }
        n++;
    }
    return n;
}

// ── Statements ───────────────────────────────────────────────────────────────

static int data_width(const char *p, size_t len) {
    if (word_is(p, len, "db")) return 1;
    if (word_is(p, len, "dw")) return 2;
    if (word_is(p, len, "dd")) return 4;
    if (word_is(p, len, "dq")) return 8;
    return 0;
}

static int reserve_width(const char *p, size_t len) {
    if (word_is(p, len, "resb")) return 1;
    if (word_is(p, len, "resw")) return 2;
    if (word_is(p, len, "resd")) return 4;
    if (word_is(p, len, "resq")) return 8;
    return 0;
}

static void parse_data(Asm *as, Stmt *st, int width, const char *p, const char *end) {
    st->kind = ST_DATA;
    st->data.width = width;
    const char *starts[4096], *ends[4096];
    int n = split_commas(p, end, starts, ends, 4096);
    if (n > 4096) { error_at(as, "too many data items on one line"); return; }
    st->data.items = calloc((size_t)n, sizeof(Expr));
    st->data.strings = calloc((size_t)n, sizeof(char *));
    st->data.string_lens = calloc((size_t)n, sizeof(size_t));
    st->data.count = n;
    for (int k = 0; k < n; k++) st->data.items[k] = EXPR_CONST(0);
    for (int k = 0; k < n; k++) {
        const char *s = skip_ws(starts[k]), *e = trim_end(s, ends[k]);
        if (e > s + 1 && (*s == '"' || *s == '\'' || *s == '`') && e[-1] == *s &&
            !memchr(s + 1, *s, (size_t)(e - s - 2))) {
            // A string: its bytes, padded with zeros to a multiple of the item width
            st->data.strings[k] = strndup(s + 1, (size_t)(e - s - 2));
            st->data.string_lens[k] = (size_t)(e - s - 2);
            continue;
        }
        if (!eval(as, s, e, false, &st->data.items[k])) return;
        const Expr *x = &st->data.items[k];
        if (x->sym != NO_SYM && x->neg_sym == NO_SYM && width != 4 && width != 8)
            error_at(as, "a symbol address needs dd or dq");
    }
}

// Read a whole file; *len gets its size. NULL if it cannot be read.
static char *read_file_bytes(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) { buf[n] = '\0'; *len = (size_t)n; }
    fclose(f);
    return buf;
}

// Find a file named in %include or incbin: next to the current file, then in
// each -I directory (a prefix, as in nasm), then as given. Records it for
// dependency output.
static char *find_file(Asm *as, const char *name, size_t len, size_t *size, const char **found) {
    char path[1024];
    char *data = NULL;
    const char *slash = strrchr(as->file, '/');
    if (slash) {
        snprintf(path, sizeof(path), "%.*s/%.*s", (int)(slash - as->file), as->file, (int)len, name);
        data = read_file_bytes(path, size);
    }
    for (int k = 0; !data && as->opts && as->opts->include_dirs && as->opts->include_dirs[k]; k++) {
        snprintf(path, sizeof(path), "%s%.*s", as->opts->include_dirs[k], (int)len, name);
        data = read_file_bytes(path, size);
    }
    if (!data) {
        snprintf(path, sizeof(path), "%.*s", (int)len, name);
        data = read_file_bytes(path, size);
    }
    if (!data) return NULL;
    AsmObject *o = as->obj;
    o->included = realloc(o->included, (size_t)(as->nincluded + 2) * sizeof(char *));
    o->included[as->nincluded++] = strdup(path);
    o->included[as->nincluded] = NULL;
    *found = o->included[as->nincluded - 1];
    return data;
}

// Parse a statement after any label and TIMES prefix
static void parse_statement(Asm *as, const char *p, const char *end, int64_t times) {
    const char *s = p;
    while (s < end && (isalnum((unsigned char)*s) || *s == '_')) s++;
    size_t len = (size_t)(s - p);
    const char *rest = skip_ws(s);
    int w;
    if ((w = data_width(p, len))) {
        Stmt st = stmt_here(as, ST_DATA);
        st.times = times;
        parse_data(as, &st, w, rest, end);
        push_stmt(as, &st);
        return;
    }
    if (word_is(p, len, "incbin")) {
        // incbin "file": the file's bytes as data
        const char *q = rest, *qe = trim_end(rest, end);
        if (qe - q < 2 || (*q != '"' && *q != '\'') || qe[-1] != *q) {
            error_at(as, "expected a quoted file name after incbin");
            return;
        }
        size_t size;
        const char *found;
        char *bytes = find_file(as, q + 1, (size_t)(qe - q - 2), &size, &found);
        if (!bytes) { error_at(as, "cannot open incbin file '%.*s'", (int)(qe - q - 2), q + 1); return; }
        Stmt st = stmt_here(as, ST_DATA);
        st.times = times;
        st.data.width = 1;
        st.data.count = 1;
        st.data.items = calloc(1, sizeof(Expr));
        st.data.items[0] = EXPR_CONST(0);
        st.data.strings = calloc(1, sizeof(char *));
        st.data.string_lens = calloc(1, sizeof(size_t));
        st.data.strings[0] = bytes;
        st.data.string_lens[0] = size;
        push_stmt(as, &st);
        return;
    }
    if ((w = reserve_width(p, len))) {
        Expr e;
        if (!eval(as, rest, end, false, &e)) return;
        if (!is_const(&e) || e.value < 0) { error_at(as, "invalid reserve count"); return; }
        Stmt st = stmt_here(as, ST_RESERVE);
        st.times = times;
        st.amount = e.value * w;
        push_stmt(as, &st);
        return;
    }
    if (word_is(p, len, "align") || word_is(p, len, "alignb")) {
        Expr e;
        if (!eval(as, rest, end, false, &e)) return;
        if (!is_const(&e) || e.value <= 0 || (e.value & (e.value - 1))) { error_at(as, "alignment must be a power of 2"); return; }
        Stmt st = stmt_here(as, ST_ALIGN);
        st.amount = e.value;
        push_stmt(as, &st);
        Section *sec = &as->obj->sections[as->section];
        if ((uint64_t)e.value > sec->align) sec->align = (uint64_t)e.value;
        return;
    }
    int prefix = 0;
    if (word_is(p, len, "rep") || word_is(p, len, "repe") || word_is(p, len, "repz")) prefix = 0xF3;
    else if (word_is(p, len, "repne") || word_is(p, len, "repnz")) prefix = 0xF2;
    if (prefix) {
        p = rest;
        s = p;
        while (s < end && isalnum((unsigned char)*s)) s++;
        len = (size_t)(s - p);
        rest = skip_ws(s);
    }
    int m = find_mnemonic(p, len);
    if (m < 0) { error_at(as, "unknown instruction '%.*s'", (int)len, p); return; }
    Stmt st = stmt_here(as, ST_INSTR);
    st.times = times;
    st.mnemonic = m;
    st.prefix = prefix;
    if (rest < end) {
        const char *starts[4], *ends[4];
        int n = split_commas(rest, end, starts, ends, 4);
        if (n > 3) { error_at(as, "too many operands"); return; }
        for (int k = 0; k < n; k++) {
            if (!parse_operand(as, starts[k], ends[k], &st.ops[k])) return;
            if (st.ops[k].expr.neg_sym != NO_SYM) st.has_difference = true;
        }
        st.nops = n;
    }
    push_stmt(as, &st);
}

static void define_label(Asm *as, const char *name, size_t len) {
    // a plain label is the base of the local labels (.x) after it; hidden
    // (..@) labels are not
    if (name[0] != '.' && len < sizeof(as->last_label)) {
        memcpy(as->last_label, name, len);
        as->last_label[len] = '\0';
    }
    int k = label_symbol(as, name, len);
    Symbol *sym = &as->obj->symbols[k];
    if (sym->defined) { error_at(as, "label '%s' is already defined", sym->name); return; }
    if (sym->external) { error_at(as, "label '%s' is declared extern", sym->name); return; }
    sym->defined = true;
    sym->section = as->section;
    Stmt st = stmt_here(as, ST_LABEL);
    st.amount = k;
    push_stmt(as, &st);
}

static bool is_keyword(const char *p, size_t len) {
    int k;
    int kind = lookup_word(p, len, &k);
    return kind == W_DIRECTIVE || kind == W_MNEMONIC;
}

static void parse_line(Asm *as, const char *p, const char *end) {
    p = skip_ws(p);
    end = trim_end(p, end);
    if (p >= end) return;

    // label:   or   label <data directive / equ>
    if (ident_start(*p)) {
        const char *s = p;
        while (s < end && ident_char(*s)) s++;
        size_t len = (size_t)(s - p);
        if (s < end && *s == ':') {
            define_label(as, p, len);
            p = skip_ws(s + 1);
            if (p >= end) return;
        } else if (!is_keyword(p, len)) {
            const char *n = skip_ws(s), *ne = n;
            while (ne < end && isalpha((unsigned char)*ne)) ne++;
            size_t nlen = (size_t)(ne - n);
            if (word_is(n, nlen, "equ")) {
                Expr e;
                if (!eval(as, ne, end, false, &e)) return;
                if (e.nregs || e.sym != NO_SYM ? e.neg_sym == NO_SYM : e.neg_sym != NO_SYM) {
                    error_at(as, "EQU needs a number or a label difference");
                    return;
                }
                int k = symbol_index(as, p, len);
                Symbol *sym = &as->obj->symbols[k];
                if (sym->defined || sym->is_equ) { error_at(as, "'%.*s' is already defined", (int)len, p); return; }
                sym->is_equ = true;
                sym->equ_value = e;
                return;
            }
            if (data_width(n, nlen) || reserve_width(n, nlen) || word_is(n, nlen, "times")) {
                define_label(as, p, len);
                p = n;
            }
        }
    }

    const char *s = p;
    while (s < end && (isalnum((unsigned char)*s) || *s == '_')) s++;
    size_t len = (size_t)(s - p);
    const char *rest = skip_ws(s);

    // An instruction (most lines): straight to its parser
    int word_index;
    if (lookup_word(p, len, &word_index) == W_MNEMONIC) {
        parse_statement(as, p, end, 1);
        return;
    }

    if (word_is(p, len, "section") || word_is(p, len, "segment")) {
        const char *n = rest, *ne = n;
        while (ne < end && *ne != ' ' && *ne != '\t') ne++;
        if (n == ne) { error_at(as, "expected a section name"); return; }
        as->section = section_index(as, n, (size_t)(ne - n));
        // attributes: align=N (others are accepted and ignored)
        for (const char *a = skip_ws(ne); a < end;) {
            const char *ae = a;
            while (ae < end && *ae != ' ' && *ae != '\t') ae++;
            if ((size_t)(ae - a) > 6 && strncasecmp(a, "align=", 6) == 0)
                as->obj->sections[as->section].align = strtoull(a + 6, NULL, 0);
            else if (word_is(a, (size_t)(ae - a), "nobits"))
                as->obj->sections[as->section].nobits = true;
            a = skip_ws(ae);
        }
        return;
    }
    if (word_is(p, len, "global") || word_is(p, len, "extern")) {
        bool global = tolower((unsigned char)p[0]) == 'g';
        const char *starts[64], *ends[64];
        int n = split_commas(rest, end, starts, ends, 64);
        for (int k = 0; k < n && k < 64; k++) {
            const char *a = skip_ws(starts[k]), *ae = a;
            while (ae < ends[k] && ident_char(*ae)) ae++;     // drops :function and similar
            if (a == ae) { error_at(as, "expected a symbol name"); continue; }
            int k = symbol_index(as, a, (size_t)(ae - a));     // may move the symbol array
            Symbol *sym = &as->obj->symbols[k];
            if (global) sym->global = true;
            else if (!sym->defined) sym->external = true;
        }
        return;
    }
    if (word_is(p, len, "default")) {
        const char *ne = rest;
        while (ne < end && isalpha((unsigned char)*ne)) ne++;
        if (word_is(rest, (size_t)(ne - rest), "rel")) as->default_rel = true;
        else if (word_is(rest, (size_t)(ne - rest), "abs")) as->default_rel = false;
        else error_at(as, "expected DEFAULT REL or DEFAULT ABS");
        return;
    }
    if (word_is(p, len, "bits")) {
        if (strncmp(rest, "64", 2) != 0) error_at(as, "only BITS 64 is supported");
        return;
    }
    int64_t times = 1;
    if (word_is(p, len, "times")) {
        // TIMES count statement: the count ends where the statement word starts
        const char *q = rest;
        const char *best = NULL;
        for (const char *t = q; t < end; t++) {
            if ((t == q || t[-1] == ' ' || t[-1] == '\t' || t[-1] == ')') && isalpha((unsigned char)*t)) {
                const char *te = t;
                while (te < end && isalnum((unsigned char)*te)) te++;
                if (is_keyword(t, (size_t)(te - t))) { best = t; break; }
            }
        }
        if (!best) { error_at(as, "expected a statement after TIMES count"); return; }
        Expr e;
        if (!eval(as, q, best, false, &e)) return;
        if (!is_const(&e) || e.value < 0) { error_at(as, "invalid TIMES count"); return; }
        times = e.value;
        p = best;
    }
    parse_statement(as, p, end, times);
}

// ── Preprocessor ─────────────────────────────────────────────────────────────

static void add_define(Asm *as, const char *name, size_t len, const char *value, size_t vlen) {
    int k = namemap_get(&as->defines, name, len);
    char *v = strndup(value, vlen);
    if (k >= 0) {
        free(as->define_values[k]);
        as->define_values[k] = v;
        return;
    }
    as->define_values = realloc(as->define_values, (size_t)(as->ndefines + 1) * sizeof(char *));
    as->define_values[as->ndefines] = v;
    namemap_put(&as->defines, name, len, as->ndefines++);
}

// Replace %define names in a line (outside quotes); false if nothing changed
static bool substitute(Asm *as, const char *in, char *out, size_t cap) {
    size_t n = 0;
    bool changed = false;
    char quote = 0;
    for (const char *p = in; *p;) {
        if (quote) {
            if (*p == quote) quote = 0;
            if (n < cap - 1) out[n++] = *p;
            p++;
            continue;
        }
        if (*p == '\'' || *p == '"' || *p == '`') {
            quote = *p;
            if (n < cap - 1) out[n++] = *p;
            p++;
            continue;
        }
        if (ident_start(*p) && (p == in || !ident_char(p[-1]))) {
            const char *s = p;
            while (ident_char(*s)) s++;
            int k = namemap_get(&as->defines, p, (size_t)(s - p));
            if (k >= 0) {
                const char *v = as->define_values[k];
                size_t vl = strlen(v);
                if (n + vl < cap - 1) { memcpy(out + n, v, vl); n += vl; }
                changed = true;
            } else {
                size_t l = (size_t)(s - p);
                if (n + l < cap - 1) { memcpy(out + n, p, l); n += l; }
            }
            p = s;
            continue;
        }
        if (n < cap - 1) out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    return changed;
}

// End of the code part of a line: before a ';' comment outside quotes
static const char *code_end(const char *p, const char *end) {
    char quote = 0;
    for (const char *s = p; s < end; s++) {
        if (quote) { if (*s == quote) quote = 0; continue; }
        if (*s == '\'' || *s == '"' || *s == '`') quote = *s;
        else if (*s == ';') return s;
    }
    return end;
}

static void assemble_text(Asm *as, const char *text, const char *file, int depth);

static void include_file(Asm *as, const char *name, size_t len, int depth) {
    size_t size;
    const char *file;
    char *text = find_file(as, name, len, &size, &file);
    if (!text) { error_at(as, "cannot open include file '%.*s'", (int)len, name); return; }
    const char *saved_file = as->file;
    int saved_line = as->line;
    assemble_text(as, text, file, depth + 1);
    as->file = saved_file;
    as->line = saved_line;
    free(text);
}

// ── Macros ───────────────────────────────────────────────────────────────────
// %macro name nargs ... %endmacro (a NASM subset): in the body %1..%9 are the
// arguments, %0 their number, %%name a label of its own in each expansion.

static int find_macro(Asm *as, const char *p, size_t len) {
    for (int k = 0; k < as->nmacros; k++)
        if (strlen(as->macros[k].name) == len && strncasecmp(as->macros[k].name, p, len) == 0) return k;
    return -1;
}

static void expand_macro(Asm *as, int m, const char *args, const char *args_end, const char *file, int depth) {
    const char *starts[16], *ends[16];
    int nargs = 0;
    const char *a = skip_ws(args);
    if (a < args_end) nargs = split_commas(a, args_end, starts, ends, 16);
    if (nargs != as->macros[m].nargs) {
        error_at(as, "macro '%s' takes %d argument%s, %d given", as->macros[m].name, as->macros[m].nargs,
                 as->macros[m].nargs == 1 ? "" : "s", nargs);
        return;
    }
    int id = ++as->expansions;
    size_t cap = strlen(as->macros[m].body) * 2 + 256, n = 0;
    char *out = malloc(cap);
    for (const char *p = as->macros[m].body; *p; p++) {
        char piece[512];
        size_t plen = 0;
        const char *add = NULL;
        if (p[0] == '%' && p[1] == '%') {
            plen = (size_t)snprintf(piece, sizeof(piece), "..@%d.", id);
            add = piece;
            p++;
        } else if (p[0] == '%' && p[1] >= '0' && p[1] <= '9') {
            int k = p[1] - '0';
            if (k == 0) { plen = (size_t)snprintf(piece, sizeof(piece), "%d", nargs); add = piece; }
            else if (k <= nargs) {
                const char *s0 = skip_ws(starts[k - 1]), *s1 = trim_end(s0, ends[k - 1]);
                plen = (size_t)(s1 - s0);
                add = s0;
            } else { plen = 0; add = ""; }
            p++;
        }
        if (!add) { add = p; plen = 1; }
        if (n + plen + 2 > cap) { cap = (n + plen) * 2 + 256; out = realloc(out, cap); }
        memcpy(out + n, add, plen);
        n += plen;
    }
    out[n] = '\0';
    int line = as->line;
    assemble_text(as, out, file, depth + 1);
    as->file = file;
    as->line = line;
    free(out);
}

static void assemble_text(Asm *as, const char *text, const char *file, int depth) {
    if (depth > 32) { error_at(as, "%%include or macro expansion nested too deeply"); return; }
    // conditional stack: active state of each open %if
    bool active[64];
    bool taken[64];
    int nif = 0;
    as->file = file;
    as->line = 0;
    char *buf = malloc(65536), *buf2 = malloc(65536);
    int recording = -1;                 // the macro whose body is being read (-2: skipped)
    size_t body_len = 0, body_cap = 0;
    char *body = NULL;
    int start_line = as->line;
    as->line = start_line;
    for (const char *p = text; *p;) {
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        as->line++;
        const char *end = code_end(p, eol);
        if (end > p && end[-1] == '\r') end--;
        const char *s = skip_ws(p);
        bool on = nif == 0 || active[nif - 1];
        if (recording != -1) {
            // the body of a macro, up to %endmacro
            if (*s == '%' && strncasecmp(s, "%endmacro", 9) == 0) {
                if (recording >= 0) {
                    if (!body) body = strdup("");
                    as->macros[recording].body = body;
                    body = NULL;
                } else {
                    free(body);
                    body = NULL;
                }
                body_len = body_cap = 0;
                recording = -1;
            } else {
                size_t len = (size_t)(eol - p);
                if (body_len + len + 2 > body_cap) { body_cap = (body_len + len) * 2 + 256; body = realloc(body, body_cap); }
                memcpy(body + body_len, p, len);
                body_len += len;
                body[body_len++] = '\n';
                body[body_len] = '\0';
            }
            p = *eol ? eol + 1 : eol;
            continue;
        }
        if (on && *s != '%' && s < end) {
            // a macro invocation: its name first on the line
            const char *ne = s;
            while (ne < end && ident_char(*ne)) ne++;
            int m = ne > s ? find_macro(as, s, (size_t)(ne - s)) : -1;
            if (m >= 0) {
                expand_macro(as, m, ne, end, file, depth);
                p = *eol ? eol + 1 : eol;
                continue;
            }
        }
        if (*s == '%' && s < end) {
            const char *d = s + 1, *de = d;
            while (de < end && isalpha((unsigned char)*de)) de++;
            size_t dl = (size_t)(de - d);
            const char *arg = skip_ws(de), *arg_end = trim_end(arg, end);
            const char *ne = arg;
            while (ne < arg_end && ident_char(*ne)) ne++;
            if (word_is(d, dl, "ifdef") || word_is(d, dl, "ifndef")) {
                if (nif == 64) { error_at(as, "%%if nested too deeply"); break; }
                bool def = namemap_get(&as->defines, arg, (size_t)(ne - arg)) >= 0;
                bool cond = word_is(d, dl, "ifdef") ? def : !def;
                active[nif] = on && cond;
                taken[nif] = cond;
                nif++;
            } else if (word_is(d, dl, "else")) {
                if (!nif) error_at(as, "%%else without %%if");
                else {
                    bool outer = nif == 1 || active[nif - 2];
                    active[nif - 1] = outer && !taken[nif - 1];
                    taken[nif - 1] = true;
                }
            } else if (word_is(d, dl, "endif")) {
                if (!nif) error_at(as, "%%endif without %%if");
                else nif--;
            } else if (!on) {
                // skipped
            } else if (word_is(d, dl, "define")) {
                if (ne == arg) error_at(as, "expected a name after %%define");
                else {
                    const char *v = skip_ws(ne);
                    add_define(as, arg, (size_t)(ne - arg), v, (size_t)(trim_end(v, arg_end) - v));
                }
            } else if (word_is(d, dl, "undef")) {
                int k = namemap_get(&as->defines, arg, (size_t)(ne - arg));
                if (k >= 0) namemap_put(&as->defines, arg, (size_t)(ne - arg), -1);    // -1: absent
            } else if (word_is(d, dl, "include")) {
                if (arg < arg_end && (*arg == '"' || *arg == '\'' || *arg == '<') && arg_end - arg >= 2)
                    include_file(as, arg + 1, (size_t)(arg_end - arg - 2), depth);
                else
                    error_at(as, "expected a quoted file name after %%include");
                as->file = file;
            } else if (word_is(d, dl, "macro")) {
                if (ne == arg) { error_at(as, "expected a name after %%macro"); recording = -2; }
                else {
                    if (as->nmacros == as->cmacros) {
                        as->cmacros = as->cmacros ? as->cmacros * 2 : 16;
                        as->macros = realloc(as->macros, (size_t)as->cmacros * sizeof(*as->macros));
                    }
                    int m = as->nmacros++;
                    as->macros[m].name = strndup(arg, (size_t)(ne - arg));
                    as->macros[m].nargs = (int)strtol(skip_ws(ne), NULL, 10);
                    as->macros[m].body = strdup("");
                    recording = m;
                }
            } else if (word_is(d, dl, "endmacro")) {
                error_at(as, "%%endmacro without %%macro");
            } else if (word_is(d, dl, "error")) {
                error_at(as, "%.*s", (int)(arg_end - arg), arg);
            } else {
                error_at(as, "unsupported preprocessor directive '%%%.*s'", (int)dl, d);
            }
        } else if (on && s < end) {
            size_t len = (size_t)(end - p);
            if (len >= 65536) { error_at(as, "line too long"); }
            else {
                memcpy(buf, p, len);
                buf[len] = '\0';
                char *cur = buf, *other = buf2;
                for (int round = 0; round < 16 && as->ndefines; round++) {
                    if (!substitute(as, cur, other, 65536)) break;
                    char *t = cur; cur = other; other = t;
                }
                parse_line(as, cur, cur + strlen(cur));
            }
        }
        p = *eol ? eol + 1 : eol;
    }
    if (nif) error_at(as, "%%if without %%endif");
    if (recording != -1) { error_at(as, "%%macro without %%endmacro"); free(body); }
    free(buf);
    free(buf2);
}

// ── Layout ───────────────────────────────────────────────────────────────────

// A label difference (a - b, both in one section) as a number, with the
// current layout (check_differences has checked the labels)
static void fold(const AsmObject *o, Expr *x) {
    if (x->neg_sym == NO_SYM) return;
    x->value += (int64_t)o->symbols[x->sym].value - (int64_t)o->symbols[x->neg_sym].value;
    x->sym = x->neg_sym = NO_SYM;
}

static Stmt folded(const AsmObject *o, const Stmt *st) {
    Stmt copy = *st;
    for (int k = 0; k < copy.nops; k++) fold(o, &copy.ops[k].expr);
    return copy;
}

static bool difference_ok(Asm *as, const Stmt *st, const Expr *x) {
    if (x->neg_sym == NO_SYM) return true;
    const Symbol *a = x->sym != NO_SYM ? &as->obj->symbols[x->sym] : NULL;
    const Symbol *b = &as->obj->symbols[x->neg_sym];
    as->file = st->file;
    as->line = st->line;
    if (!a) { error_at(as, "a subtracted label needs a label to subtract it from"); return false; }
    if (!a->defined || !b->defined || a->section != b->section) {
        error_at(as, "a label difference needs two labels of one section ('%s' - '%s')", a->name, b->name);
        return false;
    }
    return true;
}

// Replace references to EQUs defined after their use by their values
static void expand_equ(Asm *as, const Stmt *st, Expr *x) {
    for (int depth = 0; depth < 16; depth++) {
        int *slot = NULL;
        if (x->sym != NO_SYM && as->obj->symbols[x->sym].is_equ) slot = &x->sym;
        else if (x->neg_sym != NO_SYM && as->obj->symbols[x->neg_sym].is_equ) slot = &x->neg_sym;
        if (!slot) return;
        Expr v = as->obj->symbols[*slot].equ_value;
        bool minus = slot == &x->neg_sym;
        *slot = NO_SYM;
        x->value += minus ? -v.value : v.value;
        int plus = minus ? v.neg_sym : v.sym, sub = minus ? v.sym : v.neg_sym;
        if ((plus != NO_SYM && x->sym != NO_SYM) || (sub != NO_SYM && x->neg_sym != NO_SYM)) {
            as->file = st->file;
            as->line = st->line;
            error_at(as, "too many symbols in an expression");
            return;
        }
        if (plus != NO_SYM) x->sym = plus;
        if (sub != NO_SYM) x->neg_sym = sub;
    }
}

static void expand_equs(Asm *as) {
    for (int i = 0; i < as->nstmts; i++) {
        Stmt *st = &as->stmts[i];
        if (st->kind == ST_INSTR) {
            for (int k = 0; k < st->nops; k++) {
                expand_equ(as, st, &st->ops[k].expr);
                if (st->ops[k].expr.neg_sym != NO_SYM) st->has_difference = true;
            }
        } else if (st->kind == ST_DATA) {
            for (int k = 0; k < st->data.count; k++)
                if (!st->data.strings[k]) expand_equ(as, st, &st->data.items[k]);
        }
    }
}

static void check_differences(Asm *as) {
    for (int i = 0; i < as->nstmts; i++) {
        const Stmt *st = &as->stmts[i];
        if (st->kind == ST_INSTR)
            for (int k = 0; k < st->nops; k++) difference_ok(as, st, &st->ops[k].expr);
        else if (st->kind == ST_DATA)
            for (int k = 0; k < st->data.count; k++)
                if (!st->data.strings[k]) difference_ok(as, st, &st->data.items[k]);
    }
}

static uint64_t data_size(const Stmt *st) {
    uint64_t n = 0;
    for (int k = 0; k < st->data.count; k++) {
        if (st->data.strings[k]) {
            uint64_t len = st->data.string_lens[k];
            n += (len + (uint64_t)st->data.width - 1) / (uint64_t)st->data.width * (uint64_t)st->data.width;
        } else {
            n += (uint64_t)st->data.width;
        }
    }
    return n;
}

// Assign offsets and label values
static void layout(Asm *as, const uint64_t *instr_size) {
    AsmObject *o = as->obj;
    uint64_t *off = calloc((size_t)o->nsections, sizeof(uint64_t));
    for (int i = 0; i < as->nstmts; i++) {
        Stmt *st = &as->stmts[i];
        uint64_t pos = off[st->section];
        st->offset = pos;
        switch (st->kind) {
            case ST_INSTR:
                st->size = (uint64_t)st->times *
                           (x86_is_jump(st->mnemonic) && st->ops[0].kind == OP_IMM
                                ? (uint64_t)x86_branch_size(st->mnemonic, st->long_jump)
                                : instr_size[i]);
                break;
            case ST_DATA:    st->size = (uint64_t)st->times * data_size(st); break;
            case ST_RESERVE: st->size = (uint64_t)st->times * (uint64_t)st->amount; break;
            case ST_ALIGN:   st->size = (uint64_t)(-(int64_t)pos) & (uint64_t)(st->amount - 1); break;
            case ST_LABEL:
                st->size = 0;
                o->symbols[st->amount].value = pos;
                break;
        }
        off[st->section] = pos + st->size;
    }
    for (int s = 0; s < o->nsections; s++) o->sections[s].size = off[s];
    free(off);
}

// ── Emission ─────────────────────────────────────────────────────────────────

static void add_reloc(Section *sec, uint64_t offset, int sym, int type, int64_t addend) {
    if (sec->nrelocs == sec->crelocs) {
        sec->crelocs = sec->crelocs ? sec->crelocs * 2 : 64;
        sec->relocs = realloc(sec->relocs, (size_t)sec->crelocs * sizeof(Reloc));
    }
    sec->relocs[sec->nrelocs++] = (Reloc){ offset, sym, type, addend };
}

static void put_le(uint8_t *p, uint64_t v, int n) {
    for (int k = 0; k < n; k++) p[k] = (uint8_t)(v >> (8 * k));
}

// Resolve a symbol reference at field offset `at` of section s (n bytes)
static void resolve(Asm *as, int s, uint8_t *field, uint64_t at, int n, int kind,
                    const Expr *x, int64_t pc_adjust) {
    AsmObject *o = as->obj;
    Symbol *sym = &o->symbols[x->sym];
    if (!sym->defined && !sym->external) {
        error_at(as, "symbol '%s' is not defined", sym->name);
        return;
    }
    bool pcrel = kind == FIX_PC32 || kind == FIX_PC8;
    if (pcrel && sym->defined && sym->section == s) {
        int64_t v = (int64_t)sym->value + x->value - (int64_t)(at + (uint64_t)pc_adjust);
        if (n == 1 && (v < -128 || v > 127)) { error_at(as, "short jump out of range"); return; }
        put_le(field, (uint64_t)v, n);
        return;
    }
    if (kind == FIX_PC8) { error_at(as, "short jump to another section"); return; }
    int type = kind == FIX_PC32 ? R_X86_64_PC32 : kind == FIX_ABS64 ? R_X86_64_64 :
               kind == FIX_ABS32 ? R_X86_64_32 : R_X86_64_32S;
    int64_t addend = x->value - (pcrel ? pc_adjust : 0);
    put_le(field, 0, n);
    add_reloc(&o->sections[s], at, x->sym, type, addend);
}

static void emit_all(Asm *as) {
    AsmObject *o = as->obj;
    for (int s = 0; s < o->nsections; s++)
        if (!o->sections[s].nobits) {
            Bytes *b = &o->sections[s].data;
            b->data = malloc(o->sections[s].size ? o->sections[s].size : 1);
            b->cap = o->sections[s].size;
            b->len = 0;
        }
    for (int i = 0; i < as->nstmts; i++) {
        Stmt *st = &as->stmts[i];
        Section *sec = &o->sections[st->section];
        as->file = st->file;
        as->line = st->line;
        if (sec->nobits) {
            if (st->kind == ST_INSTR || st->kind == ST_DATA)
                error_at(as, "initialized data or code in a nobits section");
            continue;
        }
        Bytes *b = &sec->data;
        switch (st->kind) {
            case ST_INSTR: {
                uint8_t code[MAX_INSTR];
                Fixup fix[MAX_FIXUPS];
                int nfix;
                const char *err;
                Stmt final = folded(o, st);
                int len = x86_encode(&final, code, fix, &nfix, &err);
                if (len < 0) { error_at(as, "%s", err); break; }
                for (int64_t t = 0; t < st->times; t++) {
                    uint64_t at = b->len;
                    bytes_put(b, code, (size_t)len);
                    for (int f = 0; f < nfix; f++) {
                        int n = fix[f].kind == FIX_PC8 ? 1 : fix[f].kind == FIX_ABS64 ? 8 : 4;
                        resolve(as, st->section, b->data + at + fix[f].at, at + (uint64_t)fix[f].at, n,
                                fix[f].kind, &fix[f].expr, fix[f].pc_adjust);
                    }
                }
                break;
            }
            case ST_DATA:
                for (int64_t t = 0; t < st->times; t++) {
                    for (int k = 0; k < st->data.count; k++) {
                        int w = st->data.width;
                        if (st->data.strings[k]) {
                            size_t len = st->data.string_lens[k];
                            bytes_put(b, st->data.strings[k], len);
                            while (len % (size_t)w) { bytes_byte(b, 0); len++; }
                            continue;
                        }
                        Expr value = st->data.items[k];
                        fold(o, &value);
                        const Expr *x = &value;
                        uint8_t v[8];
                        put_le(v, (uint64_t)x->value, w);
                        uint64_t at = b->len;
                        bytes_put(b, v, (size_t)w);
                        if (x->sym != NO_SYM)
                            resolve(as, st->section, b->data + at, at, w, w == 8 ? FIX_ABS64 : FIX_ABS32, x, 0);
                    }
                }
                break;
            case ST_RESERVE:
                for (uint64_t k = 0; k < st->size; k++) bytes_byte(b, 0);
                break;
            case ST_ALIGN:
                for (uint64_t k = 0; k < st->size; k++) bytes_byte(b, 0x90);
                break;
        }
    }
}

// ── Entry point ──────────────────────────────────────────────────────────────

AsmObject *asm_assemble(const char *source, const char *filename, const AsmOptions *opts) {
    Asm as;
    memset(&as, 0, sizeof(as));
    as.obj = calloc(1, sizeof(AsmObject));
    as.opts = opts;
    as.obj->source_name = strdup(filename);
    as.obj->included = calloc(1, sizeof(char *));
    as.file = filename;
    as.section = section_index(&as, ".text", 5);
    for (int k = 0; opts && opts->defines && opts->defines[k]; k++) {
        const char *d = opts->defines[k], *eq = strchr(d, '=');
        if (eq) add_define(&as, d, (size_t)(eq - d), eq + 1, strlen(eq + 1));
        else add_define(&as, d, strlen(d), "", 0);
    }
    assemble_text(&as, source, filename, 0);

    AsmObject *o = as.obj;
    for (int k = 0; k < o->nsymbols; k++) {
        Symbol *sym = &o->symbols[k];
        if (sym->global && !sym->defined && !sym->external) {
            fprintf(stderr, "%s: error: global symbol '%s' is not defined\n", filename, sym->name);
            as.errors++;
        }
    }

    if (!as.errors) expand_equs(&as);
    if (!as.errors) check_differences(&as);

    // Sizes of instructions other than label jumps: encode them (again in
    // each layout round when they use a label difference)
    uint64_t *instr_size = calloc((size_t)as.nstmts + 1, sizeof(uint64_t));
    for (int i = 0; i < as.nstmts; i++) {         // (also after errors: report them all)
        Stmt *st = &as.stmts[i];
        if (st->kind != ST_INSTR) continue;
        if (x86_is_jump(st->mnemonic) && st->ops[0].kind == OP_IMM) {
            // short only to a label of this section; anything else is near
            int sym = st->ops[0].expr.sym;
            st->long_jump = !(sym != NO_SYM && o->symbols[sym].defined && o->symbols[sym].section == st->section);
            continue;
        }
        uint8_t code[MAX_INSTR];
        Fixup fix[MAX_FIXUPS];
        int nfix;
        const char *err;
        Stmt sized = folded(o, st);
        int len = x86_encode(&sized, code, fix, &nfix, &err);
        if (len < 0) {
            as.file = st->file;
            as.line = st->line;
            error_at(&as, "%s", err);
            continue;
        }
        instr_size[i] = (uint64_t)len;
    }

    // Relaxation: a short jump whose target does not fit becomes near; the
    // layout changes, so repeat until every short jump reaches
    for (bool changed = true; changed && !as.errors;) {
        changed = false;
        layout(&as, instr_size);
        for (int i = 0; i < as.nstmts; i++) {
            Stmt *st = &as.stmts[i];
            if (st->kind == ST_INSTR && st->has_difference) {
                uint8_t code[MAX_INSTR];
                Fixup fix[MAX_FIXUPS];
                int nfix;
                const char *err;
                Stmt sized = folded(o, st);
                int len = x86_encode(&sized, code, fix, &nfix, &err);
                if (len >= 0 && (uint64_t)len != instr_size[i]) {
                    instr_size[i] = (uint64_t)len;
                    changed = true;
                }
                continue;
            }
            if (st->kind != ST_INSTR || st->long_jump || !x86_is_jump(st->mnemonic) || st->ops[0].kind != OP_IMM)
                continue;
            const Expr *x = &st->ops[0].expr;
            int64_t disp = (int64_t)o->symbols[x->sym].value + x->value - (int64_t)(st->offset + 2);
            if (disp < -128 || disp > 127 || st->times != 1) {
                st->long_jump = true;
                changed = true;
            }
        }
    }
    free(instr_size);
    if (!as.errors) emit_all(&as);

    for (int i = 0; i < as.nstmts; i++) {
        Stmt *st = &as.stmts[i];
        if (st->kind != ST_DATA) continue;
        for (int k = 0; k < st->data.count; k++) free(st->data.strings[k]);
        free(st->data.items);
        free(st->data.strings);
        free(st->data.string_lens);
    }
    free(as.stmts);
    for (int k = 0; k < as.ndefines; k++) free(as.define_values[k]);
    free(as.define_values);
    namemap_free(&as.defines);
    if (as.errors) {
        asm_free(o);
        return NULL;
    }
    return o;
}

const char *const *asm_included_files(const AsmObject *obj) {
    return (const char *const *)obj->included;
}

void asm_free(AsmObject *obj) {
    if (!obj) return;
    for (int s = 0; s < obj->nsections; s++) {
        free(obj->sections[s].name);
        free(obj->sections[s].data.data);
        free(obj->sections[s].relocs);
    }
    free(obj->sections);
    for (int k = 0; k < obj->nsymbols; k++) free(obj->symbols[k].name);
    free(obj->symbols);
    namemap_free(&obj->symbol_names);
    for (int k = 0; obj->included && obj->included[k]; k++) free(obj->included[k]);
    free(obj->included);
    free(obj->source_name);
    free(obj);
}
