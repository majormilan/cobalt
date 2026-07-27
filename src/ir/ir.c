/* src/ir/ir.c */
#include "ir.h"
#include "ast.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
typedef struct {
    char *name;
    int size;
    int decimal_places;
    bool is_numeric;
    AstUsageType usage_type; // Add usage type to VarInfo
    char pad_char;          // ' ' or '0' or 0 if no pad
    char *init_content;     // raw content (string or digits without '.')
    int init_len;
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
    i->usage_type = usage_type; // Initialize the new field
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

static IrInstr *new_jump_instr(const char *target) {
    IrInstr *i = calloc(1, sizeof(IrInstr));
    if (!i) return NULL;
    i->op = IR_JUMP;
    i->jump_target = target ? strdup(target) : NULL;
    return i;
}

static IrInstr *new_cmp_jump_instr(
        const char *left_name, int left_size, bool left_is_lit,
        const char *right_name, int right_size, bool right_is_lit,
        const char *op, const char *target) {
    IrInstr *i = calloc(1, sizeof(IrInstr));
    if (!i) return NULL;
    i->op             = IR_CMP_JUMP_IF_FALSE;
    i->var_name       = left_name  ? strdup(left_name)  : NULL;
    i->size           = left_size;
    i->left_is_literal  = left_is_lit;
    i->lit_name       = right_name ? strdup(right_name) : NULL;
    i->lit_len        = right_size;
    i->right_is_literal = right_is_lit;
    i->cmp_op         = op     ? strdup(op)     : NULL;
    i->jump_target    = target ? strdup(target) : NULL;
    return i;
}

static VarInfo *find_var(const char *name) {
    for (int i = 0; i < num_vars; i++) {
        if (strcasecmp(vars[i].name, name) == 0) return &vars[i];
    }
    return NULL;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// Resolve an AST operand node (IDENTIFIER or LITERAL_NUMBER) for a condition.
// Fills name, size, is_literal; returns false on error.
static bool resolve_cond_operand(AstNode *node,
                                 const char **out_name, int *out_size, bool *out_is_lit) {
    if (!node) return false;
    if (node->kind == AST_IDENTIFIER) {
        VarInfo *v = find_var(node->name);
        if (!v) { cobalt_warn("Variable '%s' not found in condition", node->name); return false; }
        *out_name   = v->name;
        *out_size   = v->size;
        *out_is_lit = false;
        return true;
    }
    if (node->kind == AST_LITERAL_NUMBER) {
        *out_name   = node->name;
        *out_size   = 0;
        *out_is_lit = true;
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
    AstNode *left_node  = cond->children;
    AstNode *right_node = left_node ? left_node->next : NULL;
    const char *op = cond->text ? cond->text : "=";
    if (negate) op = negate_op(op);

    const char *ln = NULL; int ls = 0; bool ll = false;
    const char *rn = NULL; int rs = 0; bool rl = false;
    if (!resolve_cond_operand(left_node,  &ln, &ls, &ll)) return;
    if (!resolve_cond_operand(right_node, &rn, &rs, &rl)) return;

    append_instr(ir, new_cmp_jump_instr(ln, ls, ll, rn, rs, rl, op, target));
}

// Allocate a numeric display temporary variable (in .bss, zeroed).
// Returns the variable name (stable heap pointer), or NULL on error.
static const char *alloc_temp_var(IrProgram *ir, int size, int decimals) {
    if (num_vars >= 512) {
        cobalt_error("Internal variable table full (512 limit)");
        return NULL;
    }
    char name[32];
    snprintf(name, sizeof(name), "cexpr_%d", lit_counter++);
    VarInfo *v = &vars[num_vars++];
    memset(v, 0, sizeof(VarInfo));
    v->name          = strdup(name);
    v->size          = size > 0 ? size : 18;
    v->decimal_places = decimals >= 0 ? decimals : 0;
    v->is_numeric    = true;
    v->usage_type    = USAGE_DISPLAY;
    v->pad_char      = '0';
    append_instr(ir, new_instr(IR_DATA_VAR, v->name, NULL, NULL,
                               0, v->size, v->decimal_places, 0, USAGE_DISPLAY));
    append_instr(ir, new_instr(IR_CALL_MOVE_FILL, v->name, NULL, NULL,
                               0, v->size, v->decimal_places, '0', USAGE_DISPLAY));
    return v->name;
}

// Evaluate an arithmetic expression AST node.
// Returns the name of the variable holding the result (may be an existing var
// for simple identifier leaves — do NOT modify it in-place).
// hint_size / hint_decimals: desired size for temporaries.
static const char *lower_expr(IrProgram *ir, AstNode *expr,
                              int hint_size, int hint_decimals);

static const char *lower_expr(IrProgram *ir, AstNode *expr,
                              int hint_size, int hint_decimals) {
    if (!expr) return NULL;

    // Leaf: existing variable — return its name directly (read-only reference)
    if (expr->kind == AST_IDENTIFIER) {
        VarInfo *v = find_var(expr->name);
        if (!v) { cobalt_warn("Variable '%s' not found in COMPUTE expression", expr->name); return NULL; }
        return v->name;
    }

    // Leaf: numeric literal — create a temp and load the value into it
    if (expr->kind == AST_LITERAL_NUMBER) {
        const char *tmp = alloc_temp_var(ir, hint_size, hint_decimals);
        if (!tmp) return NULL;
        VarInfo *tv = find_var(tmp);
        char lit_lbl[32];
        snprintf(lit_lbl, sizeof(lit_lbl), "clit_%d", lit_counter++);
        const char *content = expr->name ? expr->name : "0";
        int len = (int)strlen(content);
        append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_lbl, content, len, 0, 0, 0, USAGE_DISPLAY));
        // temp already zeroed; add literal into it
        append_instr(ir, new_instr(IR_CALL_ADD_TO, tv->name, lit_lbl, NULL,
                                   len, tv->size, tv->decimal_places, 0, tv->usage_type));
        return tv->name;
    }

    // Binary operation: evaluate both sub-expressions, then combine into a temp
    AstNode *left  = expr->children;
    AstNode *right = left ? left->next : NULL;
    if (!left || !right) return NULL;

    const char *left_name  = lower_expr(ir, left,  hint_size, hint_decimals);
    const char *right_name = lower_expr(ir, right, hint_size, hint_decimals);
    if (!left_name || !right_name) return NULL;

    // Re-lookup after potential alloc_temp_var calls (safe: names are heap strings)
    VarInfo *lv = find_var(left_name);
    VarInfo *rv = find_var(right_name);
    if (!lv || !rv) return NULL;

    int res_size = (lv->size > rv->size) ? lv->size : rv->size;
    int res_dec  = (lv->decimal_places > rv->decimal_places)
                   ? lv->decimal_places : rv->decimal_places;
    if (res_size <= 0) res_size = hint_size > 0 ? hint_size : 18;

    const char *res_name = alloc_temp_var(ir, res_size, res_dec);
    if (!res_name) return NULL;
    VarInfo *res = find_var(res_name);
    if (!res) return NULL;

    // result = copy of left  (temp is already zeroed, so just add lv)
    append_instr(ir, new_instr(IR_CALL_ADD_TO, res->name, lv->name, NULL,
                               lv->size, res->size, res->decimal_places, 0, res->usage_type));

    // result op= right
    IrOpcode op_code;
    switch (expr->kind) {
        case AST_EXPR_ADD: op_code = IR_CALL_ADD_TO;    break;
        case AST_EXPR_SUB: op_code = IR_CALL_SUB_FROM;  break;
        case AST_EXPR_MUL: op_code = IR_CALL_MUL_BY;    break;
        case AST_EXPR_DIV: op_code = IR_CALL_DIV_INTO;  break;
        default: return res->name;
    }
    append_instr(ir, new_instr(op_code, res->name, rv->name, NULL,
                               rv->size, res->size, res->decimal_places, 0, res->usage_type));
    return res->name;
}

static void lower_block(IrProgram *ir, AstNode *block);
static void emit_copy_to(IrProgram *ir, VarInfo *dest, AstNode *src, const char *lit_prefix);
static void emit_op_from_src(IrProgram *ir, IrOpcode op, VarInfo *dest, AstNode *src, const char *lit_prefix);

static void lower_arith_node(IrProgram *ir, AstNode *node) {
    // ── SUBTRACT ─────────────────────────────────────────────────────────────
    if (node->kind == AST_STATEMENT_SUBTRACT) {
        int n_sub = node->decimal_places;
        bool giving = node->is_numeric;

        // Build subtrahends array
        AstNode *subs[64]; int sub_cnt = 0;
        AstNode *child = node->children;
        for (int i = 0; i < n_sub && child; i++, child = child->next)
            subs[sub_cnt++] = child;

        if (!giving) {
            // SUBTRACT a b FROM x y  →  x -= a+b, y -= a+b
            while (child) {
                VarInfo *dest = find_var(child->name);
                if (dest) for (int i = 0; i < sub_cnt; i++)
                    emit_op_from_src(ir, IR_CALL_SUB_FROM, dest, subs[i], "sublit");
                else cobalt_warn("Variable '%s' not found for SUBTRACT target", child->name);
                child = child->next;
            }
        } else {
            // SUBTRACT a b FROM x GIVING z  →  z = x - a - b
            AstNode *from_op = child;          // one FROM operand
            if (!from_op) return;
            AstNode *giving_start = from_op->next;
            while (giving_start) {
                VarInfo *dest = find_var(giving_start->name);
                if (dest) {
                    emit_copy_to(ir, dest, from_op, "sublit");
                    for (int i = 0; i < sub_cnt; i++)
                        emit_op_from_src(ir, IR_CALL_SUB_FROM, dest, subs[i], "sublit");
                } else cobalt_warn("Variable '%s' not found for SUBTRACT GIVING", giving_start->name);
                giving_start = giving_start->next;
            }
        }
    }
    // ── MULTIPLY ─────────────────────────────────────────────────────────────
    else if (node->kind == AST_STATEMENT_MULTIPLY) {
        bool giving = node->is_numeric;
        AstNode *factor1 = node->children;
        if (!factor1) return;
        AstNode *rest = factor1->next;

        if (!giving) {
            // MULTIPLY a BY x y  →  x *= a, y *= a
            while (rest) {
                VarInfo *dest = find_var(rest->name);
                if (dest) emit_op_from_src(ir, IR_CALL_MUL_BY, dest, factor1, "mullit");
                else cobalt_warn("Variable '%s' not found for MULTIPLY target", rest->name);
                rest = rest->next;
            }
        } else {
            // MULTIPLY a BY b GIVING z  →  z = a * b
            AstNode *factor2 = rest;
            if (!factor2) return;
            AstNode *giving_start = factor2->next;
            while (giving_start) {
                VarInfo *dest = find_var(giving_start->name);
                if (dest) {
                    emit_copy_to(ir, dest, factor1, "mullit");
                    emit_op_from_src(ir, IR_CALL_MUL_BY, dest, factor2, "mullit");
                } else cobalt_warn("Variable '%s' not found for MULTIPLY GIVING", giving_start->name);
                giving_start = giving_start->next;
            }
        }
    }
    // ── DIVIDE ───────────────────────────────────────────────────────────────
    else if (node->kind == AST_STATEMENT_DIVIDE) {
        bool giving   = node->is_numeric;
        bool has_rem  = node->is_signed;
        bool is_by    = node->name && strcasecmp(node->name, "BY") == 0;
        AstNode *child = node->children;

        if (!giving && !is_by) {
            // DIVIDE divisor INTO target+
            AstNode *divisor = child;
            AstNode *tgt = divisor ? divisor->next : NULL;
            while (tgt) {
                VarInfo *dest = find_var(tgt->name);
                if (dest) emit_op_from_src(ir, IR_CALL_DIV_INTO, dest, divisor, "divlit");
                else cobalt_warn("Variable '%s' not found for DIVIDE target", tgt->name);
                tgt = tgt->next;
            }
        } else if (giving && !is_by) {
            // DIVIDE divisor INTO dividend GIVING result+ [REMAINDER r]
            AstNode *divisor  = child;
            AstNode *dividend = child ? child->next : NULL;
            if (!divisor || !dividend) return;

            // Giving targets: skip last child if REMAINDER
            AstNode *giving_cur = dividend->next;
            // Count giving nodes to know which is last (remainder)
            int giving_total = 0;
            for (AstNode *n = giving_cur; n; n = n->next) giving_total++;
            int giving_count = has_rem ? giving_total - 1 : giving_total;

            int gi = 0;
            for (AstNode *gn = giving_cur; gn && gi < giving_count; gn = gn->next, gi++) {
                VarInfo *dest = find_var(gn->name);
                if (dest) {
                    emit_copy_to(ir, dest, dividend, "divlit");
                    emit_op_from_src(ir, IR_CALL_DIV_INTO, dest, divisor, "divlit");
                }
            }
            if (has_rem && giving_total > 0) {
                // Walk to last child (remainder var)
                AstNode *rem_node = giving_cur;
                for (int k = 0; k + 1 < giving_total; k++) rem_node = rem_node->next;
                VarInfo *rem = find_var(rem_node->name);
                if (rem) {
                    emit_copy_to(ir, rem, dividend, "divlit");
                    emit_op_from_src(ir, IR_CALL_MOD_INTO, rem, divisor, "divlit");
                }
            }
        } else if (giving && is_by) {
            // DIVIDE dividend BY divisor GIVING result+ [REMAINDER r]
            AstNode *dividend = child;
            AstNode *divisor  = child ? child->next : NULL;
            if (!dividend || !divisor) return;

            AstNode *giving_cur = divisor->next;
            int giving_total = 0;
            for (AstNode *n = giving_cur; n; n = n->next) giving_total++;
            int giving_count = has_rem ? giving_total - 1 : giving_total;

            int gi = 0;
            for (AstNode *gn = giving_cur; gn && gi < giving_count; gn = gn->next, gi++) {
                VarInfo *dest = find_var(gn->name);
                if (dest) {
                    emit_copy_to(ir, dest, dividend, "divlit");
                    emit_op_from_src(ir, IR_CALL_DIV_INTO, dest, divisor, "divlit");
                }
            }
            if (has_rem && giving_total > 0) {
                AstNode *rem_node = giving_cur;
                for (int k = 0; k + 1 < giving_total; k++) rem_node = rem_node->next;
                VarInfo *rem = find_var(rem_node->name);
                if (rem) {
                    emit_copy_to(ir, rem, dividend, "divlit");
                    emit_op_from_src(ir, IR_CALL_MOD_INTO, rem, divisor, "divlit");
                }
            }
        }
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

            append_instr(ir, new_instr(IR_LABEL, loop_lbl, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));

            // Jump to done when condition is TRUE (negate=true fires on TRUE)
            emit_cond_jump(ir, cond, done_lbl, true);

            // Execute the paragraph, then loop back
            append_instr(ir, new_instr(IR_CALL_PARA, target->name, NULL, NULL,
                                       0, 0, 0, 0, USAGE_DISPLAY));
            append_instr(ir, new_jump_instr(loop_lbl));

            append_instr(ir, new_instr(IR_LABEL, done_lbl, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));

        } else if (!node->is_numeric) {
            // Basic PERFORM: call the paragraph once
            append_instr(ir, new_instr(IR_CALL_PARA, target->name, NULL, NULL,
                                       0, 0, 0, 0, USAGE_DISPLAY));
        } else {
            // PERFORM n TIMES — target->next is the count node
            AstNode *count = target->next;
            if (!count) return;

            if (count->kind == AST_LITERAL_NUMBER) {
                int n = count->name ? atoi(count->name) : 0;
                append_instr(ir, new_instr(IR_PERFORM_TIMES, target->name, NULL,
                                           NULL, 0, n, 0, 0, USAGE_DISPLAY));
            } else if (count->kind == AST_IDENTIFIER) {
                VarInfo *cnt = find_var(count->name);
                if (!cnt) { cobalt_warn("Variable '%s' not found for PERFORM TIMES count", count->name); return; }
                append_instr(ir, new_instr(IR_PERFORM_TIMES, target->name, cnt->name,
                                           NULL, cnt->size, 0, 0, 0, USAGE_DISPLAY));
            }
        }
    } else if (node->kind == AST_STATEMENT_COMPUTE) {
        AstNode *dest_node = node->children;
        AstNode *expr_node = dest_node ? dest_node->next : NULL;
        if (!dest_node || !expr_node) return;

        VarInfo *dest = find_var(dest_node->name);
        if (!dest) {
            cobalt_warn("Variable '%s' not found for COMPUTE", dest_node->name);
            return;
        }

        const char *result_name = lower_expr(ir, expr_node, dest->size, dest->decimal_places);
        if (!result_name) return;

        VarInfo *result = find_var(result_name);
        if (!result) return;

        // dest = result  (zero dest, then add result into it)
        append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->name, NULL, NULL,
                                   0, dest->size, dest->decimal_places, '0', dest->usage_type));
        append_instr(ir, new_instr(IR_CALL_ADD_TO, dest->name, result->name, NULL,
                                   result->size, dest->size, dest->decimal_places, 0, dest->usage_type));
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
            append_instr(ir, new_jump_instr(end_lbl));
            append_instr(ir, new_instr(IR_LABEL, else_lbl, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
            lower_block(ir, else_blk->children);
        }

        append_instr(ir, new_instr(IR_LABEL, end_lbl, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    } else if (node->kind == AST_STATEMENT_DISPLAY) {
        AstNode *arg = node->children;
        while (arg) {
            const char *content = arg->name ? arg->name : "";
            int len = strlen(content);
            if (arg->kind == AST_STRING_LITERAL || arg->kind == AST_LITERAL_NUMBER) {
                char lit_name[32];
                sprintf(lit_name, "lit_%d", lit_counter++);
                append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name, content, len, 0, 0, 0, USAGE_DISPLAY));
                append_instr(ir, new_instr(IR_CALL_DISPLAY_STR, NULL, lit_name, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
            } else if (arg->kind == AST_FIGURATIVE_CONSTANT) {
                char pad_char = 0;
                if (strcasecmp(arg->name, "ZEROS") == 0 || strcasecmp(arg->name, "ZERO") == 0) {
                    pad_char = '0';
                } else if (strcasecmp(arg->name, "SPACES") == 0 || strcasecmp(arg->name, "SPACE") == 0) {
                    pad_char = ' ';
                }
                // Need variable info for size and usage type if displaying to screen.
                // For direct display of figurative constant, assume default string display.
                // The backend will need to know what to fill.
                // For simplicity here, we pass a dummy size and usage,
                // and use the pad_char as the primary instruction.
                // If it's a variable, we pass the variable's properties.
                // If it's a literal figurative constant, we need to know what size to fill/display.
                // For now, let's assume a default size for direct literal display, or this needs rethinking.
                // For now, I'll pass 0 for size and decimals, and USAGE_DISPLAY.
                append_instr(ir, new_instr(IR_CALL_DISPLAY_FILL, NULL, NULL, NULL, 0, 1, 0, pad_char, USAGE_DISPLAY));
            } else if (arg->kind == AST_IDENTIFIER) {
                VarInfo *v = find_var(arg->name);
                if (v) {
                    if (v->is_numeric) {
                        append_instr(ir, new_instr(IR_CALL_DISPLAY_NUM, arg->name, NULL, NULL, 0, v->size, v->decimal_places, 0, v->usage_type));
                    } else {
                        append_instr(ir, new_instr(IR_CALL_DISPLAY_VAR_STR, arg->name, NULL, NULL, 0, v->size, 0, 0, v->usage_type));
                    }
                } else {
                    cobalt_warn("Variable '%s' not found for DISPLAY", arg->name);
                }
            }
            arg = arg->next;
        }
        append_instr(ir, new_instr(IR_EMIT_NEWLINE, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    } else if (node->kind == AST_STATEMENT_CONTINUE) {
        // No-op: CONTINUE does nothing
    } else if (node->kind == AST_STATEMENT_MOVE) {
        AstNode *src = node->children;
        AstNode *dest = src ? src->next : NULL;
        if (!src || !dest) return;

        // For literal sources, emit the data section entry once and reuse the label
        char lit_name[32];
        int src_lit_len = 0;
        bool src_is_lit = (src->kind == AST_STRING_LITERAL || src->kind == AST_LITERAL_NUMBER);
        if (src_is_lit) {
            const char *content = src->name ? src->name : "";
            src_lit_len = (int)strlen(content);
            sprintf(lit_name, "lit_%d", lit_counter++);
            append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name, content,
                                       src_lit_len, 0, 0, 0, USAGE_DISPLAY));
        }

        // For variable sources, look up once
        VarInfo *src_var = NULL;
        if (src->kind == AST_IDENTIFIER) {
            src_var = find_var(src->name);
            if (!src_var) cobalt_warn("Variable '%s' not found for MOVE source", src->name);
        }

        // Emit a move for each destination
        while (dest && dest->kind == AST_IDENTIFIER) {
            VarInfo *v = find_var(dest->name);
            if (!v) {
                cobalt_warn("Variable '%s' not found for MOVE target", dest->name);
                dest = dest->next;
                continue;
            }
            if (src_is_lit) {
                int use_len = src_lit_len > v->size ? v->size : src_lit_len;
                IrOpcode op = v->is_numeric ? IR_CALL_MOVE_NUM : IR_CALL_MOVE_ALPHA;
                append_instr(ir, new_instr(op, dest->name, lit_name, NULL,
                                           use_len, v->size, v->decimal_places, 0, v->usage_type));
            } else if (src->kind == AST_FIGURATIVE_CONSTANT) {
                char fill_char = 0;
                if (strcasecmp(src->name, "ZEROS") == 0 || strcasecmp(src->name, "ZERO") == 0)
                    fill_char = '0';
                else if (strcasecmp(src->name, "SPACES") == 0 || strcasecmp(src->name, "SPACE") == 0)
                    fill_char = ' ';
                append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->name, NULL, NULL,
                                           0, v->size, v->decimal_places, fill_char, v->usage_type));
            } else if (src->kind == AST_IDENTIFIER && src_var) {
                // Variable-to-variable: reuse move opcodes — lit_name = src var label (BSS)
                IrOpcode op = v->is_numeric ? IR_CALL_MOVE_NUM : IR_CALL_MOVE_ALPHA;
                append_instr(ir, new_instr(op, dest->name, src_var->name, NULL,
                                           src_var->size, v->size, v->decimal_places, 0, v->usage_type));
            }
            dest = dest->next;
        }
    } else if (node->kind == AST_STATEMENT_ADD) {
        // is_numeric encodes form: false = TO (dest += sources), true = GIVING (dest = sum of sources)
        bool is_giving = node->is_numeric;
        int num_sources = node->decimal_places;

        // Split children into sources (first num_sources) and targets (the rest)
        AstNode *sources[64];
        int src_count = 0;
        AstNode *child = node->children;
        for (int si = 0; si < num_sources && child; si++, child = child->next)
            sources[src_count++] = child;

        AstNode *targets[64];
        int tgt_count = 0;
        while (child && tgt_count < 64) {
            targets[tgt_count++] = child;
            child = child->next;
        }

        for (int ti = 0; ti < tgt_count; ti++) {
            AstNode *tgt = targets[ti];
            if (tgt->kind != AST_IDENTIFIER) continue;
            VarInfo *dest_var = find_var(tgt->name);
            if (!dest_var) {
                cobalt_warn("Variable '%s' not found for ADD target", tgt->name);
                continue;
            }

            // GIVING form: zero out destination before adding
            if (is_giving) {
                append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest_var->name, NULL, NULL,
                                           0, dest_var->size, dest_var->decimal_places,
                                           '0', dest_var->usage_type));
            }

            // Add each source into the destination
            for (int si = 0; si < src_count; si++) {
                AstNode *src = sources[si];
                if (src->kind == AST_IDENTIFIER) {
                    VarInfo *src_var = find_var(src->name);
                    if (!src_var) {
                        cobalt_warn("Variable '%s' not found for ADD source", src->name);
                        continue;
                    }
                    append_instr(ir, new_instr(IR_CALL_ADD_TO,
                                               dest_var->name, src_var->name,
                                               NULL, src_var->size, dest_var->size,
                                               dest_var->decimal_places, 0, dest_var->usage_type));
                } else if (src->kind == AST_LITERAL_NUMBER) {
                    char lit_name_buf[32];
                    sprintf(lit_name_buf, "addlit_%d", lit_counter++);
                    const char *content = src->name ? src->name : "0";
                    int len = (int)strlen(content);
                    append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name_buf, content,
                                               len, 0, 0, 0, USAGE_DISPLAY));
                    append_instr(ir, new_instr(IR_CALL_ADD_TO,
                                               dest_var->name, lit_name_buf,
                                               NULL, len, dest_var->size,
                                               dest_var->decimal_places, 0, dest_var->usage_type));
                }
            }
        }
    } else if (node->kind == AST_STATEMENT_SUBTRACT ||
               node->kind == AST_STATEMENT_MULTIPLY ||
               node->kind == AST_STATEMENT_DIVIDE) {
        lower_arith_node(ir, node);
    } else if (node->kind == AST_STATEMENT_STOP) {
        append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    } else if (node->kind == AST_STATEMENT_ACCEPT) {
        AstNode *var_node = node->children; // Should be an AST_IDENTIFIER
        if (var_node && var_node->kind == AST_IDENTIFIER) {
            VarInfo *v = find_var(var_node->name);
            if (v) {
                char pad = v->is_numeric ? '0' : ' ';
                append_instr(ir, new_instr(IR_CALL_ACCEPT, v->name, NULL, NULL, 0, v->size, v->decimal_places, pad, v->usage_type));
            } else {
                cobalt_warn("Variable '%s' not found for ACCEPT", var_node->name);
            }
        } else {
            cobalt_warn("ACCEPT statement requires a variable", NULL);
        }
    }
}

