/* src/ir/promote.c
 * Loops whose numeric fields can live in registers (include/promote.h).
 *
 * A loop qualifies when
 * - it is LABEL H ... JUMP H, no instruction outside it jumps to H or to a
 *   label inside it, and it contains no other backward jump (only innermost
 *   loops are promoted);
 * - its body has only numeric stack instructions, size-error tests, labels,
 *   jumps and address computations: no calls that could observe a field
 *   (DISPLAY, MOVE, PERFORM, I/O, ...).
 * A field of the loop is promoted when every access to its bytes in the loop
 * is a numeric load or store of that same item ("%r<n>+<offset>", same size,
 * usage and flags), and its record is not addressed through an address slot
 * in the loop.
 */
#include "promote.h"
#include <stdlib.h>
#include <string.h>

static bool allowed(IrOpcode op) {
    switch (op) {
        case IR_NUM_LOAD: case IR_NUM_CONST: case IR_NUM_RESCALE:
        case IR_NUM_ADD: case IR_NUM_SUB: case IR_NUM_MUL: case IR_NUM_DIV:
        case IR_NUM_NEG: case IR_NUM_DUP: case IR_NUM_DROP: case IR_NUM_SWAP:
        case IR_NUM_TRUNC: case IR_NUM_POW:
        case IR_ERR_SAVE: case IR_ERR_RESTORE: case IR_NUM_STORE:
        case IR_NUM_CMP_JUMP_IF_FALSE: case IR_SIZE_ERR_CLEAR: case IR_JUMP_IF_NO_SIZE_ERR:
        case IR_LABEL: case IR_JUMP:
        case IR_ADDR_SET: case IR_ADDR_ADD: case IR_LEN_SET:
            return true;
        default:
            return false;
    }
}

// Numeric stack effect of an allowed instruction: entries it needs, and the
// change of the depth
static void stack_effect(const IrInstr *i, int *need, int *delta) {
    *need = 0;
    *delta = 0;
    switch (i->op) {
        case IR_NUM_LOAD: case IR_NUM_CONST: *delta = 1; break;
        case IR_NUM_DUP: *need = 1; *delta = 1; break;
        case IR_NUM_RESCALE: case IR_NUM_NEG: case IR_NUM_TRUNC: *need = 1; break;
        case IR_NUM_ADD: case IR_NUM_SUB: case IR_NUM_MUL: case IR_NUM_DIV:
        case IR_NUM_POW: *need = 2; *delta = -1; break;
        case IR_NUM_SWAP: *need = 2; break;
        case IR_NUM_DROP: case IR_NUM_STORE: case IR_ADDR_ADD: case IR_LEN_SET:
            *need = 1; *delta = -1; break;
        case IR_NUM_CMP_JUMP_IF_FALSE: *need = 2; *delta = -2; break;
        default: break;
    }
}

// The label an instruction may transfer control to, or NULL
static const char *label_ref(const IrInstr *i) {
    if (i->op == IR_TIMES_END || i->op == IR_TIMES_BEGIN) return i->var_name;
    return i->jump_target;
}

typedef struct {
    const char *name;
    int index;
} LabelIdx;

static int cmp_label(const void *a, const void *b) {
    return strcmp(((const LabelIdx *)a)->name, ((const LabelIdx *)b)->name);
}

// "%r<n>+<offset>": record n and offset; false for any other operand
static bool parse_field(const char *name, int *rec, int *off) {
    if (!name || name[0] != '%' || name[1] != 'r') return false;
    char *end;
    long r = strtol(name + 2, &end, 10);
    long o = 0;
    if (*end == '+') o = strtol(end + 1, &end, 10);
    if (*end) return false;
    *rec = (int)r;
    *off = (int)o;
    return true;
}

// Address slot of a "*A_<k>[+offset]" operand, or -1
static int slot_of(const char *name) {
    if (!name || name[0] != '*' || strncmp(name, "*A_", 3) != 0) return -1;
    return (int)strtol(name + 3, NULL, 10);
}

typedef struct {
    const IrInstr *instr;
    int rec, off, size;
} Access;

