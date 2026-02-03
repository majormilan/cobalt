/* src/sema/sema.c */
#include "sema.h"
#include "diag.h"
#include "ast.h"
#include <strings.h>
#include <stdbool.h>
#include <string.h> // For strchr, strncpy
#include <stdlib.h> // For atoi
#include <ctype.h>  // For toupper, isdigit

// Helper to calculate the effective length/size of a PIC clause
static int calculate_pic_length(const char *pic_str) {
    int length = 0;
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

        // Only '9', 'X', 'A' contribute to length directly
        if (c == '9' || c == 'X' || c == 'A') {
            length += repeat;
        }
        // 'S', 'V', 'P' are not length-contributing characters
    }
    return length > 0 ? length : 1; // Minimum length 1
}

// Helper to determine the storage size for COMP (binary) based on number of digits
// This is a simplified version; real COBOL compilers have more complex rules.
static size_t get_comp_size(int num_digits) {
    if (num_digits >= 1 && num_digits <= 4) return 2; // S9(4) COMP -> 2 bytes (short)
    if (num_digits >= 5 && num_digits <= 9) return 4; // S9(9) COMP -> 4 bytes (int)
    if (num_digits >= 10 && num_digits <= 18) return 8; // S9(18) COMP -> 8 bytes (long long)
    // Default to 4 bytes for now, or error
    return 4;
}

static void sema_analyze_var_decl(AstNode *var_decl, SemaContext *ctx) {
    // Default values (AstNode constructor already sets most, but re-init for clarity)
    var_decl->usage_type = USAGE_DISPLAY;
    var_decl->is_numeric = false;
    var_decl->is_signed = false;
    var_decl->decimal_places = 0;
    var_decl->data_size = 0; // Will be calculated

    // Process PIC clause, USAGE clause, and VALUE clause
    AstNode *pic_node = NULL;
    AstNode *usage_clause_node = NULL;
    AstNode *value_literal_node = NULL; // Can be STRING, NUMBER, or FIGURATIVE_CONSTANT

    AstNode *current_child = var_decl->children;
    while (current_child) {
        if (current_child->kind == AST_PIC_CLAUSE) {
            pic_node = current_child;
        } else if (current_child->kind == AST_USAGE_CLAUSE) {
            usage_clause_node = current_child;
        } else if (current_child->kind == AST_STRING_LITERAL ||
                   current_child->kind == AST_LITERAL_NUMBER ||
                   current_child->kind == AST_FIGURATIVE_CONSTANT) {
            value_literal_node = current_child;
        }
        current_child = current_child->next;
    }



    if (!pic_node || !pic_node->name) {
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "Variable '%s' missing PIC clause", var_decl->name);
        return;
    }

    const char *pic_str = pic_node->name;

    // Determine basic PIC properties (is_signed, decimal_places)
    if (strchr(pic_str, 'S')) {
        var_decl->is_signed = true;
    }
    if (strchr(pic_str, 'V')) {
        const char *v_pos = strchr(pic_str, 'V');
        // Count 9s after V to get decimal places
        int dec_count = 0;
        for (const char *p = v_pos + 1; *p; ++p) {
            if (*p == '9') dec_count++;
        }
        var_decl->decimal_places = dec_count;
    }

    // Process USAGE clause to get the type
    if (usage_clause_node) {
        if (usage_clause_node->children && usage_clause_node->children->kind == AST_USAGE_TYPE_COMP) {
            var_decl->usage_type = USAGE_COMP;
        } else { // Default to DISPLAY if not COMP
            var_decl->usage_type = USAGE_DISPLAY;
        }
    }
    // If no USAGE clause explicitly found, it defaults to USAGE_DISPLAY (already set)


    // Now determine size and numeric property based on PIC and USAGE
    if (strchr(pic_str, 'X')) {
        var_decl->is_numeric = false;
        var_decl->data_size = calculate_pic_length(pic_str);
    }
    else if (strchr(pic_str, '9')) {
        var_decl->is_numeric = true;
        int num_digits = calculate_pic_length(pic_str); // Use the helper for repetitions

        // For signed numbers, 'S' is not a digit but implies one more possible value range
        // For PICTURE clause 'S99' represents two digits, 'S' does not represent a storage position.
        // It's already handled by 'is_signed' flag, not included in num_digits count here.

        if (var_decl->usage_type == USAGE_COMP) {
            var_decl->data_size = get_comp_size(num_digits);
        } else { // USAGE_DISPLAY or implied DISPLAY
            var_decl->data_size = num_digits; // Each digit is 1 byte for DISPLAY
            // For signed display, COBOL often adds an implicit byte or special character, but for simplicity here,
            // we'll keep it as num_digits for the actual numeric characters.
        }
    } else {
        // Unknown PIC characters
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "Variable '%s' has unsupported PIC string '%s'", var_decl->name, pic_str);
        return;
    }

    // Semantic validation: USAGE COMP with PIC X
    if (var_decl->usage_type == USAGE_COMP && !var_decl->is_numeric) {
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "USAGE COMP cannot be used with alphanumeric PIC '%s'", pic_str);
    }

    // Semantic validation for VALUE clause
    if (value_literal_node) {
        if (value_literal_node->kind == AST_FIGURATIVE_CONSTANT) {
            if (strcasecmp(value_literal_node->name, "ZEROS") == 0 || strcasecmp(value_literal_node->name, "ZERO") == 0) {
                if (!var_decl->is_numeric) {
                    cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                                     "ZEROS cannot be used with alphanumeric variable '%s'", var_decl->name);
                }
            } else if (strcasecmp(value_literal_node->name, "SPACES") == 0 || strcasecmp(value_literal_node->name, "SPACE") == 0) {
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