// ── Arithmetic helpers ────────────────────────────────────────────────────────

// Emit `IR_DATA_LIT` for a literal operand and return the label name (caller
// must free). Returns NULL for identifier operands (use src->name directly).
static char *emit_arith_src_lit(IrProgram *ir, AstNode *src, const char *prefix) {
    if (src->kind != AST_LITERAL_NUMBER) return NULL;
    char *lname = malloc(48);
    sprintf(lname, "%s_%d", prefix, lit_counter++);
    const char *content = src->name ? src->name : "0";
    append_instr(ir, new_instr(IR_DATA_LIT, NULL, lname, content,
                               (int)strlen(content), 0, 0, 0, USAGE_DISPLAY));
    return lname; // caller frees
}

// Zero a dest variable, then copy src into it: dest = src.
static void emit_copy_to(IrProgram *ir, VarInfo *dest, AstNode *src, const char *lit_prefix) {
    append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->name, NULL, NULL,
                               0, dest->size, dest->decimal_places, '0', dest->usage_type));
    if (src->kind == AST_IDENTIFIER) {
        VarInfo *sv = find_var(src->name);
        if (!sv) { cobalt_warn("Variable '%s' not found", src->name); return; }
        append_instr(ir, new_instr(IR_CALL_ADD_TO, dest->name, sv->name,
                                   NULL, sv->size, dest->size,
                                   dest->decimal_places, 0, dest->usage_type));
    } else {
        char *lname = emit_arith_src_lit(ir, src, lit_prefix);
        if (lname) {
            append_instr(ir, new_instr(IR_CALL_ADD_TO, dest->name, lname,
                                       NULL, (int)strlen(src->name ? src->name : "0"),
                                       dest->size, dest->decimal_places, 0, dest->usage_type));
            free(lname);
        }
    }
}

