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

static VarInfo vars[128];
static int num_vars = 0;
static int lit_counter = 0;

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

static VarInfo *find_var(const char *name) {
    for (int i = 0; i < num_vars; i++) {
        if (strcasecmp(vars[i].name, name) == 0) return &vars[i];
    }
    return NULL;
}

static void lower_block(IrProgram *ir, AstNode *block);

static void lower_node(IrProgram *ir, AstNode *node) {
    if (!node) return;

    if (node->kind == AST_PARAGRAPH) {
        lower_block(ir, node->children);
    } else if (node->kind == AST_STATEMENT_IF) {
        // Stub: execute THEN block
        if (node->children && node->children->next) {
            lower_block(ir, node->children->next);
        }
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
    } else if (node->kind == AST_STATEMENT_MOVE) {
        AstNode *src = node->children;
        AstNode *dest = src ? src->next : NULL;
        if (src && dest && dest->kind == AST_IDENTIFIER) {
            VarInfo *v = find_var(dest->name);
            if (v && (src->kind == AST_STRING_LITERAL || src->kind == AST_LITERAL_NUMBER)) {
                const char *content = src->name;
                int len = strlen(content);
                if (len > v->size) len = v->size;
                char lit_name[32];
                sprintf(lit_name, "lit_%d", lit_counter++);
                append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name, content, len, 0, 0, 0, USAGE_DISPLAY));
                IrOpcode move_op = v->is_numeric ? IR_CALL_MOVE_NUM : IR_CALL_MOVE_ALPHA;
                append_instr(ir, new_instr(move_op, dest->name, lit_name, NULL, len, v->size, v->decimal_places, 0, v->usage_type));
            } else if (v && src->kind == AST_FIGURATIVE_CONSTANT) {
                char fill_char = 0;
                if (strcasecmp(src->name, "ZEROS") == 0 || strcasecmp(src->name, "ZERO") == 0) {
                    fill_char = '0';
                } else if (strcasecmp(src->name, "SPACES") == 0 || strcasecmp(src->name, "SPACE") == 0) {
                    fill_char = ' ';
                }
                append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->name, NULL, NULL, 0, v->size, v->decimal_places, fill_char, v->usage_type));
            }
        }
    } else if (node->kind == AST_STATEMENT_STOP) {
        append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
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
                            while(val_node && val_node->kind != AST_STRING_LITERAL && val_node->kind != AST_LITERAL_NUMBER && val_node->kind != AST_IDENTIFIER) {
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
            // No IR instruction to emit for just padding, backend will handle by default
        }
    }

    // Lower PROCEDURE DIVISION
    div = program->children;
    while (div) {
        if (div->kind == AST_DIVISION && strcasecmp(div->name, "PROCEDURE") == 0) {
            lower_block(ir, div->children);
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
        free(i);
        i = next;
    }
    for (int i = 0; i < num_vars; i++) {
        free(vars[i].name);
        free(vars[i].init_content);
    }
    free(ir);
}
