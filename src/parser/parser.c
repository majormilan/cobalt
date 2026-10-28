/* src/parser/parser.c */
#include "parser.h"
#include "lexer.h"
#include "diag.h"
#include "ast.h"
#include "config.h"
#include "symtab.h" // Added for symbol table integration
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <ctype.h>

static Token tok;
static Lexer *current_lexer; // Temporary global lexer for parser functions
static SymbolTable *current_symtab; // Global symbol table for parser functions
static bool parser_panic_mode = false;

static void next(void) {
    tok = lexer_next(current_lexer);
}

// Function to synchronize parser after an error
static void synchronize() {
    parser_panic_mode = true;
    while (tok.kind != TOK_EOF) {
        // We've already advanced past the error, now look for a safe point
        // A period often ends a statement, good place to recover.
        if (tok.kind == TOK_PERIOD) {
            next(); // Consume the period
            parser_panic_mode = false; // Reset panic mode
            return;
        }

        switch (tok.kind) {
            // Division headers are strong synchronization points
            case TOK_IDENTIFICATION:
            case TOK_ENVIRONMENT:
            case TOK_DATA:
            case TOK_PROCEDURE:
            // Section headers are also strong synchronization points
            case TOK_SECTION:
            // Common statement keywords are good synchronization points
            case TOK_DISPLAY:
            case TOK_STOP:
            case TOK_MOVE:
            case TOK_ADD:
            case TOK_SUBTRACT:
            case TOK_MULTIPLY:
            case TOK_DIVIDE:
            case TOK_IF:
            case TOK_PERFORM:
            case TOK_ACCEPT:
            case TOK_CONTINUE:
            case TOK_COMPUTE:
            case TOK_EVALUATE:
            case TOK_GO:
            case TOK_SET:
            case TOK_INITIALIZE:
            case TOK_SEARCH:
            case TOK_INSPECT:
            case TOK_OPEN:
            case TOK_CLOSE:
            case TOK_READ:
            case TOK_WRITE:
            case TOK_REWRITE:
                parser_panic_mode = false; // Reset panic mode
                return; 
            default:
                next(); // Keep consuming tokens
        }
    }
}


static char *tok_to_str(void) {
    char *s = malloc(tok.length + 1);
    if (s) {
        strncpy(s, tok.lexeme, tok.length);
        s[tok.length] = '\0';
    }
    return s;
}

// Text of the current token; for a nonnumeric literal the doubled delimiter
// ("" or '') inside it stands for one delimiter character
static char *tok_value(void) {
    char *s = tok_to_str();
    if (s && tok.kind == TOK_STRING) {
        char q = tok.lexeme[-1];
        char *d = s;
        for (const char *p = s; *p; p++) {
            *d++ = *p;
            if (*p == q && p[1] == q) p++;
        }
        *d = '\0';
    }
    return s;
}

static void expect(TokenKind k, const char *msg) {
    if (tok.kind != k) {
        if (!parser_panic_mode) { // Only report first error in a sequence
            char *got_str = tok_to_str();
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected %s, got '%.*s'", msg, (int)tok.length, tok.lexeme);
            free(got_str);
            synchronize(); // Enter panic mode and synchronize
        } else {
            // Already in panic mode, just consume until next synchronization point
            next(); // Advance one token, synchronize will handle the rest
        }
    } else {
        next(); // Consume the expected token
        parser_panic_mode = false; // Successfully parsed, exit panic mode
    }
}


static bool is_known_ident_para(const char *s) {
    return strcasecmp(s, "AUTHOR") == 0 ||
           strcasecmp(s, "DATE-WRITTEN") == 0 ||
           strcasecmp(s, "DATE-COMPILED") == 0 ||
           strcasecmp(s, "INSTALLATION") == 0 ||
           strcasecmp(s, "SECURITY") == 0;
}

// Build an operand AST node (identifier or number literal) and resolve/mark
// the symbol if it is an identifier. Returns NULL on allocation failure.
static AstNode *make_operand_node(const char *val, TokenKind kind) {
    SourceLocation op_loc = {current_lexer->filename, tok.line, tok.column};
    AstNode *op = ast_new(
        kind == TOK_NUMBER ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
        val, op_loc);
    if (kind != TOK_NUMBER) {
        Symbol *sym = symtab_resolve_symbol(current_symtab, val);
        if (sym) symtab_mark_symbol_as_used(sym);
        // (forward refs are silently allowed; IR will warn if var not found)
    }
    return op;
}

// Append node to a singly-linked list tracked by head/tail pointers.
static void list_append(AstNode **head, AstNode **tail, AstNode *node) {
    if (!*head) *head = node;
    else (*tail)->next = node;
    *tail = node;
}

static bool is_division_start(TokenKind kind) {
    return kind == TOK_IDENTIFICATION ||
           kind == TOK_ENVIRONMENT ||
           kind == TOK_DATA ||
           kind == TOK_PROCEDURE;
}

static bool is_figurative(TokenKind kind) {
    return kind == TOK_ZEROS || kind == TOK_SPACES || kind == TOK_HIGH_VALUES ||
           kind == TOK_LOW_VALUES || kind == TOK_QUOTES;
}

// Look at the token after the current one without consuming anything.
static Token peek_token(void) {
    Lexer copy = *current_lexer;
    copy.prev = NULL;           // never pop back into an including file
    return lexer_next(&copy);
}

static AstNode *parse_identification_paragraph(void) {
    if (tok.kind != TOK_IDENT) return NULL;
    if (parser_panic_mode) return NULL; // Don't parse if in panic mode

    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    char *name = tok_to_str();
    next();
    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) { free(name); return NULL; }

    AstNode *para = ast_new(AST_IDENTIFICATION_PARAGRAPH, name, loc);
    free(name);

    char val_buf[256] = {0};
    size_t pos = 0;

    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && pos < sizeof(val_buf) - 1 && !parser_panic_mode) {
        char *part = tok_to_str();

        if (tok.kind == TOK_IDENT && is_known_ident_para(part)) {
            free(part);
            break;
        }

        if (is_division_start(tok.kind)) {
            free(part);
            break;
        }

        if (pos > 0) {
            val_buf[pos++] = ' ';
        }
        strncat(val_buf + pos, part, sizeof(val_buf) - pos - 1);
        pos += strlen(part);
        free(part);
        next();
    }

    para->text = strdup(val_buf);

    if (tok.kind == TOK_PERIOD) {
        next();
    }
    
    return para;
}

static AstNode *parse_rel_operand(void);
static AstNode *parse_all_literal(void);
static AstNode *parse_identifier(void);
static AstNode *parse_value_operand(void);
static AstNode *parse_arith_operand(void);
static AstNode *parse_procedure_name(void);
static AstNode *parse_imperative_block(void);
static Token peek_token(void);
static bool tok_is_word(const char *word);
static void arith_error(const char *msg);

// Map a usage keyword to its usage type; returns false if tok is not one
static bool usage_from_token(TokenKind kind, AstUsageType *out) {
    switch (kind) {
        case TOK_DISPLAY: *out = USAGE_DISPLAY; return true;
        case TOK_COMP:
        case TOK_BINARY:  *out = USAGE_COMP;    return true;
        case TOK_COMP_3:  *out = USAGE_COMP3;   return true;
        case TOK_COMP_5:  *out = USAGE_COMP5;   return true;
        default:          return false;
    }
}

static void append_child(AstNode *parent, AstNode **tail, AstNode *child) {
    if (!parent->children) parent->children = child;
    else (*tail)->next = child;
    *tail = child;
}

static AstNode *parse_var_decl(void) {
    if (tok.kind != TOK_NUMBER) return NULL;
    if (parser_panic_mode) return NULL; // Don't parse if in panic mode

    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    char *level_str = tok_to_str();
    int level_num = atoi(level_str);
    next();

    // data-name, FILLER, or nothing (an implicit FILLER)
    char *name = NULL;
    if (tok.kind == TOK_IDENT) {
        name = tok_to_str();
        next();
    } else {
        if (tok.kind == TOK_FILLER) next();
        name = strdup("FILLER");
    }

    AstNode *var = ast_new(AST_VAR_DECL, name, loc);
    // Data names may repeat in different groups; references are qualified
    // and resolved during IR lowering
    Symbol *sym = NULL;
    if (strcasecmp(name, "FILLER") != 0) {
        sym = symtab_resolve_symbol(current_symtab, name);
        if (!sym) sym = symtab_declare_symbol(current_symtab, name, SYM_VAR, loc);
        if (sym) sym->level = level_num;
    }

    free(name);
    var->text = level_str; // Keep level_str in AST node for now, or just free it.
    AstNode *tail = NULL;

    // Level 88: VALUE[S] [IS|ARE] literal [THRU literal] {literal [THRU literal]}...
    // children: the literals; a THRU range keeps its upper bound in extra
    if (level_num == 88) {
        if (tok.kind != TOK_VALUE) {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Condition-name '%s' requires VALUE", var->name);
            synchronize();
            ast_free(var);
            return NULL;
        }
        next();
        if (tok.kind == TOK_IS) next();
        while (tok.kind == TOK_STRING || tok.kind == TOK_NUMBER || is_figurative(tok.kind) ||
               tok.kind == TOK_ALL) {
            AstNode *lo = parse_rel_operand();
            if (tok.kind == TOK_THROUGH) {
                next();
                lo->extra = parse_rel_operand();
                if (!lo->extra) {
                    ast_free(lo);
                    ast_free(var);
                    arith_error("Expected literal after THRU");
                    return NULL;
                }
            }
            append_child(var, &tail, lo);
        }
        if (!var->children) {
            ast_free(var);
            arith_error("Expected literal in condition-name VALUE");
            return NULL;
        }
        expect(TOK_PERIOD, ".");
        if (parser_panic_mode) { ast_free(var); return NULL; }
        return var;
    }

    // Clauses may appear in any order
    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !parser_panic_mode) {
        SourceLocation cl_loc = {current_lexer->filename, tok.line, tok.column};
        AstUsageType usage;

        if (tok.kind == TOK_PIC) {
            next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_PIC_STRING) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "Expected PICTURE character-string, got '%.*s'", (int)tok.length, tok.lexeme);
                synchronize();
                ast_free(var);
                return NULL;
            }
            char *pic = tok_to_str();
            append_child(var, &tail, ast_new(AST_PIC_CLAUSE, pic, cl_loc));
            // Store PIC string in symbol and parse its attributes
            if (sym) {
                sym->pic_string = strdup(pic);
                symtab_parse_pic_string(sym, sym->pic_string);
            }
            free(pic);
            next();
        } else if (tok.kind == TOK_USAGE || tok.kind == TOK_INDEX || usage_from_token(tok.kind, &usage)) {
            if (tok.kind == TOK_USAGE) {
                next();
                if (tok.kind == TOK_IS) next();
            }
            if (tok.kind == TOK_INDEX) {
                // USAGE INDEX: an occurrence number, stored like an index-name
                AstNode *usage_clause = ast_new(AST_USAGE_CLAUSE, "INDEX", cl_loc);
                usage_clause->usage_type = USAGE_COMP5;
                append_child(var, &tail, usage_clause);
                next();
                continue;
            }
            {
                if (!usage_from_token(tok.kind, &usage)) {
                    cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                     "Unsupported USAGE '%.*s'", (int)tok.length, tok.lexeme);
                    synchronize();
                    ast_free(var);
                    return NULL;
                }
            }
            AstNode *usage_clause = ast_new(AST_USAGE_CLAUSE, NULL, cl_loc);
            usage_clause->usage_type = usage;
            append_child(var, &tail, usage_clause);
            if (sym && usage != USAGE_DISPLAY) sym->var_type = VAR_TYPE_COMP;
            next();
        } else if (tok.kind == TOK_VALUE) {
            next();
            if (tok.kind == TOK_IS) next();
            char *val = tok_value();
            AstNode *val_node = NULL;
            SourceLocation val_loc = {current_lexer->filename, tok.line, tok.column};
            if (tok.kind == TOK_STRING) {
                val_node = ast_new(AST_STRING_LITERAL, val, val_loc);
            } else if (tok.kind == TOK_NUMBER) {
                val_node = ast_new(AST_LITERAL_NUMBER, val, val_loc);
            } else if (is_figurative(tok.kind)) {
                val_node = ast_new(AST_FIGURATIVE_CONSTANT, val, val_loc);
            } else if (tok.kind == TOK_ALL) {
                free(val);
                val_node = parse_all_literal();
                if (!val_node) { ast_free(var); return NULL; }
                append_child(var, &tail, val_node);
                continue;
            } else {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected literal or figurative constant after VALUE, got '%.*s'", (int)tok.length, tok.lexeme);
                free(val);
                synchronize(); // Error, enter panic mode
                ast_free(var);
                return NULL;
            }
            free(val);
            append_child(var, &tail, val_node);
            next();
        } else if (tok.kind == TOK_REDEFINES) {
            next();
            if (tok.kind != TOK_IDENT) { ast_free(var); arith_error("Expected data-name after REDEFINES"); return NULL; }
            char *target = tok_to_str();
            Symbol *tsym = symtab_resolve_symbol(current_symtab, target);
            if (tsym) symtab_mark_symbol_as_used(tsym);
            append_child(var, &tail, ast_new(AST_REDEFINES, target, cl_loc));
            free(target);
            next();
        } else if (tok.kind == TOK_OCCURS) {
            // OCCURS n [TIMES] [{ASCENDING|DESCENDING} [KEY] [IS] name...]...
            //   [INDEXED [BY] name...]
            next();
            if (tok.kind != TOK_NUMBER) { ast_free(var); arith_error("Expected integer after OCCURS"); return NULL; }
            char *count = tok_to_str();
            AstNode *occurs = ast_new(AST_OCCURS, count, cl_loc);
            free(count);
            append_child(var, &tail, occurs);
            next();
            if (tok.kind == TOK_TO || tok.kind == TOK_DEPENDING) {
                ast_free(var);
                arith_error("OCCURS ... DEPENDING ON is not supported yet");
                return NULL;
            }
            if (tok.kind == TOK_TIMES) next();
            while (tok.kind == TOK_ASCENDING || tok.kind == TOK_DESCENDING) {
                next();
                if (tok.kind == TOK_KEY) next();
                if (tok.kind == TOK_IS) next();
                while (tok.kind == TOK_IDENT) next();   // keys only matter to SEARCH ALL ordering
            }
            if (tok.kind == TOK_INDEXED) {
                next();
                if (tok.kind == TOK_BY) next();
                AstNode *idx_tail = NULL;
                while (tok.kind == TOK_IDENT) {
                    char *idx = tok_to_str();
                    SourceLocation iloc = {current_lexer->filename, tok.line, tok.column};
                    if (!symtab_resolve_symbol(current_symtab, idx)) {
                        Symbol *isym = symtab_declare_symbol(current_symtab, idx, SYM_VAR, iloc);
                        if (isym) symtab_mark_symbol_as_used(isym);
                    }
                    list_append(&occurs->children, &idx_tail, ast_new(AST_IDENTIFIER, idx, iloc));
                    free(idx);
                    next();
                }
            }
        } else if (tok.kind == TOK_JUSTIFIED) {
            next();
            if (tok.kind == TOK_RIGHT) next();
            append_child(var, &tail, ast_new(AST_JUSTIFIED, NULL, cl_loc));
        } else if (tok.kind == TOK_SYNCHRONIZED) {
            next();                             // alignment is not needed on x86_64
            if (tok.kind == TOK_LEFT || tok.kind == TOK_RIGHT) next();
        } else if (tok.kind == TOK_BLANK) {
            next();
            if (tok.kind == TOK_WHEN) next();
            if (tok.kind != TOK_ZEROS) { ast_free(var); arith_error("Expected ZERO after BLANK WHEN"); return NULL; }
            next();
            var->blank_when_zero = true;
        } else if (tok.kind == TOK_SIGN || tok.kind == TOK_LEADING || tok.kind == TOK_TRAILING) {
            // [SIGN [IS]] {LEADING | TRAILING} [SEPARATE [CHARACTER]]
            if (tok.kind == TOK_SIGN) {
                next();
                if (tok.kind == TOK_IS) next();
            }
            if (tok.kind != TOK_LEADING && tok.kind != TOK_TRAILING) {
                ast_free(var);
                arith_error("Expected LEADING or TRAILING");
                return NULL;
            }
            var->sign_mode = SIGN_GIVEN | (tok.kind == TOK_LEADING ? SIGN_LEADING : 0);
            next();
            if (tok.kind == TOK_SEPARATE) {
                var->sign_mode |= SIGN_SEPARATE;
                next();
                if (tok.kind == TOK_CHARACTER) next();
            }
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Unexpected '%.*s' in data description of '%s'", (int)tok.length, tok.lexeme, var->name);
            synchronize();
            ast_free(var);
            return NULL;
        }
    }

    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) return NULL;
    return var;
}