// Emit an in-place arithmetic op (ADD_TO/SUB_FROM/MUL_BY/DIV_INTO/MOD_INTO)
// of dest op src. src can be a VarInfo name (src_var != NULL) or a literal
// label (lit_name != NULL, lit_len = length).
static void emit_inplace(IrProgram *ir, IrOpcode op, VarInfo *dest,
                         const char *src_name, int src_size) {
    append_instr(ir, new_instr(op, dest->name, src_name, NULL,
                               src_size, dest->size,
                               dest->decimal_places, 0, dest->usage_type));
}

// Apply `op` (SUB_FROM/MUL_BY/DIV_INTO/MOD_INTO) to dest using an AST source.
static void emit_op_from_src(IrProgram *ir, IrOpcode op, VarInfo *dest,
                              AstNode *src, const char *lit_prefix) {
    if (src->kind == AST_IDENTIFIER) {
        VarInfo *sv = find_var(src->name);
        if (!sv) { cobalt_warn("Variable '%s' not found", src->name); return; }
        emit_inplace(ir, op, dest, sv->name, sv->size);
    } else {
        char *lname = emit_arith_src_lit(ir, src, lit_prefix);
        if (lname) {
            emit_inplace(ir, op, dest, lname,
                         (int)strlen(src->name ? src->name : "0"));
            free(lname);
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

IrProgram *lower_to_ir(AstNode *program) {
    IrProgram *ir = calloc(1, sizeof(IrProgram));
    if (!ir) return NULL;

    num_vars = 0;
    lit_counter = 0;
    if_counter = 0;

    AstNode *div = program->children;
    while (div) {
        if (div->kind == AST_DIVISION && strcasecmp(div->name, "DATA") == 0) {
            AstNode *sec = div->children;
            while (sec) {
                if (sec->kind == AST_SECTION) {
                    AstNode *decl = sec->children;
                    while (decl) {
                        if (decl->kind == AST_VAR_DECL) {
                            // Retrieve semantic info directly from the AST node
                            // sema.c has already populated these fields
                            size_t size = decl->data_size;
                            int decimals = decl->decimal_places;
                            bool is_numeric = decl->is_numeric;
                            AstUsageType usage_type = decl->usage_type;

                            VarInfo *v = calloc(1, sizeof(VarInfo));
                            if (!v) {
                                decl = decl->next;
                                continue;
                            }
                            v->name = strdup(decl->name);
                            v->size = size;
                            v->decimal_places = decimals;
                            v->is_numeric = is_numeric;
                            v->usage_type = usage_type; // Store usage type
                            v->pad_char = 0;
                            v->init_content = NULL;
                            v->init_len = 0;

                            // Find VALUE clause
                            AstNode *val_node = decl->children;
                            while(val_node && val_node->kind != AST_STRING_LITERAL && val_node->kind != AST_LITERAL_NUMBER && val_node->kind != AST_IDENTIFIER && val_node->kind != AST_FIGURATIVE_CONSTANT) {
                                val_node = val_node->next;
                            }

                            if (val_node) {
                                const char *val_str = val_node->name ? val_node->name : "";
                                if (val_node->kind == AST_STRING_LITERAL) {
                                    if (is_numeric) {
                                        cobalt_error("String literal not allowed for numeric variable %s", v->name);
                                    } else {
                                        v->init_content = strdup(val_str);
                                        v->init_len = strlen(val_str);
                                        v->pad_char = ' ';
                                    }
                                } else if (val_node->kind == AST_LITERAL_NUMBER) {
                                    if (!is_numeric) {
                                        cobalt_error("Numeric literal not allowed for alphanumeric variable %s", v->name);
                                    } else {
                                        char *digits = strdup(val_str);
                                        char *dot = strchr(digits, '.');
                                        if (dot) {
                                            memmove(dot, dot + 1, strlen(dot));
                                        }
                                        v->init_content = digits;
                                        v->init_len = strlen(digits);
                                        v->pad_char = '0';
                                    }
                                } else if (val_node->kind == AST_FIGURATIVE_CONSTANT) { // Figurative constants
                                    if (strcasecmp(val_str, "ZERO") == 0 || strcasecmp(val_str, "ZEROS") == 0) {
                                        if (!is_numeric) cobalt_error("ZEROS not allowed for alphanumeric variable %s", v->name);
                                        v->pad_char = '0';
                                    } else if (strcasecmp(val_str, "SPACE") == 0 || strcasecmp(val_str, "SPACES") == 0) {
                                        if (is_numeric) cobalt_error("SPACES not allowed for numeric variable %s", v->name);
                                        v->pad_char = ' ';
                                    }
                                }
                            }

                            vars[num_vars++] = *v;
                            free(v);

                            append_instr(ir, new_instr(IR_DATA_VAR, vars[num_vars-1].name, NULL, NULL, 0, size, decimals, 0, usage_type));
                        }
                        decl = decl->next;
                    }
                }
                sec = sec->next;
            }
        }
        div = div->next;
    }

    // Emit initializations
    append_instr(ir, new_instr(IR_CALL_INIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY)); // Default usage for init call
    for (int i = 0; i < num_vars; i++) {
        VarInfo *v = &vars[i];
        if (v->init_content) {
            char lit_name[32];
            sprintf(lit_name, "initlit_%d", lit_counter++);
            int use_len = v->init_len;
            if (use_len > v->size) use_len = v->size;
            append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name, v->init_content, use_len, 0, 0, 0, USAGE_DISPLAY)); // Default usage for literal
            IrOpcode move_op = v->is_numeric ? IR_CALL_MOVE_NUM : IR_CALL_MOVE_ALPHA;
            append_instr(ir, new_instr(move_op, v->name, lit_name, NULL, use_len, v->size, v->decimal_places, 0, v->usage_type)); // Pass v->usage_type
            free(v->init_content);
            v->init_content = NULL;
        } else if (v->pad_char) {
            append_instr(ir, new_instr(IR_CALL_MOVE_FILL, v->name, NULL, NULL,
                                       0, v->size, v->decimal_places, v->pad_char, v->usage_type));
        }
    }

    // Lower PROCEDURE DIVISION
    div = program->children;
    while (div) {
        if (div->kind == AST_DIVISION && strcasecmp(div->name, "PROCEDURE") == 0) {
            AstNode *child = div->children;

            // Check whether the division contains any paragraph nodes
            bool has_paras = false;
            for (AstNode *n = child; n; n = n->next) {
                if (n->kind == AST_PARAGRAPH) { has_paras = true; break; }
            }

            if (!has_paras) {
                // No paragraphs: inline everything directly
                lower_block(ir, child);
            } else {
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
                        append_instr(ir, new_instr(IR_LABEL, n->name, NULL, NULL,
                                                   0, 0, 0, 0, USAGE_DISPLAY));
                        lower_block(ir, n->children);
                        append_instr(ir, new_instr(IR_RET, NULL, NULL, NULL,
                                                   0, 0, 0, 0, USAGE_DISPLAY));
                    }
                }
            }
        }
        div = div->next;
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
        free(vars[i].init_content);
    }
    free(ir);
}
