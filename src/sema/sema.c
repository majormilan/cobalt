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
typedef enum { PIC_NUMERIC, PIC_NUMERIC_EDITED, PIC_ALNUM, PIC_ALNUM_EDITED, PIC_INVALID } PicClass;

typedef struct {
    PicClass cls;
    bool is_signed;     // S, or + - CR DB in an edited picture
    int digits;         // numeric: 9 positions; edited: all digit positions
    int scale;          // digits after the (implied) decimal point; P scaling
                        // makes it larger than digits (PP99) or negative (99PP)
    int length;         // character positions (bytes) of the item
    char mask[1024];    // edited pictures: one symbol per character position
    char float_char;    // floating $, + or - in a numeric edited picture
    char bad;           // first symbol that is not valid here, or 0
} PicInfo;

// Edit masks (runtime/ABI.md section 8) use one symbol per position:
//   9 digit, Z digit or space, * digit or asterisk, f floating digit,
//   F floating symbol position (leftmost of a floating string),
//   B 0 / , . insertion, S fixed + sign, M fixed - sign, $ fixed currency,
//   c r = CR, d b = DB; alphanumeric edited: X (any character), B 0 /.
static PicInfo analyze_pic(const char *pic) {
    PicInfo info = {0};
    // Symbols with their repeat factors; CR and DB become c r / d b
    char sym[128];
    int rep[128];
    int n = 0;
    for (const char *p = pic; *p && n < 126; ) {
        char c = toupper((unsigned char)*p++);
        if ((c == 'C' && toupper((unsigned char)*p) == 'R') || (c == 'D' && toupper((unsigned char)*p) == 'B')) {
            sym[n] = c == 'C' ? 'c' : 'd'; rep[n++] = 1;
            sym[n] = c == 'C' ? 'r' : 'b'; rep[n++] = 1;
            p++;
            continue;
        }
        int repeat = 1;
        if (*p == '(') {
            p++;
            repeat = 0;
            while (isdigit((unsigned char)*p)) repeat = repeat * 10 + (*p++ - '0');
            if (*p == ')') p++;
        }
        sym[n] = c;
        rep[n++] = repeat;
    }

    bool has_x = false, has_edit = false;
    int count_dollar = 0, count_plus = 0, count_minus = 0;
    for (int i = 0; i < n; i++) {
        switch (sym[i]) {
            case 'X': case 'A': has_x = true; break;
            case '9': case 'S': case 'V': case 'P': break;
            case 'B': case '0': case '/': case ',': case '.': case 'Z': case '*':
            case 'c': case 'r': case 'd': case 'b':
                has_edit = true; break;
            case '$': count_dollar += rep[i]; has_edit = true; break;
            case '+': count_plus += rep[i]; has_edit = true; break;
            case '-': count_minus += rep[i]; has_edit = true; break;
            default: if (!info.bad) info.bad = sym[i]; break;
        }
    }

    if (has_x) {
        // Alphanumeric (X, A, 9) or alphanumeric edited (with B 0 /)
        bool edited = false;
        for (int i = 0; i < n; i++) {
            char c = sym[i];
            if (c == 'B' || c == '0' || c == '/') edited = true;
            else if (c != 'X' && c != 'A' && c != '9' && !info.bad) info.bad = c;
            info.length += rep[i];
        }
        info.cls = edited ? PIC_ALNUM_EDITED : PIC_ALNUM;
        if (edited) {
            if (info.length >= (int)sizeof(info.mask)) {
                info.bad = '(';                 // edit mask too long
                return info;
            }
            int m = 0;
            for (int i = 0; i < n; i++)
                for (int k = 0; k < rep[i]; k++)
                    info.mask[m++] = (sym[i] == 'B' || sym[i] == '0' || sym[i] == '/') ? sym[i] : 'X';
            info.mask[m] = '\0';
        }
        return info;
    }

    if (!has_edit) {
        // Numeric: 9, S, V, P
        int nines = 0, after_v = 0, p_count = 0;
        bool seen_v = false, p_left = false;
        for (int i = 0; i < n; i++) {
            switch (sym[i]) {
                case '9': nines += rep[i]; if (seen_v) after_v += rep[i]; break;
                case 'S': info.is_signed = true; break;
                case 'V': seen_v = true; break;
                case 'P': p_count += rep[i]; if (nines == 0) p_left = true; break;
            }
        }
        info.cls = nines ? PIC_NUMERIC : PIC_INVALID;
        info.digits = nines;
        info.length = nines;
        if (p_count && p_left) info.scale = p_count + nines;     // PP99: all fractional
        else if (p_count) info.scale = -p_count;                 // 99PP: scaled up
        else info.scale = after_v;
        return info;
    }

    // Numeric edited. A symbol + - $ that appears more than once is floating.
    char floating = count_dollar > 1 ? '$' : count_plus > 1 ? '+' : count_minus > 1 ? '-' : 0;
    info.float_char = floating;
    bool seen_point = false, seen_float = false;
    int m = 0, p_left = 0, p_right = 0;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < rep[i]; k++) {
            char c = sym[i], out = 0;
            bool digit = false;
            switch (c) {
                case '9': case 'Z': case '*': out = c; digit = true; break;
                case 'B': case '0': case '/': case ',': out = c; break;
                case 'c': case 'r': case 'd': case 'b': out = c; info.is_signed = true; break;
                case '.': out = '.'; seen_point = true; break;
                case 'V': seen_point = true; break;             // implied: no position
                case 'P':                                       // scaling: no position
                    if (info.digits == 0) p_left++;
                    else p_right++;
                    break;
                case '$': case '+': case '-':
                    if (c == floating) {
                        out = seen_float ? 'f' : 'F';
                        digit = seen_float;
                        seen_float = true;
                    } else {
                        out = c == '$' ? '$' : c == '+' ? 'S' : 'M';
                    }
                    if (c != '$') info.is_signed = true;
                    break;
                default: break;
            }
            if (out) {
                if (m >= (int)sizeof(info.mask) - 1) { info.bad = '('; return info; }
                info.mask[m++] = out;
            }
            if (digit) {
                info.digits++;
                if (seen_point) info.scale++;
            }
        }
    }
    info.mask[m] = '\0';
    info.length = m;
    info.cls = PIC_NUMERIC_EDITED;
    if (p_right) info.scale -= p_right;     // ZZZPP: digits worth hundreds
    if (p_left) info.scale += p_left;       // PPZZ: digits after implied zeros
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

    if (!pic_node && !var_decl->is_group && usage_clause_node && usage_clause_node->name &&
        strcasecmp(usage_clause_node->name, "INDEX") == 0) {
        // USAGE INDEX: an 8-byte binary occurrence number
        var_decl->usage_type = USAGE_COMP5;
        var_decl->is_numeric = true;
        var_decl->is_signed = true;
        var_decl->digits = 18;
        var_decl->data_size = 8;
        return;
    }
    if (!pic_node || !pic_node->name) {
        // A group item: its size comes from its subordinate items (IR
        // lowering); its USAGE has been passed down to them
        var_decl->usage_type = USAGE_DISPLAY;
        return;
    }
    if (usage_clause_node) var_decl->usage_type = usage_clause_node->usage_type;

    const char *pic_str = pic_node->name;
    PicInfo pic = analyze_pic(pic_str);

    if (pic.bad || pic.cls == PIC_INVALID) {
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "PICTURE '%s' of '%s' is not valid (symbol '%c')",
                         pic_str, var_decl->name, pic.bad ? pic.bad : '?');
        return;
    }

    if (pic.cls == PIC_ALNUM || pic.cls == PIC_ALNUM_EDITED) {
        var_decl->data_size = pic.length;
        if (pic.cls == PIC_ALNUM_EDITED) var_decl->mask = strdup(pic.mask);
        if (var_decl->usage_type != USAGE_DISPLAY) {
            cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                             "USAGE COMP cannot be used with alphanumeric PIC '%s'", pic_str);
        }
    } else if (pic.cls == PIC_NUMERIC_EDITED) {
        // Stored as characters: an alphanumeric item that numeric values are
        // edited into
        if (pic.digits > 18) {
            cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                             "PICTURE '%s' of '%s' has %d digit positions; the maximum is 18",
                             pic_str, var_decl->name, pic.digits);
        }
        var_decl->data_size = pic.length;
        var_decl->mask = strdup(pic.mask);
        var_decl->float_char = pic.float_char;
        var_decl->digits = pic.digits;
        var_decl->decimal_places = pic.scale;
        var_decl->is_signed = pic.is_signed;
    } else {
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
        if (var_decl->blank_when_zero && var_decl->usage_type == USAGE_DISPLAY) {
            // BLANK WHEN ZERO on a numeric item makes it numeric edited
            char mask[64];
            int k = 0;
            for (int i = 0; i < pic.digits && k < 60; i++) {
                if (i == pic.digits - pic.scale && pic.scale > 0) mask[k++] = 'V';
                mask[k++] = '9';
            }
            mask[k] = '\0';
            // V has no position in a mask: keep only the 9s
            char m2[64];
            int j = 0;
            for (int i = 0; mask[i]; i++) if (mask[i] == '9') m2[j++] = '9';
            m2[j] = '\0';
            var_decl->mask = strdup(m2);
            var_decl->is_numeric = false;
            var_decl->is_signed = false;
        }
    }
    if (var_decl->blank_when_zero && !var_decl->mask) {
        cobalt_error_loc(ctx->original_source, var_decl->loc.line, var_decl->loc.column,
                         "BLANK WHEN ZERO requires a numeric DISPLAY or numeric edited item");
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
                int int_capacity = pic.digits - pic.scale;  // P scaling: 99PP holds 4 digits
                if (pic.scale < 0) int_capacity = pic.digits - pic.scale;
                if (int_capacity < 0) int_capacity = 0;
                if (int_digits > int_capacity || frac_digits > (pic.scale > 0 ? pic.scale : 0)) {
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

static AstNode *usage_clause_of(AstNode *decl) {
    for (AstNode *c = decl->children; c; c = c->next)
        if (c->kind == AST_USAGE_CLAUSE) return c;
    return NULL;
}

// A group's USAGE applies to every elementary item below it without one.
// Entries arrive as a flat list in source order; levels give the hierarchy.
static void inherit_group_usage(AstNode *first) {
    AstNode *stack[64];
    int levels[64], depth = 0;
    for (AstNode *d = first; d; d = d->next) {
        if (d->kind != AST_VAR_DECL || !d->text) continue;
        int level = atoi(d->text);
        if (level == 88 || level == 66) continue;
        if (level == 1 || level == 77) depth = 0;
        while (depth > 0 && levels[depth - 1] >= level) depth--;
        // A group: the next data entry has a higher level number
        AstNode *n = d->next;
        while (n && (n->kind != AST_VAR_DECL || !n->text || atoi(n->text) == 88)) n = n->next;
        d->is_group = n && level != 77 && atoi(n->text) > level && atoi(n->text) != 66 &&
                      atoi(n->text) != 77 && atoi(n->text) != 1;
        if (!usage_clause_of(d)) {
            for (int k = depth - 1; k >= 0; k--) {
                AstNode *u = usage_clause_of(stack[k]);
                if (u) {
                    AstNode *copy = ast_new(AST_USAGE_CLAUSE, u->name, d->loc);
                    copy->usage_type = u->usage_type;
                    copy->next = d->children;
                    d->children = copy;
                    break;
                }
            }
        }
        if (depth < 64) { stack[depth] = d; levels[depth] = level; depth++; }
    }
}

static void sema_walk_ast(AstNode *node, SemaContext *ctx) {
    if (!node) return;

    switch (node->kind) {
        case AST_SECTION:
        case AST_FILE_DECL:
            inherit_group_usage(node->children);
            break;
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