static AstNode *parse_statement(void);

// Forward declarations for recursive-descent expression parser (used by COMPUTE)
static AstNode *parse_expr(void);
static AstNode *parse_term(void);
static AstNode *parse_factor(void);

// identifier := name {OF|IN name}... [(subscript...)] [(start:[length])]
// AST_IDENTIFIER: children = subscripts, extra = qualifiers (innermost
// first), loop = AST_REFMOD with children start and optional length.
static AstNode *parse_identifier(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    char *name = tok_to_str();
    AstNode *id = ast_new(AST_IDENTIFIER, name, loc);
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    if (sym) symtab_mark_symbol_as_used(sym);
    else cobalt_error_loc(current_lexer->original_source, loc.line, loc.column, "Undeclared identifier '%s'.", name);
    free(name);
    next();

    AstNode *qual_tail = NULL;
    while ((tok.kind == TOK_OF || tok.kind == TOK_IN) && !parser_panic_mode) {
        next();
        if (tok.kind != TOK_IDENT) { arith_error("Expected a name after OF/IN"); return id; }
        char *q = tok_to_str();
        Symbol *qsym = symtab_resolve_symbol(current_symtab, q);
        if (qsym) symtab_mark_symbol_as_used(qsym);
        list_append(&id->extra, &qual_tail, ast_new(AST_IDENTIFIER, q, (SourceLocation){current_lexer->filename, tok.line, tok.column}));
        free(q);
        next();
    }

    for (int part = 0; part < 2 && tok.kind == TOK_LPAREN && !id->loop && !parser_panic_mode; part++) {
        SourceLocation ploc = {current_lexer->filename, tok.line, tok.column};
        next();
        AstNode *first = parse_expr();
        if (!first) { arith_error("Expected subscript or reference modifier"); return id; }
        if (tok.kind == TOK_COLON) {
            next();
            AstNode *refmod = ast_new(AST_REFMOD, NULL, ploc);
            refmod->children = first;
            if (tok.kind != TOK_RPAREN) first->next = parse_expr();
            id->loop = refmod;
        } else {
            if (id->children) { ast_free(first); arith_error("Unexpected second subscript list"); return id; }
            AstNode *tail = first;
            id->children = first;
            while (tok.kind != TOK_RPAREN && tok.kind != TOK_EOF && !parser_panic_mode) {
                AstNode *sub = parse_expr();
                if (!sub) break;
                tail->next = sub;
                tail = sub;
            }
        }
        if (tok.kind != TOK_RPAREN) { arith_error("Expected ')'"); return id; }
        next();
    }
    return id;
}

// ALL literal (a figurative constant repeating the literal) or ALL followed
// by a figurative constant (the same constant). Assumes tok is ALL.
static AstNode *parse_all_literal(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    next();
    if (tok.kind != TOK_STRING && !is_figurative(tok.kind)) {
        arith_error("Expected a literal after ALL");
        return NULL;
    }
    char *val = tok_value();
    AstNode *node;
    if (tok.kind == TOK_STRING) {
        node = ast_new(AST_FIGURATIVE_CONSTANT, "ALL", loc);
        node->text = val;
    } else {
        node = ast_new(AST_FIGURATIVE_CONSTANT, val, loc);
        free(val);
    }
    next();
    return node;
}

// Identifier, literal or figurative constant (DISPLAY and MOVE operands)
static AstNode *parse_value_operand(void) {
    if (tok.kind == TOK_IDENT) return parse_identifier();
    if (tok.kind == TOK_ALL) return parse_all_literal();
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    AstKind kind;
    if (tok.kind == TOK_STRING) kind = AST_STRING_LITERAL;
    else if (tok.kind == TOK_NUMBER) kind = AST_LITERAL_NUMBER;
    else if (is_figurative(tok.kind)) kind = AST_FIGURATIVE_CONSTANT;
    else return NULL;
    char *val = tok_value();
    AstNode *node = ast_new(kind, val, loc);
    free(val);
    next();
    return node;
}

static AstNode *parse_factor(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};

    if (tok.kind == TOK_LPAREN) {
        next();
        AstNode *inner = parse_expr();
        if (tok.kind == TOK_RPAREN) next();
        else cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                              "Expected ')' in arithmetic expression");
        return inner;
    }

    // Unary sign (the lexer already folds -5 into the literal "-5")
    if (tok.kind == TOK_PLUS) {
        next();
        return parse_factor();
    }
    if (tok.kind == TOK_MINUS) {
        next();
        AstNode *operand = parse_factor();
        if (!operand) return NULL;
        if (operand->kind == AST_LITERAL_NUMBER && operand->name) {
            const char *digits = operand->name;
            char *neg = malloc(strlen(digits) + 2);
            if (digits[0] == '-') strcpy(neg, digits + 1);
            else if (digits[0] == '+') sprintf(neg, "-%s", digits + 1);
            else sprintf(neg, "-%s", digits);
            free(operand->name);
            operand->name = neg;
            return operand;
        }
        AstNode *node = ast_new(AST_EXPR_NEG, NULL, loc);
        node->children = operand;
        return node;
    }

    if (tok.kind == TOK_NUMBER) {
        char *val = tok_to_str();
        AstNode *node = ast_new(AST_LITERAL_NUMBER, val, loc);
        free(val);
        next();
        return node;
    }

    if (tok.kind == TOK_ZEROS) {                // ZERO in an expression is 0
        next();
        return ast_new(AST_LITERAL_NUMBER, "0", loc);
    }

    if (tok.kind == TOK_IDENT) return parse_identifier();

    return NULL;
}

// Exponentiation: left-to-right, higher precedence than * and /
static AstNode *parse_power(void) {
    AstNode *left = parse_factor();

    while (tok.kind == TOK_POW) {
        SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
        next();
        AstNode *right = parse_factor();
        AstNode *node = ast_new(AST_EXPR_POW, NULL, loc);
        node->children = left;
        if (left) left->next = right;
        left = node;
    }
    return left;
}

// Multiplication / division: left-to-right, higher precedence than +/-
static AstNode *parse_term(void) {
    AstNode *left = parse_power();

    while (tok.kind == TOK_MUL || tok.kind == TOK_DIV) {
        AstKind op = (tok.kind == TOK_MUL) ? AST_EXPR_MUL : AST_EXPR_DIV;
        SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
        next();
        AstNode *right = parse_power();
        AstNode *node = ast_new(op, NULL, loc);
        node->children = left;
        if (left) left->next = right;
        left = node;
    }
    return left;
}

// Addition / subtraction: left-to-right, lowest precedence
static AstNode *parse_expr(void) {
    AstNode *left = parse_term();

    while (tok.kind == TOK_PLUS || tok.kind == TOK_MINUS) {
        AstKind op = (tok.kind == TOK_PLUS) ? AST_EXPR_ADD : AST_EXPR_SUB;
        SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
        next();
        AstNode *right = parse_term();
        AstNode *node = ast_new(op, NULL, loc);
        node->children = left;
        if (left) left->next = right;
        left = node;
    }
    return left;
}

static void arith_error(const char *msg) {
    cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                     "%s, got '%.*s'", msg, (int)tok.length, tok.lexeme);
    synchronize();
}

// ── Conditions ───────────────────────────────────────────────────────────────
//
// condition  := and-cond {OR and-cond}
// and-cond   := not-cond {AND not-cond}
// not-cond   := NOT not-cond | primary
// primary    := ( condition ) | condition-name
//             | operand [IS] [NOT] {relop operand | class | POSITIVE | NEGATIVE | ZERO}
//             | abbreviated relation: [[NOT] relop] operand, reusing the last
//               subject (and operator) after AND / OR
// operand    := nonnumeric literal | figurative constant | arithmetic expression

