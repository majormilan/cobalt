/* src/backend/peephole.c
 * A pass over the generated x86-64 assembly: a numeric item loaded again
 * while a callee-saved register still holds the value it was loaded into
 * takes it from that register instead of calling the runtime.
 *
 * A load is the three lines the backend writes for a static item
 *     mov edi, Q_<k>
 *     call runtime_num_load_d          (or runtime_packed_load_d)
 *     mov <reg>, rax
 * after which <reg> (r12 to r15, kept across calls) holds the value of the
 * item of descriptor Q_<k>. It holds it until the register is written, or
 * the item may change or control may come from elsewhere:
 *   - a call of anything but the loads and the console output routines;
 *   - a write to memory other than the stack, the error bytes (cobrt_...),
 *     the PERFORM return slots (R_...) and the program's own labels (L_...);
 *   - a label, an unconditional jump or a return; a section directive.
 * A conditional jump keeps it (the code after it is the same straight line).
 * The register instructions are those the backend writes; any instruction
 * but a compare, a test, a push or a jump counts as writing its first operand.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

#define NREG 4
static const char *const held_regs[NREG] = { "r12", "r13", "r14", "r15" };

typedef struct {
    char key[48];               // routine and descriptor, "" when none
} Held;

// The callee-saved register a register name refers to (r12d, r12w, r12b too)
static int reg_index(const char *op, size_t len) {
    for (int k = 0; k < NREG; k++) {
        size_t n = strlen(held_regs[k]);
        if (len >= n && strncmp(op, held_regs[k], n) == 0 &&
            (len == n || ((len == n + 1) && (op[n] == 'd' || op[n] == 'w' || op[n] == 'b'))))
            return k;
    }
    return -1;
}

// memmem, which not every C library has
static bool contains(const char *s, size_t n, const char *what) {
    size_t w = strlen(what);
    for (size_t k = 0; k + w <= n; k++)
        if (memcmp(s + k, what, w) == 0) return true;
    return false;
}

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// The first operand of an instruction line (after the mnemonic): start, length
static size_t first_operand(const char *ops, const char **start) {
    ops = skip_space(ops);
    // a size keyword before a memory operand
    static const char *const sizes[] = { "byte ", "word ", "dword ", "qword ", NULL };
    for (int k = 0; sizes[k]; k++)
        if (strncmp(ops, sizes[k], strlen(sizes[k])) == 0) ops += strlen(sizes[k]);
    *start = ops;
    size_t n = 0;
    while (ops[n] && ops[n] != ',' && ops[n] != '\n' && ops[n] != ';') n++;
    while (n > 0 && (ops[n - 1] == ' ' || ops[n - 1] == '\t')) n--;
    return n;
}

// A memory operand the pass knows is not a data item
static bool harmless_memory(const char *op, size_t n) {
    const char *in = memchr(op, '[', n);
    if (!in) return false;
    in = skip_space(in + 1);
    if (strncmp(in, "rel ", 4) == 0) in = skip_space(in + 4);
    return strncmp(in, "rsp", 3) == 0 || strncmp(in, "cobrt_", 6) == 0 || strncmp(in, "R_", 2) == 0 ||
           strncmp(in, "L_", 2) == 0;
}

static bool pure_call(const char *target) {
    static const char *const pure[] = {
        "runtime_num_load_d", "runtime_packed_load_d", "runtime_num_load_p", "runtime_num_load",
        "runtime_packed_load", "runtime_display_string", "runtime_display_var_string",
        "runtime_display_newline", "runtime_display_fill", NULL };
    size_t n = 0;
    while (target[n] && !isspace((unsigned char)target[n])) n++;
    for (int k = 0; pure[k]; k++)
        if (strlen(pure[k]) == n && strncmp(target, pure[k], n) == 0) return true;
    return false;
}

// The line's text without its indentation and end
static bool line_is(const char *line, const char *text) {
    line = skip_space(line);
    size_t n = strlen(text);
    return strncmp(line, text, n) == 0 && (line[n] == '\n' || line[n] == '\0' || line[n] == ' ');
}

// Does code from here read rax before writing it?
static bool reads_rax_first(const char *p) {
    for (int lines = 0; *p && lines < 8; lines++) {
        const char *s = skip_space(p);
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len && s[len - 1] == ':') return true;                  // a label: unknown
        bool writes = strncmp(s, "mov rax,", 8) == 0 || strncmp(s, "mov eax,", 8) == 0 ||
                      strncmp(s, "xor eax, eax", 12) == 0 || strncmp(s, "lea rax,", 8) == 0 ||
                      strncmp(s, "call ", 5) == 0;
        // a write that does not read rax itself
        if (writes) {
            const char *src = memchr(s, ',', len);
            if (strncmp(s, "call ", 5) == 0) return false;
            if (strncmp(s, "xor eax, eax", 12) == 0) return false;
            if (src && !contains(src, (size_t)(s + len - src), "rax") && !contains(src, (size_t)(s + len - src), "eax"))
                return false;
            return true;
        }
        if (contains(s, len, "rax") || contains(s, len, "eax") || contains(s, len, " ax") ||
            contains(s, len, " al") || strncmp(s, "mul ", 4) == 0 || strncmp(s, "div ", 4) == 0 ||
            strncmp(s, "idiv ", 5) == 0 || strncmp(s, "cqo", 3) == 0 || strncmp(s, "ret", 3) == 0 ||
            strncmp(s, "jmp ", 4) == 0)
            return true;
        if (!e) break;
        p = e + 1;
    }
    return true;
}

char *x86_reuse_loaded_values(const char *text, size_t len, size_t *out_len) {
    char *out = malloc(len + 1);
    if (!out) return NULL;
    size_t o = 0;
    Held held[NREG];
    bool in_text = true;        // in a code section
    bool dead = false;          // after an unconditional jump, before a label
    memset(held, 0, sizeof(held));
    const char *p = text, *end = text + len;
    while (p < end) {
        const char *e = memchr(p, '\n', (size_t)(end - p));
        const char *next = e ? e + 1 : end;
        const char *s = skip_space(p);
        size_t slen = (size_t)((e ? e : end) - s);

        // a load: mov edi, Q_k / call runtime_..._load_d / mov reg, rax
        // unreachable instructions: after jmp or ret, up to the next label
        if (dead && in_text && slen && *s != ';' && p != s && s[slen - 1] != ':' &&
            strncmp(s, "align", 5) != 0 && strncmp(s, "global", 6) != 0 && strncmp(s, "extern", 6) != 0) {
            p = next;
            continue;
        }
        if (p == s && slen && memchr(s, ':', slen)) dead = false;   // a label
        if (strncmp(s, "section", 7) == 0) {
            in_text = strncmp(skip_space(s + 7), ".text", 5) == 0;
            dead = false;
        }
        if (strncmp(s, "mov edi, Q_", 11) == 0) {
            const char *l2 = next, *e2 = l2 < end ? memchr(l2, '\n', (size_t)(end - l2)) : NULL;
            const char *l3 = e2 ? e2 + 1 : end, *e3 = l3 < end ? memchr(l3, '\n', (size_t)(end - l3)) : NULL;
            bool num = l2 < end && line_is(l2, "call runtime_num_load_d");
            bool pk = l2 < end && line_is(l2, "call runtime_packed_load_d");
            const char *m3 = l3 < end ? skip_space(l3) : "";
            int dst = -1;
            if ((num || pk) && e3 && strncmp(m3, "mov ", 4) == 0) {
                const char *op;
                size_t n = first_operand(m3 + 4, &op);
                const char *comma = memchr(m3, ',', (size_t)(e3 - m3));
                if (comma && strncmp(skip_space(comma + 1), "rax", 3) == 0) dst = reg_index(op, n);
            }
            if (dst >= 0) {
                char key[48];
                size_t qn = 0;
                const char *q = s + 9;              // "Q_..."
                while (q[qn] && !isspace((unsigned char)q[qn])) qn++;
                snprintf(key, sizeof(key), "%c%.*s", num ? 'n' : 'p', (int)(qn < 40 ? qn : 40), q);
                int src = -1;
                for (int k = 0; k < NREG; k++) if (held[k].key[0] && !strcmp(held[k].key, key)) src = k;
                const char *after = e3 + 1;
                if (src >= 0) {
                    // the value is in a register already
                    if (src != dst) o += (size_t)sprintf(out + o, "    mov %s, %s\n", held_regs[dst], held_regs[src]);
                    if (reads_rax_first(after))
                        o += (size_t)sprintf(out + o, "    mov rax, %s\n", held_regs[dst]);
                } else {
                    size_t n = (size_t)(after - p);
                    memcpy(out + o, p, n);
                    o += n;
                }
                snprintf(held[dst].key, sizeof(held[dst].key), "%s", key);
                p = after;
                continue;
            }
        }

        // copy the line, then what it does to the held values
        const char *line_start = p;
        memcpy(out + o, p, (size_t)(next - p));
        o += (size_t)(next - p);
        p = next;
        if (slen == 0 || *s == ';') continue;
        // a label (alone, or before a directive), a section, a jump away
        bool label = s[slen - 1] == ':' || (s == line_start && memchr(s, ':', slen));
        if (label || strncmp(s, "section", 7) == 0 || strncmp(s, "jmp ", 4) == 0 || strncmp(s, "ret", 3) == 0) {
            memset(held, 0, sizeof(held));
            if (in_text && (strncmp(s, "jmp ", 4) == 0 || strncmp(s, "ret", 3) == 0)) dead = true;
            continue;
        }
        if (strncmp(s, "call ", 5) == 0) {
            if (!pure_call(skip_space(s + 5))) memset(held, 0, sizeof(held));
            continue;
        }
        if (s[0] == 'j' || strncmp(s, "cmp ", 4) == 0 || strncmp(s, "test ", 5) == 0 ||
            strncmp(s, "push ", 5) == 0 || strncmp(s, "align", 5) == 0 || strncmp(s, "global", 6) == 0 ||
            strncmp(s, "extern", 6) == 0)
            continue;
        // the mnemonic, then its first operand: written
        size_t m = 0;
        while (m < slen && !isspace((unsigned char)s[m])) m++;
        const char *op;
        size_t n = first_operand(s + m, &op);
        if (n == 0) {                               // no operand (cqo, rep movsb, ...)
            if (contains(s, slen, "movs") || contains(s, slen, "stos")) memset(held, 0, sizeof(held));
            continue;
        }
        if (memchr(op, '[', n)) {
            if (!harmless_memory(op, n)) memset(held, 0, sizeof(held));
            continue;
        }
        int r = reg_index(op, n);
        if (r >= 0) held[r].key[0] = '\0';
        if (strncmp(s, "xchg", 4) == 0) memset(held, 0, sizeof(held));
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}