static void analyze(IrInstr **code, int h, int j, PromoteLoop *loop,
                    bool (*accept)(const IrInstr *, const IrInstr *)) {
    loop->nfields = 0;
    int cap = 16, n = 0;
    Access *acc = malloc(sizeof(Access) * cap);
    int dyn_recs[64], ndyn = 0;

    // Records reached through address slots in the loop
    for (int k = h + 1; k < j; k++) {
        const IrInstr *i = code[k];
        if (i->op != IR_ADDR_SET) continue;
        int rec, off;
        if (!parse_field(i->var_name, &rec, &off) || ndyn == 64) goto out;
        dyn_recs[ndyn++] = rec;
    }
    for (int k = h + 1; k < j; k++) {
        const IrInstr *i = code[k];
        const char *ops[3] = { i->var_name, i->lit_name, i->content };
        if (i->op == IR_LABEL || i->op == IR_JUMP) continue;
        for (int o = 0; o < 3; o++) {
            if (!ops[o]) continue;
            if (slot_of(ops[o]) >= 0) {
                // the slot must be set inside the loop, so its record is known
                bool set = false;
                for (int m = h + 1; m < j && !set; m++)
                    set = code[m]->op == IR_ADDR_SET && code[m]->imm == slot_of(ops[o]);
                if (!set) goto out;
                continue;
            }
            int rec, off;
            if (!parse_field(ops[o], &rec, &off)) continue;
            if (n == cap) acc = realloc(acc, sizeof(Access) * (cap *= 2));
            acc[n++] = (Access){ i, rec, off, i->op == IR_NUM_LOAD || i->op == IR_NUM_STORE ? i->size : -1 };
        }
    }

    for (int a = 0; a < n; a++) {
        const IrInstr *i = acc[a].instr;
        bool plain = (i->op == IR_NUM_LOAD || (i->op == IR_NUM_STORE && !i->lit_name)) && i->size > 0;
        if (!plain) continue;
        // already considered?
        bool seen = false;
        for (int b = 0; b < a && !seen; b++) seen = strcmp(acc[b].instr->var_name ? acc[b].instr->var_name : "", i->var_name) == 0;
        if (seen) continue;
        bool ok = true;
        for (int d = 0; d < ndyn && ok; d++) ok = dyn_recs[d] != acc[a].rec;
        const IrInstr *load = NULL, *store = NULL;
        int uses = 0;
        for (int b = 0; b < n && ok; b++) {
            const IrInstr *o = acc[b].instr;
            if (acc[b].rec != acc[a].rec) continue;
            bool same = o->var_name && strcmp(o->var_name, i->var_name) == 0;
            if (!same) {
                // any other access overlapping the field's bytes
                int lo = acc[b].off, hi = acc[b].size > 0 ? acc[b].off + acc[b].size : 1 << 30;
                if (lo < acc[a].off + i->size && acc[a].off < hi) ok = false;
                continue;
            }
            bool o_plain = (o->op == IR_NUM_LOAD || (o->op == IR_NUM_STORE && !o->lit_name)) &&
                           o->size == i->size && o->usage_type == i->usage_type &&
                           (o->flags & ~IR_NUM_KEEP) == (i->flags & ~IR_NUM_KEEP);
            if (!o_plain) { ok = false; continue; }
            if (o->op == IR_NUM_STORE) {
                if (store && store->digits != o->digits) ok = false;
                store = o;
            } else if (!load) {
                load = o;
            }
            uses++;
        }
        if (!ok || !accept(load ? load : store, store)) continue;
        PromotedField pf = { load ? load : store, store, i->var_name, uses };
        // keep the most used fields
        int pos = loop->nfields;
        while (pos > 0 && loop->fields[pos - 1].uses < uses) pos--;
        if (pos >= PROMOTE_MAX_FIELDS) continue;
        int last = loop->nfields < PROMOTE_MAX_FIELDS ? loop->nfields : PROMOTE_MAX_FIELDS - 1;
        memmove(loop->fields + pos + 1, loop->fields + pos, sizeof(PromotedField) * (size_t)(last - pos));
        loop->fields[pos] = pf;
        if (loop->nfields < PROMOTE_MAX_FIELDS) loop->nfields++;
    }
out:
    free(acc);
}

PromotePlan promote_plan(const IrProgram *ir,
                         bool (*accept)(const IrInstr *load, const IrInstr *store)) {
    PromotePlan plan = { NULL, 0 };
    int n = 0;
    for (IrInstr *i = ir->head; i; i = i->next) n++;
    if (n == 0) return plan;
    IrInstr **code = malloc(sizeof(IrInstr *) * (size_t)n);
    int k = 0;
    for (IrInstr *i = ir->head; i; i = i->next) code[k++] = i;

    int nl = 0;
    LabelIdx *labels = malloc(sizeof(LabelIdx) * (size_t)n);
    for (k = 0; k < n; k++)
        if (code[k]->op == IR_LABEL && code[k]->var_name) labels[nl++] = (LabelIdx){ code[k]->var_name, k };
    qsort(labels, (size_t)nl, sizeof(LabelIdx), cmp_label);
    int *ref = malloc(sizeof(int) * (size_t)n);  // index of the referenced label, or -1
    for (k = 0; k < n; k++) {
        ref[k] = -1;
        const char *t = label_ref(code[k]);
        if (!t) continue;
        LabelIdx key = { t, 0 };
        LabelIdx *hit = bsearch(&key, labels, (size_t)nl, sizeof(LabelIdx), cmp_label);
        if (hit) ref[k] = hit->index;
    }

    int cap = 0;
    for (int j = 0; j < n; j++) {
        if (code[j]->op != IR_JUMP || ref[j] < 0 || ref[j] >= j) continue;
        int h = ref[j];
        bool ok = true;
        // The body must only use numeric stack entries it pushed itself
        // (the backend saves a register on the stack before the loop) and
        // be at depth 0 at every label
        int depth = 0;
        for (k = h + 1; k < j && ok; k++) {
            int need, delta;
            if (!allowed(code[k]->op)) ok = false;
            // another backward jump: an inner loop
            else if (ref[k] >= 0 && ref[k] <= k && ref[k] >= h) ok = false;
            else if (code[k]->op == IR_LABEL && depth != 0) ok = false;
            stack_effect(code[k], &need, &delta);
            if (depth < need) ok = false;
            depth += delta;
        }
        if (depth != 0) ok = false;
        for (k = 0; k < n && ok; k++)
            if ((k < h || k > j) && ref[k] >= h && ref[k] <= j) ok = false;
        if (!ok) continue;
        PromoteLoop loop = { code[h], code[j], {{0}}, 0 };
        analyze(code, h, j, &loop, accept);
        if (loop.nfields == 0) continue;
        if (plan.nloops == cap) plan.loops = realloc(plan.loops, sizeof(PromoteLoop) * (size_t)(cap = cap ? cap * 2 : 4));
        plan.loops[plan.nloops++] = loop;
    }
    free(ref);
    free(labels);
    free(code);
    return plan;
}