static AstNode *abbrev_subject = NULL;  // subject of the last relation
static const char *abbrev_op = NULL;    // operator of the last relation

static AstNode *parse_or_cond(void);

static bool is_relop_start(TokenKind kind) {
    return kind == TOK_GREATER || kind == TOK_LESS || kind == TOK_EQUAL ||
           kind == TOK_GT || kind == TOK_LT || kind == TOK_GE || kind == TOK_LE ||
           kind == TOK_EQ || kind == TOK_NE;
}

static bool is_class_or_sign(TokenKind kind) {
    return kind == TOK_NUMERIC || kind == TOK_ALPHABETIC || kind == TOK_ALPHABETIC_LOWER ||
           kind == TOK_ALPHABETIC_UPPER || kind == TOK_POSITIVE || kind == TOK_NEGATIVE;
}

static const char *negate_relop(const char *op) {
    if (!strcmp(op, "="))  return "<>";
    if (!strcmp(op, "<>")) return "=";
    if (!strcmp(op, ">"))  return "<=";
    if (!strcmp(op, "<"))  return ">=";
    if (!strcmp(op, ">=")) return "<";
    return ">";                         // "<="
}

// GREATER [THAN] [OR EQUAL [TO]] | LESS [THAN] [OR EQUAL [TO]] | EQUAL [TO]
// | > | < | = | >= | <= | <>
static const char *parse_relop(void) {
    TokenKind kind = tok.kind;
    next();
    switch (kind) {
        case TOK_GT: return ">";
        case TOK_LT: return "<";
        case TOK_GE: return ">=";
        case TOK_LE: return "<=";
        case TOK_EQ: return "=";
        case TOK_NE: return "<>";
        case TOK_EQUAL:
            if (tok.kind == TOK_TO) next();
            return "=";
        default: {
            bool greater = (kind == TOK_GREATER);
            if (tok.kind == TOK_THAN) next();
            if (tok.kind == TOK_OR && peek_token().kind == TOK_EQUAL) {
                next();
                next();
                if (tok.kind == TOK_TO) next();
                return greater ? ">=" : "<=";
            }
            return greater ? ">" : "<";
        }
    }
}

static AstNode *make_cond(AstKind kind, AstNode *left, AstNode *right, SourceLocation loc) {
    AstNode *c = ast_new(kind, NULL, loc);
    c->children = left;
    if (right) left->next = right;
    return c;
}

static AstNode *make_relation(AstNode *left, const char *op, AstNode *right) {
    AstNode *c = make_cond(AST_CONDITION, left, right, left->loc);
    c->text = strdup(op);
    return c;
}

// Relation operand: nonnumeric literal, figurative constant or arithmetic
// expression. Returns NULL, consuming nothing, if none starts here.
static AstNode *parse_rel_operand(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    if (tok.kind == TOK_ALL) return parse_all_literal();
    if (tok.kind == TOK_ZEROS) {
        // ZERO starting an arithmetic expression ("ZERO - A IS NEGATIVE")
        TokenKind after = peek_token().kind;
        if (after == TOK_PLUS || after == TOK_MINUS || after == TOK_MUL ||
            after == TOK_DIV || after == TOK_POW) return parse_expr();
    }
    if (tok.kind == TOK_STRING || is_figurative(tok.kind)) {
        char *val = tok_value();
        AstNode *op = ast_new(tok.kind == TOK_STRING ? AST_STRING_LITERAL : AST_FIGURATIVE_CONSTANT, val, loc);
        free(val);
        next();
        return op;
    }
    if (tok.kind != TOK_IDENT && tok.kind != TOK_NUMBER && tok.kind != TOK_LPAREN &&
        tok.kind != TOK_PLUS && tok.kind != TOK_MINUS) return NULL;
    return parse_expr();
}

static bool is_condition_name(const AstNode *n) {
    if (!n || n->kind != AST_IDENTIFIER) return false;
    Symbol *sym = symtab_resolve_symbol(current_symtab, n->name);
    return sym && sym->level == 88;
}

// Relation, class or sign condition after its subject; NULL (consuming
// nothing) if none follows.
static AstNode *parse_condition_tail(AstNode *subject) {
    if (tok.kind != TOK_IS && tok.kind != TOK_NOT && !is_relop_start(tok.kind) &&
        !is_class_or_sign(tok.kind) && tok.kind != TOK_ZEROS) return NULL;

    bool negated = false;
    if (tok.kind == TOK_IS) next();
    if (tok.kind == TOK_NOT) {
        negated = true;
        next();
    }

    AstNode *cond;
    if (is_relop_start(tok.kind)) {
        const char *op = parse_relop();
        AstNode *object = parse_rel_operand();
        if (!object) { ast_free(subject); arith_error("Expected operand after relational operator"); return NULL; }
        if (negated) op = negate_relop(op);
        cond = make_relation(subject, op, object);
        abbrev_subject = subject;
        abbrev_op = cond->text;
        return cond;
    }

    const char *word;
    AstKind kind = AST_COND_CLASS;
    switch (tok.kind) {
        case TOK_NUMERIC:          word = "NUMERIC"; break;
        case TOK_ALPHABETIC:       word = "ALPHABETIC"; break;
        case TOK_ALPHABETIC_LOWER: word = "ALPHABETIC-LOWER"; break;
        case TOK_ALPHABETIC_UPPER: word = "ALPHABETIC-UPPER"; break;
        case TOK_POSITIVE:         word = "POSITIVE"; kind = AST_COND_SIGN; break;
        case TOK_NEGATIVE:         word = "NEGATIVE"; kind = AST_COND_SIGN; break;
        case TOK_ZEROS:            word = "ZERO";     kind = AST_COND_SIGN; break;
        default:
            ast_free(subject);
            arith_error("Expected relational operator, class or sign condition");
            return NULL;
    }
    next();
    cond = make_cond(kind, subject, NULL, subject->loc);
    cond->name = strdup(word);
    if (negated) cond = make_cond(AST_COND_NOT, cond, NULL, cond->loc);
    return cond;
}

// After '(' : true if it opens a parenthesized condition rather than an
// arithmetic expression (scans ahead without consuming).
static bool paren_is_condition(void) {
    Lexer copy = *current_lexer;
    copy.prev = NULL;
    int depth = 1;
    for (int guard = 0; guard < 1000; guard++) {
        Token t = lexer_next(&copy);
        if (t.kind == TOK_EOF || t.kind == TOK_PERIOD) return false;
        if (t.kind == TOK_LPAREN) { depth++; continue; }
        if (t.kind == TOK_RPAREN) {
            if (--depth == 0) return false;
            continue;
        }
        if (depth != 1) continue;
        if (is_relop_start(t.kind) || is_class_or_sign(t.kind) || t.kind == TOK_IS ||
            t.kind == TOK_NOT || t.kind == TOK_AND || t.kind == TOK_OR) return true;
        if (t.kind == TOK_IDENT) {
            char name[64];
            size_t n = t.length < sizeof(name) - 1 ? t.length : sizeof(name) - 1;
            memcpy(name, t.lexeme, n);
            name[n] = '\0';
            Symbol *sym = symtab_resolve_symbol(current_symtab, name);
            if (sym && sym->level == 88) return true;
        }
    }
    return false;
}

static AstNode *parse_primary_cond(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};

    // Abbreviated relation with its operator: the subject is omitted
    if (abbrev_subject && tok.kind == TOK_IS) {
        TokenKind after = peek_token().kind;
        if (after == TOK_NOT || is_relop_start(after)) next();
    }
    if (abbrev_subject && (is_relop_start(tok.kind) ||
                           (tok.kind == TOK_NOT && is_relop_start(peek_token().kind)))) {
        bool negated = false;
        if (tok.kind == TOK_NOT) { negated = true; next(); }
        const char *op = parse_relop();
        if (negated) op = negate_relop(op);
        AstNode *object = parse_rel_operand();
        if (!object) { arith_error("Expected operand after relational operator"); return NULL; }
        AstNode *cond = make_relation(ast_clone(abbrev_subject), op, object);
        abbrev_op = cond->text;
        return cond;
    }

    if (tok.kind == TOK_LPAREN && paren_is_condition()) {
        next();
        AstNode *inner = parse_or_cond();
        if (!inner) return NULL;
        if (tok.kind != TOK_RPAREN) { ast_free(inner); arith_error("Expected ')' in condition"); return NULL; }
        next();
        return inner;
    }

    AstNode *subject = parse_rel_operand();
    if (!subject) { arith_error("Expected a condition"); return NULL; }
    if (tok.kind == TOK_IS || tok.kind == TOK_NOT || is_relop_start(tok.kind) ||
        is_class_or_sign(tok.kind) || tok.kind == TOK_ZEROS)
        return parse_condition_tail(subject);

    if (is_condition_name(subject)) {
        subject->kind = AST_COND_NAME;
        return subject;
    }
    if (abbrev_subject && abbrev_op) {
        // Abbreviated relation without operator: only the object is given
        return make_relation(ast_clone(abbrev_subject), abbrev_op, subject);
    }
    ast_free(subject);
    cobalt_error_loc(current_lexer->original_source, loc.line, loc.column, "Expected a condition");
    synchronize();
    return NULL;
}

static AstNode *parse_not_cond(void) {
    if (tok.kind == TOK_NOT && !(abbrev_subject && is_relop_start(peek_token().kind))) {
        SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
        next();
        AstNode *inner = parse_not_cond();
        return inner ? make_cond(AST_COND_NOT, inner, NULL, loc) : NULL;
    }
    return parse_primary_cond();
}

static AstNode *parse_and_cond(void) {
    AstNode *left = parse_not_cond();
    while (left && tok.kind == TOK_AND) {
        next();
        AstNode *right = parse_not_cond();
        if (!right) { ast_free(left); return NULL; }
        left = make_cond(AST_COND_AND, left, right, left->loc);
    }
    return left;
}

static AstNode *parse_or_cond(void) {
    AstNode *left = parse_and_cond();
    while (left && tok.kind == TOK_OR) {
        next();
        AstNode *right = parse_and_cond();
        if (!right) { ast_free(left); return NULL; }
        left = make_cond(AST_COND_OR, left, right, left->loc);
    }
    return left;
}

static AstNode *parse_condition(void) {
    abbrev_subject = NULL;
    abbrev_op = NULL;
    AstNode *cond = parse_or_cond();
    abbrev_subject = NULL;
    abbrev_op = NULL;
    return cond;
}

// ── EVALUATE helpers ─────────────────────────────────────────────────────────

static bool is_condition_node(const AstNode *n) {
    switch (n->kind) {
        case AST_CONDITION: case AST_COND_AND: case AST_COND_OR: case AST_COND_NOT:
        case AST_COND_CLASS: case AST_COND_SIGN: case AST_COND_NAME:
            return true;
        default:
            return false;
    }
}

static AstNode *make_bool(bool value, SourceLocation loc) {
    return ast_new(AST_BOOL_LITERAL, value ? "TRUE" : "FALSE", loc);
}

static void free_subjects(AstNode **subjects, int n) {
    for (int i = 0; i < n; i++) ast_free(subjects[i]);
}

// Subject: TRUE | FALSE | condition | identifier, literal or expression
static AstNode *parse_evaluate_subject(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    if (tok.kind == TOK_TRUE || tok.kind == TOK_FALSE) {
        bool value = tok.kind == TOK_TRUE;
        next();
        return make_bool(value, loc);
    }
    abbrev_subject = NULL;
    abbrev_op = NULL;
    AstNode *operand = parse_rel_operand();
    if (!operand) { arith_error("Expected EVALUATE subject"); return NULL; }
    if (tok.kind == TOK_IS || tok.kind == TOK_NOT || is_relop_start(tok.kind) ||
        is_class_or_sign(tok.kind))
        return parse_condition_tail(operand);
    if (is_condition_name(operand)) operand->kind = AST_COND_NAME;
    return operand;
}

