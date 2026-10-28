/* src/ir/ir.c */
#include "ir.h"
#include "ast.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

typedef struct {
    char *name;
    int size;
    int decimal_places;     // scale: digits after the implied point
    int digits;             // digit positions (numeric items)
    bool is_numeric;
    bool is_signed;
    AstUsageType usage_type;
    AstNode *value;         // VALUE clause literal (owned by the AST), or NULL
} VarInfo;

static VarInfo vars[512];
static int num_vars = 0;
static int lit_counter = 0;
static int if_counter = 0;

static IrInstr *new_instr(IrOpcode op, const char *var_name, const char *lit_name,
                          const char *content, int lit_len, int size, int decimal_places, char pad_char, AstUsageType usage_type) {
    IrInstr *i = calloc(1, sizeof(IrInstr));
    if (!i) return NULL;
    i->op = op;
    i->var_name = var_name ? strdup(var_name) : NULL;
    i->lit_name = lit_name ? strdup(lit_name) : NULL;
    i->content = content ? strdup(content) : NULL;
    i->lit_len = lit_len;
    i->size = size;
    i->decimal_places = decimal_places;
    i->pad_char = pad_char;
    i->usage_type = usage_type;
    return i;
}

static void append_instr(IrProgram *ir, IrInstr *i) {
    if (!ir->head) {
        ir->head = ir->tail = i;
    } else {
        ir->tail->next = i;
        ir->tail = i;
    }
}

static IrInstr *emit(IrProgram *ir, IrOpcode op) {
    IrInstr *i = calloc(1, sizeof(IrInstr));
    if (!i) return NULL;
    i->op = op;
    append_instr(ir, i);
    return i;
}

