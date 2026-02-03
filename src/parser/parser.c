/* src/parser/parser.c */
#include "parser.h"
#include "lexer.h"
#include "diag.h"
#include "ast.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // for strcasecmp

static Token tok;
static Lexer *current_lexer; // Temporary global lexer for parser functions

static void next(void) {
    tok = lexer_next(current_lexer);
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
        char *got_str = tok_to_str();
        if (current_lexer->mode->strictness == STRICT_RELAXED) {
            cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Expected %s, got '%.*s'. Continuing in relaxed mode.", msg, (int)tok.length, tok.lexeme);
        } else {
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected %s, got '%.*s'", msg, (int)tok.length, tok.lexeme);
            free(got_str);
            while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF) next();
            return;
        }
        free(got_str);
        while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF) next();
    } else {
        next();
    }
}

static bool is_known_ident_para(const char *s) {
    return strcasecmp(s, "AUTHOR") == 0 ||
           strcasecmp(s, "DATE-WRITTEN") == 0 ||
           strcasecmp(s, "DATE-COMPILED") == 0 ||
           strcasecmp(s, "INSTALLATION") == 0 ||
           strcasecmp(s, "SECURITY") == 0;
}

static bool is_division_start(TokenKind kind) {
    return kind == TOK_IDENTIFICATION ||
           kind == TOK_ENVIRONMENT ||
           kind == TOK_DATA ||
           kind == TOK_PROCEDURE;
}

static AstNode *parse_identification_paragraph(void) {
    if (tok.kind != TOK_IDENT) return NULL;

    SourceLocation loc = {tok.line, tok.column};
    char *name = tok_to_str();
    next();
    expect(TOK_PERIOD, ".");

    AstNode *para = ast_new(AST_IDENTIFICATION_PARAGRAPH, name, loc);
    free(name);

    char val_buf[256] = {0};
    size_t pos = 0;

    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && pos < sizeof(val_buf) - 1) {
        char *part = tok_to_str();

        // Example of standard-specific behavior:
        // In COBOL85, these are common identification paragraphs.
        // In newer standards, some might be deprecated or have different implications.
        // For now, we'll keep the general check, but this is where
        // current_lexer->mode->std could be used for more fine-grained control.
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

    SourceLocation loc = {tok.line, tok.column};
    char *level = tok_to_str();
    next();

    char *name = NULL;
    if (tok.kind == TOK_IDENT) {
        name = tok_to_str();
        next();
    } else {
        free(level);
        return NULL;
    }

    AstNode *var = ast_new(AST_VAR_DECL, name, loc);
    free(name);
    var->text = level;
    AstNode *current_child_tail = NULL; // New helper to track the end of the children list

    if (tok.kind == TOK_PIC) {
        next();
        char pic_buf[64] = {0};
        size_t pos = 0;
        while (tok.kind != TOK_VALUE && tok.kind != TOK_USAGE && tok.kind != TOK_PERIOD && tok.kind != TOK_EOF) {
            char *part = tok_to_str();
            strncat(pic_buf + pos, part, sizeof(pic_buf) - pos - 1);
            pos += strlen(part);
            free(part);
            next();
        }
        AstNode *pic = ast_new(AST_PIC_CLAUSE, strdup(pic_buf), loc);
        if (!var->children) { // If it's the first child
            var->children = pic;
        } else { // Append to the existing children list
            current_child_tail->next = pic;
        }
        current_child_tail = pic; // Update tail
    }

    // --- Start of USAGE parsing logic ---
    if (tok.kind == TOK_USAGE) {
        next(); // Consume TOK_USAGE

        // Optional IS
        if (tok.kind == TOK_IS) {
            next(); // Consume TOK_IS
        }

        AstNode *usage_clause = ast_new(AST_USAGE_CLAUSE, NULL, loc);
        AstNode *usage_type = NULL;

        if (tok.kind == TOK_COMP) {
            next(); // Consume TOK_COMP
            usage_type = ast_new(AST_USAGE_TYPE_COMP, NULL, loc);
        } else if (tok.kind == TOK_DISPLAY) {
            next(); // Consume TOK_DISPLAY
            usage_type = ast_new(AST_USAGE_TYPE_DISPLAY, NULL, loc);
        } else {
            cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Expected COMP or DISPLAY after USAGE, got '%.*s'", (int)tok.length, tok.lexeme);
            // Optionally, handle error or default to DISPLAY
        }

        if (usage_type) {
            usage_clause->children = usage_type;
        }

        if (!var->children) { // If it's the first child
            var->children = usage_clause;
        } else { // Append to the existing children list
            current_child_tail->next = usage_clause;
        }
        current_child_tail = usage_clause; // Update tail
    }
    // --- End of USAGE parsing logic ---

    if (tok.kind == TOK_VALUE) {
        next();
        char *val = tok_to_str();
        AstNode *val_node = NULL;
        if (tok.kind == TOK_STRING) {
            val_node = ast_new(AST_STRING_LITERAL, val, (SourceLocation){tok.line, tok.column});
        } else if (tok.kind == TOK_NUMBER) {
            val_node = ast_new(AST_LITERAL_NUMBER, val, (SourceLocation){tok.line, tok.column});
        } else if (tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) {
            val_node = ast_new(AST_FIGURATIVE_CONSTANT, val, (SourceLocation){tok.line, tok.column});
        } else if (tok.kind == TOK_IDENT) {
            val_node = ast_new(AST_IDENTIFIER, val, (SourceLocation){tok.line, tok.column});
        } else {
            // Handle error: unexpected token after VALUE
            cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Expected literal or figurative constant after VALUE, got '%.*s'", (int)tok.length, tok.lexeme);
            free(val);
            return NULL; // Or try to recover
        }
        free(val);
        if (!var->children) { // If it's the first child
            var->children = val_node;
        } else { // Append to the existing children list
            current_child_tail->next = val_node;
        }
        current_child_tail = val_node; // Update tail
        next();
    }

    expect(TOK_PERIOD, ".");
    return var;
}