// Object for one subject, returned as the condition "subject matches object":
// ANY | TRUE | FALSE | condition | [NOT] operand [THRU operand]
static AstNode *parse_when_object(AstNode *subject) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    if (tok.kind == TOK_ANY) {
        next();
        return make_bool(true, loc);
    }
    if (subject->kind == AST_BOOL_LITERAL) {
        bool subject_true = strcmp(subject->name, "TRUE") == 0;
        if (tok.kind == TOK_TRUE || tok.kind == TOK_FALSE) {
            bool object_true = tok.kind == TOK_TRUE;
            next();
            return make_bool(subject_true == object_true, loc);
        }
        AstNode *cond = parse_condition();
        if (!cond) return NULL;
        return subject_true ? cond : make_cond(AST_COND_NOT, cond, NULL, loc);
    }
    if (is_condition_node(subject)) {
        if (tok.kind != TOK_TRUE && tok.kind != TOK_FALSE) {
            arith_error("Expected TRUE, FALSE or ANY for a conditional EVALUATE subject");
            return NULL;
        }
        bool object_true = tok.kind == TOK_TRUE;
        next();
        AstNode *cond = ast_clone(subject);
        return object_true ? cond : make_cond(AST_COND_NOT, cond, NULL, loc);
    }

    bool negated = false;
    if (tok.kind == TOK_NOT) {
        negated = true;
        next();
    }
    AstNode *lo = parse_rel_operand();
    if (!lo) { arith_error("Expected EVALUATE object"); return NULL; }
    AstNode *cond;
    if (tok.kind == TOK_THROUGH) {
        next();
        AstNode *hi = parse_rel_operand();
        if (!hi) { ast_free(lo); arith_error("Expected operand after THRU"); return NULL; }
        cond = make_cond(AST_COND_AND, make_relation(ast_clone(subject), ">=", lo),
                         make_relation(ast_clone(subject), "<=", hi), loc);
    } else {
        cond = make_relation(ast_clone(subject), "=", lo);
    }
    return negated ? make_cond(AST_COND_NOT, cond, NULL, loc) : cond;
}

// ── Arithmetic statement helpers ─────────────────────────────────────────────

// Parse an arithmetic operand (identifier or numeric literal).
// Returns NULL, consuming nothing, if the token is not an operand.
static AstNode *parse_arith_operand(void) {
    if (tok.kind == TOK_IDENT) return parse_identifier();
    if (tok.kind == TOK_ZEROS) {                // ZERO is the number 0
        SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
        next();
        return ast_new(AST_LITERAL_NUMBER, "0", loc);
    }
    if (tok.kind != TOK_NUMBER) return NULL;
    char *val = tok_to_str();
    AstNode *op = make_operand_node(val, tok.kind);
    free(val);
    next();
    return op;
}

// Procedure names are words or unsigned integers ("0001")
static bool is_procedure_name_token(void) {
    if (tok.kind == TOK_IDENT) return true;
    if (tok.kind != TOK_NUMBER) return false;
    for (size_t i = 0; i < tok.length; i++)
        if (!isdigit((unsigned char)tok.lexeme[i])) return false;
    return true;
}

// Procedure (paragraph or section) name; may be a forward reference, so it is
// resolved during IR lowering.
static AstNode *parse_procedure_name(void) {
    char *name = tok_to_str();
    AstNode *proc = ast_new(AST_IDENTIFIER, name, (SourceLocation){current_lexer->filename, tok.line, tok.column});
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    if (sym) symtab_mark_symbol_as_used(sym);
    free(name);
    next();
    // paragraph-name OF|IN section-name (extra = the section)
    if ((tok.kind == TOK_OF || tok.kind == TOK_IN) &&
        (peek_token().kind == TOK_IDENT || peek_token().kind == TOK_NUMBER)) {
        next();
        char *sec = tok_to_str();
        proc->extra = ast_new(AST_IDENTIFIER, sec, (SourceLocation){current_lexer->filename, tok.line, tok.column});
        free(sec);
        next();
    }
    return proc;
}

// Parse receiving identifiers, each with optional ROUNDED. Returns the count.
static int parse_receivers(AstNode **head, AstNode **tail) {
    int n = 0;
    while (tok.kind == TOK_IDENT && !parser_panic_mode) {
        AstNode *op = parse_arith_operand();
        if (tok.kind == TOK_ROUNDED) {
            op->rounded = true;
            next();
        }
        list_append(head, tail, op);
        n++;
    }
    return n;
}

// Parse operands that may be receivers (identifiers with ROUNDED) or, if a
// GIVING follows, sending operands. Returns the count; *all_idents tells
// whether every one was an identifier.
static int parse_operands_or_receivers(AstNode **head, AstNode **tail, bool *all_idents) {
    int n = 0;
    *all_idents = true;
    AstNode *op;
    while (!parser_panic_mode && (op = parse_arith_operand())) {
        if (op->kind != AST_IDENTIFIER) *all_idents = false;
        if (tok.kind == TOK_ROUNDED) {
            op->rounded = true;
            next();
        }
        list_append(head, tail, op);
        n++;
    }
    return n;
}

static bool is_scope_terminator(TokenKind kind) {
    return kind == TOK_END_IF || kind == TOK_END_ADD || kind == TOK_END_SUBTRACT ||
           kind == TOK_END_MULTIPLY || kind == TOK_END_DIVIDE || kind == TOK_END_COMPUTE ||
           kind == TOK_END_PERFORM || kind == TOK_END_EVALUATE || kind == TOK_END_SEARCH ||
           kind == TOK_END_READ || kind == TOK_END_WRITE || kind == TOK_END_REWRITE;
}

// True where a statement sequence ends: end of sentence, a phrase keyword of
// an enclosing statement, a scope terminator, or a procedure name (a word or
// an all-digit name; no statement starts with a number).
static bool at_block_end(void) {
    return tok.kind == TOK_PERIOD || tok.kind == TOK_EOF || tok.kind == TOK_ELSE ||
           tok.kind == TOK_NOT || tok.kind == TOK_WHEN || tok.kind == TOK_IDENT ||
           tok.kind == TOK_NUMBER ||
           is_scope_terminator(tok.kind) ||
           is_division_start(tok.kind);
}

// Parse a (possibly empty) sequence of statements up to at_block_end().
static AstNode *parse_block(void) {
    AstNode *head = NULL, *tail = NULL;
    while (!at_block_end() && !parser_panic_mode) {
        AstNode *stmt = parse_statement();
        if (!stmt) break;
        list_append(&head, &tail, stmt);
    }
    return head;
}

// Parse imperative statements up to the end of a conditional phrase.
static AstNode *parse_imperative_block(void) {
    AstNode *head = parse_block();
    if (!head) {
        cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                         "Expected an imperative statement, got '%.*s'", (int)tok.length, tok.lexeme);
        SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
        head = ast_new(AST_STATEMENT_CONTINUE, NULL, loc);
    }
    return head;
}

// [ON] SIZE ERROR imperative  [NOT [ON] SIZE ERROR imperative]  [end_tok]
static void parse_size_error_phrases(AstNode *stmt, TokenKind end_tok) {
    if (tok.kind == TOK_ON || tok.kind == TOK_SIZE) {
        if (tok.kind == TOK_ON) next();
        expect(TOK_SIZE, "SIZE");
        expect(TOK_ERROR, "ERROR");
        if (parser_panic_mode) return;
        stmt->on_error = parse_imperative_block();
    }
    if (tok.kind == TOK_NOT) {
        next();
        if (tok.kind == TOK_ON) next();
        expect(TOK_SIZE, "SIZE");
        expect(TOK_ERROR, "ERROR");
        if (parser_panic_mode) return;
        stmt->not_on_error = parse_imperative_block();
    }
    if (tok.kind == end_tok) next();
}

// True if an INSPECT operand (identifier, literal, figurative) starts here
static bool inspect_operand_start(void) {
    return tok.kind == TOK_IDENT || tok.kind == TOK_STRING || tok.kind == TOK_NUMBER ||
           is_figurative(tok.kind);
}

// [{BEFORE|AFTER} [INITIAL] operand]... of an INSPECT operand
static bool parse_inspect_phrases(AstNode *item) {
    while (tok.kind == TOK_BEFORE || tok.kind == TOK_AFTER) {
        bool before = tok.kind == TOK_BEFORE;
        next();
        if (tok.kind == TOK_INITIAL) next();
        AstNode *delim = parse_value_operand();
        if (!delim) { arith_error("Expected BEFORE/AFTER operand"); return false; }
        if (before) { ast_free(item->not_on_error); item->not_on_error = delim; }
        else { ast_free(item->children); item->children = delim; }
    }
    return true;
}