static void emit_label(IrProgram *ir, const char *label) {
    append_instr(ir, new_instr(IR_LABEL, label, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
}

static void emit_jump(IrProgram *ir, IrOpcode op, const char *target) {
    IrInstr *i = emit(ir, op);
    i->jump_target = strdup(target);
}

static VarInfo *find_var(const char *name) {
    for (int i = 0; i < num_vars; i++) {
        if (strcasecmp(vars[i].name, name) == 0) return &vars[i];
    }
    return NULL;
}

// Emit a data literal and return its label in buf
static const char *emit_data_lit(IrProgram *ir, const char *prefix, const char *content,
                                 char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "%s_%d", prefix, lit_counter++);
    append_instr(ir, new_instr(IR_DATA_LIT, NULL, buf, content, (int)strlen(content), 0, 0, 0, USAGE_DISPLAY));
    return buf;
}

static bool is_zero_constant(const AstNode *n) {
    return n->kind == AST_FIGURATIVE_CONSTANT && n->name &&
           (strcasecmp(n->name, "ZERO") == 0 || strcasecmp(n->name, "ZEROS") == 0 ||
            strcasecmp(n->name, "ZEROES") == 0);
}

static bool is_space_constant(const AstNode *n) {
    return n->kind == AST_FIGURATIVE_CONSTANT && n->name &&
           (strcasecmp(n->name, "SPACE") == 0 || strcasecmp(n->name, "SPACES") == 0);
}

// ── Numeric expressions ──────────────────────────────────────────────────────

// Parse a numeric literal ("-12.50") into an unscaled integer and its scale.
static bool parse_num_literal(const char *s, long long *value, int *scale) {
    bool neg = false;
    if (*s == '+' || *s == '-') neg = (*s++ == '-');
    long long v = 0;
    int sc = 0;
    bool frac = false, any = false;
    for (; *s; s++) {
        if (*s == '.') {
            if (frac) return false;
            frac = true;
            continue;
        }
        if (!isdigit((unsigned char)*s)) return false;
        if (v > (LLONG_MAX - 9) / 10) return false;
        v = v * 10 + (*s - '0');
        if (frac) sc++;
        any = true;
    }
    if (!any) return false;
    *value = neg ? -v : v;
    *scale = sc;
    return true;
}

static int var_flags(const VarInfo *v) {
    return v->is_signed ? IR_NUM_SIGNED : 0;
}

static VarInfo *numeric_var(AstNode *n, const char *role) {
    VarInfo *v = find_var(n->name);
    if (!v) {
        cobalt_error_loc(NULL, n->loc.line, n->loc.column, "Undeclared identifier '%s'", n->name);
        return NULL;
    }
    if (!v->is_numeric) {
        cobalt_error_loc(NULL, n->loc.line, n->loc.column, "%s '%s' is not numeric", role, n->name);
        return NULL;
    }
    return v;
}

// Report problems in an arithmetic expression; true if it can be lowered.
static bool num_expr_valid(AstNode *e) {
    if (!e) return false;
    long long value;
    int scale;
    switch (e->kind) {
        case AST_IDENTIFIER:
            return numeric_var(e, "Operand") != NULL;
        case AST_LITERAL_NUMBER:
            if (!parse_num_literal(e->name, &value, &scale)) {
                cobalt_error_loc(NULL, e->loc.line, e->loc.column, "Invalid numeric literal '%s'", e->name);
                return false;
            }
            return true;
        case AST_EXPR_NEG:
            return num_expr_valid(e->children);
        case AST_EXPR_POW: {
            AstNode *exp = e->children ? e->children->next : NULL;
            if (!exp || exp->kind != AST_LITERAL_NUMBER ||
                !parse_num_literal(exp->name, &value, &scale) || scale != 0 || value < 0 || value > 18) {
                cobalt_error_loc(NULL, e->loc.line, e->loc.column,
                                 "Only integer literal exponents from 0 to 18 are supported");
                return false;
            }
            return num_expr_valid(e->children);
        }
        case AST_EXPR_ADD:
        case AST_EXPR_SUB:
        case AST_EXPR_MUL:
        case AST_EXPR_DIV: {
            bool left = num_expr_valid(e->children);
            bool right = e->children && num_expr_valid(e->children->next);
            return left && right;
        }
        default:
            cobalt_error_loc(NULL, e->loc.line, e->loc.column, "Invalid operand in arithmetic expression");
            return false;
    }
}

// Largest scale of any operand in the expression
static int max_leaf_scale(AstNode *e) {
    if (!e) return 0;
    if (e->kind == AST_IDENTIFIER) {
        VarInfo *v = find_var(e->name);
        return v ? v->decimal_places : 0;
    }
    if (e->kind == AST_LITERAL_NUMBER) {
        long long value;
        int scale = 0;
        parse_num_literal(e->name, &value, &scale);
        return scale;
    }
    int m = max_leaf_scale(e->children);
    if (e->children) {
        int r = max_leaf_scale(e->children->next);
        if (r > m) m = r;
    }
    return m;
}

static void emit_num(IrProgram *ir, IrOpcode op, long long imm, int flags) {
    IrInstr *i = emit(ir, op);
    i->imm = imm;
    i->flags = flags;
}

static void emit_rescale(IrProgram *ir, int shift, bool rounded) {
    if (shift != 0) emit_num(ir, IR_NUM_RESCALE, shift, rounded ? 1 : 0);
}

static void emit_load(IrProgram *ir, const VarInfo *v) {
    IrInstr *i = emit(ir, IR_NUM_LOAD);
    i->var_name = strdup(v->name);
    i->size = v->size;
    i->usage_type = v->usage_type;
    i->flags = var_flags(v);
}

static void emit_store(IrProgram *ir, const VarInfo *v, bool keep) {
    IrInstr *i = emit(ir, IR_NUM_STORE);
    i->var_name = strdup(v->name);
    i->size = v->size;
    i->digits = v->digits;
    i->usage_type = v->usage_type;
    i->flags = var_flags(v) | (keep ? IR_NUM_KEEP : 0);
}

// Compute the scale of a valid expression; when ir is non-NULL also emit the
// code that pushes its value. W is the working scale: products are cut back
// to W digits after the point and quotients are computed to W digits.
// The single function keeps the computed scale and the emitted code in step.
static int num_expr(IrProgram *ir, AstNode *e, int W) {
    switch (e->kind) {
        case AST_IDENTIFIER: {
            VarInfo *v = find_var(e->name);
            if (ir) emit_load(ir, v);
            return v->decimal_places;
        }
        case AST_LITERAL_NUMBER: {
            long long value;
            int scale;
            parse_num_literal(e->name, &value, &scale);
            if (ir) emit_num(ir, IR_NUM_CONST, value, 0);
            return scale;
        }
        case AST_EXPR_NEG: {
            int s = num_expr(ir, e->children, W);
            if (ir) emit_num(ir, IR_NUM_NEG, 0, 0);
            return s;
        }
        case AST_EXPR_ADD:
        case AST_EXPR_SUB: {
            AstNode *l = e->children, *r = l->next;
            int sl = num_expr(NULL, l, W), sr = num_expr(NULL, r, W);
            int s = sl > sr ? sl : sr;
            if (ir) {
                num_expr(ir, l, W);
                emit_rescale(ir, s - sl, false);
                num_expr(ir, r, W);
                emit_rescale(ir, s - sr, false);
                emit_num(ir, e->kind == AST_EXPR_ADD ? IR_NUM_ADD : IR_NUM_SUB, 0, 0);
            }
            return s;
        }
        case AST_EXPR_MUL: {
            AstNode *l = e->children, *r = l->next;
            int sl = num_expr(ir, l, W), sr = num_expr(ir, r, W);
            int p = sl + sr, shift = p > W ? p - W : 0;
            if (ir) emit_num(ir, IR_NUM_MUL, shift, 0);
            return p - shift;
        }
        case AST_EXPR_DIV: {
            AstNode *l = e->children, *r = l->next;
            int sl = num_expr(NULL, l, W), sr = num_expr(NULL, r, W);
            int shift = W - sl + sr;
            if (ir) {
                num_expr(ir, l, W);
                if (shift < 0) emit_rescale(ir, shift, false);
                num_expr(ir, r, W);
                emit_num(ir, IR_NUM_DIV, shift < 0 ? 0 : shift, 0);
            }
            return W;
        }
        case AST_EXPR_POW: {
            AstNode *base = e->children;
            long long n;
            int ignored;
            parse_num_literal(base->next->name, &n, &ignored);
            if (n == 0) {
                if (ir) emit_num(ir, IR_NUM_CONST, 1, 0);
                return 0;
            }
            int sb = num_expr(ir, base, W), s = sb;
            for (long long k = 1; k < n; k++) {
                num_expr(ir, base, W);
                int p = s + sb, shift = p > W ? p - W : 0;
                if (ir) emit_num(ir, IR_NUM_MUL, shift, 0);
                s = p - shift;
            }
            return s;
        }
        default:
            return 0;
    }
}

// Working scale for computing tree into target
static int working_scale(AstNode *tree, const VarInfo *target) {
    int w = max_leaf_scale(tree);
    if (target && target->decimal_places > w) w = target->decimal_places;
    w += 2;
    return w > 18 ? 18 : w;
}

// Evaluate tree and store it into the receiver (an identifier node)
static void lower_assign(IrProgram *ir, AstNode *tree, AstNode *receiver, bool keep) {
    VarInfo *t = numeric_var(receiver, "Receiving item");
    if (!t || !num_expr_valid(tree)) return;
    int s = num_expr(ir, tree, working_scale(tree, t));
    emit_rescale(ir, t->decimal_places - s, receiver->rounded);
    emit_store(ir, t, keep);
}

// Expression trees built from statement operands (freed with ast_free)
static AstNode *leaf_copy(AstNode *n) {
    return ast_new(n->kind, n->name, n->loc);
}

static AstNode *mk_bin(AstKind kind, AstNode *l, AstNode *r) {
    AstNode *n = ast_new(kind, NULL, l->loc);
    n->children = l;
    l->next = r;
    return n;
}

// first op ops[0] op ops[1] ... (left-associative), each operand copied
static AstNode *fold_ops(AstKind kind, AstNode *first, AstNode **ops, int n) {
    AstNode *tree = first;
    for (int i = 0; i < n; i++) tree = mk_bin(kind, tree, leaf_copy(ops[i]));
    return tree;
}

static void lower_block(IrProgram *ir, AstNode *block);

// ON SIZE ERROR / NOT ON SIZE ERROR branches after the receivers are stored
static void lower_size_error_phrases(IrProgram *ir, AstNode *stmt) {
    if (!stmt->on_error && !stmt->not_on_error) return;
    char no_err_lbl[32], end_lbl[32];
    int idx = if_counter++;
    snprintf(no_err_lbl, sizeof(no_err_lbl), "size_ok_%d", idx);
    snprintf(end_lbl, sizeof(end_lbl), "size_end_%d", idx);

    emit_jump(ir, IR_JUMP_IF_NO_SIZE_ERR, no_err_lbl);
    lower_block(ir, stmt->on_error);
    emit_jump(ir, IR_JUMP, end_lbl);
    emit_label(ir, no_err_lbl);
    lower_block(ir, stmt->not_on_error);
    emit_label(ir, end_lbl);
}

// ADD, SUBTRACT, MULTIPLY, DIVIDE and COMPUTE: one expression per receiver.
// Child layouts are documented at the statement parsers.
static void lower_arith_node(IrProgram *ir, AstNode *node) {
    AstNode *ops[128];
    int n = 0;
    for (AstNode *c = node->children; c && n < 128; c = c->next) ops[n++] = c;
    bool giving = node->is_numeric;
    bool keep = node->on_error != NULL;

    emit(ir, IR_SIZE_ERR_CLEAR);

    switch (node->kind) {
        case AST_STATEMENT_ADD: {
            int nsrc = node->decimal_places;
            for (int i = nsrc; i < n; i++) {
                AstNode *tree = giving ? fold_ops(AST_EXPR_ADD, leaf_copy(ops[0]), ops + 1, nsrc - 1)
                                       : fold_ops(AST_EXPR_ADD, leaf_copy(ops[i]), ops, nsrc);
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_SUBTRACT: {
            int nsrc = node->decimal_places;
            int first_dest = giving ? nsrc + 1 : nsrc;
            for (int i = first_dest; i < n; i++) {
                AstNode *minuend = giving ? ops[nsrc] : ops[i];
                AstNode *tree = fold_ops(AST_EXPR_SUB, leaf_copy(minuend), ops, nsrc);
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_MULTIPLY: {
            int first_dest = giving ? 2 : 1;
            for (int i = first_dest; i < n; i++) {
                AstNode *tree = mk_bin(AST_EXPR_MUL, leaf_copy(ops[0]),
                                       leaf_copy(giving ? ops[1] : ops[i]));
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_DIVIDE: {
            bool is_by = node->name && strcasecmp(node->name, "BY") == 0;
            bool has_rem = node->is_signed;
            if (!giving) {
                // DIVIDE divisor INTO dest+
                for (int i = 1; i < n; i++) {
                    AstNode *tree = mk_bin(AST_EXPR_DIV, leaf_copy(ops[i]), leaf_copy(ops[0]));
                    lower_assign(ir, tree, ops[i], keep);
                    ast_free(tree);
                }
                break;
            }
            AstNode *dividend = is_by ? ops[0] : ops[1];
            AstNode *divisor  = is_by ? ops[1] : ops[0];
            int last_dest = has_rem ? n - 1 : n;
            for (int i = 2; i < last_dest; i++) {
                AstNode *tree = mk_bin(AST_EXPR_DIV, leaf_copy(dividend), leaf_copy(divisor));
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            if (has_rem) {
                // remainder = dividend - quotient * divisor, using the stored quotient
                AstNode *tree = mk_bin(AST_EXPR_SUB, leaf_copy(dividend),
                                       mk_bin(AST_EXPR_MUL, leaf_copy(ops[2]), leaf_copy(divisor)));
                lower_assign(ir, tree, ops[n - 1], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_COMPUTE:
            for (int i = 1; i < n; i++) lower_assign(ir, ops[0], ops[i], keep);
            break;
        default:
            break;
    }

    lower_size_error_phrases(ir, node);
}

// ── Conditions ───────────────────────────────────────────────────────────────

// Return the opposite of a comparison operator (used by PERFORM UNTIL).
static const char *negate_op(const char *op) {
    if (!op)              return "<>";
    if (!strcmp(op, "="))  return "<>";
    if (!strcmp(op, "<>")) return "=";
    if (!strcmp(op, ">"))  return "<=";
    if (!strcmp(op, "<"))  return ">=";
    if (!strcmp(op, ">=")) return "<";
    if (!strcmp(op, "<=")) return ">";
    return op;
}

static bool is_numeric_operand(AstNode *n) {
    if (n->kind == AST_LITERAL_NUMBER) return true;
    if (n->kind == AST_IDENTIFIER) {
        VarInfo *v = find_var(n->name);
        return v && v->is_numeric;
    }
    return false;
}

// Address and length of an operand compared as alphanumeric
static bool alnum_operand(IrProgram *ir, AstNode *n, const char **label, int *len,
                          char *buf, size_t buf_size) {
    if (n->kind == AST_IDENTIFIER) {
        VarInfo *v = find_var(n->name);
        if (!v) {
            cobalt_error_loc(NULL, n->loc.line, n->loc.column, "Undeclared identifier '%s'", n->name);
            return false;
        }
        *label = v->name;
        *len = v->size;
        return true;
    }
    if (n->kind == AST_LITERAL_NUMBER || n->kind == AST_STRING_LITERAL) {
        *label = emit_data_lit(ir, "cmplit", n->name, buf, buf_size);
        *len = (int)strlen(n->name);
        return true;
    }
    return false;
}

// Emit a conditional jump for an AST_CONDITION node.
// If negate=false → jump to target when condition is FALSE  (used by IF).
// If negate=true  → jump to target when condition is TRUE   (used by PERFORM UNTIL).
static void emit_cond_jump(IrProgram *ir, AstNode *cond,
                           const char *target, bool negate) {
    if (!cond || cond->kind != AST_CONDITION || !cond->children) return;
    AstNode *left  = cond->children;
    AstNode *right = left->next;
    if (!right) return;
    const char *op = cond->text ? cond->text : "=";
    if (negate) op = negate_op(op);

    IrInstr *jump;
    if (is_numeric_operand(left) && is_numeric_operand(right)) {
        if (!num_expr_valid(left) || !num_expr_valid(right)) return;
        int sl = num_expr(NULL, left, 0), sr = num_expr(NULL, right, 0);
        int s = sl > sr ? sl : sr;
        num_expr(ir, left, 0);
        emit_rescale(ir, s - sl, false);
        num_expr(ir, right, 0);
        emit_rescale(ir, s - sr, false);
        jump = emit(ir, IR_NUM_CMP_JUMP_IF_FALSE);
    } else {
        char lbuf[32], rbuf[32];
        const char *ll, *rl;
        int llen, rlen;
        if (!alnum_operand(ir, left, &ll, &llen, lbuf, sizeof(lbuf)) ||
            !alnum_operand(ir, right, &rl, &rlen, rbuf, sizeof(rbuf))) return;
        jump = emit(ir, IR_ALNUM_CMP_JUMP_IF_FALSE);
        jump->var_name = strdup(ll);
        jump->size = llen;
        jump->lit_name = strdup(rl);
        jump->lit_len = rlen;
    }
    jump->cmp_op = strdup(op);
    jump->jump_target = strdup(target);
}

// ── Statements ───────────────────────────────────────────────────────────────

// MOVE src TO dest (one receiver)
static void lower_move(IrProgram *ir, AstNode *src, AstNode *dest_node, const char *src_lit) {
    VarInfo *dest = find_var(dest_node->name);
    if (!dest) {
        cobalt_error_loc(NULL, dest_node->loc.line, dest_node->loc.column,
                         "Undeclared identifier '%s'", dest_node->name);
        return;
    }

    if (is_zero_constant(src) || is_space_constant(src)) {
        if (dest->is_numeric && is_space_constant(src)) {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "SPACES cannot be moved to numeric '%s'", dest->name);
        } else if (dest->is_numeric) {
            emit_num(ir, IR_NUM_CONST, 0, 0);
            emit_store(ir, dest, false);
        } else {
            append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->name, NULL, NULL, 0, dest->size, 0,
                                       is_space_constant(src) ? ' ' : '0', dest->usage_type));
        }
        return;
    }

    if (src->kind == AST_STRING_LITERAL) {
        if (dest->is_numeric) {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "A nonnumeric literal cannot be moved to numeric '%s'", dest->name);
            return;
        }
        int len = (int)strlen(src->name);
        append_instr(ir, new_instr(IR_CALL_MOVE_ALPHA, dest->name, src_lit, NULL,
                                   len > dest->size ? dest->size : len, dest->size, 0, 0, dest->usage_type));
        return;
    }

    if (src->kind == AST_LITERAL_NUMBER) {
        if (dest->is_numeric) {
            if (!num_expr_valid(src)) return;
            int s = num_expr(ir, src, 0);
            emit_rescale(ir, dest->decimal_places - s, false);
            emit_store(ir, dest, false);
        } else {
            int len = (int)strlen(src->name);
            append_instr(ir, new_instr(IR_CALL_MOVE_ALPHA, dest->name, src_lit, NULL,
                                       len > dest->size ? dest->size : len, dest->size, 0, 0, dest->usage_type));
        }
        return;
    }

    if (src->kind != AST_IDENTIFIER) return;
    VarInfo *sv = find_var(src->name);
    if (!sv) {
        cobalt_error_loc(NULL, src->loc.line, src->loc.column, "Undeclared identifier '%s'", src->name);
        return;
    }
    if (sv->is_numeric && dest->is_numeric) {
        emit_load(ir, sv);
        emit_rescale(ir, dest->decimal_places - sv->decimal_places, false);
        emit_store(ir, dest, false);
    } else if (!dest->is_numeric) {
        if (sv->is_numeric && sv->usage_type != USAGE_DISPLAY) {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "MOVE of non-DISPLAY numeric '%s' to alphanumeric is not supported yet", sv->name);
            return;
        }
        append_instr(ir, new_instr(IR_CALL_MOVE_ALPHA, dest->name, sv->name, NULL,
                                   sv->size, dest->size, 0, 0, dest->usage_type));
    } else if (dest->usage_type == USAGE_DISPLAY && !dest->is_signed && dest->decimal_places == 0) {
        int len = sv->size > dest->size ? dest->size : sv->size;
        append_instr(ir, new_instr(IR_CALL_MOVE_NUM, dest->name, sv->name, NULL,
                                   len, dest->size, 0, 0, dest->usage_type));
    } else {
        cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                         "MOVE of alphanumeric '%s' to this numeric item is not supported yet", sv->name);
    }
}

static void lower_node(IrProgram *ir, AstNode *node) {
    if (!node) return;

    if (node->kind == AST_PARAGRAPH) {
        // Paragraphs are emitted as subroutines by lower_to_ir; this fallback
        // inlines the body if lower_node is called directly (e.g. in stub paths).
        lower_block(ir, node->children);
    } else if (node->kind == AST_STATEMENT_PERFORM) {
        AstNode *target = node->children;
        if (!target || target->kind != AST_IDENTIFIER) return;

        if (node->name && strcasecmp(node->name, "UNTIL") == 0) {
            // PERFORM para UNTIL condition
            AstNode *cond = target->next; // AST_CONDITION

            char loop_lbl[32], done_lbl[32];
            int idx = if_counter++;
            snprintf(loop_lbl, sizeof(loop_lbl), "perf_until_%d", idx);
            snprintf(done_lbl, sizeof(done_lbl), "perf_done_%d",  idx);

            emit_label(ir, loop_lbl);

            // Jump to done when condition is TRUE (negate=true fires on TRUE)
            emit_cond_jump(ir, cond, done_lbl, true);

            // Execute the paragraph, then loop back
            append_instr(ir, new_instr(IR_CALL_PARA, target->name, NULL, NULL,
                                       0, 0, 0, 0, USAGE_DISPLAY));
            emit_jump(ir, IR_JUMP, loop_lbl);

            emit_label(ir, done_lbl);

        } else if (!node->is_numeric) {
            // Basic PERFORM: call the paragraph once
            append_instr(ir, new_instr(IR_CALL_PARA, target->name, NULL, NULL,
                                       0, 0, 0, 0, USAGE_DISPLAY));
        } else {
            // PERFORM n TIMES — target->next is the count node
            AstNode *count = target->next;
            if (!count) return;

            IrInstr *times;
            if (count->kind == AST_LITERAL_NUMBER) {
                times = new_instr(IR_PERFORM_TIMES, target->name, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY);
                times->imm = count->name ? atoll(count->name) : 0;
            } else {
                VarInfo *cnt = numeric_var(count, "PERFORM TIMES count");
                if (!cnt) return;
                emit_load(ir, cnt);
                emit_rescale(ir, -cnt->decimal_places, false); // integer part
                times = new_instr(IR_PERFORM_TIMES, target->name, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY);
                times->flags = 1; // count on the numeric stack
            }
            append_instr(ir, times);
        }
    } else if (node->kind == AST_STATEMENT_IF) {
        AstNode *cond      = node->children;
        AstNode *then_blk  = cond  ? cond->next  : NULL;
        AstNode *else_blk  = then_blk ? then_blk->next : NULL;

        char else_lbl[32], end_lbl[32];
        int idx = if_counter++;
        snprintf(else_lbl, sizeof(else_lbl), "if_else_%d", idx);
        snprintf(end_lbl,  sizeof(end_lbl),  "if_end_%d",  idx);
        bool has_else = (else_blk && else_blk->kind == AST_ELSE_BLOCK);

        // Jump to else/end when condition is FALSE
        emit_cond_jump(ir, cond, has_else ? else_lbl : end_lbl, false);

        if (then_blk) lower_block(ir, then_blk->children);

        if (has_else) {
            emit_jump(ir, IR_JUMP, end_lbl);
            emit_label(ir, else_lbl);
            lower_block(ir, else_blk->children);
        }

        emit_label(ir, end_lbl);
    } else if (node->kind == AST_STATEMENT_DISPLAY) {
        for (AstNode *arg = node->children; arg; arg = arg->next) {
            const char *content = arg->name ? arg->name : "";
            if (arg->kind == AST_STRING_LITERAL || arg->kind == AST_LITERAL_NUMBER) {
                char lit_name[32];
                emit_data_lit(ir, "lit", content, lit_name, sizeof(lit_name));
                append_instr(ir, new_instr(IR_CALL_DISPLAY_STR, NULL, lit_name, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
            } else if (arg->kind == AST_FIGURATIVE_CONSTANT) {
                char pad_char = is_zero_constant(arg) ? '0' : ' ';
                append_instr(ir, new_instr(IR_CALL_DISPLAY_FILL, NULL, NULL, NULL, 0, 1, 0, pad_char, USAGE_DISPLAY));
            } else if (arg->kind == AST_IDENTIFIER) {
                VarInfo *v = find_var(arg->name);
                if (!v) {
                    cobalt_warn("Variable '%s' not found for DISPLAY", arg->name);
                } else if (v->is_numeric) {
                    IrInstr *d = new_instr(IR_CALL_DISPLAY_NUM, v->name, NULL, NULL, 0, v->size,
                                           v->decimal_places, 0, v->usage_type);
                    d->flags = var_flags(v);
                    append_instr(ir, d);
                } else {
                    append_instr(ir, new_instr(IR_CALL_DISPLAY_VAR_STR, v->name, NULL, NULL, 0, v->size, 0, 0, v->usage_type));
                }
            }
        }
        append_instr(ir, new_instr(IR_EMIT_NEWLINE, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    } else if (node->kind == AST_STATEMENT_CONTINUE) {
        // No-op: CONTINUE does nothing
    } else if (node->kind == AST_STATEMENT_MOVE) {
        AstNode *src = node->children;
        if (!src) return;

        // A literal source is emitted once and shared by all receivers
        char lit_name[32] = {0};
        if (src->kind == AST_STRING_LITERAL || src->kind == AST_LITERAL_NUMBER)
            emit_data_lit(ir, "lit", src->name ? src->name : "", lit_name, sizeof(lit_name));

        emit(ir, IR_SIZE_ERR_CLEAR);
        for (AstNode *dest = src->next; dest && dest->kind == AST_IDENTIFIER; dest = dest->next)
            lower_move(ir, src, dest, lit_name);
    } else if (node->kind == AST_STATEMENT_ADD || node->kind == AST_STATEMENT_SUBTRACT ||
               node->kind == AST_STATEMENT_MULTIPLY || node->kind == AST_STATEMENT_DIVIDE ||
               node->kind == AST_STATEMENT_COMPUTE) {
        lower_arith_node(ir, node);
    } else if (node->kind == AST_STATEMENT_STOP) {
        append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    } else if (node->kind == AST_STATEMENT_ACCEPT) {
        AstNode *var_node = node->children; // Should be an AST_IDENTIFIER
        if (var_node && var_node->kind == AST_IDENTIFIER) {
            VarInfo *v = find_var(var_node->name);
            if (!v) {
                cobalt_warn("Variable '%s' not found for ACCEPT", var_node->name);
            } else if (v->is_numeric) {
                IrInstr *a = new_instr(IR_CALL_ACCEPT_NUM, v->name, NULL, NULL, 0, v->size,
                                       v->decimal_places, 0, v->usage_type);
                a->digits = v->digits;
                a->flags = var_flags(v);
                append_instr(ir, a);
            } else {
                append_instr(ir, new_instr(IR_CALL_ACCEPT, v->name, NULL, NULL, 0, v->size, 0, ' ', v->usage_type));
            }
        } else {
            cobalt_warn("ACCEPT statement requires a variable", NULL);
        }
    }
}

static void lower_block(IrProgram *ir, AstNode *block) {
    AstNode *n = block;
    while (n) {
        lower_node(ir, n);
        n = n->next;
    }
}

// Initial value of one data item from its VALUE clause
static void lower_initial_value(IrProgram *ir, VarInfo *v) {
    AstNode *val = v->value;
    if (!val) return;
    if (is_zero_constant(val) || is_space_constant(val)) {
        if (v->is_numeric) {
            emit_num(ir, IR_NUM_CONST, 0, 0);
            emit_store(ir, v, false);
        } else {
            append_instr(ir, new_instr(IR_CALL_MOVE_FILL, v->name, NULL, NULL, 0, v->size, 0,
                                       is_space_constant(val) ? ' ' : '0', v->usage_type));
        }
    } else if (val->kind == AST_LITERAL_NUMBER && v->is_numeric) {
        if (!num_expr_valid(val)) return;
        int s = num_expr(ir, val, 0);
        emit_rescale(ir, v->decimal_places - s, false);
        emit_store(ir, v, false);
    } else if (val->kind == AST_STRING_LITERAL && !v->is_numeric) {
        char lit_name[32];
        int len = (int)strlen(val->name);
        emit_data_lit(ir, "initlit", val->name, lit_name, sizeof(lit_name));
        append_instr(ir, new_instr(IR_CALL_MOVE_ALPHA, v->name, lit_name, NULL,
                                   len > v->size ? v->size : len, v->size, 0, 0, v->usage_type));
    }
}

IrProgram *lower_to_ir(AstNode *program) {
    IrProgram *ir = calloc(1, sizeof(IrProgram));
    if (!ir) return NULL;

    num_vars = 0;
    lit_counter = 0;
    if_counter = 0;

    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "DATA") != 0) continue;
        for (AstNode *sec = div->children; sec; sec = sec->next) {
            if (sec->kind != AST_SECTION) continue;
            for (AstNode *decl = sec->children; decl; decl = decl->next) {
                if (decl->kind != AST_VAR_DECL) continue;
                if (num_vars >= 512) {
                    cobalt_error("Internal variable table full (512 limit)");
                    break;
                }
                // sema.c has already populated the semantic fields
                VarInfo *v = &vars[num_vars++];
                memset(v, 0, sizeof(VarInfo));
                v->name = strdup(decl->name);
                v->size = (int)decl->data_size;
                v->decimal_places = decl->decimal_places;
                v->digits = decl->digits;
                v->is_numeric = decl->is_numeric;
                v->is_signed = decl->is_signed;
                v->usage_type = decl->usage_type;
                for (AstNode *c = decl->children; c; c = c->next) {
                    if (c->kind == AST_STRING_LITERAL || c->kind == AST_LITERAL_NUMBER ||
                        c->kind == AST_FIGURATIVE_CONSTANT) v->value = c;
                }
                append_instr(ir, new_instr(IR_DATA_VAR, v->name, NULL, NULL, 0, v->size,
                                           v->decimal_places, 0, v->usage_type));
            }
        }
    }

    // Emit initializations
    append_instr(ir, new_instr(IR_CALL_INIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    emit(ir, IR_SIZE_ERR_CLEAR);
    for (int i = 0; i < num_vars; i++) lower_initial_value(ir, &vars[i]);

    // Lower PROCEDURE DIVISION
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "PROCEDURE") != 0) continue;
        AstNode *child = div->children;

        // Check whether the division contains any paragraph nodes
        bool has_paras = false;
        for (AstNode *n = child; n; n = n->next) {
            if (n->kind == AST_PARAGRAPH) { has_paras = true; break; }
        }

        if (!has_paras) {
            // No paragraphs: inline everything directly
            lower_block(ir, child);
            continue;
        }

        // Each paragraph becomes a subroutine.
        // The main sequence calls them in source order so that sequential
        // fall-through (Para1 → Para2 → Para3) still works correctly.
        for (AstNode *n = child; n; n = n->next) {
            if (n->kind == AST_PARAGRAPH) {
                append_instr(ir, new_instr(IR_CALL_PARA, n->name, NULL, NULL,
                                           0, 0, 0, 0, USAGE_DISPLAY));
            } else {
                lower_node(ir, n); // top-level non-paragraph statement
            }
        }
        // Safety exit in case no paragraph reaches STOP RUN
        append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL,
                                   0, 0, 0, 0, USAGE_DISPLAY));

        // Emit each paragraph body as a labelled subroutine
        for (AstNode *n = child; n; n = n->next) {
            if (n->kind == AST_PARAGRAPH) {
                emit_label(ir, n->name);
                lower_block(ir, n->children);
                append_instr(ir, new_instr(IR_RET, NULL, NULL, NULL,
                                           0, 0, 0, 0, USAGE_DISPLAY));
            }
        }
    }

    return ir;
}

void ir_free(IrProgram *ir) {
    if (!ir) return;
    IrInstr *i = ir->head;
    while (i) {
        IrInstr *next = i->next;
        free((void*)i->var_name);
        free((void*)i->lit_name);
        free((void*)i->content);
        free((void*)i->cmp_op);
        free((void*)i->jump_target);
        free(i);
        i = next;
    }
    for (int i = 0; i < num_vars; i++) {
        free(vars[i].name);
    }
    free(ir);
}
