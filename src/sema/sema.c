/* src/sema/sema.c */
#include "sema.h"
#include "diag.h"
#include "ast.h"
#include <strings.h>
#include <stdbool.h>
#include <string.h> // For strchr, strncpy
#include <stdlib.h> // For atoi
#include <ctype.h>  // For toupper, isdigit

// Result of analyzing a PICTURE character-string
typedef struct {
    bool numeric;       // only 9, S, V
    bool alphanumeric;  // only X, A, 9 with at least one X or A
    bool is_signed;     // S present
    int digits;         // number of 9 positions (numeric)
    int scale;          // 9 positions after V
    int length;         // character positions (alphanumeric)
    char unsupported;   // first PICTURE symbol not supported yet, or 0
} PicInfo;

static PicInfo analyze_pic(const char *pic) {
    PicInfo info = {0};
    bool seen_v = false, seen_x = false;
    const char *p = pic;

    while (*p) {
        char c = toupper((unsigned char)*p++);
        int repeat = 1;
        if (*p == '(') {
            p++;
            repeat = 0;
            while (isdigit((unsigned char)*p)) repeat = repeat * 10 + (*p++ - '0');
            if (*p == ')') p++;
        }
        switch (c) {
            case '9':
                info.digits += repeat;
                info.length += repeat;
                if (seen_v) info.scale += repeat;
                break;
            case 'X':
            case 'A':
                seen_x = true;
                info.length += repeat;
                break;
            case 'S': info.is_signed = true; break;
            case 'V': seen_v = true; break;
            default:
                if (!info.unsupported) info.unsupported = c;
                break;
        }
    }
    info.alphanumeric = seen_x;
    info.numeric = !seen_x && info.digits > 0;
    return info;
}

// Storage size of a numeric item by usage (runtime/ABI.md section 5)
static size_t numeric_size(AstUsageType usage, int digits) {
    switch (usage) {
        case USAGE_COMP:
        case USAGE_COMP5:
            if (digits <= 4) return 2;
            if (digits <= 9) return 4;
            return 8;
        case USAGE_COMP3:
            return digits / 2 + 1;
        default:
            return digits;
    }
}

// Count the digits of a numeric literal before and after its decimal point
static void literal_digits(const char *lit, int *int_digits, int *frac_digits) {
    *int_digits = *frac_digits = 0;
    bool frac = false;
    for (const char *p = lit; *p; p++) {
        if (*p == '.') frac = true;
        else if (isdigit((unsigned char)*p)) {
            if (frac) (*frac_digits)++;
            else (*int_digits)++;
        }
    }
}