static AstNode *parse_statement(void) {
    if (parser_panic_mode) return NULL; // Don't try to parse statements if in panic mode

    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};

    if (tok.kind == TOK_DISPLAY) {
        next();
        AstNode *display = ast_new(AST_STATEMENT_DISPLAY, NULL, loc);
        AstNode *last = NULL, *arg;
        while (!parser_panic_mode && (arg = parse_value_operand()))
            list_append(&display->children, &last, arg);
        return display;
    }
    else if (tok.kind == TOK_STOP) {
        next();
        expect(TOK_RUN, "RUN");
        if (parser_panic_mode) return NULL;
        return ast_new(AST_STATEMENT_STOP, NULL, loc);
    }
    // ─── MOVE ────────────────────────────────────────────────────────────────
    // MOVE source TO identifier+; children: [source, dest1,...]
    else if (tok.kind == TOK_MOVE) {
        next();
        AstNode *move = ast_new(AST_STATEMENT_MOVE, NULL, loc);
        AstNode *tail = NULL;
        if (tok.kind == TOK_CORRESPONDING) {
            // MOVE CORRESPONDING group TO group; name = "CORR"
            next();
            move->name = strdup("CORR");
            if (tok.kind != TOK_IDENT) { ast_free(move); arith_error("Expected group after CORRESPONDING"); return NULL; }
        }
        AstNode *src = parse_value_operand();
        if (!src) { ast_free(move); arith_error("Expected MOVE source"); return NULL; }
        list_append(&move->children, &tail, src);
        if (tok.kind != TOK_TO) { ast_free(move); arith_error("Expected TO"); return NULL; }
        next();
        while (tok.kind == TOK_IDENT && !parser_panic_mode)
            list_append(&move->children, &tail, parse_identifier());
        if (!src->next) { ast_free(move); arith_error("Expected MOVE receiver"); return NULL; }
        return move;
    }
    // ─── ADD ─────────────────────────────────────────────────────────────────
    // Format 1: ADD src+ TO dest+ [ROUNDED]
    // Format 2: ADD src+ [TO src] GIVING dest+ [ROUNDED]
    // children: [src1,...,srcN, dest1,...]; decimal_places = N
    // is_numeric = GIVING form
    else if (tok.kind == TOK_ADD) {
        next();
        AstNode *add = ast_new(AST_STATEMENT_ADD, NULL, loc);
        AstNode *head = NULL, *tail = NULL, *op;
        int num_src = 0, num_dest = 0;
        if (tok.kind == TOK_CORRESPONDING) {
            // ADD CORRESPONDING group TO group [ROUNDED]; name = "CORR"
            next();
            add->name = strdup("CORR");
        }

        while (!parser_panic_mode && (op = parse_arith_operand())) {
            list_append(&head, &tail, op);
            num_src++;
        }
        if (tok.kind == TOK_TO) {
            next();
            bool all_idents;
            AstNode *to_head = NULL, *to_tail = NULL;
            int n_to = parse_operands_or_receivers(&to_head, &to_tail, &all_idents);
            if (to_head) {
                if (!head) head = to_head;
                else tail->next = to_head;
                tail = to_tail;
            }
            if (tok.kind == TOK_GIVING) {
                next();
                add->is_numeric = true;
                num_src += n_to;
                num_dest = parse_receivers(&head, &tail);
            } else {
                if (!all_idents) { ast_free(head); ast_free(add); arith_error("ADD ... TO requires identifiers as receivers"); return NULL; }
                num_dest = n_to;
            }
        } else if (tok.kind == TOK_GIVING) {
            next();
            add->is_numeric = true;
            num_dest = parse_receivers(&head, &tail);
        }
        add->children = head;
        add->decimal_places = num_src;
        if (num_src == 0 || num_dest == 0) { ast_free(add); arith_error("Incomplete ADD statement"); return NULL; }

        parse_size_error_phrases(add, TOK_END_ADD);
        return add;
    }
    // ─── SUBTRACT ────────────────────────────────────────────────────────────
    // Format 1: SUBTRACT src+ FROM dest+ [ROUNDED]
    // Format 2: SUBTRACT src+ FROM minuend GIVING dest+ [ROUNDED]
    // children: [src1,...,srcN, dest1,...] or [src1,...,srcN, minuend, dest1,...]
    // decimal_places = N; is_numeric = GIVING form
    else if (tok.kind == TOK_SUBTRACT) {
        next();
        AstNode *sub = ast_new(AST_STATEMENT_SUBTRACT, NULL, loc);
        AstNode *head = NULL, *tail = NULL, *op;
        int num_src = 0, num_dest = 0;
        if (tok.kind == TOK_CORRESPONDING) {
            // SUBTRACT CORRESPONDING group FROM group [ROUNDED]; name = "CORR"
            next();
            sub->name = strdup("CORR");
        }

        while (!parser_panic_mode && (op = parse_arith_operand())) {
            list_append(&head, &tail, op);
            num_src++;
        }
        sub->children = head;
        sub->decimal_places = num_src;
        if (num_src == 0 || tok.kind != TOK_FROM) { ast_free(sub); arith_error("Expected FROM in SUBTRACT statement"); return NULL; }
        next();

        bool all_idents;
        AstNode *from_head = NULL, *from_tail = NULL;
        int n_from = parse_operands_or_receivers(&from_head, &from_tail, &all_idents);
        if (from_head) {
            if (!head) sub->children = head = from_head;
            else tail->next = from_head;
            tail = from_tail;
        }
        if (tok.kind == TOK_GIVING) {
            next();
            if (n_from != 1) { ast_free(sub); arith_error("SUBTRACT ... GIVING takes exactly one FROM operand"); return NULL; }
            sub->is_numeric = true;
            num_dest = parse_receivers(&head, &tail);
        } else {
            if (!all_idents) { ast_free(sub); arith_error("SUBTRACT ... FROM requires identifiers as receivers"); return NULL; }
            num_dest = n_from;
        }
        if (num_dest == 0) { ast_free(sub); arith_error("Expected receiving identifier in SUBTRACT statement"); return NULL; }

        parse_size_error_phrases(sub, TOK_END_SUBTRACT);
        return sub;
    }
    // ─── MULTIPLY ────────────────────────────────────────────────────────────
    // Format 1: MULTIPLY factor BY dest+ [ROUNDED]
    // Format 2: MULTIPLY factor BY factor2 GIVING dest+ [ROUNDED]
    // children: [factor, dest1,...] or [factor, factor2, dest1,...]
    // is_numeric = GIVING form
    else if (tok.kind == TOK_MULTIPLY) {
        next();
        AstNode *mul = ast_new(AST_STATEMENT_MULTIPLY, NULL, loc);
        AstNode *head = NULL, *tail = NULL;

        AstNode *factor = parse_arith_operand();
        if (!factor) { ast_free(mul); arith_error("Expected operand after MULTIPLY"); return NULL; }
        list_append(&head, &tail, factor);
        mul->children = head;
        mul->decimal_places = 1;
        if (tok.kind != TOK_BY) { ast_free(mul); arith_error("Expected BY in MULTIPLY statement"); return NULL; }
        next();

        bool all_idents;
        int n_by = parse_operands_or_receivers(&head, &tail, &all_idents);
        int num_dest;
        if (tok.kind == TOK_GIVING) {
            next();
            if (n_by != 1) { ast_free(mul); arith_error("MULTIPLY ... GIVING takes exactly one BY operand"); return NULL; }
            mul->is_numeric = true;
            num_dest = parse_receivers(&head, &tail);
        } else {
            if (!all_idents) { ast_free(mul); arith_error("MULTIPLY ... BY requires identifiers as receivers"); return NULL; }
            num_dest = n_by;
        }
        if (num_dest == 0) { ast_free(mul); arith_error("Expected receiving identifier in MULTIPLY statement"); return NULL; }

        parse_size_error_phrases(mul, TOK_END_MULTIPLY);
        return mul;
    }
    // ─── DIVIDE ──────────────────────────────────────────────────────────────
    // Format 1: DIVIDE divisor INTO dest+ [ROUNDED]
    // Format 2: DIVIDE divisor INTO dividend GIVING dest+ [ROUNDED] [REMAINDER r]
    // Format 3: DIVIDE dividend BY divisor GIVING dest+ [ROUNDED] [REMAINDER r]
    // name = "INTO" or "BY"; is_numeric = GIVING form
    // is_signed = REMAINDER present (last child is the remainder receiver)
    // children (INTO, no GIVING): [divisor, dest1,...]
    // children (INTO, GIVING):    [divisor, dividend, dest1,..., [rem]]
    // children (BY,  GIVING):     [dividend, divisor, dest1,..., [rem]]
    else if (tok.kind == TOK_DIVIDE) {
        next();
        AstNode *div_stmt = ast_new(AST_STATEMENT_DIVIDE, NULL, loc);
        AstNode *head = NULL, *tail = NULL;

        AstNode *first = parse_arith_operand();
        if (!first) { ast_free(div_stmt); arith_error("Expected operand after DIVIDE"); return NULL; }
        list_append(&head, &tail, first);
        div_stmt->children = head;

        bool is_by = (tok.kind == TOK_BY);
        if (tok.kind != TOK_INTO && !is_by) { ast_free(div_stmt); arith_error("Expected INTO or BY in DIVIDE statement"); return NULL; }
        div_stmt->name = strdup(is_by ? "BY" : "INTO");
        div_stmt->decimal_places = is_by ? 2 : 1;
        next();

        bool all_idents;
        int n_second = parse_operands_or_receivers(&head, &tail, &all_idents);
        int num_dest;
        if (tok.kind == TOK_GIVING) {
            next();
            if (n_second != 1) { ast_free(div_stmt); arith_error("DIVIDE ... GIVING takes exactly one operand before GIVING"); return NULL; }
            div_stmt->is_numeric = true;
            num_dest = parse_receivers(&head, &tail);
            if (tok.kind == TOK_REMAINDER) {
                next();
                if (num_dest != 1) { ast_free(div_stmt); arith_error("REMAINDER requires exactly one GIVING receiver"); return NULL; }
                if (parse_receivers(&head, &tail) != 1) { ast_free(div_stmt); arith_error("Expected one identifier after REMAINDER"); return NULL; }
                div_stmt->is_signed = true;
            }
        } else {
            if (is_by) { ast_free(div_stmt); arith_error("DIVIDE ... BY requires GIVING"); return NULL; }
            if (!all_idents) { ast_free(div_stmt); arith_error("DIVIDE ... INTO requires identifiers as receivers"); return NULL; }
            num_dest = n_second;
        }
        if (num_dest == 0) { ast_free(div_stmt); arith_error("Expected receiving identifier in DIVIDE statement"); return NULL; }

        parse_size_error_phrases(div_stmt, TOK_END_DIVIDE);
        return div_stmt;
    }
    // ─── IF ──────────────────────────────────────────────────────────────────
    // IF condition [THEN] {statements | NEXT SENTENCE}
    //    [ELSE {statements | NEXT SENTENCE}] [END-IF]
    // A period ends every open IF; ELSE pairs with the nearest open IF.
    // children: [condition, THEN-BLOCK, [ELSE-BLOCK]]
    else if (tok.kind == TOK_IF) {
        next();
        AstNode *if_stmt = ast_new(AST_STATEMENT_IF, NULL, loc);
        AstNode *cond = parse_condition();
        if (!cond) { ast_free(if_stmt); return NULL; }
        if_stmt->children = cond;

        if (tok.kind == TOK_THEN) next();
        AstNode *then_node = ast_new(AST_THEN_BLOCK, NULL, loc);
        then_node->children = parse_block();
        cond->next = then_node;

        if (tok.kind == TOK_ELSE) {
            next();
            AstNode *else_node = ast_new(AST_ELSE_BLOCK, NULL, loc);
            else_node->children = parse_block();
            then_node->next = else_node;
        }
        if (tok.kind == TOK_END_IF) next();
        return if_stmt;
    }
    // ─── PERFORM ─────────────────────────────────────────────────────────────
    // Out-of-line: PERFORM proc [THRU proc] [loop]
    // Inline:      PERFORM [loop] statements END-PERFORM
    // loop: n TIMES | [WITH TEST BEFORE|AFTER] UNTIL condition
    //     | [WITH TEST BEFORE|AFTER] VARYING id FROM x BY y UNTIL condition
    //       {AFTER id FROM x BY y UNTIL condition}...
    // name: ONCE, TIMES, UNTIL or VARYING; extra: procedure range (first, then
    // THRU procedure); loop: count operand, condition, or VARYING clauses;
    // is_signed: WITH TEST AFTER; children: inline statements
    else if (tok.kind == TOK_PERFORM) {
        next();
        AstNode *perf = ast_new(AST_STATEMENT_PERFORM, NULL, loc);
        const char *form = "ONCE";

        if (tok.kind == TOK_IDENT ||
            (is_procedure_name_token() && peek_token().kind != TOK_TIMES)) {
            AstNode *first = parse_procedure_name();
            if (tok.kind == TOK_TIMES && first->name && !isdigit((unsigned char)first->name[0])) {
                // Inline PERFORM identifier TIMES: the name is a data item
                if (!symtab_resolve_symbol(current_symtab, first->name))
                    cobalt_error_loc(current_lexer->original_source, first->loc.line, first->loc.column,
                                     "Undeclared identifier '%s'.", first->name);
                next();
                perf->loop = first;
                form = "TIMES";
            } else {
                perf->extra = first;
                if (tok.kind == TOK_THROUGH) {
                    next();
                    if (!is_procedure_name_token()) { ast_free(perf); arith_error("Expected procedure name after THRU"); return NULL; }
                    first->next = parse_procedure_name();
                }
            }
        }

        if (strcmp(form, "ONCE") == 0) {
            if (tok.kind == TOK_NUMBER || tok.kind == TOK_IDENT) {
                perf->loop = parse_arith_operand();
                if (tok.kind != TOK_TIMES) { ast_free(perf); arith_error("Expected TIMES"); return NULL; }
                next();
                form = "TIMES";
            } else if (tok.kind == TOK_WITH || tok.kind == TOK_TEST || tok.kind == TOK_UNTIL ||
                       tok.kind == TOK_VARYING) {
                if (tok.kind == TOK_WITH) next();
                if (tok.kind == TOK_TEST) {
                    next();
                    if (tok.kind == TOK_AFTER) perf->is_signed = true;
                    else if (tok.kind != TOK_BEFORE) { ast_free(perf); arith_error("Expected BEFORE or AFTER"); return NULL; }
                    next();
                }
                if (tok.kind == TOK_UNTIL) {
                    next();
                    perf->loop = parse_condition();
                    if (!perf->loop) { ast_free(perf); return NULL; }
                    form = "UNTIL";
                } else if (tok.kind == TOK_VARYING) {
                    AstNode *tail = NULL;
                    while (tok.kind == TOK_VARYING || (perf->loop && tok.kind == TOK_AFTER)) {
                        SourceLocation vloc = {current_lexer->filename, tok.line, tok.column};
                        next();
                        AstNode *var = parse_arith_operand();
                        if (!var || var->kind != AST_IDENTIFIER || tok.kind != TOK_FROM) { ast_free(var); ast_free(perf); arith_error("Expected identifier FROM in VARYING"); return NULL; }
                        next();
                        AstNode *from = parse_arith_operand();
                        if (!from || tok.kind != TOK_BY) { ast_free(var); ast_free(from); ast_free(perf); arith_error("Expected BY in VARYING"); return NULL; }
                        next();
                        AstNode *by = parse_arith_operand();
                        if (!by || tok.kind != TOK_UNTIL) { ast_free(var); ast_free(from); ast_free(by); ast_free(perf); arith_error("Expected UNTIL in VARYING"); return NULL; }
                        next();
                        AstNode *cond = parse_condition();
                        if (!cond) { ast_free(var); ast_free(from); ast_free(by); ast_free(perf); return NULL; }
                        AstNode *clause = ast_new(AST_VARYING, NULL, vloc);
                        clause->children = var;
                        var->next = from;
                        from->next = by;
                        by->next = cond;
                        list_append(&perf->loop, &tail, clause);
                    }
                    form = "VARYING";
                } else {
                    ast_free(perf);
                    arith_error("Expected UNTIL or VARYING");
                    return NULL;
                }
            }
        }
        perf->name = strdup(form);

        if (!perf->extra) {
            perf->children = parse_block();
            if (tok.kind != TOK_END_PERFORM) { ast_free(perf); arith_error("Expected END-PERFORM"); return NULL; }
            next();
        }
        return perf;
    }
    // ─── GO TO ───────────────────────────────────────────────────────────────
    // GO [TO] proc  |  GO [TO] proc+ DEPENDING [ON] identifier
    // children: procedure names; extra: DEPENDING ON identifier
    else if (tok.kind == TOK_GO) {
        next();
        if (tok.kind == TOK_TO) next();
        AstNode *go = ast_new(AST_STATEMENT_GOTO, NULL, loc);
        AstNode *tail = NULL;
        while (is_procedure_name_token()) list_append(&go->children, &tail, parse_procedure_name());
        if (!go->children) { ast_free(go); arith_error("Expected procedure name after GO TO"); return NULL; }
        if (tok.kind == TOK_DEPENDING) {
            next();
            if (tok.kind == TOK_ON) next();
            go->extra = parse_arith_operand();
            if (!go->extra) { ast_free(go); arith_error("Expected identifier after DEPENDING ON"); return NULL; }
        } else if (go->children->next) {
            ast_free(go);
            arith_error("GO TO with several procedures requires DEPENDING ON");
            return NULL;
        }
        return go;
    }
    else if (tok.kind == TOK_NEXT) {
        next();
        expect(TOK_SENTENCE, "SENTENCE");
        return ast_new(AST_STATEMENT_NEXT_SENTENCE, NULL, loc);
    }
    else if (tok.kind == TOK_EXIT) {
        next();
        AstNode *exit_stmt = ast_new(AST_STATEMENT_EXIT, NULL, loc);
        if (tok.kind == TOK_IDENT && tok.length == 7 && strncasecmp(tok.lexeme, "PROGRAM", 7) == 0) {
            exit_stmt->name = strdup("PROGRAM");
            next();
        }
        return exit_stmt;
    }
    // ─── EVALUATE ────────────────────────────────────────────────────────────
    // EVALUATE subject {ALSO subject}...
    //   {WHEN object {ALSO object}...}... statements ... [WHEN OTHER statements]
    // [END-EVALUATE]
    // Each WHEN group is turned into one condition here (WHEN lines of a group
    // are ORed, ALSO objects ANDed), so lowering is a chain of IFs.
    // children: AST_WHEN nodes (extra = condition, NULL for OTHER)
    else if (tok.kind == TOK_EVALUATE) {
        next();
        AstNode *eval = ast_new(AST_STATEMENT_EVALUATE, NULL, loc);
        AstNode *subjects[16];
        int nsub = 0;
        do {
            if (nsub > 0) next(); // ALSO
            if (nsub == 16) { ast_free(eval); arith_error("Too many EVALUATE subjects"); return NULL; }
            subjects[nsub] = parse_evaluate_subject();
            if (!subjects[nsub]) { ast_free(eval); free_subjects(subjects, nsub); return NULL; }
            nsub++;
        } while (tok.kind == TOK_ALSO);

        AstNode *when_tail = NULL;
        bool seen_other = false;
        while (tok.kind == TOK_WHEN && !seen_other) {
            SourceLocation wloc = {current_lexer->filename, tok.line, tok.column};
            AstNode *group = NULL;
            while (tok.kind == TOK_WHEN) {
                next();
                if (tok.kind == TOK_OTHER) {
                    next();
                    seen_other = true;
                    break;
                }
                AstNode *alt = NULL;
                for (int k = 0; k < nsub; k++) {
                    if (k > 0) {
                        if (tok.kind != TOK_ALSO) { ast_free(alt); ast_free(group); ast_free(eval); free_subjects(subjects, nsub); arith_error("Expected ALSO"); return NULL; }
                        next();
                    }
                    AstNode *obj = parse_when_object(subjects[k]);
                    if (!obj) { ast_free(alt); ast_free(group); ast_free(eval); free_subjects(subjects, nsub); return NULL; }
                    alt = alt ? make_cond(AST_COND_AND, alt, obj, alt->loc) : obj;
                }
                group = group ? make_cond(AST_COND_OR, group, alt, group->loc) : alt;
            }
            AstNode *when = ast_new(AST_WHEN, NULL, wloc);
            when->extra = seen_other ? (ast_free(group), NULL) : group;
            when->children = parse_block();
            list_append(&eval->children, &when_tail, when);
        }
        free_subjects(subjects, nsub);
        if (!eval->children) { ast_free(eval); arith_error("Expected WHEN"); return NULL; }
        if (tok.kind == TOK_END_EVALUATE) next();
        return eval;
    }
    // ─── SET ─────────────────────────────────────────────────────────────────
    // SET condition-name+ TO TRUE          (name TRUE)
    // SET identifier+ TO operand           (name TO)
    // SET index-name+ UP|DOWN BY operand   (name UP / DOWN)
    // children: receivers; extra: sending operand
    else if (tok.kind == TOK_SET) {
        next();
        AstNode *set = ast_new(AST_STATEMENT_SET, NULL, loc);
        AstNode *tail = NULL;
        while (tok.kind == TOK_IDENT && !parser_panic_mode)
            list_append(&set->children, &tail, parse_identifier());
        if (!set->children) { ast_free(set); arith_error("Expected identifier after SET"); return NULL; }
        if (tok.kind == TOK_TO) {
            next();
            if (tok.kind == TOK_TRUE) {
                next();
                set->name = strdup("TRUE");
                for (AstNode *c = set->children; c; c = c->next) {
                    if (!is_condition_name(c))
                        cobalt_error_loc(current_lexer->original_source, c->loc.line, c->loc.column,
                                         "'%s' is not a condition-name", c->name);
                }
                return set;
            }
            set->name = strdup("TO");
        } else if (tok.kind == TOK_UP || tok.kind == TOK_DOWN) {
            set->name = strdup(tok.kind == TOK_UP ? "UP" : "DOWN");
            next();
            if (tok.kind != TOK_BY) { ast_free(set); arith_error("Expected BY"); return NULL; }
            next();
        } else {
            ast_free(set);
            arith_error("Expected TO, UP BY or DOWN BY");
            return NULL;
        }
        set->extra = parse_arith_operand();
        if (!set->extra) { ast_free(set); arith_error("Expected SET operand"); return NULL; }
        return set;
    }
    // ─── INITIALIZE ──────────────────────────────────────────────────────────
    // INITIALIZE identifier+ (numeric items to zero, others to spaces)
    else if (tok.kind == TOK_INITIALIZE) {
        next();
        AstNode *init = ast_new(AST_STATEMENT_INITIALIZE, NULL, loc);
        AstNode *tail = NULL;
        while (tok.kind == TOK_IDENT && !parser_panic_mode)
            list_append(&init->children, &tail, parse_identifier());
        if (!init->children) { ast_free(init); arith_error("Expected identifier after INITIALIZE"); return NULL; }
        return init;
    }
    // ─── SEARCH ──────────────────────────────────────────────────────────────
    // SEARCH table [VARYING identifier] [AT END statements]
    //     {WHEN condition statements}... [END-SEARCH]
    // SEARCH ALL table [AT END statements] WHEN condition statements [END-SEARCH]
    else if (tok.kind == TOK_SEARCH) {
        next();
        AstNode *search = ast_new(AST_STATEMENT_SEARCH, NULL, loc);
        if (tok.kind == TOK_ALL) {
            next();
            search->name = strdup("ALL");
        }
        if (tok.kind != TOK_IDENT) { ast_free(search); arith_error("Expected table name after SEARCH"); return NULL; }
        search->extra = parse_identifier();
        if (tok.kind == TOK_VARYING) {
            next();
            search->loop = parse_arith_operand();
        }
        if (tok.kind == TOK_AT || tok.kind == TOK_END) {
            if (tok.kind == TOK_AT) next();
            if (tok.kind != TOK_END) { ast_free(search); arith_error("Expected END after AT"); return NULL; }
            next();
            search->on_error = parse_block();
            if (!search->on_error) search->on_error = ast_new(AST_STATEMENT_CONTINUE, NULL, loc);
        }
        AstNode *tail = NULL;
        while (tok.kind == TOK_WHEN && !parser_panic_mode) {
            SourceLocation wloc = {current_lexer->filename, tok.line, tok.column};
            next();
            AstNode *when = ast_new(AST_WHEN, NULL, wloc);
            when->extra = parse_condition();
            if (!when->extra) { ast_free(when); ast_free(search); return NULL; }
            when->children = parse_block();
            list_append(&search->children, &tail, when);
        }
        if (!search->children) { ast_free(search); arith_error("Expected WHEN in SEARCH"); return NULL; }
        if (tok.kind == TOK_END_SEARCH) next();
        return search;
    }
    // ─── INSPECT ─────────────────────────────────────────────────────────────
    // INSPECT id [TALLYING {counter FOR {CHARACTERS [phrases] |
    //                                    {ALL|LEADING} {operand [phrases]}...}...}...]
    //            [REPLACING {CHARACTERS BY operand [phrases] |
    //                        {ALL|LEADING|FIRST} {operand BY operand [phrases]}...}...]
    // INSPECT id CONVERTING operand TO operand [phrases]
    // phrases: [{BEFORE|AFTER} [INITIAL] operand]...
    else if (tok.kind == TOK_INSPECT) {
        next();
        AstNode *insp = ast_new(AST_STATEMENT_INSPECT, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(insp); arith_error("Expected identifier after INSPECT"); return NULL; }
        insp->extra = parse_identifier();
        AstNode *tail = NULL;
        if (tok.kind == TOK_CONVERTING) {
            next();
            AstNode *item = ast_new(AST_INSPECT_ITEM, "CONVERT", loc);
            item->extra = parse_value_operand();
            if (!item->extra || tok.kind != TOK_TO) { ast_free(item); ast_free(insp); arith_error("Expected CONVERTING operand TO operand"); return NULL; }
            next();
            item->on_error = parse_value_operand();
            if (!item->on_error || !parse_inspect_phrases(item)) { ast_free(item); ast_free(insp); return NULL; }
            list_append(&insp->children, &tail, item);
            return insp;
        }
        if (tok.kind == TOK_TALLYING) {
            next();
            while (tok.kind == TOK_IDENT) {
                AstNode *counter = parse_identifier();
                if (tok.kind != TOK_FOR) { ast_free(counter); ast_free(insp); arith_error("Expected FOR"); return NULL; }
                next();
                const char *kind = NULL;
                while (true) {
                    if (tok.kind == TOK_CHARACTERS) kind = "CHARACTERS";
                    else if (tok.kind == TOK_ALL) kind = "ALL";
                    else if (tok.kind == TOK_LEADING) kind = "LEADING";
                    if (tok.kind == TOK_CHARACTERS || tok.kind == TOK_ALL || tok.kind == TOK_LEADING) next();
                    else if (!kind) break;
                    else if (strcmp(kind, "CHARACTERS") == 0 || !inspect_operand_start()) break;
                    AstNode *item = ast_new(AST_INSPECT_ITEM, kind, loc);
                    item->loop = ast_clone(counter);
                    if (strcmp(kind, "CHARACTERS") != 0) {
                        item->extra = parse_value_operand();
                        if (!item->extra) { ast_free(item); ast_free(counter); ast_free(insp); arith_error("Expected TALLYING operand"); return NULL; }
                    }
                    if (!parse_inspect_phrases(item)) { ast_free(item); ast_free(counter); ast_free(insp); return NULL; }
                    list_append(&insp->children, &tail, item);
                    if (strcmp(kind, "CHARACTERS") == 0) kind = NULL;
                    if (!inspect_operand_start() && tok.kind != TOK_CHARACTERS &&
                        tok.kind != TOK_ALL && tok.kind != TOK_LEADING) break;
                    if (tok.kind == TOK_IDENT && peek_token().kind == TOK_FOR) break;  // next counter
                }
                ast_free(counter);
            }
        }
        if (tok.kind == TOK_REPLACING) {
            next();
            const char *kind = NULL;
            while (true) {
                if (tok.kind == TOK_CHARACTERS) kind = "CHARACTERS";
                else if (tok.kind == TOK_ALL) kind = "ALL";
                else if (tok.kind == TOK_LEADING) kind = "LEADING";
                else if (tok.kind == TOK_FIRST) kind = "FIRST";
                if (tok.kind == TOK_CHARACTERS || tok.kind == TOK_ALL || tok.kind == TOK_LEADING || tok.kind == TOK_FIRST) next();
                else if (!kind || strcmp(kind, "CHARACTERS") == 0 || !inspect_operand_start()) break;
                AstNode *item = ast_new(AST_INSPECT_ITEM, kind, loc);
                if (strcmp(kind, "CHARACTERS") != 0) {
                    item->extra = parse_value_operand();
                    if (!item->extra) { ast_free(item); ast_free(insp); arith_error("Expected REPLACING operand"); return NULL; }
                }
                if (tok.kind != TOK_BY) { ast_free(item); ast_free(insp); arith_error("Expected BY"); return NULL; }
                next();
                item->on_error = parse_value_operand();
                if (!item->on_error || !parse_inspect_phrases(item)) { ast_free(item); ast_free(insp); arith_error("Expected replacement operand"); return NULL; }
                list_append(&insp->children, &tail, item);
                if (strcmp(kind, "CHARACTERS") == 0) kind = NULL;
            }
        }
        if (!insp->children) { ast_free(insp); arith_error("Expected TALLYING, REPLACING or CONVERTING"); return NULL; }
        return insp;
    }
    // ─── OPEN {INPUT|OUTPUT|I-O|EXTEND} file... ...  CLOSE file... ──────────
    else if (tok.kind == TOK_OPEN || tok.kind == TOK_CLOSE) {
        bool is_open = tok.kind == TOK_OPEN;
        next();
        AstNode *stmt = ast_new(is_open ? AST_STATEMENT_OPEN : AST_STATEMENT_CLOSE, NULL, loc);
        AstNode *tail = NULL;
        int mode = 0;
        while (!parser_panic_mode) {
            if (is_open && (tok.kind == TOK_INPUT || tok.kind == TOK_OUTPUT ||
                            tok.kind == TOK_I_O || tok.kind == TOK_EXTEND)) {
                mode = tok.kind == TOK_INPUT ? 1 : tok.kind == TOK_OUTPUT ? 2 : tok.kind == TOK_I_O ? 3 : 4;
                next();
            } else if (tok.kind == TOK_IDENT && (!is_open || mode)) {
                AstNode *file = parse_procedure_name();
                file->decimal_places = mode;
                list_append(&stmt->children, &tail, file);
                // CLOSE options: REEL/UNIT, WITH NO REWIND, WITH LOCK
                while (!is_open && (tok_is_word("REEL") || tok_is_word("UNIT") || tok.kind == TOK_WITH ||
                                    tok_is_word("NO") || tok_is_word("REWIND") || tok_is_word("LOCK") ||
                                    tok.kind == TOK_FOR || tok_is_word("REMOVAL"))) next();
            } else {
                break;
            }
        }
        if (!stmt->children) { ast_free(stmt); arith_error(is_open ? "Expected OPEN mode and file" : "Expected file name"); return NULL; }
        return stmt;
    }
    // ─── READ file [NEXT] [RECORD] [INTO id] [AT END s] [NOT AT END s] ───────
    else if (tok.kind == TOK_READ) {
        next();
        AstNode *read = ast_new(AST_STATEMENT_READ, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(read); arith_error("Expected file name after READ"); return NULL; }
        read->extra = parse_procedure_name();
        if (tok.kind == TOK_NEXT) next();
        if (tok.kind == TOK_RECORD) next();
        if (tok.kind == TOK_INTO) {
            next();
            if (tok.kind != TOK_IDENT) { ast_free(read); arith_error("Expected identifier after INTO"); return NULL; }
            read->loop = parse_identifier();
        }
        if (tok.kind == TOK_AT || tok.kind == TOK_END) {
            if (tok.kind == TOK_AT) next();
            if (tok.kind != TOK_END) { ast_free(read); arith_error("Expected END"); return NULL; }
            next();
            read->on_error = parse_imperative_block();
        }
        if (tok.kind == TOK_NOT) {
            next();
            if (tok.kind == TOK_AT) next();
            if (tok.kind != TOK_END) { ast_free(read); arith_error("Expected END"); return NULL; }
            next();
            read->not_on_error = parse_imperative_block();
        }
        if (tok.kind == TOK_END_READ) next();
        return read;
    }
    // ─── WRITE record [FROM id] [{BEFORE|AFTER} [ADVANCING] {n [LINES] | PAGE}]
    //     [[AT] END-OF-PAGE s] [NOT [AT] END-OF-PAGE s] [END-WRITE]
    // ─── REWRITE record [FROM id] [END-REWRITE]
    else if (tok.kind == TOK_WRITE || tok.kind == TOK_REWRITE) {
        bool is_write = tok.kind == TOK_WRITE;
        next();
        AstNode *w = ast_new(is_write ? AST_STATEMENT_WRITE : AST_STATEMENT_REWRITE, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(w); arith_error("Expected record name"); return NULL; }
        w->extra = parse_identifier();
        if (tok.kind == TOK_FROM) {
            next();
            w->loop = parse_value_operand();
            if (!w->loop) { ast_free(w); arith_error("Expected identifier after FROM"); return NULL; }
        }
        if (is_write && (tok.kind == TOK_BEFORE || tok.kind == TOK_AFTER)) {
            w->name = strdup(tok.kind == TOK_BEFORE ? "BEFORE" : "AFTER");
            next();
            if (tok.kind == TOK_ADVANCING) next();
            if (tok.kind == TOK_PAGE) {
                w->children = ast_new(AST_BOOL_LITERAL, "PAGE", loc);
                next();
            } else {
                w->children = parse_arith_operand();
                if (!w->children) { ast_free(w); arith_error("Expected line count or PAGE"); return NULL; }
                if (tok.kind == TOK_LINE || tok.kind == TOK_LINES) next();
            }
        }
        if (is_write && (tok.kind == TOK_AT || tok.kind == TOK_END_OF_PAGE)) {
            if (tok.kind == TOK_AT) next();
            next();
            w->on_error = parse_imperative_block();     // never raised without LINAGE
        }
        if (is_write && tok.kind == TOK_NOT && peek_token().kind != TOK_EOF) {
            next();
            if (tok.kind == TOK_AT) next();
            next();
            w->not_on_error = parse_imperative_block();
        }
        if (tok.kind == TOK_END_WRITE || tok.kind == TOK_END_REWRITE) next();
        return w;
    }
    else if (tok.kind == TOK_ACCEPT) {
        next(); 
        AstNode *accept_stmt = ast_new(AST_STATEMENT_ACCEPT, NULL, loc);

        if (tok.kind == TOK_IDENT) {
            accept_stmt->children = parse_identifier();
            if (tok.kind == TOK_FROM) {
                // FROM DATE | DAY | DAY-OF-WEEK | TIME, or a device (the console)
                next();
                if (tok.kind == TOK_DATE) accept_stmt->name = strdup("DATE");
                else if (tok.kind == TOK_DAY) accept_stmt->name = strdup("DAY");
                else if (tok.kind == TOK_DAY_OF_WEEK) accept_stmt->name = strdup("DAY-OF-WEEK");
                else if (tok.kind == TOK_TIME) accept_stmt->name = strdup("TIME");
                else if (tok.kind != TOK_IDENT) { ast_free(accept_stmt); arith_error("Expected DATE, DAY, DAY-OF-WEEK, TIME or a device"); return NULL; }
                next();
            }
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "ACCEPT statement requires a variable name");
            synchronize(); // Error, enter panic mode
            return NULL;
        }

        return accept_stmt;
    }
    else if (tok.kind == TOK_CONTINUE) {
        next();
        return ast_new(AST_STATEMENT_CONTINUE, NULL, loc);
    }
    // ─── COMPUTE ─────────────────────────────────────────────────────────────
    // COMPUTE dest+ [ROUNDED] = expression
    // children: [expression, dest1,...]
    else if (tok.kind == TOK_COMPUTE) {
        next();
        AstNode *compute = ast_new(AST_STATEMENT_COMPUTE, NULL, loc);
        AstNode *dest_head = NULL, *dest_tail = NULL;

        if (parse_receivers(&dest_head, &dest_tail) == 0) {
            ast_free(compute);
            arith_error("COMPUTE: expected receiving identifier");
            return NULL;
        }
        if (tok.kind != TOK_EQ) {
            ast_free(dest_head);
            ast_free(compute);
            arith_error("COMPUTE: expected '='");
            return NULL;
        }
        next();

        AstNode *expr = parse_expr();
        if (!expr) {
            ast_free(dest_head);
            ast_free(compute);
            arith_error("COMPUTE: expected arithmetic expression");
            return NULL;
        }
        expr->next = dest_head;
        compute->children = expr;

        parse_size_error_phrases(compute, TOK_END_COMPUTE);
        return compute;
    }

    if (tok.kind != TOK_EOF) {
        cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Skipping unknown statement starting with '%.*s'", (int)tok.length, tok.lexeme);
        synchronize(); // Try to recover
    }
    return NULL;
}

