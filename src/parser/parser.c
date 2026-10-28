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

    char *name = NULL;
    if (tok.kind == TOK_IDENT) {
        name = tok_to_str();
        next();
    } else {
        free(level_str);
        synchronize(); // No identifier after level, error
        return NULL;
    }

    AstNode *var = ast_new(AST_VAR_DECL, name, loc);
    // Declare the symbol
    Symbol *sym = symtab_declare_symbol(current_symtab, name, SYM_VAR, loc);
    if (sym) {
        sym->level = level_num;
    }

    free(name);
    var->text = level_str; // Keep level_str in AST node for now, or just free it.
    AstNode *tail = NULL;

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
        } else if (tok.kind == TOK_USAGE || usage_from_token(tok.kind, &usage)) {
            if (tok.kind == TOK_USAGE) {
                next();
                if (tok.kind == TOK_IS) next();
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
            char *val = tok_to_str();
            AstNode *val_node = NULL;
            SourceLocation val_loc = {current_lexer->filename, tok.line, tok.column};
            if (tok.kind == TOK_STRING) {
                val_node = ast_new(AST_STRING_LITERAL, val, val_loc);
            } else if (tok.kind == TOK_NUMBER) {
                val_node = ast_new(AST_LITERAL_NUMBER, val, val_loc);
            } else if (tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) {
                val_node = ast_new(AST_FIGURATIVE_CONSTANT, val, val_loc);
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

static AstNode *parse_statement(bool require_period);

// Forward declarations for recursive-descent expression parser (used by COMPUTE)
static AstNode *parse_expr(void);
static AstNode *parse_term(void);
static AstNode *parse_factor(void);

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

    if (tok.kind == TOK_IDENT) {
        char *name = tok_to_str();
        AstNode *node = ast_new(AST_IDENTIFIER, name, loc);
        Symbol *sym = symtab_resolve_symbol(current_symtab, name);
        if (sym) symtab_mark_symbol_as_used(sym);
        else cobalt_error_loc(current_lexer->original_source, loc.line, loc.column,
                              "Undeclared identifier '%s' in expression", name);
        free(name);
        next();
        return node;
    }

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

// ── Arithmetic statement helpers ─────────────────────────────────────────────

// Parse an arithmetic operand (identifier or numeric literal).
// Returns NULL, consuming nothing, if the token is not an operand.
static AstNode *parse_arith_operand(void) {
    if (tok.kind != TOK_IDENT && tok.kind != TOK_NUMBER) return NULL;
    char *val = tok_to_str();
    if (tok.kind == TOK_IDENT && !symtab_resolve_symbol(current_symtab, val))
        cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                         "Undeclared identifier '%s'.", val);
    AstNode *op = make_operand_node(val, tok.kind);
    free(val);
    next();
    return op;
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
           kind == TOK_END_MULTIPLY || kind == TOK_END_DIVIDE || kind == TOK_END_COMPUTE;
}

// Parse imperative statements up to the end of a conditional phrase.
static AstNode *parse_imperative_block(void) {
    AstNode *head = NULL, *tail = NULL;
    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && tok.kind != TOK_ELSE &&
           tok.kind != TOK_NOT && !is_scope_terminator(tok.kind) && !parser_panic_mode) {
        AstNode *stmt = parse_statement(false);
        if (!stmt) break;
        list_append(&head, &tail, stmt);
    }
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

static void arith_error(const char *msg) {
    cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                     "%s, got '%.*s'", msg, (int)tok.length, tok.lexeme);
    synchronize();
}

static AstNode *parse_statement(bool require_period) {
    if (parser_panic_mode) return NULL; // Don't try to parse statements if in panic mode

    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};

    if (tok.kind == TOK_DISPLAY) {
        next();
        AstNode *display = ast_new(AST_STATEMENT_DISPLAY, NULL, loc);
        AstNode *last = NULL;

        while ((tok.kind == TOK_STRING || tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER ||
                tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) && !parser_panic_mode) {
            {
                char *val = tok_to_str();
                AstNode *arg = NULL;
                if (tok.kind == TOK_STRING) {
                    arg = ast_new(AST_STRING_LITERAL, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
                } else if (tok.kind == TOK_NUMBER) {
                    arg = ast_new(AST_LITERAL_NUMBER, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
                } else if (tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) {
                    arg = ast_new(AST_FIGURATIVE_CONSTANT, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
                            } else { 
                                arg = ast_new(AST_IDENTIFIER, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
                                // Resolve and mark as used
                                Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, val);
                                if (!resolved_sym) {
                                    cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Undeclared identifier '%s'.", val);
                                } else {
                                    symtab_mark_symbol_as_used(resolved_sym);
                                    // Optionally, attach symbol to AST node for later use
                                    // arg->symbol = resolved_sym; 
                                }
                            }                free(val);
                if (!display->children) display->children = arg;
                else last->next = arg;
                last = arg;
            }
            next();
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return display;
    }
    else if (tok.kind == TOK_STOP) {
        next();
        expect(TOK_RUN, "RUN");
        if (parser_panic_mode) return NULL;
        if (require_period) expect(TOK_PERIOD, ".");
        return ast_new(AST_STATEMENT_STOP, NULL, loc);
    }
    else if (tok.kind == TOK_MOVE) {
        next();
        AstNode *move = ast_new(AST_STATEMENT_MOVE, NULL, loc);

        if (tok.kind == TOK_STRING || tok.kind == TOK_NUMBER || tok.kind == TOK_IDENT ||
            tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) { 
            char *val = tok_to_str();
            AstNode *src = NULL;
            if (tok.kind == TOK_STRING) {
                src = ast_new(AST_STRING_LITERAL, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
            } else if (tok.kind == TOK_NUMBER) {
                src = ast_new(AST_LITERAL_NUMBER, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
            } else if (tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) {
                src = ast_new(AST_FIGURATIVE_CONSTANT, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
            } else { 
                src = ast_new(AST_IDENTIFIER, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
                // Resolve and mark source as used
                Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, val);
                if (!resolved_sym) {
                    cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Undeclared identifier '%s'.", val);
                } else {
                    symtab_mark_symbol_as_used(resolved_sym);
                    // src->symbol = resolved_sym; 
                }
            }
            free(val);
            move->children = src;
            next();
        }

        expect(TOK_TO, "TO");
        if (parser_panic_mode) return NULL;

        // Collect one or more target identifiers
        if (tok.kind != TOK_IDENT) {
            synchronize();
            return NULL;
        }
        AstNode *tgt_tail = move->children; // currently points to src; we'll extend it
        while (tok.kind == TOK_IDENT && !parser_panic_mode) {
            char *target = tok_to_str();
            AstNode *tgt = ast_new(AST_IDENTIFIER, target,
                                   (SourceLocation){current_lexer->filename, tok.line, tok.column});
            Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, target);
            if (!resolved_sym) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Undeclared identifier '%s'.", target);
            } else {
                symtab_mark_symbol_as_used(resolved_sym);
            }
            free(target);
            tgt_tail->next = tgt;
            tgt_tail = tgt;
            next();
        }

        if (require_period) expect(TOK_PERIOD, ".");
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
        if (require_period) expect(TOK_PERIOD, ".");
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
        if (require_period) expect(TOK_PERIOD, ".");
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
        if (require_period) expect(TOK_PERIOD, ".");
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
        if (require_period) expect(TOK_PERIOD, ".");
        return div_stmt;
    }
    else if (tok.kind == TOK_IF) {
        next();
        AstNode *if_stmt = ast_new(AST_STATEMENT_IF, NULL, loc);

        AstNode *cond = ast_new(AST_CONDITION, NULL, loc);
        if_stmt->children = cond;

        if (tok.kind == TOK_IDENT) {
            char *left = tok_to_str();
            cond->children = ast_new(AST_IDENTIFIER, left, (SourceLocation){current_lexer->filename, tok.line, tok.column});
            // Resolve and mark left operand as used
            Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, left);
            if (!resolved_sym) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Undeclared identifier '%s'.", left);
            } else {
                symtab_mark_symbol_as_used(resolved_sym);
                // cond->children->symbol = resolved_sym;
            }
            free(left);
            next();
        } else {
            synchronize(); // Missing left operand for IF
            return NULL;
        }

        const char *op = NULL;
        if (tok.kind == TOK_GT) { op = ">"; next(); }
        else if (tok.kind == TOK_LT) { op = "<"; next(); }
        else if (tok.kind == TOK_GE) { op = ">="; next(); }
        else if (tok.kind == TOK_LE) { op = "<="; next(); }
        else if (tok.kind == TOK_EQ) { op = "="; next(); }
        else if (tok.kind == TOK_NE) { op = "<>"; next(); }
        else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected comparison operator, got '%.*s'", (int)tok.length, tok.lexeme);
            synchronize();
            return NULL;
        }
        if (op) cond->text = strdup(op);

        if (tok.kind == TOK_NUMBER || tok.kind == TOK_IDENT) {
            char *right = tok_to_str();
            AstNode *right_node = ast_new(
                tok.kind == TOK_NUMBER ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
                right, (SourceLocation){current_lexer->filename, tok.line, tok.column}
            );
            if (tok.kind == TOK_IDENT) {
                // Resolve and mark right operand as used
                Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, right);
                if (!resolved_sym) {
                    cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Undeclared identifier '%s'.", right);
                } else {
                    symtab_mark_symbol_as_used(resolved_sym);
                    // right_node->symbol = resolved_sym;
                }
            }
            free(right);
            cond->children->next = right_node;
            next();
        } else {
            synchronize(); // Missing right operand for IF
            return NULL;
        }

        if (tok.kind == TOK_THEN) next();

        AstNode *then_block = NULL;
        AstNode *last_then = NULL;
        while (tok.kind != TOK_ELSE && tok.kind != TOK_END_IF && tok.kind != TOK_EOF && !parser_panic_mode) { 
            AstNode *stmt = parse_statement(false);  
            if (stmt) {
                if (!then_block) then_block = stmt;
                else last_then->next = stmt;
                last_then = stmt;
            } else {
                next(); // Skip unknown token in statement block
            }
        }
        if (parser_panic_mode) return NULL;

        if (then_block) {
            AstNode *then_node = ast_new(AST_THEN_BLOCK, NULL, loc);
            then_node->children = then_block;
            cond->next = then_node;

            if (tok.kind == TOK_ELSE) {
                next();
                AstNode *else_block = NULL;
                AstNode *last_else = NULL;
                while (tok.kind != TOK_END_IF && tok.kind != TOK_EOF && !parser_panic_mode) { 
                    AstNode *stmt = parse_statement(false);
                    if (stmt) {
                        if (!else_block) else_block = stmt;
                        else last_else->next = stmt;
                        last_else = stmt;
                    } else {
                        next();
                    }
                }
                if (parser_panic_mode) return NULL;

                AstNode *else_node = ast_new(AST_ELSE_BLOCK, NULL, loc);
                else_node->children = else_block;
                then_node->next = else_node;
            }
        } else if (tok.kind == TOK_ELSE) {
            // THEN block was empty; still parse and discard the ELSE block
            next();
            while (tok.kind != TOK_END_IF && tok.kind != TOK_EOF && !parser_panic_mode)
                next();
        }

        expect(TOK_END_IF, "END-IF");
        if (require_period) expect(TOK_PERIOD, ".");
        return if_stmt;
    }
    else if (tok.kind == TOK_PERFORM) {
        next();
        AstNode *perf = ast_new(AST_STATEMENT_PERFORM, NULL, loc);

        if (tok.kind != TOK_IDENT) {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "PERFORM statement requires a paragraph name");
            synchronize();
            return NULL;
        }

        char *para = tok_to_str();
        AstNode *target = ast_new(AST_IDENTIFIER, para, (SourceLocation){current_lexer->filename, tok.line, tok.column});
        // Allow forward references: downgrade "not found" to a warning so forward
        // PERFORMs (where the paragraph is declared later) still compile.
        Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, para);
        if (resolved_sym) {
            symtab_mark_symbol_as_used(resolved_sym);
            if (resolved_sym->kind != SYM_PARAGRAPH && resolved_sym->kind != SYM_SECTION && resolved_sym->kind != SYM_PROGRAM) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "'%s' is not a paragraph, section, or program-id and cannot be PERFORMed.", para);
            }
        }
        // (unresolved symbols are forward references; the IR will validate)
        free(para);
        perf->children = target;
        next();

        // Optional count TIMES: PERFORM para N TIMES  or  PERFORM para var TIMES
        if (tok.kind == TOK_UNTIL) {
            // PERFORM para UNTIL condition
            next(); // consume UNTIL
            perf->name = strdup("UNTIL");

            AstNode *cond = ast_new(AST_CONDITION, NULL, loc);

            // Left operand
            if (tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER) {
                char *left_val = tok_to_str();
                AstNode *left_node = ast_new(
                    tok.kind == TOK_NUMBER ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
                    left_val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
                if (tok.kind == TOK_IDENT) {
                    Symbol *s = symtab_resolve_symbol(current_symtab, left_val);
                    if (s) symtab_mark_symbol_as_used(s);
                }
                free(left_val);
                cond->children = left_node;
                next();
            } else {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "PERFORM UNTIL: expected left operand");
                synchronize();
                return NULL;
            }

            // Comparison operator
            const char *op = NULL;
            if      (tok.kind == TOK_GT) { op = ">";  next(); }
            else if (tok.kind == TOK_LT) { op = "<";  next(); }
            else if (tok.kind == TOK_GE) { op = ">="; next(); }
            else if (tok.kind == TOK_LE) { op = "<="; next(); }
            else if (tok.kind == TOK_EQ) { op = "=";  next(); }
            else if (tok.kind == TOK_NE) { op = "<>"; next(); }
            else {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "PERFORM UNTIL: expected comparison operator");
                synchronize();
                return NULL;
            }
            cond->text = strdup(op);

            // Right operand
            if (tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER) {
                char *right_val = tok_to_str();
                AstNode *right_node = ast_new(
                    tok.kind == TOK_NUMBER ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
                    right_val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
                if (tok.kind == TOK_IDENT) {
                    Symbol *s = symtab_resolve_symbol(current_symtab, right_val);
                    if (s) symtab_mark_symbol_as_used(s);
                }
                free(right_val);
                cond->children->next = right_node;
                next();
            } else {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "PERFORM UNTIL: expected right operand");
                synchronize();
                return NULL;
            }

            target->next = cond;

        } else if (tok.kind == TOK_NUMBER || tok.kind == TOK_IDENT) {
            char *count_val = tok_to_str();
            bool count_is_num = (tok.kind == TOK_NUMBER);
            next(); // consume count

            if (tok.kind == TOK_TIMES) {
                AstNode *count_node = ast_new(
                    count_is_num ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
                    count_val, (SourceLocation){current_lexer->filename, tok.line, tok.column}
                );
                if (!count_is_num) {
                    Symbol *cnt_sym = symtab_resolve_symbol(current_symtab, count_val);
                    if (cnt_sym) symtab_mark_symbol_as_used(cnt_sym);
                }
                target->next = count_node;
                perf->is_numeric = true; // flag: PERFORM TIMES form
                next(); // consume TIMES
            } else {
                cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column,
                                "Unsupported PERFORM variant; ignoring extra tokens");
                free(count_val);
                synchronize();
                return perf;
            }
            free(count_val);
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return perf;
    }
    else if (tok.kind == TOK_ACCEPT) {
        next(); 
        AstNode *accept_stmt = ast_new(AST_STATEMENT_ACCEPT, NULL, loc);

        if (tok.kind == TOK_IDENT) {
            char *var_name = tok_to_str();
            AstNode *target = ast_new(AST_IDENTIFIER, var_name, (SourceLocation){current_lexer->filename, tok.line, tok.column});
            // Resolve and mark ACCEPT target as used
            Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, var_name);
            if (!resolved_sym) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Undeclared identifier '%s'.", var_name);
            } else {
                symtab_mark_symbol_as_used(resolved_sym);
                // target->symbol = resolved_sym;
            }
            free(var_name);
            accept_stmt->children = target;
            next();
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "ACCEPT statement requires a variable name");
            synchronize(); // Error, enter panic mode
            return NULL;
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return accept_stmt;
    }
    else if (tok.kind == TOK_CONTINUE) {
        next();
        if (require_period) expect(TOK_PERIOD, ".");
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
        if (require_period) expect(TOK_PERIOD, ".");
        return compute;
    }

    if (tok.kind != TOK_EOF) {
        cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Skipping unknown statement starting with '%.*s'", (int)tok.length, tok.lexeme);
        synchronize(); // Try to recover
    }
    return NULL;
}