static AstNode *parse_statement(bool require_period);

static AstNode *parse_statement(bool require_period) {
    SourceLocation loc = {tok.line, tok.column};

    if (tok.kind == TOK_DISPLAY) {
        next();
        AstNode *display = ast_new(AST_STATEMENT_DISPLAY, NULL, loc);
        AstNode *last = NULL;

        while (tok.kind != TOK_PERIOD &&
               tok.kind != TOK_ELSE &&
               tok.kind != TOK_END_IF &&
               tok.kind != TOK_EOF) {
            if (tok.kind == TOK_STRING || tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER ||
                tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) { // Added figurative constants
                char *val = tok_to_str();
                AstNode *arg = NULL;
                if (tok.kind == TOK_STRING) {
                    arg = ast_new(AST_STRING_LITERAL, val, (SourceLocation){tok.line, tok.column});
                } else if (tok.kind == TOK_NUMBER) {
                    arg = ast_new(AST_LITERAL_NUMBER, val, (SourceLocation){tok.line, tok.column});
                } else if (tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) {
                    arg = ast_new(AST_FIGURATIVE_CONSTANT, val, (SourceLocation){tok.line, tok.column});
                } else { // TOK_IDENT
                    arg = ast_new(AST_IDENTIFIER, val, (SourceLocation){tok.line, tok.column});
                }
                free(val);
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
        if (require_period) expect(TOK_PERIOD, ".");
        return ast_new(AST_STATEMENT_STOP, NULL, loc);
    }
    else if (tok.kind == TOK_MOVE) {
        next();
        AstNode *move = ast_new(AST_STATEMENT_MOVE, NULL, loc);

        if (tok.kind == TOK_STRING || tok.kind == TOK_NUMBER || tok.kind == TOK_IDENT ||
            tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) { // Added figurative constants
            char *val = tok_to_str();
            AstNode *src = NULL;
            if (tok.kind == TOK_STRING) {
                src = ast_new(AST_STRING_LITERAL, val, (SourceLocation){tok.line, tok.column});
            } else if (tok.kind == TOK_NUMBER) {
                src = ast_new(AST_LITERAL_NUMBER, val, (SourceLocation){tok.line, tok.column});
            } else if (tok.kind == TOK_ZEROS || tok.kind == TOK_SPACES) {
                src = ast_new(AST_FIGURATIVE_CONSTANT, val, (SourceLocation){tok.line, tok.column});
            } else { // TOK_IDENT
                src = ast_new(AST_IDENTIFIER, val, (SourceLocation){tok.line, tok.column});
            }
            free(val);
            move->children = src;
            next();
        }

        expect(TOK_TO, "TO");

        if (tok.kind == TOK_IDENT) {
            char *target = tok_to_str();
            AstNode *tgt = ast_new(AST_IDENTIFIER, target, (SourceLocation){tok.line, tok.column});
            free(target);
            move->children->next = tgt;
            next();
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return move;
    }
    else if (tok.kind == TOK_ADD) {
        next();
        AstNode *add = ast_new(AST_STATEMENT_ADD, NULL, loc);

        AstNode *sources = NULL;
        AstNode *last_src = NULL;

        while (tok.kind != TOK_TO && tok.kind != TOK_GIVING && tok.kind != TOK_PERIOD && tok.kind != TOK_EOF &&
               tok.kind != TOK_ELSE && tok.kind != TOK_END_IF) {
            if (tok.kind == TOK_IDENT || tok.kind == TOK_NUMBER) {
                char *val = tok_to_str();
                AstNode *op = ast_new(
                    tok.kind == TOK_NUMBER ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
                    val, (SourceLocation){tok.line, tok.column}
                );
                free(val);
                if (!sources) sources = op;
                else last_src->next = op;
                last_src = op;
            }
            next();
        }

        add->children = sources;

        if (tok.kind == TOK_TO) {
            next();
            AstNode *dest = NULL;
            if (tok.kind == TOK_IDENT) {
                char *val = tok_to_str();
                dest = ast_new(AST_IDENTIFIER, val, (SourceLocation){tok.line, tok.column});
                free(val);
                next();
            }
            if (last_src) last_src->next = dest;
            else if (sources) sources->next = dest;
            last_src = dest;
        }

        if (tok.kind == TOK_GIVING) {
            next();
            AstNode *giving = NULL;
            if (tok.kind == TOK_IDENT) {
                char *val = tok_to_str();
                giving = ast_new(AST_IDENTIFIER, val, (SourceLocation){tok.line, tok.column});
                free(val);
                next();
            }
            if (last_src) last_src->next = giving;
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return add;
    }
    else if (tok.kind == TOK_IF) {
        next();
        AstNode *if_stmt = ast_new(AST_STATEMENT_IF, NULL, loc);

        AstNode *cond = ast_new(AST_CONDITION, NULL, loc);
        if_stmt->children = cond;

        // left operand
        if (tok.kind == TOK_IDENT) {
            char *left = tok_to_str();
            cond->children = ast_new(AST_IDENTIFIER, left, (SourceLocation){tok.line, tok.column});
            free(left);
            next();
        }

        // operator
        const char *op = NULL;
        if (tok.kind == TOK_GT) { op = ">"; next(); }
        else if (tok.kind == TOK_LT) { op = "<"; next(); }
        else if (tok.kind == TOK_GE) { op = ">="; next(); }
        else if (tok.kind == TOK_LE) { op = "<="; next(); }
        else if (tok.kind == TOK_EQ) { op = "="; next(); }
        else if (tok.kind == TOK_NE) { op = "<>"; next(); }
        if (op) cond->text = strdup(op);

        // right operand
        if (tok.kind == TOK_NUMBER || tok.kind == TOK_IDENT) {
            char *right = tok_to_str();
            AstNode *right_node = ast_new(
                tok.kind == TOK_NUMBER ? AST_LITERAL_NUMBER : AST_IDENTIFIER,
                right, (SourceLocation){tok.line, tok.column}
            );
            free(right);
            cond->children->next = right_node;
            next();
        }

        // Optional THEN
        if (tok.kind == TOK_THEN) next();

        AstNode *then_block = NULL;
        AstNode *last_then = NULL;
        while (tok.kind != TOK_ELSE && tok.kind != TOK_END_IF && tok.kind != TOK_EOF) {
            AstNode *stmt = parse_statement(false);  // no period required inside blocks
            if (stmt) {
                if (!then_block) then_block = stmt;
                else last_then->next = stmt;
                last_then = stmt;
            } else {
                next();
            }
        }

        if (then_block) {
            cond->next = then_block;
        }

        if (tok.kind == TOK_ELSE) {
            next();
            AstNode *else_block = NULL;
            AstNode *last_else = NULL;
            while (tok.kind != TOK_END_IF && tok.kind != TOK_EOF) {
                AstNode *stmt = parse_statement(false);  // no period required inside blocks
                if (stmt) {
                    if (!else_block) else_block = stmt;
                    else last_else->next = stmt;
                    last_else = stmt;
                } else {
                    next();
                }
            }
            if (else_block && last_then) {
                last_then->next = else_block;
            }
        }

        expect(TOK_END_IF, "END-IF");
        if (require_period) expect(TOK_PERIOD, ".");
        return if_stmt;
    }
    else if (tok.kind == TOK_PERFORM) {
        next();
        AstNode *perf = ast_new(AST_STATEMENT_PERFORM, NULL, loc);

        if (tok.kind == TOK_IDENT) {
            char *para = tok_to_str();
            AstNode *target = ast_new(AST_IDENTIFIER, para, (SourceLocation){tok.line, tok.column});
            free(para);
            perf->children = target;
            next();
        }

        if (require_period) expect(TOK_PERIOD, ".");
        return perf;
    }

    cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Skipping unknown statement starting with '%.*s'", (int)tok.length, tok.lexeme);
    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF &&
           tok.kind != TOK_ELSE && tok.kind != TOK_END_IF) next();
    if (require_period && tok.kind == TOK_PERIOD) next();
    return NULL;
}

static AstNode *parse_paragraph(CompilerMode *mode) {
    if (tok.kind != TOK_IDENT) return NULL;

    SourceLocation loc = {tok.line, tok.column};
    char *name = tok_to_str();
    next();
    expect(TOK_PERIOD, ".");

    AstNode *para = ast_new(AST_PARAGRAPH, name, loc);
    free(name);

    AstNode *last = NULL;
    while (tok.kind != TOK_EOF &&
           tok.kind != TOK_IDENT && tok.kind != TOK_SECTION &&
           tok.kind != TOK_END) {
        AstNode *stmt = parse_statement(true);
        if (stmt) {
            if (!para->children) para->children = stmt;
            else last->next = stmt;
            last = stmt;
        } else {
            next();
        }
    }

    return para;
}

static AstNode *parse_section(CompilerMode *mode) {
    SourceLocation loc = {tok.line, tok.column};
    expect(TOK_SECTION, "SECTION");
    expect(TOK_PERIOD, ".");

    AstNode *sec = ast_new(AST_SECTION, NULL, loc);
    AstNode *last = NULL;

    while (tok.kind != TOK_EOF &&
           tok.kind != TOK_IDENTIFICATION &&
           tok.kind != TOK_ENVIRONMENT &&
           tok.kind != TOK_DATA &&
           tok.kind != TOK_PROCEDURE) {
        AstNode *decl = parse_var_decl();
        if (decl) {
            if (!sec->children) sec->children = decl;
            else last->next = decl;
            last = decl;
            continue;
        }

        AstNode *para = parse_paragraph(mode);
        if (para) {
            if (!sec->children) sec->children = para;
            else last->next = para;
            last = para;
        } else {
            AstNode *stmt = parse_statement(true);
            if (stmt) {
                if (!sec->children) sec->children = stmt;
                else last->next = stmt;
                last = stmt;
            } else {
                next();
            }
        }
    }

    return sec;
}

static AstNode *parse_division(CompilerMode *mode) {
    SourceLocation loc = {tok.line, tok.column};
    char *name = tok_to_str();
    next();
    expect(TOK_DIVISION, "DIVISION");
    expect(TOK_PERIOD, ".");

    AstNode *div = ast_new(AST_DIVISION, name, loc);
    free(name);

    if (strcasecmp(div->name, "IDENTIFICATION") == 0) {
        expect(TOK_PROGRAM_ID, "PROGRAM-ID");
        expect(TOK_PERIOD, ".");
        char *prog = tok_to_str();
        expect(TOK_IDENT, "program name");
        expect(TOK_PERIOD, ".");
        div->text = prog;

        AstNode *last_para = NULL;
        while (tok.kind == TOK_IDENT && 
               tok.kind != TOK_ENVIRONMENT && tok.kind != TOK_DATA && tok.kind != TOK_PROCEDURE) {
            AstNode *para = parse_identification_paragraph();
            if (para) {
                if (!div->children) div->children = para;
                else last_para->next = para;
                last_para = para;
            } else {
                next();
            }
        }
    }
    else if (strcasecmp(div->name, "PROCEDURE") == 0) {
        AstNode *last = NULL;
        while (tok.kind != TOK_EOF &&
               tok.kind != TOK_IDENTIFICATION &&
               tok.kind != TOK_ENVIRONMENT &&
               tok.kind != TOK_DATA) {
            AstNode *para = parse_paragraph(mode);
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
                    next();
                }
            }
        }
    }
    else if (strcasecmp(div->name, "DATA") == 0) {
        AstNode *sec = NULL;
        if (tok.kind == TOK_WORKING_STORAGE) {
            next();
            sec = parse_section(mode);
            div->children = sec;
        } else {
            cobalt_warn_loc(current_lexer->original_source, tok.line, tok.column, "Only WORKING-STORAGE supported in DATA DIVISION for now");
        }
    }

    return div;
}

AstNode *parse_program(Lexer *l, CompilerMode *mode, const char *source) {
    current_lexer = l; // Initialize the temporary global lexer
    next();

    SourceLocation loc = {tok.line, tok.column};
    AstNode *prog = ast_new(AST_PROGRAM, NULL, loc);
    AstNode *last_div = NULL;
    int has_proc = 0;

    if (tok.kind == TOK_IDENTIFICATION) {
        AstNode *div = parse_division(mode);
        prog->children = div;
        last_div = div;

        if (div->text) {
            prog->name = div->text;
            div->text = NULL;
        }
    } else {
        cobalt_error_loc(current_lexer->original_source, tok.line, tok.column, "Missing IDENTIFICATION DIVISION");
    }

    while (tok.kind != TOK_EOF) {
        if (tok.kind == TOK_ENVIRONMENT ||
            tok.kind == TOK_DATA ||
            tok.kind == TOK_PROCEDURE) {
            AstNode *div = parse_division(mode);
            if (last_div) last_div->next = div;
            else prog->children = div;
            last_div = div;

            if (strcasecmp(div->name, "PROCEDURE") == 0) has_proc = 1;
        } else {
            next();
        }
    }

    if (!has_proc) {
        cobalt_error("Missing PROCEDURE DIVISION");
    }

    return prog;
}