// Sentence: statements up to a period. AST_SENTENCE with the statements as children.
static AstNode *parse_sentence(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    AstNode *sent = ast_new(AST_SENTENCE, NULL, loc);
    AstNode *tail = NULL;
    int errors_before = error_count;

    while (!at_block_end() && !parser_panic_mode) {
        AstNode *stmt = parse_statement();
        if (!stmt) break;
        list_append(&sent->children, &tail, stmt);
    }
    if (tok.kind == TOK_PERIOD) {
        next();
    } else if (tok.kind != TOK_IDENT && tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
        // A token that cannot continue the sentence (e.g. a stray ELSE)
        if (error_count == errors_before)
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Unexpected '%.*s'", (int)tok.length, tok.lexeme);
        synchronize();
        if (tok.kind != TOK_EOF && !sent->children && !parser_panic_mode && at_block_end() &&
            tok.kind != TOK_IDENT && !is_division_start(tok.kind)) next();
    } else if (error_count == errors_before && sent->children) {
        cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                         "Expected '.' at end of sentence, got '%.*s'", (int)tok.length, tok.lexeme);
    }
    parser_panic_mode = false;
    return sent;
}

static void declare_procedure(const char *name, SymbolKind kind, SourceLocation loc) {
    // Paragraph names may repeat in different sections; IR lowering resolves them
    if (!symtab_resolve_symbol(current_symtab, name))
        symtab_declare_symbol(current_symtab, name, kind, loc);
}