static AstNode *parse_paragraph(void) {
    if (tok.kind != TOK_IDENT) return NULL;
    if (parser_panic_mode) return NULL; // Don't parse if in panic mode

    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
        char *name = tok_to_str();
        next();
        expect(TOK_PERIOD, ".");
        if (parser_panic_mode) { free(name); return NULL; }
    
        AstNode *para = ast_new(AST_PARAGRAPH, name, loc);
        symtab_declare_symbol(current_symtab, name, SYM_PARAGRAPH, loc);
        free(name);

    AstNode *last = NULL;
    while (tok.kind != TOK_EOF &&
           tok.kind != TOK_IDENT && tok.kind != TOK_SECTION &&
           !is_division_start(tok.kind) && // Division starts also end a paragraph
           tok.kind != TOK_END && !parser_panic_mode) {
        AstNode *stmt = parse_statement(true);
        if (stmt) {
            if (!para->children) para->children = stmt;
            else last->next = stmt;
            last = stmt;
        } else {
            // If parse_statement failed but didn't cause panic,
            // we should still advance to avoid infinite loop on bad tokens.
            if (!parser_panic_mode && tok.kind != TOK_EOF) next();
        }
    }

    return para;
}

static AstNode *parse_section(void) {
    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};
    expect(TOK_SECTION, "SECTION");
    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) return NULL;

    AstNode *sec = ast_new(AST_SECTION, NULL, loc);
    AstNode *last = NULL;

    while (tok.kind != TOK_EOF &&
           !is_division_start(tok.kind) && // Division starts end a section
           tok.kind != TOK_IDENTIFICATION && // Explicit check for safety
           !parser_panic_mode) {
        
        // Try parsing a variable declaration
        AstNode *decl = parse_var_decl();
        if (decl) {
            if (!sec->children) sec->children = decl;
            else last->next = decl;
            last = decl;
            continue;
        }

        // Consume periods that might be standalone after included sections/declarations
        if (tok.kind == TOK_PERIOD) {
            next(); // Consume the period
            continue;
        }

        // Try parsing a paragraph
        AstNode *para = parse_paragraph();
        if (para) {
            if (!sec->children) sec->children = para;
            else last->next = para;
            last = para;
        } else {
            // If neither worked, try a single statement
            AstNode *stmt = parse_statement(true);
            if (stmt) {
                if (!sec->children) sec->children = stmt;
                else last->next = stmt;
                last = stmt;
            } else {
                // If all parsing attempts failed, advance to avoid infinite loop
                if (!parser_panic_mode && tok.kind != TOK_EOF) next();
            }
        }
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
        AstNode *last = NULL;
        while (tok.kind != TOK_EOF &&
               !is_division_start(tok.kind) && // Division starts end a procedure
               !parser_panic_mode) {
            AstNode *para = parse_paragraph();
            if (para) {
                if (!div->children) div->children = para;
                else last->next = para;
                last = para;
            } else {
                AstNode *stmt = parse_statement(true);
                if (stmt) {
                    if (!div->children) div->children = stmt;
                    else last->next = stmt;
                    last = stmt;
                } else {
                     if (!parser_panic_mode && tok.kind != TOK_EOF) next(); // Skip unknown token
                }
            }
        }
    }
    else if (strcasecmp(div->name, "DATA") == 0) {
        AstNode *sec = NULL;
        if (tok.kind == TOK_WORKING_STORAGE) {
            SourceLocation ws_loc = {current_lexer->filename, tok.line, tok.column};
            next(); // Consume TOK_WORKING_STORAGE
            symtab_declare_symbol(current_symtab, "WORKING-STORAGE", SYM_SECTION, ws_loc); // Declare WORKING-STORAGE as a section
            sec = parse_section();
            div->children = sec;
        } else {
            cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Only WORKING-STORAGE supported in DATA DIVISION for now");
            synchronize();
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
            // Unexpected token at global scope, outside a division
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected a DIVISION start");
            synchronize();
        }
    }

    if (!has_proc && error_count == error_count_before) { // Only report if no other errors prevented parsing PROCEDURE
        cobalt_error("Missing PROCEDURE DIVISION");
    }
    
    symtab_check_unused_symbols(current_symtab); // Check for unused symbols
    symtab_free(current_symtab); // Free the symbol table
    return prog;
}