void promote_free(PromotePlan *plan) {
    free(plan->loops);
    plan->loops = NULL;
    plan->nloops = 0;
}

// ── Flow queries ─────────────────────────────────────────────────────────────

struct IrFlow {
    IrInstr **code;
    int n;
    LabelIdx *labels;
    int nlabels;
    int *mark;                  // visit marks of the current query
    int gen;
};

IrFlow *ir_flow_new(const IrProgram *ir) {
    IrFlow *fl = calloc(1, sizeof(IrFlow));
    for (IrInstr *i = ir->head; i; i = i->next) fl->n++;
    fl->code = malloc(sizeof(IrInstr *) * (size_t)(fl->n + 1));
    fl->labels = malloc(sizeof(LabelIdx) * (size_t)(fl->n + 1));
    fl->mark = calloc((size_t)(fl->n + 1), sizeof(int));
    int k = 0;
    for (IrInstr *i = ir->head; i; i = i->next, k++) {
        fl->code[k] = i;
        if (i->op == IR_LABEL && i->var_name) fl->labels[fl->nlabels++] = (LabelIdx){ i->var_name, k };
    }
    qsort(fl->labels, (size_t)fl->nlabels, sizeof(LabelIdx), cmp_label);
    return fl;
}

void ir_flow_free(IrFlow *fl) {
    if (!fl) return;
    free(fl->code);
    free(fl->labels);
    free(fl->mark);
    free(fl);
}

static int flow_label(const IrFlow *fl, const char *name) {
    LabelIdx key = { name, 0 };
    LabelIdx *hit = bsearch(&key, fl->labels, (size_t)fl->nlabels, sizeof(LabelIdx), cmp_label);
    return hit ? hit->index : -1;
}

// Instructions that neither read nor write cobrt_stmt_err and continue at
// the next instruction (or, for jumps, at known labels)
static bool size_err_transparent(IrOpcode op) {
    switch (op) {
        case IR_NUM_LOAD: case IR_NUM_CONST: case IR_NUM_RESCALE:
        case IR_NUM_ADD: case IR_NUM_SUB: case IR_NUM_MUL: case IR_NUM_DIV:
        case IR_NUM_NEG: case IR_NUM_DUP: case IR_NUM_DROP: case IR_NUM_SWAP:
        case IR_NUM_TRUNC: case IR_NUM_POW: case IR_NUM_DEEDIT: case IR_NUM_TO_TEXT:
        case IR_ERR_SAVE: case IR_ERR_RESTORE: case IR_NUM_STORE:
        case IR_LABEL: case IR_PROC_ENTRY:
        case IR_CALL_DISPLAY_STR: case IR_CALL_DISPLAY_VAR_STR: case IR_EMIT_NEWLINE:
        case IR_CALL_DISPLAY_FILL: case IR_CALL_MOVE_ALPHA: case IR_CALL_MOVE_NUM:
        case IR_CALL_MOVE_FILL: case IR_CALL_REPLICATE: case IR_CALL_EDIT_ALNUM:
        case IR_ADDR_SET: case IR_ADDR_ADD: case IR_LEN_SET:
            return true;
        default:
            return false;
    }
}

bool ir_size_err_dead_after(IrFlow *fl, int start) {
    if (start < 0 || start >= fl->n) return false;
    fl->gen++;
    int stack[64], sp = 0, budget = 256;
    stack[sp++] = start + 1;
    while (sp > 0) {
        int k = stack[--sp];
        for (;;) {
            if (k >= fl->n) return false;           // falls off the program
            if (fl->mark[k] == fl->gen) break;      // already followed
            fl->mark[k] = fl->gen;
            if (--budget < 0) return false;
            const IrInstr *i = fl->code[k];
            if (i->op == IR_SIZE_ERR_CLEAR || i->op == IR_CALL_EXIT) break;
            if (i->op == IR_JUMP || i->op == IR_NUM_CMP_JUMP_IF_FALSE) {
                int t = i->jump_target ? flow_label(fl, i->jump_target) : -1;
                if (t < 0) return false;
                if (i->op == IR_JUMP) { k = t; continue; }
                if (sp == 64) return false;
                stack[sp++] = t;
                k++;
                continue;
            }
            if (!size_err_transparent(i->op)) return false;
            k++;
        }
    }
    return true;
}