// PROCEDURE DIVISION body: [sentences] then sections and/or paragraphs.
// Division children: leading sentences, AST_SECTION and AST_PARAGRAPH nodes.
// A section's children: its leading sentences and its paragraphs.
// A paragraph's children: its sentences.
static void parse_procedure_body(AstNode *div) {
    AstNode *div_tail = NULL, *sec = NULL, *sec_tail = NULL;

    while (tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
        const char *before = tok.lexeme;
        AstNode *node = NULL;
        if (is_procedure_name_token()) {
            SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
            char *name = tok_to_str();
            next();
            if (tok.kind == TOK_SECTION) {
                next();
                expect(TOK_PERIOD, ".");
                parser_panic_mode = false;
                sec = ast_new(AST_SECTION, name, loc);
                sec_tail = NULL;
                declare_procedure(name, SYM_SECTION, loc);
                list_append(&div->children, &div_tail, sec);
                free(name);
                continue;
            }
            expect(TOK_PERIOD, ".");
            parser_panic_mode = false;
            node = ast_new(AST_PARAGRAPH, name, loc);
            declare_procedure(name, SYM_PARAGRAPH, loc);
            free(name);
            AstNode *ptail = NULL;
            while (!is_procedure_name_token() && tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
                const char *at = tok.lexeme;
                list_append(&node->children, &ptail, parse_sentence());
                if (tok.lexeme == at && tok.kind != TOK_EOF) next();   // always make progress
            }
        } else {
            node = parse_sentence();
        }
        if (sec) list_append(&sec->children, &sec_tail, node);
        else list_append(&div->children, &div_tail, node);
        if (tok.lexeme == before && tok.kind != TOK_EOF) next();       // always make progress
    }
}

