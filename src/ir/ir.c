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
    char pad_char;          // ' ' or '0' or 0 if no pad
    char *init_content;     // raw content (string or digits without '.')
    int init_len;
} VarInfo;

static VarInfo vars[128];
static int num_vars = 0;
static int lit_counter = 0;

static IrInstr *new_instr(IrOpcode op, const char *var_name, const char *lit_name,
                          const char *content, int lit_len, int size, int decimal_places, char pad_char) {
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

static void calc_pic(const char *pic_str, int *size_out, int *decimals_out, bool *is_numeric_out) {
    printf("DEBUG: calc_pic called with PIC: '%s'\n", pic_str ? pic_str : "(null)");

    int size = 0;
    int decimals = 0;
    bool after_v = false;
    bool is_numeric = true;
    const char *p = pic_str;

    while (*p) {
        char c = toupper(*p);
        p++;

        int repeat = 1;
        if (*p == '(') {
            p++;  // skip '('
            repeat = 0;
            while (isdigit(*p)) {
                repeat = repeat * 10 + (*p - '0');
                p++;
            }
            if (*p == ')') p++;  // skip ')'
        }

        printf("DEBUG:   char '%c', repeat=%d, after_v=%d\n", c, repeat, after_v);

        if (c == 'V') {
            after_v = true;
            continue;
        }

        if (c == 'X' || c == 'A') {
            is_numeric = false;
        }

        if (c == '9' || c == 'X' || c == 'A' || c == 'S' || c == 'P') {
            size += repeat;
            if (after_v) decimals += repeat;
        }
    }

    *size_out = size ? size : 1;
    *decimals_out = decimals;
    *is_numeric_out = is_numeric;

    printf("DEBUG: calc_pic result for '%s': size=%d, decimals=%d, is_numeric=%d\n",
           pic_str ? pic_str : "(null)", *size_out, *decimals_out, *is_numeric_out);
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
            if ((arg->kind == AST_STRING_LITERAL || arg->kind == AST_LITERAL_NUMBER) && len > 0) {
                char lit_name[32];
                sprintf(lit_name, "lit_%d", lit_counter++);
                printf("DEBUG: Creating literal for DISPLAY: '%s' (len=%d) as %s\n", content, len, lit_name);
                append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name, content, len, 0, 0, 0));
                append_instr(ir, new_instr(IR_CALL_DISPLAY_STR, NULL, lit_name, NULL, 0, 0, 0, 0));
            } else if (arg->kind == AST_IDENTIFIER) {
                VarInfo *v = find_var(arg->name);
                if (v) {
                    printf("DEBUG: DISPLAY variable '%s' (size=%d, decimals=%d)\n", arg->name, v->size, v->decimal_places);
                    append_instr(ir, new_instr(IR_CALL_DISPLAY_NUM, arg->name, NULL, NULL, 0, v->size, v->decimal_places, 0));
                } else {
                    printf("DEBUG: WARNING: Variable '%s' not found for DISPLAY\n", arg->name);
                }
            }
            arg = arg->next;
        }
        append_instr(ir, new_instr(IR_EMIT_NEWLINE, NULL, NULL, NULL, 0, 0, 0, 0));
    } else if (node->kind == AST_STATEMENT_MOVE) {
        AstNode *src = node->children;
        AstNode *dest = src ? src->next : NULL;
        if (src && dest && dest->kind == AST_IDENTIFIER) {
            VarInfo *v = find_var(dest->name);
            if (v && src->kind == AST_STRING_LITERAL) {
                const char *content = src->name;
                int len = strlen(content);
                if (len > v->size) len = v->size;
                char lit_name[32];
                sprintf(lit_name, "lit_%d", lit_counter++);
                printf("DEBUG: MOVE literal '%s' (len=%d) to '%s' (size=%d)\n", content, len, dest->name, v->size);
                append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name, content, len, 0, 0, 0));
                IrOpcode move_op = v->is_numeric ? IR_CALL_MOVE_NUM : IR_CALL_MOVE_ALPHA;
                append_instr(ir, new_instr(move_op, dest->name, lit_name, NULL, len, v->size, v->decimal_places, 0));
            }
        }
    } else if (node->kind == AST_STATEMENT_STOP) {
        append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0));
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

    printf("DEBUG: Starting lower_to_ir\n");

    AstNode *div = program->children;
    while (div) {
        if (div->kind == AST_DIVISION && strcasecmp(div->name, "DATA") == 0) {
            AstNode *sec = div->children;
            while (sec) {
                if (sec->kind == AST_SECTION) {
                    AstNode *decl = sec->children;
                    while (decl) {
                        if (decl->kind == AST_VAR_DECL) {
                            const char *pic_str = "";
                            AstNode *pic_node = decl->children;
                            if (pic_node && pic_node->kind == AST_PIC_CLAUSE) {
                                pic_str = pic_node->name ? pic_node->name : "";
                            } else {
                                printf("DEBUG: WARNING: Missing or invalid PIC clause for variable '%s'\n", decl->name);
                            }

                            int size, decimals;
                            bool is_numeric;
                            calc_pic(pic_str, &size, &decimals, &is_numeric);

                            VarInfo *v = calloc(1, sizeof(VarInfo));
                            if (!v) {
                                decl = decl->next;
                                continue;
                            }
                            v->name = strdup(decl->name);
                            v->size = size;
                            v->decimal_places = decimals;
                            v->is_numeric = is_numeric;
                            v->pad_char = 0;
                            v->init_content = NULL;
                            v->init_len = 0;

                            printf("DEBUG: Created variable '%s': PIC='%s', size=%d, decimals=%d, is_numeric=%d\n",
                                   v->name, pic_str, v->size, v->decimal_places, v->is_numeric);

                            AstNode *val_node = pic_node ? pic_node->next : NULL;
                            if (val_node) {
                                const char *val_str = val_node->name ? val_node->name : "";
                                if (val_node->kind == AST_STRING_LITERAL) {
                                    if (is_numeric) {
                                        cobalt_error("String literal not allowed for numeric variable %s", v->name);
                                    } else {
                                        v->init_content = strdup(val_str);
                                        v->init_len = strlen(val_str);
                                        v->pad_char = ' ';
                                        printf("DEBUG:   VALUE string '%s' (len=%d), pad=' '\n", val_str, v->init_len);
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
                                        printf("DEBUG:   VALUE number '%s' → digits '%s' (len=%d), pad='0'\n",
                                               val_str, digits, v->init_len);
                                    }
                                } else if (val_node->kind == AST_IDENTIFIER) {
                                    if (strcasecmp(val_str, "ZERO") == 0 || strcasecmp(val_str, "ZEROS") == 0) {
                                        if (!is_numeric) cobalt_error("ZERO not allowed for alphanumeric %s", v->name);
                                        v->pad_char = '0';
                                        printf("DEBUG:   VALUE ZERO → pad='0'\n");
                                    } else if (strcasecmp(val_str, "SPACE") == 0 || strcasecmp(val_str, "SPACES") == 0) {
                                        if (is_numeric) cobalt_error("SPACE not allowed for numeric %s", v->name);
                                        v->pad_char = ' ';
                                        printf("DEBUG:   VALUE SPACE → pad=' '\n");
                                    }
                                }
                            }

                            vars[num_vars++] = *v;
                            free(v);

                            append_instr(ir, new_instr(IR_DATA_VAR, vars[num_vars-1].name, NULL, NULL, 0, size, decimals, 0));
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
    append_instr(ir, new_instr(IR_CALL_INIT, NULL, NULL, NULL, 0, 0, 0, 0));
    for (int i = 0; i < num_vars; i++) {
        VarInfo *v = &vars[i];
        if (v->init_content) {
            char lit_name[32];
            sprintf(lit_name, "initlit_%d", lit_counter++);
            int use_len = v->init_len;
            if (use_len > v->size) use_len = v->size;
            printf("DEBUG: Emitting init literal for '%s': '%s' (use_len=%d) → %s\n",
                   v->name, v->init_content, use_len, lit_name);
            append_instr(ir, new_instr(IR_DATA_LIT, NULL, lit_name, v->init_content, use_len, 0, 0, 0));
            IrOpcode move_op = v->is_numeric ? IR_CALL_MOVE_NUM : IR_CALL_MOVE_ALPHA;
            append_instr(ir, new_instr(move_op, v->name, lit_name, NULL, use_len, v->size, v->decimal_places, 0));
            free(v->init_content);
            v->init_content = NULL;
        } else if (v->pad_char) {
            printf("DEBUG: Emitting pad-only init for '%s': pad_char='%c', size=%d\n",
                   v->name, v->pad_char, v->size);
        }
    }

    printf("DEBUG: lower_to_ir complete: %d variables processed\n", num_vars);

    // Lower PROCEDURE DIVISION
    div = program->children;
    while (div) {
        if (div->kind == AST_DIVISION && strcasecmp(div->name, "PROCEDURE") == 0) {
            lower_block(ir, div->children);
        }
        div = div->next;
    }

    append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0));

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
