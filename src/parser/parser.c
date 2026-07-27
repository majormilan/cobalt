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
    AstNode *current_child_tail = NULL; 

    if (tok.kind == TOK_PIC) {
        next();
        char pic_buf[64] = {0};
        size_t pos = 0;
        // Keep track of the start of the PIC string for location
        SourceLocation pic_loc = {current_lexer->filename, tok.line, tok.column}; 
        while (tok.kind != TOK_VALUE && tok.kind != TOK_USAGE && tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !parser_panic_mode) {
            char *part = tok_to_str();
            strncat(pic_buf + pos, part, sizeof(pic_buf) - pos - 1);
            pos += strlen(part);
            free(part);
            next();
        }
        AstNode *pic_node = ast_new(AST_PIC_CLAUSE, strdup(pic_buf), pic_loc);
        if (!var->children) { 
            var->children = pic_node;
        } else { 
            current_child_tail->next = pic_node;
        }
        current_child_tail = pic_node; 

        // Store PIC string in symbol and parse its attributes
        if (sym) {
            sym->pic_string = strdup(pic_buf);
            symtab_parse_pic_string(sym, sym->pic_string);
        }
    }

    if (tok.kind == TOK_USAGE) {
        next(); 

        if (tok.kind == TOK_IS) {
            next(); 
        }

        AstNode *usage_clause = ast_new(AST_USAGE_CLAUSE, NULL, loc);
        AstNode *usage_type = NULL;

        if (tok.kind == TOK_COMP) {
            next(); 
            usage_type = ast_new(AST_USAGE_TYPE_COMP, NULL, loc);
            if (sym) {
                sym->var_type = VAR_TYPE_COMP; // Update symbol type for COMP
            }
        } else if (tok.kind == TOK_DISPLAY) {
            next(); 
            usage_type = ast_new(AST_USAGE_TYPE_DISPLAY, NULL, loc);
            // Default alphanumeric, no change needed if PIC already set it
        } else {
            cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Expected COMP or DISPLAY after USAGE, got '%.*s'", (int)tok.length, tok.lexeme);
        }

        if (usage_type) {
            usage_clause->children = usage_type;
        }

        if (!var->children) { 
            var->children = usage_clause;
        } else { 
            current_child_tail->next = usage_clause;
        }
        current_child_tail = usage_clause; 
    }

    if (tok.kind == TOK_VALUE) {
        next();
        char *val = tok_to_str();
        AstNode *val_node = NULL;
        if (tok.kind == TOK_STRING) {
            val_node = ast_new(AST_STRING_LITERAL, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
        } else if (tok.kind == TOK_NUMBER) {
            val_node = ast_new(AST_LITERAL_NUMBER, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
        } else if (tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) {
            val_node = ast_new(AST_FIGURATIVE_CONSTANT, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
        } else if (tok.kind == TOK_IDENT) {
            val_node = ast_new(AST_IDENTIFIER, val, (SourceLocation){current_lexer->filename, tok.line, tok.column});
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected literal or figurative constant after VALUE, got '%.*s'", (int)tok.length, tok.lexeme);
            free(val);
            synchronize(); // Error, enter panic mode
            return NULL;
        }
        free(val);
        if (!var->children) { 
            var->children = val_node;
        } else { 
            current_child_tail->next = val_node;
        }
        current_child_tail = val_node; 
        next();
    }

    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) return NULL;
    return var;
}

static AstNode *parse_statement(bool require_period);

static AstNode *parse_statement(bool require_period) {
    if (parser_panic_mode) return NULL; // Don't try to parse statements if in panic mode

    SourceLocation loc = {current_lexer->filename, tok.line, tok.column};

    if (tok.kind == TOK_DISPLAY) {
        next();
        AstNode *display = ast_new(AST_STATEMENT_DISPLAY, NULL, loc);
        AstNode *last = NULL;

        while (tok.kind != TOK_PERIOD &&
               tok.kind != TOK_ELSE &&
               tok.kind != TOK_END_IF &&
               tok.kind != TOK_EOF && !parser_panic_mode) { // Added panic mode check
            if (tok.kind == TOK_STRING || tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER ||
                tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) { 
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
    else if (tok.kind == TOK_ADD) {
        next();
        AstNode *add = ast_new(AST_STATEMENT_ADD, NULL, loc);

        AstNode *ops_head = NULL;
        AstNode *ops_tail = NULL;
        int num_sources = 0;

        // Collect source operands (before TO or GIVING)
        while (tok.kind != TOK_TO && tok.kind != TOK_GIVING && tok.kind != TOK_PERIOD && tok.kind != TOK_EOF &&
               tok.kind != TOK_ELSE && tok.kind != TOK_END_IF && !parser_panic_mode) {
            if (tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER) {
                char *val = tok_to_str();
                AstNode *op = ast_new(
                    tok.kind == TOK_NUMBER ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
                    val, (SourceLocation){current_lexer->filename, tok.line, tok.column}
                );
                if (tok.kind == TOK_IDENT) {
                    Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, val);
                    if (!resolved_sym) {
                        cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Undeclared identifier '%s'.", val);
                    } else {
                        symtab_mark_symbol_as_used(resolved_sym);
                    }
                }
                free(val);
                if (!ops_head) ops_head = op;
                else ops_tail->next = op;
                ops_tail = op;
                num_sources++;
            }
            next();
        }
        if (parser_panic_mode) return NULL;

        // is_numeric encodes the form: false = TO, true = GIVING
        if (tok.kind == TOK_TO) {
            add->is_numeric = false;
        } else if (tok.kind == TOK_GIVING) {
            add->is_numeric = true;
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected TO or GIVING in ADD statement.");
            synchronize();
            return NULL;
        }
        // decimal_places stores the number of source operands
        add->decimal_places = num_sources;
        add->children = ops_head;
        next(); // consume TO or GIVING

        // Collect one or more target identifiers
        bool found_target = false;
        while (tok.kind == TOK_IDENT && !parser_panic_mode) {
            char *val = tok_to_str();
            AstNode *tgt = ast_new(AST_IDENTIFIER, val,
                                   (SourceLocation){current_lexer->filename, tok.line, tok.column});
            Symbol *resolved_sym = symtab_resolve_symbol(current_symtab, val);
            if (!resolved_sym) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "Undeclared identifier '%s'.", val);
            } else {
                symtab_mark_symbol_as_used(resolved_sym);
            }
            free(val);
            if (!ops_head) { ops_head = ops_tail = tgt; add->children = ops_head; }
            else { ops_tail->next = tgt; ops_tail = tgt; }
            found_target = true;
            next();
            if (tok.kind == TOK_ROUNDED) next(); // consume ROUNDED (not yet implemented)
        }

        if (!found_target) {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected at least one target identifier in ADD statement.");
            synchronize();
            return NULL;
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return add;
    }
    // ─── SUBTRACT ────────────────────────────────────────────────────────────
    // Format 1: SUBTRACT src+ FROM dest+ [ROUNDED]
    // Format 2: SUBTRACT src+ FROM operand [ROUNDED] GIVING dest+ [ROUNDED]
    // is_numeric=false → no GIVING;  is_numeric=true → GIVING
    // decimal_places   → number of subtrahend (source) operands
    // children: [sub1,...,subN, from_op1,...] or [sub1,...,subN, from_op, giving1,...]
    else if (tok.kind == TOK_SUBTRACT) {
        next();
        AstNode *sub_stmt = ast_new(AST_STATEMENT_SUBTRACT, NULL, loc);
        AstNode *ops_head = NULL, *ops_tail = NULL;
        int num_src = 0;

        while (tok.kind != TOK_FROM && tok.kind != TOK_PERIOD && tok.kind != TOK_EOF
               && tok.kind != TOK_ELSE && tok.kind != TOK_END_IF && !parser_panic_mode) {
            if (tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER) {
                char *val = tok_to_str();
                list_append(&ops_head, &ops_tail, make_operand_node(val, tok.kind));
                free(val);
                num_src++;
            }
            next();
        }
        if (parser_panic_mode) return NULL;
        if (tok.kind != TOK_FROM) {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected FROM in SUBTRACT statement.");
            synchronize(); return NULL;
        }
        next(); // consume FROM
        sub_stmt->decimal_places = num_src;
        sub_stmt->children = ops_head;

        // Collect FROM targets (and optional GIVING)
        while (tok.kind == TOK_IDENT && !parser_panic_mode) {
            char *val = tok_to_str();
            list_append(&ops_head, &ops_tail, make_operand_node(val, tok.kind));
            free(val);
            next();
            if (tok.kind == TOK_ROUNDED) next(); // skip ROUNDED
            if (tok.kind == TOK_GIVING) break;   // stop collecting FROM targets
        }

        if (tok.kind == TOK_GIVING) {
            next();
            sub_stmt->is_numeric = true;
            while (tok.kind == TOK_IDENT && !parser_panic_mode) {
                char *val = tok_to_str();
                list_append(&ops_head, &ops_tail, make_operand_node(val, tok.kind));
                free(val);
                next();
                if (tok.kind == TOK_ROUNDED) next();
            }
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return sub_stmt;
    }
    // ─── MULTIPLY ────────────────────────────────────────────────────────────
    // Format 1: MULTIPLY factor BY dest+ [ROUNDED]
    // Format 2: MULTIPLY factor BY factor2 GIVING dest+ [ROUNDED]
    // is_numeric=false → no GIVING;  is_numeric=true → GIVING
    // decimal_places=1 always (one factor before BY)
    // children: [factor, dest1,...] or [factor1, factor2, giving1,...]
    else if (tok.kind == TOK_MULTIPLY) {
        next();
        AstNode *mul_stmt = ast_new(AST_STATEMENT_MULTIPLY, NULL, loc);
        AstNode *ops_head = NULL, *ops_tail = NULL;

        if (tok.kind != TOK_IDENT && tok.kind != TOK_NUMBER) {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected operand after MULTIPLY."); synchronize(); return NULL;
        }
        char *val = tok_to_str();
        list_append(&ops_head, &ops_tail, make_operand_node(val, tok.kind));
        free(val); next();
        mul_stmt->decimal_places = 1;
        mul_stmt->children = ops_head;

        if (tok.kind != TOK_BY) {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected BY in MULTIPLY statement."); synchronize(); return NULL;
        }
        next(); // consume BY

        // Collect one or more targets (no GIVING) or one factor2 + GIVING
        while (tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER) {
            char *v = tok_to_str();
            list_append(&ops_head, &ops_tail, make_operand_node(v, tok.kind));
            free(v); next();
            if (tok.kind == TOK_ROUNDED) next();
            if (tok.kind == TOK_GIVING) break;
        }
        if (tok.kind == TOK_GIVING) {
            next();
            mul_stmt->is_numeric = true;
            while (tok.kind == TOK_IDENT && !parser_panic_mode) {
                char *v = tok_to_str();
                list_append(&ops_head, &ops_tail, make_operand_node(v, tok.kind));
                free(v); next();
                if (tok.kind == TOK_ROUNDED) next();
            }
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return mul_stmt;
    }
    // ─── DIVIDE ──────────────────────────────────────────────────────────────
    // Format 1: DIVIDE divisor INTO dest+ [ROUNDED]
    // Format 2: DIVIDE divisor INTO dividend GIVING dest+ [REMAINDER r]
    // Format 3: DIVIDE dividend BY divisor GIVING dest+ [REMAINDER r]
    // name = "INTO" or "BY"
    // is_numeric=false → no GIVING (INTO only);  is_numeric=true → GIVING
    // is_signed=true   → REMAINDER clause present (last child is remainder var)
    // decimal_places=1 (INTO) or 2 (BY) — number of operands before INTO/BY
    // children (INTO, no GIVING): [divisor, target1,...]
    // children (INTO, GIVING):    [divisor, dividend, giving1,..., [rem]]
    // children (BY,  GIVING):     [dividend, divisor, giving1,..., [rem]]
    else if (tok.kind == TOK_DIVIDE) {
        next();
        AstNode *div_stmt = ast_new(AST_STATEMENT_DIVIDE, NULL, loc);
        AstNode *ops_head = NULL, *ops_tail = NULL;

        if (tok.kind != TOK_IDENT && tok.kind != TOK_NUMBER) {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected operand after DIVIDE."); synchronize(); return NULL;
        }
        char *val = tok_to_str();
        list_append(&ops_head, &ops_tail, make_operand_node(val, tok.kind));
        free(val); next();
        div_stmt->children = ops_head;

        if (tok.kind == TOK_INTO) {
            next();
            div_stmt->name = strdup("INTO");
            div_stmt->decimal_places = 1; // one divisor operand

            // Collect targets (no GIVING) or one dividend then GIVING
            while ((tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER) && !parser_panic_mode) {
                char *v = tok_to_str();
                list_append(&ops_head, &ops_tail, make_operand_node(v, tok.kind));
                free(v); next();
                if (tok.kind == TOK_ROUNDED) next();
                if (tok.kind == TOK_GIVING) break;
            }
            if (tok.kind == TOK_GIVING) {
                next();
                div_stmt->is_numeric = true;
                while (tok.kind == TOK_IDENT && !parser_panic_mode) {
                    char *v = tok_to_str();
                    list_append(&ops_head, &ops_tail, make_operand_node(v, tok.kind));
                    free(v); next();
                    if (tok.kind == TOK_ROUNDED) next();
                    if (tok.kind == TOK_REMAINDER) break;
                }
                if (tok.kind == TOK_REMAINDER) {
                    next();
                    if (tok.kind == TOK_IDENT) {
                        char *v = tok_to_str();
                        list_append(&ops_head, &ops_tail, make_operand_node(v, tok.kind));
                        free(v); next();
                        div_stmt->is_signed = true;
                    }
                }
            }
        } else if (tok.kind == TOK_BY) {
            next();
            div_stmt->name = strdup("BY");
            div_stmt->decimal_places = 2; // dividend + divisor both before GIVING

            // second operand (divisor)
            if (tok.kind != TOK_IDENT && tok.kind != TOK_NUMBER) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "Expected divisor after BY."); synchronize(); return NULL;
            }
            char *v = tok_to_str();
            list_append(&ops_head, &ops_tail, make_operand_node(v, tok.kind));
            free(v); next();

            if (tok.kind != TOK_GIVING) {
                cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                                 "DIVIDE ... BY requires GIVING."); synchronize(); return NULL;
            }
            next();
            div_stmt->is_numeric = true;
            while (tok.kind == TOK_IDENT && !parser_panic_mode) {
                char *v2 = tok_to_str();
                list_append(&ops_head, &ops_tail, make_operand_node(v2, tok.kind));
                free(v2); next();
                if (tok.kind == TOK_ROUNDED) next();
                if (tok.kind == TOK_REMAINDER) break;
            }
            if (tok.kind == TOK_REMAINDER) {
                next();
                if (tok.kind == TOK_IDENT) {
                    char *v2 = tok_to_str();
                    list_append(&ops_head, &ops_tail, make_operand_node(v2, tok.kind));
                    free(v2); next();
                    div_stmt->is_signed = true;
                }
            }
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column,
                             "Expected INTO or BY in DIVIDE statement."); synchronize(); return NULL;
        }

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
        if (tok.kind == TOK_NUMBER || tok.kind == TOK_IDENT) {
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
                // Not TIMES — unsupported variant; treat as basic PERFORM and warn
                cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column,
                                "Unsupported PERFORM variant (only TIMES supported); ignoring extra tokens");
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