// DATA DIVISION section body: data description entries
static AstNode *parse_section(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    expect(TOK_SECTION, "SECTION");
    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) return NULL;

    AstNode *sec = ast_new(AST_SECTION, NULL, loc);
    AstNode *last = NULL;

    while (tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
           tok.kind != TOK_FILE && tok.kind != TOK_LINKAGE && tok.kind != TOK_WORKING_STORAGE) {
        const char *before = tok.lexeme;
        parser_panic_mode = false;
        if (tok.kind == TOK_NUMBER) {
            AstNode *decl = parse_var_decl();
            if (decl) list_append(&sec->children, &last, decl);
        } else if (tok.kind == TOK_PERIOD) {
            next();
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected a data description entry, got '%.*s'", (int)tok.length, tok.lexeme);
            while (tok.kind != TOK_EOF && tok.kind != TOK_PERIOD && !is_division_start(tok.kind)) next();
        }
        if (tok.lexeme == before && tok.kind != TOK_EOF) next();   // always make progress
    }

    return sec;
}

static bool tok_is_word(const char *word) {
    size_t n = strlen(word);
    return tok.kind == TOK_IDENT && tok.length == n && strncasecmp(tok.lexeme, word, n) == 0;
}

// SELECT [OPTIONAL] file ASSIGN [TO] name [ORGANIZATION [IS] [LINE|RECORD]
//   SEQUENTIAL] [ACCESS [MODE] [IS] SEQUENTIAL] [[FILE] STATUS [IS] item]
//   [RESERVE n [AREA|AREAS]] ... .
static AstNode *parse_select(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    next();
    AstNode *sel = ast_new(AST_SELECT, NULL, loc);
    if (tok.kind == TOK_OPTIONAL) {
        sel->is_signed = true;
        next();
    }
    if (tok.kind != TOK_IDENT) { ast_free(sel); arith_error("Expected file name after SELECT"); return NULL; }
    free(sel->name);
    sel->name = tok_to_str();
    if (!symtab_resolve_symbol(current_symtab, sel->name)) {
        Symbol *fs = symtab_declare_symbol(current_symtab, sel->name, SYM_VAR, loc);
        if (fs) symtab_mark_symbol_as_used(fs);
    }
    next();
    if (tok.kind != TOK_ASSIGN) { ast_free(sel); arith_error("Expected ASSIGN"); return NULL; }
    next();
    if (tok.kind == TOK_TO) next();
    if (tok.kind == TOK_STRING || tok.kind == TOK_IDENT) {
        sel->is_numeric = tok.kind == TOK_STRING;
        sel->text = tok_value();
        next();
    } else {
        ast_free(sel);
        arith_error("Expected file name or literal after ASSIGN");
        return NULL;
    }
    while (tok.kind == TOK_IDENT || tok.kind == TOK_STRING) next();  // further ASSIGN names

    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
        if (tok.kind == TOK_ORGANIZATION || tok.kind == TOK_LINE || tok.kind == TOK_SEQUENTIAL ||
            tok.kind == TOK_INDEXED || tok.kind == TOK_RELATIVE ||
            (tok.kind == TOK_RECORD && peek_token().kind == TOK_SEQUENTIAL)) {
            if (tok.kind == TOK_ORGANIZATION) next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind == TOK_LINE) { sel->decimal_places = 1; next(); }
            else if (tok.kind == TOK_RECORD) next();
            if (tok.kind == TOK_INDEXED || tok.kind == TOK_RELATIVE) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "%s files are not supported yet", tok.kind == TOK_INDEXED ? "INDEXED" : "RELATIVE");
                next();
                continue;
            }
            if (tok.kind != TOK_SEQUENTIAL) { ast_free(sel); arith_error("Expected SEQUENTIAL"); return NULL; }
            next();
        } else if (tok.kind == TOK_ACCESS) {
            next();
            if (tok.kind == TOK_MODE) next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind == TOK_RANDOM || tok.kind == TOK_DYNAMIC)
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "Only ACCESS MODE SEQUENTIAL is supported yet");
            next();
            if (tok.kind == TOK_RECORD || tok.kind == TOK_RELATIVE) {
                // RECORD KEY / RELATIVE KEY of indexed and relative files
                while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && tok.kind != TOK_ACCESS &&
                       tok.kind != TOK_FILE && tok.kind != TOK_STATUS) next();
            }
        } else if (tok.kind == TOK_FILE || tok.kind == TOK_STATUS) {
            if (tok.kind == TOK_FILE) next();
            if (tok.kind != TOK_STATUS) { ast_free(sel); arith_error("Expected STATUS"); return NULL; }
            next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_IDENT) { ast_free(sel); arith_error("Expected FILE STATUS data item"); return NULL; }
            // Declared later in the DATA DIVISION: resolved during IR lowering
            sel->extra = parse_procedure_name();
        } else {
            next();     // RESERVE, PADDING, RECORD DELIMITER: no effect here
        }
    }
    if (tok.kind == TOK_PERIOD) next();
    return sel;
}

// FILE SECTION. {FD file-name [clauses]. record-description-entries}...
static AstNode *parse_file_section(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    next();                                 // FILE
    expect(TOK_SECTION, "SECTION");
    expect(TOK_PERIOD, ".");
    parser_panic_mode = false;
    AstNode *sec = ast_new(AST_SECTION, "FILE", loc);
    AstNode *fd_tail = NULL;

    while (tok.kind == TOK_FD || tok.kind == TOK_SD) {
        SourceLocation fd_loc = {current_lexer->filename, tok.line, tok.column};
        bool sort_file = tok.kind == TOK_SD;
        next();
        if (tok.kind != TOK_IDENT) { arith_error("Expected file name after FD"); break; }
        char *name = tok_to_str();
        if (sort_file)
            cobalt_error_loc(current_lexer->original_source, fd_loc.line, fd_loc.column,
                             "SD (sort files) is not supported yet");
        AstNode *fd = ast_new(AST_FILE_DECL, name, fd_loc);
        free(name);
        next();
        // BLOCK, RECORD, LABEL, VALUE OF, DATA RECORDS, CODE-SET: no effect here
        while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF &&
               !(is_division_start(tok.kind) && peek_token().kind == TOK_DIVISION)) {
            if (tok.kind == TOK_LINAGE)
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "LINAGE is not supported yet");
            next();
        }
        if (tok.kind == TOK_PERIOD) next();

        AstNode *tail = NULL;
        while (tok.kind == TOK_NUMBER) {
            const char *before = tok.lexeme;
            parser_panic_mode = false;
            AstNode *decl = parse_var_decl();
            if (decl) list_append(&fd->children, &tail, decl);
            if (tok.lexeme == before) next();
        }
        list_append(&sec->children, &fd_tail, fd);
    }
    return sec;
}

static AstNode *parse_division(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    char *name = tok_to_str();
    next();
    expect(TOK_DIVISION, "DIVISION");
    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) { free(name); return NULL; }

    AstNode *div = ast_new(AST_DIVISION, name, loc);
    free(name);

    if (strcasecmp(div->name, "IDENTIFICATION") == 0) {
        expect(TOK_PROGRAM_ID, "PROGRAM-ID");
        expect(TOK_PERIOD, ".");
        char *prog_name_str = tok_to_str();
        expect(TOK_IDENT, "program name");
        expect(TOK_PERIOD, ".");
        div->text = prog_name_str; // Assign to AST node

        // Declare the PROGRAM-ID in the symbol table
        symtab_declare_symbol(current_symtab, prog_name_str, SYM_PROGRAM, (SourceLocation){current_lexer->filename, tok.line, tok.column});

        AstNode *last_para = NULL;
        while (tok.kind == TOK_IDENT && 
               !is_division_start(tok.kind) &&
               !parser_panic_mode) {
            AstNode *para = parse_identification_paragraph();
            if (para) {
                if (!div->children) div->children = para;
                else last_para->next = para;
                last_para = para;
            } else {
                if (!parser_panic_mode && tok.kind != TOK_EOF) next(); // Skip unknown token
            }
        }
    }
    else if (strcasecmp(div->name, "PROCEDURE") == 0) {
        parse_procedure_body(div);
    }
    else if (strcasecmp(div->name, "ENVIRONMENT") == 0) {
        // CONFIGURATION SECTION and I-O-CONTROL entries do not affect the
        // generated code; FILE-CONTROL entries become AST_SELECT children
        AstNode *sel_tail = NULL;
        bool in_file_control = false;
        while (tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
            if (tok_is_word("FILE-CONTROL")) {
                next();
                if (tok.kind == TOK_PERIOD) next();
                in_file_control = true;
                continue;
            }
            if (tok_is_word("I-O-CONTROL") || tok_is_word("CONFIGURATION") || tok_is_word("INPUT-OUTPUT"))
                in_file_control = false;
            if (in_file_control && tok.kind == TOK_SELECT) {
                AstNode *sel = parse_select();
                if (sel) list_append(&div->children, &sel_tail, sel);
                continue;
            }
            if (tok_is_word("SPECIAL-NAMES"))
                cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column,
                                "SPECIAL-NAMES entries are ignored");
            next();
        }
    }
    else if (strcasecmp(div->name, "DATA") == 0) {
        // FILE SECTION, WORKING-STORAGE SECTION, LINKAGE SECTION in this order
        AstNode *sec_tail = NULL;
        while (tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
            if (tok.kind == TOK_WORKING_STORAGE) {
                next();
                AstNode *sec = parse_section();
                if (sec) sec->name = strdup("WORKING-STORAGE");
                if (sec) list_append(&div->children, &sec_tail, sec);
                continue;
            }
            if (tok.kind == TOK_FILE) {
                AstNode *sec = parse_file_section();
                if (sec) list_append(&div->children, &sec_tail, sec);
                continue;
            }
            const char *what = tok.kind == TOK_LINKAGE ? "LINKAGE SECTION" : NULL;
            if (what) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "%s is not supported yet", what);
            } else {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "Expected a DATA DIVISION section, got '%.*s'", (int)tok.length, tok.lexeme);
            }
            // Skip to the next section of this division
            next();
            while (tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
                   tok.kind != TOK_WORKING_STORAGE && tok.kind != TOK_FILE && tok.kind != TOK_LINKAGE) next();
        }
    }

    return div;
}

AstNode *parse_program(Lexer *l) {
    current_lexer = l; 
    parser_panic_mode = false;
    reset_diagnostics(); // Reset counters for this compilation
    next();

    // Initialize and push global scope
    current_symtab = symtab_init();

    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    AstNode *prog = ast_new(AST_PROGRAM, NULL, loc);
    AstNode *last_div = NULL;
    int has_proc = 0;
    int error_count_before = error_count;

    if (tok.kind == TOK_IDENTIFICATION) {
        AstNode *div = parse_division();
        if (div) {
            prog->children = div;
            last_div = div;

            if (div->text) {
                prog->name = div->text;
                div->text = NULL;
            }
        }
    } else {
        cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Missing IDENTIFICATION DIVISION");
        synchronize();
    }

    while (tok.kind != TOK_EOF && !parser_panic_mode) {
        if (is_division_start(tok.kind)) {
             AstNode *div = parse_division();
             if (div) {
                if (last_div) last_div->next = div;
                else prog->children = div;
                last_div = div;
                if (strcasecmp(div->name, "PROCEDURE") == 0) has_proc = 1;
             }
        } else if (tok.kind == TOK_PERIOD) { // Consume standalone periods that might be after COPY statements
            next(); // Consume the period
        }
        else {
            // Unexpected token at global scope, outside a division; always
            // consume it, so recovery cannot stop on the same token again
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected a DIVISION start");
            next();
            while (tok.kind != TOK_EOF && !is_division_start(tok.kind)) next();
        }
    }

    if (!has_proc && error_count == error_count_before) { // Only report if no other errors prevented parsing PROCEDURE
        cobalt_error("Missing PROCEDURE DIVISION");
    }
    
    symtab_check_unused_symbols(current_symtab); // Check for unused symbols
    symtab_free(current_symtab); // Free the symbol table
    return prog;
}