static void sema_analyze_var_decl(AstNode *var_decl, SemaContext *ctx) {
    if (var_decl->text && atoi(var_decl->text) == 88) return; // condition-name: no storage

    var_decl->usage_type = USAGE_DISPLAY;
    var_decl->is_numeric = false;
    var_decl->is_signed = false;
    var_decl->decimal_places = 0;
    var_decl->digits = 0;
    var_decl->data_size = 0;

    AstNode *pic_node = NULL;
    AstNode *usage_clause_node = NULL;
    AstNode *value_literal_node = NULL; // STRING, NUMBER or FIGURATIVE_CONSTANT

    for (AstNode *c = var_decl->children; c; c = c->next) {
        if (c->kind == AST_PIC_CLAUSE) pic_node = c;
        else if (c->kind == AST_USAGE_CLAUSE) usage_clause_node = c;
        else if (c->kind == AST_STRING_LITERAL || c->kind == AST_LITERAL_NUMBER ||
                 c->kind == AST_FIGURATIVE_CONSTANT) value_literal_node = c;
    }

    if (!pic_node || !pic_node->name) {
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "Variable '%s' missing PIC clause", var_decl->name);
        return;
    }
    if (usage_clause_node) var_decl->usage_type = usage_clause_node->usage_type;

    const char *pic_str = pic_node->name;
    PicInfo pic = analyze_pic(pic_str);

    if (pic.unsupported) {
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "PICTURE symbol '%c' in '%s' of '%s' is not supported yet",
                         pic.unsupported, pic_str, var_decl->name);
        return;
    }

    if (pic.alphanumeric) {
        if (pic.is_signed || pic.scale) {
            cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                             "PICTURE '%s' of '%s' mixes S or V with X or A", pic_str, var_decl->name);
        }
        var_decl->data_size = pic.length;
        if (var_decl->usage_type != USAGE_DISPLAY) {
            cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                             "USAGE COMP cannot be used with alphanumeric PIC '%s'", pic_str);
        }
    } else if (pic.numeric) {
        if (pic.digits > 18) {
            cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                             "PICTURE '%s' of '%s' has %d digits; the maximum is 18",
                             pic_str, var_decl->name, pic.digits);
        }
        var_decl->is_numeric = true;
        var_decl->is_signed = pic.is_signed;
        var_decl->decimal_places = pic.scale;
        var_decl->digits = pic.digits;
        var_decl->data_size = numeric_size(var_decl->usage_type, pic.digits);
    } else {
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "Variable '%s' has unsupported PIC string '%s'", var_decl->name, pic_str);
        return;
    }

    // Semantic validation for VALUE clause
    if (value_literal_node) {
        if (value_literal_node->kind == AST_FIGURATIVE_CONSTANT) {
            if (strcasecmp(value_literal_node->name, "SPACES") == 0 || strcasecmp(value_literal_node->name, "SPACE") == 0) {
                if (var_decl->is_numeric) {
                    cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                                     "SPACES cannot be used with numeric variable '%s'", var_decl->name);
                }
            }
        } else if (value_literal_node->kind == AST_STRING_LITERAL) {
            if (var_decl->is_numeric) {
                cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                                 "String literal '%s' cannot be used with numeric variable '%s'", value_literal_node->name, var_decl->name);
            }
        } else if (value_literal_node->kind == AST_LITERAL_NUMBER) {
            if (!var_decl->is_numeric) {
                cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                                 "Numeric literal '%s' cannot be used with alphanumeric variable '%s'", value_literal_node->name, var_decl->name);
            } else {
                int int_digits, frac_digits;
                literal_digits(value_literal_node->name, &int_digits, &frac_digits);
                // Leading zeros do not count against the integer positions
                const char *p = value_literal_node->name;
                while (*p == '+' || *p == '-') p++;
                while (*p == '0' && int_digits > 0) { p++; int_digits--; }
                if (int_digits > pic.digits - pic.scale || frac_digits > pic.scale) {
                    cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                                     "VALUE %s does not fit PICTURE '%s' of '%s'",
                                     value_literal_node->name, pic_str, var_decl->name);
                }
                if (value_literal_node->name[0] == '-' && !pic.is_signed) {
                    cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                                     "Negative VALUE %s for unsigned '%s'", value_literal_node->name, var_decl->name);
                }
            }
        }
    }
}

static void sema_walk_ast(AstNode *node, SemaContext *ctx) {
    if (!node) return;

    switch (node->kind) {
        case AST_VAR_DECL:
            sema_analyze_var_decl(node, ctx);
            break;
        case AST_STATEMENT_ACCEPT:
            if (!node->children || node->children->kind != AST_IDENTIFIER) {
                cobalt_error_loc(ctx->original_source, node->loc.line, node->loc.column,
                                 "ACCEPT statement requires an identifier (variable name)");
            } else {
                // Future: Check if the identifier is a declared variable.
                // For now, just ensure it's an identifier.
            }
            break;
        // Add other cases for statements, etc., as semantic analysis grows
        default:
            break;
    }

    sema_walk_ast(node->children, ctx); // Recurse into children
    sema_walk_ast(node->next, ctx);     // Recurse into siblings
}

static bool has_stop_run(AstNode *node) {
    if (!node) return false;

    if (node->kind == AST_STATEMENT_STOP) {
        return true;
    }

    // Depth-first: check children
    if (has_stop_run(node->children)) {
        return true;
    }

    // Then siblings
    if (has_stop_run(node->next)) {
        return true;
    }

    return false;
}

static AstNode *find_procedure_division(AstNode *program) {
    AstNode *div = program->children;
    while (div) {
        if (div->kind == AST_DIVISION &&
            strcasecmp(div->name, "PROCEDURE") == 0) {
            return div;
        }
        div = div->next;
    }
    return NULL;
}

void sema_basic_check(AstNode *program, SemaContext *ctx) {
    // Traverse the AST to perform variable semantic analysis first
    sema_walk_ast(program, ctx);

    AstNode *proc_div = find_procedure_division(program);
    if (!proc_div) {
        // Parser already reports missing PROCEDURE DIVISION
        return;
    }

    if (!has_stop_run(proc_div)) {
        cobalt_error("No STOP RUN found in PROCEDURE DIVISION");
    }

    // Future checks can be added here:
    // - Undeclared identifiers
    // - PERFORM targets exist
    // - Type compatibility in arithmetic
    // - etc.
}