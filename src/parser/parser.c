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

// The location of the current token
#define TOK_LOC ((SourceLocation){tok.filename, tok.line, tok.column})

static Token prev_tok;                  // the token before tok

// Error recovery: synchronize() just consumed a period (a missing period
// then is not reported again), or stopped at a statement inside a sentence
// (scope terminators up to the next period are not reported)
static bool synced_at_period, synced_in_sentence;

static void next(void) {
    prev_tok = tok;
    tok = lexer_next(current_lexer);
    synced_at_period = false;
}

// Just after the previous token: where something missing belongs
#define AFTER_PREV ((SourceLocation){prev_tok.filename, prev_tok.line, prev_tok.column + (int)prev_tok.length})

static bool is_statement_verb(void);
static bool later_statement(const char *name, SourceLocation loc);
static void skip_exec_block(void);

// Function to synchronize parser after an error
static void synchronize() {
    parser_panic_mode = true;
    while (tok.kind != TOK_EOF) {
        // We've already advanced past the error, now look for a safe point
        // A period often ends a statement, good place to recover.
        if (tok.kind == TOK_PERIOD) {
            next(); // Consume the period
            parser_panic_mode = false; // Reset panic mode
            synced_at_period = true;
            synced_in_sentence = false;
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
                parser_panic_mode = false;
                synced_in_sentence = true;
                return;
            default:
                // A statement verb starts the next statement
                if (tok.kind != TOK_IDENT && tok.kind != TOK_STRING && tok.kind != TOK_NUMBER && is_statement_verb()) {
                    parser_panic_mode = false;
                    synced_in_sentence = true;
                    return;
                }
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
// The current token's text in a buffer valid until the next call
static const char *tok_value_tmp(void) {
    static char buf[256];
    size_t n = tok.length < sizeof(buf) - 1 ? tok.length : sizeof(buf) - 1;
    memcpy(buf, tok.lexeme, n);
    buf[n] = '\0';
    return buf;
}

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

// The verbs that start statements (for "did you mean")
static const char *const statement_verbs[] = {
    "ACCEPT", "ADD", "ALTER", "CALL", "CANCEL", "CLOSE", "COMPUTE", "CONTINUE", "DELETE", "DISPLAY",
    "DIVIDE", "EVALUATE", "EXIT", "GENERATE", "GO", "GOBACK", "IF", "INITIALIZE", "INITIATE", "INSPECT", "MERGE",
    "MOVE", "MULTIPLY", "OPEN", "PERFORM", "READ", "RELEASE", "RETURN", "REWRITE", "SEARCH", "SET", "SORT",
    "START", "STOP", "STRING", "SUBTRACT", "SUPPRESS", "TERMINATE", "UNSTRING", "WRITE", NULL
};

// The current token is a statement verb
static bool is_statement_verb(void) {
    for (int k = 0; statement_verbs[k]; k++)
        if (tok.length == strlen(statement_verbs[k]) && strncasecmp(tok.lexeme, statement_verbs[k], tok.length) == 0)
            return true;
    return false;
}

// The current token for a message: 'MOVE', '.', "end of file"
static const char *tok_desc(void) {
    static char buf[96];
    if (tok.kind == TOK_EOF) return "end of file";
    if (tok.kind == TOK_STRING) {
        snprintf(buf, sizeof(buf), "the literal \"%.*s\"%s", tok.length > 40 ? 40 : (int)tok.length, tok.lexeme,
                 tok.length > 40 ? "..." : "");
        return buf;
    }
    snprintf(buf, sizeof(buf), "'%.*s'", tok.length > 60 ? 60 : (int)tok.length, tok.lexeme);
    return buf;
}

// The Communication module (CD entries, SEND, RECEIVE, ENABLE, DISABLE,
// PURGE, ACCEPT MESSAGE COUNT) is not supported (docs/release-checklist.md 1.3)
static void communication_unsupported(SourceLocation loc, const char *what) {
    static bool noted;
    diag_error(loc, "%s belongs to the Communication module, which cobalt does not support", what);
    if (!noted) diag_note_plain("the Communication module (message queues of a host system) is obsolete in practice; "
                    "most COBOL compilers leave it out");
    noted = true;
}

// FIPS flagging: an obsolete element of COBOL-85 (with --flag-obsolete)
static void flag_obsolete(int line, int col, const char *what) {
    diag_warning(W_OBSOLETE, (SourceLocation){tok.filename, line, col}, "OBSOLETE: %s", what);
}

static void expect(TokenKind k, const char *msg) {
    if (tok.kind != k) {
        if (k == TOK_PERIOD && synced_at_period) {
            // error recovery already went past the period
        } else if (!parser_panic_mode) { // Only report first error in a sequence
            if (k == TOK_IDENT && tok.length && isalpha((unsigned char)tok.lexeme[0]) && tok.kind != TOK_EOF) {
                diag_error(TOK_LOC, "Expected %s, found the reserved word '%.*s'", msg, (int)tok.length, tok.lexeme);
                diag_note_plain("a reserved word cannot be used as a name; choose another name");
            } else if (k == TOK_PERIOD && prev_tok.line && prev_tok.line != tok.line)
                diag_error(AFTER_PREV, "Expected '.' after '%.*s', found %s", (int)prev_tok.length, prev_tok.lexeme, tok_desc());
            else if (k == TOK_PERIOD)
                diag_error(TOK_LOC, "Expected '.' before %s", tok_desc());
            else
                diag_error(TOK_LOC, "Expected %s, found %s", msg, tok_desc());
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
    SourceLocation op_loc = {tok.filename, tok.line, tok.column};
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
    copy.quiet = true;
    copy.prev = NULL;           // never pop back into an including file
    return lexer_next(&copy);
}

static AstNode *parse_identification_paragraph(void) {
    if (tok.kind != TOK_IDENT) return NULL;
    if (parser_panic_mode) return NULL; // Don't parse if in panic mode

    SourceLocation loc = {tok.filename, tok.line, tok.column};
    char *name = tok_to_str();
    if (is_known_ident_para(name)) flag_obsolete(tok.line, tok.column, "the paragraph is obsolete");
    if (strcasecmp(name, "DATE-COMPILED") == 0) diag_subset(MOD_NC, 2, loc, "The DATE-COMPILED paragraph");
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
static void parse_special_names(AstNode *div, AstNode **tail);
static AstNode *parse_arith_operand(void);
static AstNode *parse_procedure_name(void);
static AstNode *parse_imperative_block(void);
static Token peek_token(void);
static bool tok_is_word(const char *word);
static bool tok_spelled(const char *word);
static void parse_report_section(AstNode *div, AstNode **sec_tail);
static void check_area_a(SourceLocation loc, const char *what);
static AstNode *parse_forward_identifier(void);
static bool end_program_follows(void);
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

// Subset flagging: the level number of the REDEFINES entry whose
// subordinate entries are being read (0: none)
static int redefines_level;

static void record_declaration(Symbol *sym, int level);

static AstNode *parse_var_decl(void) {
    if (tok.kind != TOK_NUMBER) return NULL;
    if (parser_panic_mode) return NULL; // Don't parse if in panic mode

    SourceLocation loc = {tok.filename, tok.line, tok.column};
    char *level_str = tok_to_str();
    int level_num = atoi(level_str);
    if (level_num == 1 || level_num == 77) check_area_a(loc, level_num == 1 ? "A level 01 entry" : "A level 77 entry");
    next();

    // data-name, FILLER, or nothing (an implicit FILLER); the entry is
    // located at its name
    char *name = NULL;
    if (tok.kind == TOK_IDENT || tok.kind == TOK_FILLER) loc = TOK_LOC;
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
    record_declaration(sym, level_num);
    {
        // The conditional variable of the condition-names that follow
        static Symbol *conditional;
        if (level_num != 88) conditional = sym;
        else if (sym && sym != conditional) sym->condition_of = conditional;
    }

    free(name);
    var->text = level_str; // Keep level_str in AST node for now, or just free it.
    AstNode *tail = NULL;
    if (level_num == 88) diag_subset(MOD_NC, 2, loc, "A condition-name (level 88)");
    if (level_num == 66) diag_subset(MOD_NC, 2, loc, "RENAMES (level 66)");
    if (level_num != 88 && level_num != 66 && redefines_level && (level_num <= redefines_level || level_num == 77))
        redefines_level = 0;                // the end of the redefining entry

    // Level 88: VALUE[S] [IS|ARE] literal [THRU literal] {literal [THRU literal]}...
    // children: the literals; a THRU range keeps its upper bound in extra
    if (level_num == 88) {
        if (tok.kind != TOK_VALUE) {
            diag_error(TOK_LOC, "Condition-name '%s' requires VALUE", var->name);
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

    // Level 66: RENAMES data-name [THRU data-name]
    // on_error: the first item renamed; not_on_error: the THRU item
    if (level_num == 66) {
        if (!tok_is_word("RENAMES")) {
            diag_error(TOK_LOC, "Level 66 entry '%s' requires RENAMES", var->name);
            synchronize();
            ast_free(var);
            return NULL;
        }
        next();
        if (tok.kind != TOK_IDENT) { ast_free(var); arith_error("Expected the item to rename"); return NULL; }
        var->on_error = parse_identifier();
        if (tok.kind == TOK_THROUGH) {
            diag_subset(MOD_NC, 2, TOK_LOC, "RENAMES ... THRU");
            next();
            if (tok.kind != TOK_IDENT) { ast_free(var); arith_error("Expected a data name after THRU"); return NULL; }
            var->not_on_error = parse_identifier();
        }
        expect(TOK_PERIOD, ".");
        if (parser_panic_mode) { ast_free(var); return NULL; }
        return var;
    }

    // Clauses may appear in any order
    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !parser_panic_mode) {
        SourceLocation cl_loc = {tok.filename, tok.line, tok.column};
        AstUsageType usage;

        if (tok.kind == TOK_PIC) {
            next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_PIC_STRING) {
                diag_error(TOK_LOC, "Expected PICTURE character-string, found %s", tok_desc());
                synchronize();
                ast_free(var);
                return NULL;
            }
            char *pic = tok_to_str();
            if (tok.length > 30)
            {
                diag_feature(F_LONG_PICTURE, TOK_LOC, "a PICTURE character-string of %d characters", (int)tok.length);
                diag_note_plain("COBOL-85 PICTURE character-strings have at most 30 characters");
            }
            append_child(var, &tail, ast_new(AST_PIC_CLAUSE, pic, TOK_LOC));
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
                    if (!diag_later_element(TOK_LOC, LATER_USAGE, tok.lexeme, tok.length))
                        diag_error(TOK_LOC, "Unknown USAGE %s", tok_desc());
                    synchronize();
                    ast_free(var);
                    return NULL;
                }
            }
            if (tok_spelled("COMP-3") || tok_spelled("COMPUTATIONAL-3") || tok.kind == TOK_COMP_5 ||
                tok_spelled("COMP-4") || tok_spelled("COMPUTATIONAL-4")) {
                diag_feature(F_COMP_N, TOK_LOC, "USAGE %.*s", (int)tok.length, tok.lexeme);
                if (tok.kind == TOK_COMP_3) diag_note_plain("the standard name of this usage is PACKED-DECIMAL");
                else if (tok.kind == TOK_COMP) diag_note_plain("the standard name of this usage is BINARY");
                else diag_note_plain("COMP-5 is native binary; BINARY is the standard usage");
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
            SourceLocation val_loc = {tok.filename, tok.line, tok.column};
            if (tok.kind == TOK_STRING) {
                val_node = ast_new(AST_STRING_LITERAL, val, val_loc);
            } else if (tok.kind == TOK_NUMBER) {
                val_node = ast_new(AST_LITERAL_NUMBER, val, val_loc);
            } else if (is_figurative(tok.kind)) {
                val_node = ast_new(AST_FIGURATIVE_CONSTANT, val, val_loc);
            } else if (tok.kind == TOK_ALL) {
                free(val);
                diag_subset(MOD_NC, 2, TOK_LOC, "VALUE ALL literal");
                val_node = parse_all_literal();
                if (!val_node) { ast_free(var); return NULL; }
                append_child(var, &tail, val_node);
                continue;
            } else {
                diag_error(TOK_LOC, "Expected literal or figurative constant after VALUE, found %s", tok_desc());
                free(val);
                synchronize(); // Error, enter panic mode
                ast_free(var);
                return NULL;
            }
            free(val);
            append_child(var, &tail, val_node);
            next();
        } else if (tok_is_word("GLOBAL") || tok_is_word("EXTERNAL")) {
            diag_subset(MOD_IC, 2, TOK_LOC, tok_is_word("GLOBAL") ? "GLOBAL" : "EXTERNAL");
            if (tok_is_word("GLOBAL")) var->is_global = true;
            else var->is_external = true;
            next();
        } else if (tok.kind == TOK_IS && (peek_token().kind == TOK_IDENT)) {
            next();                         // IS GLOBAL / IS EXTERNAL
        } else if (tok.kind == TOK_REDEFINES) {
            if (redefines_level && level_num > redefines_level)
                diag_subset(MOD_NC, 2, TOK_LOC, "REDEFINES subordinate to a redefining entry");
            else
                redefines_level = level_num;
            next();
            if (tok.kind != TOK_IDENT) { ast_free(var); arith_error("Expected data-name after REDEFINES"); return NULL; }
            char *target = tok_to_str();
            Symbol *tsym = symtab_resolve_symbol(current_symtab, target);
            if (tsym) symtab_mark_symbol_as_used(tsym);
            append_child(var, &tail, ast_new(AST_REDEFINES, target, cl_loc));
            free(target);
            next();
        } else if (tok.kind == TOK_OCCURS) {
            // OCCURS [min TO] n [TIMES] [DEPENDING [ON] data-name]
            //   [{ASCENDING|DESCENDING} [KEY] [IS] name...]... [INDEXED [BY] name...]
            // name: the (maximum) count; digits: the minimum; extra: DEPENDING ON item
            next();
            if (tok.kind != TOK_NUMBER) { ast_free(var); arith_error("Expected integer after OCCURS"); return NULL; }
            char *count = tok_to_str();
            AstNode *occurs = ast_new(AST_OCCURS, count, cl_loc);
            free(count);
            append_child(var, &tail, occurs);
            next();
            if (tok.kind == TOK_TO) {
                next();
                if (tok.kind != TOK_NUMBER) { ast_free(var); arith_error("Expected integer after TO"); return NULL; }
                occurs->digits = atoi(occurs->name);
                free(occurs->name);
                occurs->name = tok_to_str();
                next();
                if (tok.kind == TOK_TIMES) next();
                if (tok.kind != TOK_DEPENDING) { ast_free(var); arith_error("Expected DEPENDING after OCCURS ... TO"); return NULL; }
            }
            if (tok.kind == TOK_TIMES) next();
            if (tok.kind == TOK_DEPENDING) {
                diag_subset(MOD_NC, 2, TOK_LOC, "OCCURS ... DEPENDING ON");
                next();
                if (tok.kind == TOK_ON) next();
                if (tok.kind != TOK_IDENT) { ast_free(var); arith_error("Expected data-name after DEPENDING ON"); return NULL; }
                // The item may be described later: resolved during IR lowering
                occurs->extra = parse_procedure_name();
            }
            while (tok.kind == TOK_ASCENDING || tok.kind == TOK_DESCENDING) {
                diag_subset(MOD_NC, 2, TOK_LOC, "The KEY phrase of OCCURS");
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
                    SourceLocation iloc = {tok.filename, tok.line, tok.column};
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
            if (!diag_later_element(TOK_LOC, LATER_USAGE, tok.lexeme, tok.length))
                diag_error(TOK_LOC, "Unexpected %s in the data description entry of '%s'", tok_desc(), var->name);
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
static AstNode *need_operand(AstNode *n, const char *after);

// identifier := name {OF|IN name}... [(subscript...)] [(start:[length])]
// AST_IDENTIFIER: children = subscripts, extra = qualifiers (innermost
// first), loop = AST_REFMOD with children start and optional length.
// FUNCTION name [(argument...)] [(start : [length])]: an AST_IDENTIFIER
// named "!" and the function's name; on_error: the arguments; loop: the
// reference modification
static AstNode *parse_function(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    next();                                     // FUNCTION
    if (tok.kind == TOK_STRING || tok.kind == TOK_NUMBER || tok.kind == TOK_PERIOD || tok.kind == TOK_EOF ||
        tok.kind == TOK_LPAREN) {
        arith_error("Expected a function name after FUNCTION");
        return ast_new(AST_IDENTIFIER, "!", loc);
    }
    char fname[64];
    size_t n = tok.length < sizeof(fname) - 2 ? tok.length : sizeof(fname) - 2;
    fname[0] = '!';
    for (size_t k = 0; k < n; k++) fname[k + 1] = (char)toupper((unsigned char)tok.lexeme[k]);
    fname[n + 1] = '\0';
    next();
    AstNode *f = ast_new(AST_IDENTIFIER, fname, loc);
    // The functions without arguments take "(" as a reference modification
    bool no_args = strcmp(fname, "!CURRENT-DATE") == 0 || strcmp(fname, "!WHEN-COMPILED") == 0;
    AstNode *tail = NULL;
    if (tok.kind == TOK_LPAREN && !no_args) {
        next();
        while (tok.kind != TOK_RPAREN && tok.kind != TOK_EOF && !parser_panic_mode) {
            AstNode *arg;
            if (tok.kind == TOK_STRING || is_figurative(tok.kind) || tok.kind == TOK_ALL) arg = parse_rel_operand();
            else arg = parse_expr();
            if (!arg) { arith_error("Expected a function argument"); return f; }
            list_append(&f->on_error, &tail, arg);
        }
        if (tok.kind != TOK_RPAREN) { arith_error("Expected ')'"); return f; }
        next();
    }
    if (tok.kind == TOK_LPAREN) {
        SourceLocation ploc = {tok.filename, tok.line, tok.column};
        next();
        AstNode *start = parse_expr();
        if (!start || tok.kind != TOK_COLON) { arith_error("Expected a reference modification"); return f; }
        next();
        AstNode *refmod = ast_new(AST_REFMOD, NULL, ploc);
        refmod->children = start;
        if (tok.kind != TOK_RPAREN) start->next = parse_expr();
        f->loop = refmod;
        if (tok.kind != TOK_RPAREN) { arith_error("Expected ')'"); return f; }
        next();
    }
    return f;
}

// A data-name that is not declared
int parser_name_errors;
int parser_statement_errors;

static void undefined_name(SourceLocation loc, const char *name) {
    int before = error_count;
    diag_error(loc, "'%s' is not defined", name);
    parser_name_errors += error_count - before;
    const char *near = symtab_closest(current_symtab, name, SYM_VAR);
    // A misspelled verb after a statement without its period reads as one
    // more operand of that statement: say so
    const char *verb = diag_closest(name, statement_verbs, -1);
    if (verb && !near) {
        diag_note_plain("did you mean the statement %s? Without a period or scope terminator before it, "
                        "'%s' is read as an operand of the statement before it", verb, name);
        return;
    }
    if (near) {
        Symbol *s = symtab_resolve_symbol(current_symtab, near);
        if (s && s->loc.line) diag_note(s->loc, "did you mean '%s'?", near);
        else diag_note_plain("did you mean '%s'?", near);
    }
}

// The RETURN-CODE special register (an extension): an item the compiler
// declares in a program that uses it, PIC S9(9) COMP-5 EXTERNAL, so every
// program of the run unit shares it; STOP RUN ends the run with its value
#define LEVEL_RETURN_CODE (-66)
static bool uses_return_code;                   // the program being parsed

static Symbol *use_return_code(SourceLocation loc) {
    static const char *reported_file;
    Symbol *sym = symtab_resolve_symbol(current_symtab, "RETURN-CODE");
    if (!sym) {
        sym = symtab_declare_symbol(current_symtab, "RETURN-CODE", SYM_VAR, (SourceLocation){0});
        if (sym) sym->level = LEVEL_RETURN_CODE;
    }
    if (!uses_return_code && reported_file != loc.filename) {
        diag_feature(F_RETURN_CODE, loc, "the RETURN-CODE special register");
        reported_file = loc.filename;
    }
    uses_return_code = true;
    return sym;
}

// 01 RETURN-CODE PIC S9(9) COMP-5 EXTERNAL, in the WORKING-STORAGE SECTION
static void declare_return_code(AstNode *prog) {
    SourceLocation none = {0};
    AstNode *data = NULL, *last = NULL;
    for (AstNode *d = prog->children; d; d = d->next) {
        if (d->kind == AST_DIVISION && strcasecmp(d->name, "DATA") == 0) data = d;
        if (d->kind == AST_DIVISION && strcasecmp(d->name, "ENVIRONMENT") == 0 && !data) last = d;
        if (d->kind == AST_DIVISION && strcasecmp(d->name, "IDENTIFICATION") == 0 && !last) last = d;
    }
    if (!data) {
        // a DATA DIVISION after the ENVIRONMENT (or IDENTIFICATION) DIVISION
        data = ast_new(AST_DIVISION, "DATA", none);
        if (last) { data->next = last->next; last->next = data; }
        else { data->next = prog->children; prog->children = data; }
    }
    AstNode *ws = NULL, *ws_after = NULL;
    for (AstNode *c = data->children; c; c = c->next) {
        if (c->name && strcasecmp(c->name, "WORKING-STORAGE") == 0) ws = c;
        if (c->name && strcasecmp(c->name, "FILE") == 0) ws_after = c;
    }
    if (!ws) {
        ws = ast_new(AST_SECTION, "WORKING-STORAGE", none);
        if (ws_after) { ws->next = ws_after->next; ws_after->next = ws; }
        else { ws->next = data->children; data->children = ws; }
    }
    AstNode *rc = ast_new(AST_VAR_DECL, "RETURN-CODE", none);
    rc->text = strdup("01");
    rc->is_external = true;
    AstNode *pic = ast_new(AST_PIC_CLAUSE, "S9(9)", none);
    AstNode *usage = ast_new(AST_USAGE_CLAUSE, NULL, none);
    usage->usage_type = USAGE_COMP5;
    rc->children = pic;
    pic->next = usage;
    AstNode **tail = &ws->children;
    while (*tail) tail = &(*tail)->next;
    *tail = rc;
}

static AstNode *parse_identifier(void) {
    if (tok_is_word("FUNCTION")) {
        diag_subset(MOD_IF, 1, TOK_LOC, "An intrinsic function");
        return parse_function();
    }
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    char *name = tok_to_str();
    AstNode *id = ast_new(AST_IDENTIFIER, name, loc);
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    if ((!sym && strcasecmp(name, "RETURN-CODE") == 0) || (sym && sym->level == LEVEL_RETURN_CODE))
        sym = use_return_code(loc);
    if (!sym && strcasecmp(name, "EXEC") == 0 &&
        diag_later_element(loc, LATER_STATEMENT, name, 4)) {
        // EXEC SQL in a sentence: read as an operand of the statement before it
        diag_note_plain("without a period before it, it is read as part of the statement before it");
        free(name);
        skip_exec_block();
        return id;
    }
    if (sym) symtab_mark_symbol_as_used(sym);
    else undefined_name(loc, name);
    free(name);
    next();
    if (sym && sym->level == -4) {
        // A symbolic character: a figurative constant
        id->kind = AST_FIGURATIVE_CONSTANT;
        id->text = id->name;
        id->name = strdup("SYMBOLIC");
        return id;
    }

    AstNode *qual_tail = NULL;
    while ((tok.kind == TOK_OF || tok.kind == TOK_IN) && !parser_panic_mode) {
        if (!qual_tail) diag_subset(MOD_NC, 2, TOK_LOC, "Qualification (OF / IN)");
        next();
        if (tok.kind != TOK_IDENT) { arith_error("Expected a name after OF/IN"); return id; }
        char *q = tok_to_str();
        Symbol *qsym = symtab_resolve_symbol(current_symtab, q);
        if (qsym) symtab_mark_symbol_as_used(qsym);
        list_append(&id->extra, &qual_tail, ast_new(AST_IDENTIFIER, q, (SourceLocation){tok.filename, tok.line, tok.column}));
        free(q);
        next();
    }

    // ("B, (C)": after a separator comma the parenthesis starts another
    // operand, not a subscript)
    for (int part = 0; part < 2 && tok.kind == TOK_LPAREN && !tok.after_comma && !id->loop &&
                       !parser_panic_mode; part++) {
        SourceLocation ploc = {tok.filename, tok.line, tok.column};
        next();
        AstNode *first;
        if (tok.kind == TOK_ALL) {
            // ALL: every occurrence (a function argument)
            first = ast_new(AST_SPECIAL, "ALL", (SourceLocation){tok.filename, tok.line, tok.column});
            next();
        } else {
            first = parse_expr();
        }
        if (!first) { arith_error("Expected subscript or reference modifier"); return id; }
        if (tok.kind == TOK_COLON) {
            diag_subset(MOD_NC, 2, ploc, "Reference modification");
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
                AstNode *sub;
                if (tok.kind == TOK_ALL) {
                    sub = ast_new(AST_SPECIAL, "ALL", (SourceLocation){tok.filename, tok.line, tok.column});
                    next();
                } else {
                    sub = parse_expr();
                }
                if (!sub) break;
                tail->next = sub;
                tail = sub;
            }
            int nsubs = 0;
            for (AstNode *x = id->children; x; x = x->next) nsubs++;
            if (nsubs > 3) diag_subset(MOD_NC, 2, ploc, "More than three subscripts");
        }
        if (tok.kind != TOK_RPAREN) { arith_error("Expected ')'"); return id; }
        next();
    }
    return id;
}

// ALL literal (a figurative constant repeating the literal) or ALL followed
// by a figurative constant (the same constant). Assumes tok is ALL.
static AstNode *parse_all_literal(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
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
    SourceLocation loc = {tok.filename, tok.line, tok.column};
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
    SourceLocation loc = {tok.filename, tok.line, tok.column};

    if (tok.kind == TOK_LPAREN) {
        next();
        AstNode *inner = need_operand(parse_expr(), "(");
        if (!inner) return NULL;
        if (tok.kind == TOK_RPAREN) next();
        else if (!parser_panic_mode) {
            diag_error(TOK_LOC, "Expected ')' in arithmetic expression, found %s", tok_desc());
            diag_note(loc, "to match this '('");
            synchronize();
        }
        return inner;
    }

    // Unary sign (the lexer already folds -5 into the literal "-5")
    if (tok.kind == TOK_PLUS) {
        next();
        return need_operand(parse_factor(), "+");
    }
    if (tok.kind == TOK_MINUS) {
        next();
        AstNode *operand = need_operand(parse_factor(), "-");
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

// The operand after an arithmetic operator (or opening parenthesis)
static AstNode *need_operand(AstNode *n, const char *after) {
    if (!n && !parser_panic_mode) {
        diag_error(TOK_LOC, "Expected an operand after '%s', found %s", after, tok_desc());
        synchronize();
    }
    return n;
}

// Exponentiation: left-to-right, higher precedence than * and /
static AstNode *parse_power(void) {
    AstNode *left = parse_factor();

    while (tok.kind == TOK_POW) {
        SourceLocation loc = {tok.filename, tok.line, tok.column};
        next();
        AstNode *right = need_operand(parse_factor(), "**");
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
        SourceLocation loc = {tok.filename, tok.line, tok.column};
        next();
        AstNode *right = need_operand(parse_power(), op == AST_EXPR_MUL ? "*" : "/");
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
        SourceLocation loc = {tok.filename, tok.line, tok.column};
        next();
        AstNode *right = need_operand(parse_term(), op == AST_EXPR_ADD ? "+" : "-");
        AstNode *node = ast_new(op, NULL, loc);
        node->children = left;
        if (left) left->next = right;
        left = node;
    }
    return left;
}

static void arith_error(const char *msg) {
    if (!parser_panic_mode) diag_error(TOK_LOC, "%s, found %s", msg, tok_desc());
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

static bool is_class_name_token(void) {
    if (tok.kind != TOK_IDENT) return false;
    char name[64];
    size_t n = tok.length < sizeof(name) - 1 ? tok.length : sizeof(name) - 1;
    memcpy(name, tok.lexeme, n);
    name[n] = '\0';
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    return sym && sym->level == -2;
}

static bool is_mnemonic_name(void) {
    if (tok.kind != TOK_IDENT) return false;
    char name[64];
    size_t n = tok.length < sizeof(name) - 1 ? tok.length : sizeof(name) - 1;
    memcpy(name, tok.lexeme, n);
    name[n] = '\0';
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    return sym && sym->level == -1;
}

static bool is_class_or_sign(TokenKind kind) {
    return kind == TOK_NUMERIC || kind == TOK_ALPHABETIC || kind == TOK_ALPHABETIC_LOWER ||
           kind == TOK_ALPHABETIC_UPPER || kind == TOK_POSITIVE || kind == TOK_NEGATIVE ||
           (kind == TOK_IDENT && is_class_name_token());
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
    SourceLocation loc = {tok.filename, tok.line, tok.column};
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
static bool expression_node(const AstNode *n) {
    return n && (n->kind == AST_EXPR_ADD || n->kind == AST_EXPR_SUB || n->kind == AST_EXPR_MUL ||
                 n->kind == AST_EXPR_DIV || n->kind == AST_EXPR_POW || n->kind == AST_EXPR_NEG);
}

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
        if (!object) { ast_free(subject); arith_error("Expected an operand after the relational operator"); return NULL; }
        if (expression_node(subject) || expression_node(object))
            diag_subset(MOD_NC, 2, subject->loc, "An arithmetic expression in a relation condition");
        if (negated) op = negate_relop(op);
        cond = make_relation(subject, op, object);
        abbrev_subject = subject;
        abbrev_op = cond->text;
        return cond;
    }

    const char *word;
    AstKind kind = AST_COND_CLASS;
    if (tok.kind == TOK_IDENT) {
        // A class-name from SPECIAL-NAMES
        char *cls = tok_to_str();
        next();
        cond = make_cond(AST_COND_CLASS, subject, NULL, subject->loc);
        cond->name = strdup("USER");
        cond->text = cls;
        if (negated) cond = make_cond(AST_COND_NOT, cond, NULL, cond->loc);
        return cond;
    }
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
    if (kind == AST_COND_SIGN) diag_subset(MOD_NC, 2, TOK_LOC, "A sign condition");
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
    copy.quiet = true;
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
        // Arithmetic never contains these at any depth: ((a = b)) is a condition
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
    SourceLocation loc = {tok.filename, tok.line, tok.column};

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
        if (!object) { arith_error("Expected an operand after the relational operator"); return NULL; }
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
    if (!subject) { arith_error("Expected a condition"); return NULL; }  // (found ... appended)
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
    if (subject->kind == AST_IDENTIFIER && subject->name) {
        diag_error(loc, "'%s' is not a condition-name, so it is not a condition by itself", subject->name);
        diag_note(TOK_LOC, "expected a relational operator (=, >, <, ...), a class or a sign condition here, found %s",
                  tok_desc());
    } else {
        diag_error(loc, "Expected a condition");
        diag_note(TOK_LOC, "expected a relational operator here, found %s", tok_desc());
    }
    ast_free(subject);
    synchronize();
    return NULL;
}

static AstNode *parse_not_cond(void) {
    if (tok.kind == TOK_NOT && !(abbrev_subject && is_relop_start(peek_token().kind))) {
        SourceLocation loc = {tok.filename, tok.line, tok.column};
        next();
        AstNode *inner = parse_not_cond();
        return inner ? make_cond(AST_COND_NOT, inner, NULL, loc) : NULL;
    }
    return parse_primary_cond();
}

static AstNode *parse_and_cond(void) {
    AstNode *left = parse_not_cond();
    while (left && tok.kind == TOK_AND) {
        diag_subset(MOD_NC, 2, TOK_LOC, "A combined condition (AND)");
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
        diag_subset(MOD_NC, 2, TOK_LOC, "A combined condition (OR)");
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
    SourceLocation loc = {tok.filename, tok.line, tok.column};
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
    SourceLocation loc = {tok.filename, tok.line, tok.column};
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
        SourceLocation loc = {tok.filename, tok.line, tok.column};
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
    AstNode *proc = ast_new(AST_IDENTIFIER, name, (SourceLocation){tok.filename, tok.line, tok.column});
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    if (sym) symtab_mark_symbol_as_used(sym);
    free(name);
    next();
    // paragraph-name OF|IN section-name (extra = the section)
    if ((tok.kind == TOK_OF || tok.kind == TOK_IN) &&
        (peek_token().kind == TOK_IDENT || peek_token().kind == TOK_NUMBER)) {
        next();
        char *sec = tok_to_str();
        proc->extra = ast_new(AST_IDENTIFIER, sec, (SourceLocation){tok.filename, tok.line, tok.column});
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
           kind == TOK_END_READ || kind == TOK_END_WRITE || kind == TOK_END_REWRITE ||
           kind == TOK_END_START || kind == TOK_END_DELETE || kind == TOK_END_CALL ||
           kind == TOK_END_RETURN || kind == TOK_END_RECEIVE || kind == TOK_END_STRING ||
           kind == TOK_END_UNSTRING;
}

// True where a statement sequence ends: end of sentence, a phrase keyword of
// an enclosing statement, a scope terminator, or a procedure name (a word or
// an all-digit name; no statement starts with a number).
// GOBACK (COBOL 2002, IBM) as a statement: not a reserved word of
// COBOL-85, so in Area A of fixed format it is still a paragraph name
static bool at_goback(void) {
    return tok.kind == TOK_IDENT && tok.length == 6 && strncasecmp(tok.lexeme, "GOBACK", 6) == 0 &&
           (current_lexer->mode->free_format || tok.column > 11);
}

static bool at_block_end(void) {
    return tok.kind == TOK_PERIOD || tok.kind == TOK_EOF || tok.kind == TOK_ELSE ||
           tok.kind == TOK_NOT || tok.kind == TOK_WHEN || (tok.kind == TOK_IDENT && !at_goback()) ||
           tok.kind == TOK_INVALID || tok.kind == TOK_AT || tok.kind == TOK_END_OF_PAGE ||
           tok.kind == TOK_EXCEPTION || tok.kind == TOK_OVERFLOW ||
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
        diag_error(TOK_LOC, "Expected an imperative statement, found %s", tok_desc());
        SourceLocation loc = {tok.filename, tok.line, tok.column};
        head = ast_new(AST_STATEMENT_CONTINUE, NULL, loc);
    }
    return head;
}

// [ON] SIZE ERROR imperative  [NOT [ON] SIZE ERROR imperative]  [end_tok]
// The kind of the token after the next one (two tokens of lookahead)
static TokenKind peek_kind2(void) {
    Lexer copy = *current_lexer;
    copy.quiet = true;
    copy.prev = NULL;
    lexer_next(&copy);
    return lexer_next(&copy).kind;
}

// [NOT] [ON] word: whether the phrase at tok is the one of word (an
// enclosing statement's NOT ON EXCEPTION is not ours)
static bool phrase_is(TokenKind word) {
    if (tok.kind == word) return true;
    if (tok.kind == TOK_ON) return peek_token().kind == word;
    if (tok.kind == TOK_NOT) {
        TokenKind k = peek_token().kind;
        return k == word || (k == TOK_ON && peek_kind2() == word);
    }
    return false;
}

static void parse_size_error_phrases(AstNode *stmt, TokenKind end_tok) {
    if ((tok.kind == TOK_ON && phrase_is(TOK_SIZE)) || tok.kind == TOK_SIZE) {
        if (tok.kind == TOK_ON) next();
        expect(TOK_SIZE, "SIZE");
        expect(TOK_ERROR, "ERROR");
        if (parser_panic_mode) return;
        stmt->on_error = parse_imperative_block();
    }
    if (tok.kind == TOK_NOT && phrase_is(TOK_SIZE)) {
        next();
        if (tok.kind == TOK_ON) next();
        expect(TOK_SIZE, "SIZE");
        expect(TOK_ERROR, "ERROR");
        if (parser_panic_mode) return;
        stmt->not_on_error = parse_imperative_block();
    }
    if (tok.kind == end_tok) next();
}

// [ON] OVERFLOW imperative  [NOT [ON] OVERFLOW imperative]  [end_tok]
static void parse_overflow_phrases(AstNode *stmt, TokenKind end_tok) {
    if ((tok.kind == TOK_ON && phrase_is(TOK_OVERFLOW)) || tok.kind == TOK_OVERFLOW) {
        if (tok.kind == TOK_ON) next();
        expect(TOK_OVERFLOW, "OVERFLOW");
        if (parser_panic_mode) return;
        stmt->on_error = parse_imperative_block();
    }
    if (tok.kind == TOK_NOT && phrase_is(TOK_OVERFLOW)) {
        next();
        if (tok.kind == TOK_ON) next();
        expect(TOK_OVERFLOW, "OVERFLOW");
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

// [AT] END s / INVALID [KEY] s, then NOT [AT] END s / NOT INVALID [KEY] s:
// on_error and not_on_error; is_signed set for the INVALID KEY form
static int file_org_of(const char *name);
static CobolModule org_module(int org);

// The module of the file (or record) an I-O statement names
static CobolModule stmt_module(const AstNode *stmt) {
    return stmt->extra && stmt->extra->name ? org_module(file_org_of(stmt->extra->name)) : MOD_SQ;
}

static bool parse_io_phrases(AstNode *stmt) {
    if (tok.kind == TOK_INVALID) diag_subset(stmt_module(stmt), 1, TOK_LOC, "INVALID KEY");
    if (tok.kind == TOK_AT || tok.kind == TOK_END) {
        if (tok.kind == TOK_AT) next();
        if (tok.kind != TOK_END) { arith_error("Expected END"); return false; }
        next();
        stmt->on_error = parse_imperative_block();
    } else if (tok.kind == TOK_INVALID) {
        next();
        if (tok.kind == TOK_KEY) next();
        stmt->is_signed = true;
        stmt->on_error = parse_imperative_block();
    }
    if (tok.kind == TOK_NOT && (peek_token().kind == TOK_AT || peek_token().kind == TOK_END ||
                                peek_token().kind == TOK_INVALID)) {
        next();
        if (tok.kind == TOK_INVALID) {
            next();
            if (tok.kind == TOK_KEY) next();
            stmt->is_signed = true;
        } else {
            if (tok.kind == TOK_AT) next();
            if (tok.kind != TOK_END) { arith_error("Expected END"); return false; }
            next();
        }
        stmt->not_on_error = parse_imperative_block();
    }
    return true;
}

// Subset flagging: the elements of a higher level that are whole statements
static void subset_statement(void) {
    if (!diag_subset_active()) return;
    switch (tok.kind) {
        case TOK_COMPUTE:    diag_subset(MOD_NC, 2, TOK_LOC, "COMPUTE"); break;
        case TOK_EVALUATE:   diag_subset(MOD_NC, 2, TOK_LOC, "EVALUATE"); break;
        case TOK_INITIALIZE: diag_subset(MOD_NC, 2, TOK_LOC, "INITIALIZE"); break;
        case TOK_SEARCH:     diag_subset(MOD_NC, 2, TOK_LOC, "SEARCH"); break;
        case TOK_STRING_KW:  diag_subset(MOD_NC, 2, TOK_LOC, "STRING"); break;
        case TOK_UNSTRING:   diag_subset(MOD_NC, 2, TOK_LOC, "UNSTRING"); break;
        case TOK_CANCEL:     diag_subset(MOD_IC, 2, TOK_LOC, "CANCEL"); break;
        case TOK_SORT:       diag_subset(MOD_ST, 1, TOK_LOC, "SORT"); break;
        case TOK_MERGE:      diag_subset(MOD_ST, 1, TOK_LOC, "MERGE"); break;
        case TOK_RELEASE:    diag_subset(MOD_ST, 1, TOK_LOC, "RELEASE"); break;
        case TOK_RETURN:     diag_subset(MOD_ST, 1, TOK_LOC, "RETURN"); break;
        case TOK_INITIATE:   diag_subset(MOD_RW, 1, TOK_LOC, "INITIATE"); break;
        case TOK_GENERATE:   diag_subset(MOD_RW, 1, TOK_LOC, "GENERATE"); break;
        case TOK_TERMINATE:  diag_subset(MOD_RW, 1, TOK_LOC, "TERMINATE"); break;
        case TOK_SUPPRESS:   diag_subset(MOD_RW, 1, TOK_LOC, "SUPPRESS"); break;
        default: break;
    }
}

static int if_depth;                    // nested IF statements (subset flagging)
static int segment_limit;               // SEGMENT-LIMIT (subset flagging)

static AstNode *parse_statement(void) {
    if (parser_panic_mode) return NULL; // Don't try to parse statements if in panic mode

    SourceLocation loc = {tok.filename, tok.line, tok.column};
    subset_statement();

    if (tok.kind == TOK_DISPLAY) {
        next();
        AstNode *display = ast_new(AST_STATEMENT_DISPLAY, NULL, loc);
        AstNode *last = NULL, *arg;
        while (!parser_panic_mode && !(tok.kind == TOK_IDENT && tok_is_word("NO")) &&
               (arg = parse_value_operand()))
            list_append(&display->children, &last, arg);
        if (tok.kind == TOK_UPON) {         // the console is the only device
            diag_subset(MOD_NC, 2, TOK_LOC, "DISPLAY ... UPON");
            next();
            if (tok.kind == TOK_IDENT) next();
        }
        if (tok.kind == TOK_WITH || tok_is_word("NO")) {
            if (tok.kind == TOK_WITH) next();
            if (tok_is_word("NO")) next();
            if (tok.kind == TOK_ADVANCING) next();
            display->is_numeric = true;     // WITH NO ADVANCING
        }
        return display;
    }
    else if (tok.kind == TOK_STOP) {
        next();
        // STOP literal: shows the literal to the operator and suspends the
        // run; with no operator to resume it, execution continues
        if (tok.kind == TOK_STRING || tok.kind == TOK_NUMBER || is_figurative(tok.kind)) {
            flag_obsolete(loc.line, loc.column, "STOP literal");
            AstNode *display = ast_new(AST_STATEMENT_DISPLAY, NULL, loc);
            display->children = parse_value_operand();
            return display;
        }
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
            diag_subset(MOD_NC, 2, TOK_LOC, "CORRESPONDING");
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
            diag_subset(MOD_NC, 2, TOK_LOC, "CORRESPONDING");
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
        if (num_src == 0) { ast_free(add); arith_error("Expected an operand after ADD"); return NULL; }
        if (num_dest == 0) { ast_free(add); arith_error("Expected TO or GIVING in the ADD statement"); return NULL; }

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
            diag_subset(MOD_NC, 2, TOK_LOC, "CORRESPONDING");
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
                diag_subset(MOD_NC, 2, TOK_LOC, "DIVIDE ... REMAINDER");
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
        if (if_depth > 0) diag_subset(MOD_NC, 2, loc, "A nested IF statement");
        next();
        AstNode *if_stmt = ast_new(AST_STATEMENT_IF, NULL, loc);
        AstNode *cond = parse_condition();
        if (!cond) { ast_free(if_stmt); return NULL; }
        if_stmt->children = cond;

        if (tok.kind == TOK_THEN) next();
        AstNode *then_node = ast_new(AST_THEN_BLOCK, NULL, loc);
        if_depth++;
        then_node->children = parse_block();
        cond->next = then_node;

        if (tok.kind == TOK_ELSE) {
            next();
            AstNode *else_node = ast_new(AST_ELSE_BLOCK, NULL, loc);
            else_node->children = parse_block();
            then_node->next = else_node;
        }
        if_depth--;
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
                    undefined_name(first->loc, first->name);
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
                    diag_subset(MOD_NC, 2, TOK_LOC, "PERFORM ... WITH TEST");
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
                    diag_subset(MOD_NC, 2, TOK_LOC, "PERFORM ... VARYING");
                    AstNode *tail = NULL;
                    while (tok.kind == TOK_VARYING || (perf->loop && tok.kind == TOK_AFTER)) {
                        SourceLocation vloc = {tok.filename, tok.line, tok.column};
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
        // GO TO without a procedure: its target is set by an ALTER statement
        if (!go->children && tok.kind == TOK_PERIOD) {
            flag_obsolete(loc.line, loc.column, "GO TO without a procedure-name");
            diag_subset(MOD_NC, 2, loc, "GO TO without a procedure-name");
            return go;
        }
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
    // ─── STRING ──────────────────────────────────────────────────────────────
    // STRING {source+ DELIMITED BY {delimiter | SIZE}}+ INTO identifier
    //     [WITH POINTER identifier] [[ON] OVERFLOW ...] [NOT [ON] OVERFLOW ...]
    //     [END-STRING]
    else if (tok.kind == TOK_STRING_KW) {
        next();
        AstNode *st = ast_new(AST_STATEMENT_STRING, NULL, loc);
        AstNode *tail = NULL;
        while (inspect_operand_start() && !parser_panic_mode) {
            AstNode *group = NULL;
            while (inspect_operand_start() && !parser_panic_mode) {
                AstNode *src = tok.kind == TOK_IDENT ? parse_identifier() : parse_rel_operand();
                if (!src) { ast_free(st); return NULL; }
                list_append(&st->children, &tail, src);
                if (!group) group = src;
            }
            if (tok.kind != TOK_DELIMITED) { ast_free(st); arith_error("Expected DELIMITED BY"); return NULL; }
            next();
            if (tok.kind == TOK_BY) next();
            AstNode *delim = NULL;
            if (tok.kind == TOK_SIZE) next();
            else {
                delim = tok.kind == TOK_IDENT ? parse_identifier() : parse_rel_operand();
                if (!delim) { ast_free(st); arith_error("Expected a delimiter"); return NULL; }
            }
            // Each source holds its own copy of its group's delimiter
            for (AstNode *s = group; s; s = s->next) s->on_error = s == group ? delim : (delim ? ast_clone(delim) : NULL);
        }
        if (!st->children) { ast_free(st); arith_error("Expected a sending item after STRING"); return NULL; }
        if (tok.kind != TOK_INTO) { ast_free(st); arith_error("Expected INTO"); return NULL; }
        next();
        if (tok.kind != TOK_IDENT) { ast_free(st); arith_error("Expected the receiving item"); return NULL; }
        st->extra = parse_identifier();
        if (tok.kind == TOK_WITH) next();
        if (tok.kind == TOK_POINTER) {
            next();
            if (tok.kind != TOK_IDENT) { ast_free(st); arith_error("Expected the pointer item"); return NULL; }
            st->loop = parse_identifier();
        }
        parse_overflow_phrases(st, TOK_END_STRING);
        return st;
    }
    // ─── UNSTRING ────────────────────────────────────────────────────────────
    // UNSTRING identifier [DELIMITED BY [ALL] delimiter {OR [ALL] delimiter}...]
    //     INTO {identifier [DELIMITER IN identifier] [COUNT IN identifier]}+
    //     [WITH POINTER identifier] [TALLYING IN identifier]
    //     [[ON] OVERFLOW ...] [NOT [ON] OVERFLOW ...] [END-UNSTRING]
    else if (tok.kind == TOK_UNSTRING) {
        next();
        AstNode *un = ast_new(AST_STATEMENT_UNSTRING, NULL, loc);
        AstNode *spec = ast_new(AST_UNSTRING_SPEC, NULL, loc);
        un->extra = spec;
        if (tok.kind != TOK_IDENT) { ast_free(un); arith_error("Expected the sending item"); return NULL; }
        spec->extra = parse_identifier();
        if (tok.kind == TOK_DELIMITED) {
            next();
            if (tok.kind == TOK_BY) next();
            AstNode *dtail = NULL;
            for (;;) {
                bool all = false;
                if (tok.kind == TOK_ALL) { all = true; next(); }
                AstNode *d = tok.kind == TOK_IDENT ? parse_identifier() : parse_rel_operand();
                if (!d) { ast_free(un); arith_error("Expected a delimiter"); return NULL; }
                d->is_signed = all;
                list_append(&spec->children, &dtail, d);
                if (tok.kind != TOK_OR) break;
                next();
            }
        }
        if (tok.kind != TOK_INTO) { ast_free(un); arith_error("Expected INTO"); return NULL; }
        next();
        AstNode *rtail = NULL;
        while (tok.kind == TOK_IDENT && !parser_panic_mode) {
            AstNode *r = parse_identifier();
            list_append(&un->children, &rtail, r);
            if (tok.kind == TOK_DELIMITER) {
                next();
                if (tok.kind == TOK_IN) next();
                if (tok.kind != TOK_IDENT) { ast_free(un); arith_error("Expected the DELIMITER IN item"); return NULL; }
                r->on_error = parse_identifier();
            }
            if (tok.kind == TOK_COUNT) {
                next();
                if (tok.kind == TOK_IN) next();
                if (tok.kind != TOK_IDENT) { ast_free(un); arith_error("Expected the COUNT IN item"); return NULL; }
                r->not_on_error = parse_identifier();
            }
        }
        if (!un->children) { ast_free(un); arith_error("Expected a receiving item after INTO"); return NULL; }
        if (tok.kind == TOK_WITH) next();
        if (tok.kind == TOK_POINTER) {
            next();
            if (tok.kind != TOK_IDENT) { ast_free(un); arith_error("Expected the pointer item"); return NULL; }
            spec->loop = parse_identifier();
        }
        if (tok.kind == TOK_TALLYING) {
            next();
            if (tok.kind == TOK_IN) next();
            if (tok.kind != TOK_IDENT) { ast_free(un); arith_error("Expected the TALLYING IN item"); return NULL; }
            spec->on_error = parse_identifier();
        }
        parse_overflow_phrases(un, TOK_END_UNSTRING);
        return un;
    }
    // ─── ALTER ───────────────────────────────────────────────────────────────
    // ALTER {paragraph TO [PROCEED TO] procedure}+
    else if (tok.kind == TOK_ALTER) {
        flag_obsolete(loc.line, loc.column, "ALTER");
        next();
        AstNode *alter = ast_new(AST_STATEMENT_ALTER, NULL, loc);
        AstNode *tail = NULL;
        while (is_procedure_name_token() && !parser_panic_mode) {
            if (alter->children) diag_subset(MOD_NC, 2, TOK_LOC, "ALTER with more than one procedure");
            AstNode *para = parse_procedure_name();
            if (tok.kind != TOK_TO) { ast_free(para); ast_free(alter); arith_error("Expected TO"); return NULL; }
            next();
            if (tok_is_word("PROCEED")) {
                next();
                if (tok.kind != TOK_TO) { ast_free(para); ast_free(alter); arith_error("Expected TO"); return NULL; }
                next();
            }
            if (!is_procedure_name_token()) { ast_free(para); ast_free(alter); arith_error("Expected a procedure name"); return NULL; }
            para->children = parse_procedure_name();
            list_append(&alter->children, &tail, para);
        }
        if (!alter->children) { ast_free(alter); arith_error("Expected a paragraph name after ALTER"); return NULL; }
        return alter;
    }
    else if (tok.kind == TOK_NEXT) {
        next();
        expect(TOK_SENTENCE, "SENTENCE");
        return ast_new(AST_STATEMENT_NEXT_SENTENCE, NULL, loc);
    }
    else if (at_goback()) {
        // GOBACK: EXIT PROGRAM in a called program, STOP RUN in the main one
        diag_feature(F_GOBACK, loc, NULL);
        next();
        AstNode *exit_stmt = ast_new(AST_STATEMENT_EXIT, NULL, loc);
        exit_stmt->name = strdup("GOBACK");
        return exit_stmt;
    }
    else if (tok.kind == TOK_EXIT) {
        next();
        AstNode *exit_stmt = ast_new(AST_STATEMENT_EXIT, NULL, loc);
        if (tok.kind == TOK_IDENT && tok.length == 7 && strncasecmp(tok.lexeme, "PROGRAM", 7) == 0) {
            exit_stmt->name = strdup("PROGRAM");
            next();
        } else if (tok.kind == TOK_PERFORM) {
            // EXIT PERFORM [CYCLE] (COBOL 2002, IBM): leave the inline
            // PERFORM, or end its iteration
            next();
            if (tok.kind == TOK_IDENT && tok.length == 5 && strncasecmp(tok.lexeme, "CYCLE", 5) == 0) {
                diag_feature(F_EXIT_PERFORM_CYCLE, loc, NULL);
                exit_stmt->name = strdup("CYCLE");
                next();
            } else {
                diag_feature(F_EXIT_PERFORM, loc, NULL);
                exit_stmt->name = strdup("PERFORM");
            }
        } else if (tok.kind == TOK_SECTION ||
                   (tok.kind == TOK_IDENT && tok.length == 9 && strncasecmp(tok.lexeme, "PARAGRAPH", 9) == 0)) {
            // EXIT PARAGRAPH / EXIT SECTION (COBOL 2002, IBM): to the end of
            // the paragraph or section
            bool section = tok.kind == TOK_SECTION;
            diag_feature(section ? F_EXIT_SECTION : F_EXIT_PARAGRAPH, loc, NULL);
            exit_stmt->name = strdup(section ? "SECTION" : "PARAGRAPH");
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
            SourceLocation wloc = {tok.filename, tok.line, tok.column};
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
    // {SET mnemonic-name+ TO ON|OFF}+      (name SWITCH; each receiver's text ON / OFF)
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
                diag_subset(MOD_NC, 2, loc, "SET ... TO TRUE");
                next();
                set->name = strdup("TRUE");
                for (AstNode *c = set->children; c; c = c->next) {
                    if (!is_condition_name(c))
                        diag_error(c->loc, "'%s' is not a condition-name", c->name);
                }
                return set;
            }
            if (tok.kind == TOK_ON || tok_is_word("OFF")) {
                set->name = strdup("SWITCH");
                AstNode *group = set->children;
                for (;;) {
                    const char *state = tok.kind == TOK_ON ? "ON" : "OFF";
                    for (; group; group = group->next) group->text = strdup(state);
                    next();
                    // another group: mnemonic-name+ TO ON|OFF
                    if (tok.kind != TOK_IDENT || !is_mnemonic_name()) break;
                    while (tok.kind == TOK_IDENT && !parser_panic_mode) {
                        AstNode *r = parse_identifier();
                        if (!group) group = r;
                        list_append(&set->children, &tail, r);
                    }
                    if (tok.kind != TOK_TO) { arith_error("Expected TO"); break; }
                    next();
                    if (tok.kind != TOK_ON && !tok_is_word("OFF")) { arith_error("Expected ON or OFF"); break; }
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
    //     [REPLACING {category [DATA] BY operand}+]
    // children: receivers; extra: one node per REPLACING phrase (name: the
    // category, children: the operand)
    else if (tok.kind == TOK_INITIALIZE) {
        next();
        AstNode *init = ast_new(AST_STATEMENT_INITIALIZE, NULL, loc);
        AstNode *tail = NULL;
        while (tok.kind == TOK_IDENT && !parser_panic_mode)
            list_append(&init->children, &tail, parse_identifier());
        if (!init->children) { ast_free(init); arith_error("Expected identifier after INITIALIZE"); return NULL; }
        if (tok.kind == TOK_REPLACING) {
            next();
            AstNode *rtail = NULL;
            for (;;) {
                const char *category = tok.kind == TOK_ALPHABETIC ? "ALPHABETIC" :
                                       tok.kind == TOK_NUMERIC ? "NUMERIC" :
                                       tok_is_word("ALPHANUMERIC") ? "ALPHANUMERIC" :
                                       tok_is_word("ALPHANUMERIC-EDITED") ? "ALPHANUMERIC-EDITED" :
                                       tok_is_word("NUMERIC-EDITED") ? "NUMERIC-EDITED" : NULL;
                if (!category) break;
                SourceLocation rloc = {tok.filename, tok.line, tok.column};
                next();
                if (tok.kind == TOK_DATA) next();
                if (tok.kind != TOK_BY) { ast_free(init); arith_error("Expected BY"); return NULL; }
                next();
                AstNode *phrase = ast_new(AST_IDENTIFIER, category, rloc);
                phrase->children = parse_rel_operand();
                if (!phrase->children) { ast_free(phrase); ast_free(init); return NULL; }
                list_append(&init->extra, &rtail, phrase);
            }
            if (!init->extra) { ast_free(init); arith_error("Expected a category after REPLACING"); return NULL; }
        }
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
            SourceLocation wloc = {tok.filename, tok.line, tok.column};
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
            diag_subset(MOD_NC, 2, TOK_LOC, "INSPECT ... CONVERTING");
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
        SourceLocation mode_loc = loc;
        while (!parser_panic_mode) {
            if (is_open && (tok.kind == TOK_INPUT || tok.kind == TOK_OUTPUT ||
                            tok.kind == TOK_I_O || tok.kind == TOK_EXTEND)) {
                mode = tok.kind == TOK_INPUT ? 1 : tok.kind == TOK_OUTPUT ? 2 : tok.kind == TOK_I_O ? 3 : 4;
                mode_loc = TOK_LOC;
                next();
            } else if (tok.kind == TOK_IDENT && (!is_open || mode) && !tok_is_word("REVERSED") &&
                       !tok_is_word("REEL") && !tok_is_word("UNIT") && !tok_is_word("NO") &&
                       !tok_is_word("LOCK")) {
                AstNode *file = parse_procedure_name();
                file->decimal_places = mode;
                list_append(&stmt->children, &tail, file);
                CobolModule fmod = org_module(file_org_of(file->name));
                if (mode == 4) diag_subset(fmod, 2, mode_loc, "OPEN EXTEND");
                // OPEN options (digits): 1 REVERSED, 2 WITH NO REWIND; CLOSE
                // options: 1 REEL / UNIT [FOR REMOVAL], 2 WITH NO REWIND, 4 WITH LOCK
                while (tok_is_word("REEL") || tok_is_word("UNIT") || tok.kind == TOK_WITH ||
                       tok_is_word("NO") || tok_is_word("REWIND") || tok_is_word("LOCK") ||
                       tok_is_word("REVERSED") || tok.kind == TOK_FOR || tok_is_word("REMOVAL")) {
                    if (tok_is_word("REEL") || tok_is_word("UNIT")) {
                        diag_subset(MOD_SQ, 2, TOK_LOC, "CLOSE REEL / UNIT");
                        file->digits |= 1;
                    }
                    else if (tok_is_word("REVERSED")) {
                        file->digits |= 1;
                        flag_obsolete(tok.line, tok.column, "OPEN REVERSED");
                        diag_subset(MOD_SQ, 2, TOK_LOC, "OPEN REVERSED");
                    }
                    else if (tok_is_word("REWIND")) {
                        diag_subset(MOD_SQ, 2, TOK_LOC, "WITH NO REWIND");
                        file->digits |= 2;
                    }
                    else if (tok_is_word("LOCK")) {
                        diag_subset(fmod, 2, TOK_LOC, "CLOSE WITH LOCK");
                        file->digits |= 4;
                    }
                    else if (tok_is_word("REMOVAL")) diag_subset(MOD_SQ, 2, TOK_LOC, "FOR REMOVAL");
                    next();
                }
            } else {
                break;
            }
        }
        if (!stmt->children) { ast_free(stmt); arith_error(is_open ? "Expected OPEN mode and file" : "Expected file name"); return NULL; }
        return stmt;
    }
    // ─── ENTER language-name [routine-name]: no other language here
    else if (tok.kind == TOK_ENTER) {
        flag_obsolete(loc.line, loc.column, "ENTER");
        next();
        while (tok.kind == TOK_IDENT) next();
        return ast_new(AST_STATEMENT_CONTINUE, NULL, loc);
    }
    // ─── CALL {identifier | literal} [USING {[BY REFERENCE] id... | BY CONTENT
    //     id...}...] [ON OVERFLOW s | [ON EXCEPTION s] [NOT ON EXCEPTION s]] [END-CALL]
    else if (tok.kind == TOK_CALL) {
        next();
        AstNode *call = ast_new(AST_STATEMENT_CALL, NULL, loc);
        if (tok.kind == TOK_STRING) {
            call->extra = ast_new(AST_STRING_LITERAL, NULL, loc);
            free(call->extra->name);
            call->extra->name = tok_value();
            next();
        } else if (tok.kind == TOK_IDENT) {
            call->extra = parse_identifier();
        } else {
            ast_free(call);
            arith_error("Expected the program name after CALL");
            return NULL;
        }
        AstNode *tail = NULL;
        if (tok.kind == TOK_USING) {
            next();
            bool content = false;
            for (;;) {
                if (tok.kind == TOK_BY) next();
                if (tok_is_word("REFERENCE")) { diag_subset(MOD_IC, 2, TOK_LOC, "CALL ... BY REFERENCE"); content = false; next(); continue; }
                if (tok_is_word("CONTENT")) { diag_subset(MOD_IC, 2, TOK_LOC, "CALL ... BY CONTENT"); content = true; next(); continue; }
                if (tok.kind != TOK_IDENT) break;
                AstNode *arg = parse_identifier();
                arg->is_signed = content;
                list_append(&call->children, &tail, arg);
            }
        }
        for (;;) {
            if (tok.kind == TOK_ON && (peek_token().kind == TOK_OVERFLOW || peek_token().kind == TOK_EXCEPTION))
                next();
            if (tok.kind == TOK_OVERFLOW || tok.kind == TOK_EXCEPTION) {
                next();
                call->on_error = parse_imperative_block();
            } else if (tok.kind == TOK_NOT && (peek_token().kind == TOK_ON || peek_token().kind == TOK_EXCEPTION ||
                                               peek_token().kind == TOK_OVERFLOW)) {
                next();
                if (tok.kind == TOK_ON) next();
                next();
                call->not_on_error = parse_imperative_block();
            } else {
                break;
            }
        }
        if (tok.kind == TOK_END_CALL) next();
        return call;
    }
    else if (tok.kind == TOK_CANCEL) {
        next();
        AstNode *cancel = ast_new(AST_STATEMENT_CANCEL, NULL, loc);
        AstNode *tail = NULL;
        while (tok.kind == TOK_STRING || tok.kind == TOK_IDENT) {
            AstNode *n;
            if (tok.kind == TOK_STRING) {
                n = ast_new(AST_STRING_LITERAL, NULL, loc);
                free(n->name);
                n->name = tok_value();
                next();
            } else {
                n = parse_identifier();
            }
            list_append(&cancel->children, &tail, n);
        }
        if (!cancel->children) { ast_free(cancel); arith_error("Expected a program name after CANCEL"); return NULL; }
        return cancel;
    }
    // ─── SORT file {ON {ASCENDING|DESCENDING} KEY data-name...}...
    //       [WITH DUPLICATES IN ORDER] [COLLATING SEQUENCE IS alphabet]
    //       {INPUT PROCEDURE IS p [THRU p] | USING file...}
    //       {OUTPUT PROCEDURE IS p [THRU p] | GIVING file...}
    //     MERGE file {ON ... KEY ...}... [COLLATING SEQUENCE IS alphabet]
    //       USING file file... {OUTPUT PROCEDURE ... | GIVING file...}
    else if (tok.kind == TOK_SORT || tok.kind == TOK_MERGE) {
        bool merge = tok.kind == TOK_MERGE;
        next();
        AstNode *st = ast_new(AST_STATEMENT_SORT, merge ? "MERGE" : "SORT", loc);
        if (tok.kind != TOK_IDENT) { ast_free(st); arith_error("Expected the sort file"); return NULL; }
        st->extra = parse_procedure_name();
        AstNode *key_tail = NULL, *loop_tail = NULL;
        for (;;) {
            if (tok.kind == TOK_ON) next();
            if (tok.kind != TOK_ASCENDING && tok.kind != TOK_DESCENDING) break;
            bool desc = tok.kind == TOK_DESCENDING;
            next();
            if (tok.kind == TOK_KEY) next();
            if (tok.kind == TOK_IS) next();
            while (tok.kind == TOK_IDENT && !tok_is_word("COLLATING") && !tok_is_word("DUPLICATES") &&
                   !tok_is_word("SEQUENCE")) {
                AstNode *k = parse_forward_identifier();
                k->is_signed = desc;
                list_append(&st->children, &key_tail, k);
            }
        }
        if (!st->children) { ast_free(st); arith_error("Expected ASCENDING or DESCENDING KEY"); return NULL; }
        if (tok.kind == TOK_WITH) next();
        if (tok_is_word("DUPLICATES")) {
            next();
            if (tok.kind == TOK_IN) next();
            if (tok_is_word("ORDER")) next();
        }
        if (tok_is_word("COLLATING") || tok_is_word("SEQUENCE")) {
            if (tok_is_word("COLLATING")) next();
            if (tok_is_word("SEQUENCE")) next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind == TOK_IDENT) { st->text = tok_to_str(); next(); }
        }
        for (int part = 0; part < 2; part++) {
            bool input = part == 0;
            if ((input && tok.kind == TOK_INPUT) || (!input && tok.kind == TOK_OUTPUT)) {
                next();
                if (tok.kind == TOK_PROCEDURE) next();
                if (tok.kind == TOK_IS) next();
                if (!is_procedure_name_token()) { ast_free(st); arith_error("Expected a procedure name"); return NULL; }
                AstNode *proc = parse_procedure_name();
                if (tok.kind == TOK_THROUGH) {
                    next();
                    proc->loop = parse_procedure_name();
                }
                if (input) st->on_error = proc;
                else st->not_on_error = proc;
            } else if ((input && tok.kind == TOK_USING) || (!input && tok.kind == TOK_GIVING)) {
                AstNode *files = ast_new(AST_SPECIAL, input ? "USING" : "GIVING", loc);
                AstNode *ftail = NULL;
                next();
                while (tok.kind == TOK_IDENT) list_append(&files->children, &ftail, parse_procedure_name());
                list_append(&st->loop, &loop_tail, files);
            } else {
                ast_free(st);
                arith_error(input ? "Expected INPUT PROCEDURE or USING" : "Expected OUTPUT PROCEDURE or GIVING");
                return NULL;
            }
        }
        return st;
    }
    // ─── RELEASE record [FROM identifier]
    else if (tok.kind == TOK_RELEASE) {
        next();
        AstNode *rel = ast_new(AST_STATEMENT_RELEASE, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(rel); arith_error("Expected a sort record"); return NULL; }
        rel->extra = parse_identifier();
        if (tok.kind == TOK_FROM) {
            next();
            rel->loop = parse_value_operand();
            if (!rel->loop) { ast_free(rel); arith_error("Expected identifier after FROM"); return NULL; }
        }
        return rel;
    }
    // ─── RETURN file [RECORD] [INTO identifier] [AT] END s [NOT [AT] END s] [END-RETURN]
    else if (tok.kind == TOK_RETURN) {
        next();
        AstNode *ret = ast_new(AST_STATEMENT_RETURN, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(ret); arith_error("Expected a sort file"); return NULL; }
        ret->extra = parse_procedure_name();
        if (tok.kind == TOK_RECORD) next();
        if (tok.kind == TOK_INTO) {
            next();
            if (tok.kind != TOK_IDENT) { ast_free(ret); arith_error("Expected identifier after INTO"); return NULL; }
            ret->loop = parse_identifier();
        }
        if (!parse_io_phrases(ret)) { ast_free(ret); return NULL; }
        if (tok.kind == TOK_END_RETURN) next();
        return ret;
    }
    // ─── DELETE file [RECORD] [INVALID [KEY] s] [NOT INVALID [KEY] s] [END-DELETE]
    else if (tok.kind == TOK_DELETE) {
        next();
        AstNode *del = ast_new(AST_STATEMENT_DELETE, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(del); arith_error("Expected file name after DELETE"); return NULL; }
        del->extra = parse_procedure_name();
        diag_subset(stmt_module(del), 1, loc, "DELETE");
        if (tok.kind == TOK_RECORD) next();
        if (!parse_io_phrases(del)) { ast_free(del); return NULL; }
        if (tok.kind == TOK_END_DELETE) next();
        return del;
    }
    // ─── START file [KEY {IS = | EQUAL TO | > | GREATER THAN | NOT < | NOT LESS
    //     THAN | >= ...} data-name] [INVALID [KEY] s] [NOT INVALID [KEY] s] [END-START]
    //     text: the relation (=, > or >=); children: the data-name
    else if (tok.kind == TOK_START) {
        next();
        AstNode *st = ast_new(AST_STATEMENT_START, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(st); arith_error("Expected file name after START"); return NULL; }
        st->extra = parse_procedure_name();
        diag_subset(stmt_module(st), 2, loc, "START");
        st->text = strdup("=");
        if (tok.kind == TOK_KEY) {
            next();
            if (tok.kind == TOK_IS) next();
            bool negated = tok.kind == TOK_NOT;
            if (negated) next();
            const char *op = parse_relop();
            if (!op) { ast_free(st); arith_error("Expected a relation after KEY"); return NULL; }
            if (negated) op = negate_relop(op);
            free(st->text);
            if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 || strcmp(op, "<>") == 0) {
                // NOT < is >=; NOT > and NOT = are not START relations
                if (strcmp(op, ">=") != 0)
                    diag_error(loc, "START KEY allows =, >, NOT < and >= only");
            }
            st->text = strdup(op);
            if (tok.kind != TOK_IDENT) { ast_free(st); arith_error("Expected the key data item"); return NULL; }
            st->children = parse_identifier();
        }
        if (!parse_io_phrases(st)) { ast_free(st); return NULL; }
        if (tok.kind == TOK_END_START) next();
        return st;
    }
    // ─── READ file [NEXT] [RECORD] [INTO id] [KEY IS data-name]
    //     {[AT] END s | INVALID [KEY] s} {NOT [AT] END s | NOT INVALID [KEY] s} [END-READ]
    //     is_numeric: NEXT; children: the KEY IS data-name; is_signed: INVALID KEY
    else if (tok.kind == TOK_READ) {
        next();
        AstNode *read = ast_new(AST_STATEMENT_READ, NULL, loc);
        if (tok.kind != TOK_IDENT) { ast_free(read); arith_error("Expected file name after READ"); return NULL; }
        read->extra = parse_procedure_name();
        if (tok.kind == TOK_NEXT) { diag_subset(stmt_module(read), 2, TOK_LOC, "READ ... NEXT"); read->is_numeric = true; next(); }
        if (tok.kind == TOK_RECORD) next();
        if (tok.kind == TOK_INTO) {
            next();
            if (tok.kind != TOK_IDENT) { ast_free(read); arith_error("Expected identifier after INTO"); return NULL; }
            read->loop = parse_identifier();
        }
        if (tok.kind == TOK_KEY) {
            diag_subset(MOD_IX, 2, TOK_LOC, "READ ... KEY IS");
            next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_IDENT) { ast_free(read); arith_error("Expected the key data item"); return NULL; }
            read->children = parse_identifier();
        }
        if (!parse_io_phrases(read)) { ast_free(read); return NULL; }
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
            Symbol *mn = tok.kind == TOK_IDENT ? symtab_resolve_symbol(current_symtab, tok_value_tmp()) : NULL;
            if (tok.kind == TOK_PAGE || (mn && mn->level == -1)) {
                // PAGE, or a mnemonic-name of a channel (C01, TOP): to the next page
                if (mn) symtab_mark_symbol_as_used(mn);
                w->children = ast_new(AST_BOOL_LITERAL, "PAGE", loc);
                next();
            } else {
                w->children = parse_arith_operand();
                if (!w->children) { ast_free(w); arith_error("Expected line count or PAGE"); return NULL; }
                if (tok.kind == TOK_LINE || tok.kind == TOK_LINES) next();
            }
        }
        if (is_write && (tok.kind == TOK_AT || tok.kind == TOK_END_OF_PAGE)) {
            diag_subset(MOD_SQ, 2, TOK_LOC, "WRITE ... AT END-OF-PAGE");
            if (tok.kind == TOK_AT) next();
            next();
            w->on_error = parse_imperative_block();     // the end-of-page condition (LINAGE)
        }
        if (is_write && tok.kind == TOK_NOT && peek_token().kind != TOK_INVALID) {
            next();
            if (tok.kind == TOK_AT) next();
            next();
            w->not_on_error = parse_imperative_block();
        }
        if (!w->on_error && !w->not_on_error && !parse_io_phrases(w)) { ast_free(w); return NULL; }
        if (tok.kind == TOK_END_WRITE || tok.kind == TOK_END_REWRITE) next();
        return w;
    }
    else if (tok.kind == TOK_ACCEPT) {
        next(); 
        AstNode *accept_stmt = ast_new(AST_STATEMENT_ACCEPT, NULL, loc);

        if (tok.kind == TOK_IDENT) {
            Token p = peek_token();
            if (p.kind == TOK_IDENT && p.length == 7 && strncasecmp(p.lexeme, "MESSAGE", 7) == 0) {
                communication_unsupported(loc, "ACCEPT MESSAGE COUNT");
                ast_free(accept_stmt);
                synchronize();
                return NULL;
            }
        }
        if (tok.kind == TOK_IDENT) {
            accept_stmt->children = parse_identifier();
            if (tok.kind == TOK_FROM) {
                // FROM DATE | DAY | DAY-OF-WEEK | TIME, or a device (the console)
                next();
                if (tok.kind == TOK_DATE) accept_stmt->name = strdup("DATE");
                else if (tok.kind == TOK_DAY) accept_stmt->name = strdup("DAY");
                else if (tok.kind == TOK_DAY_OF_WEEK) {
                    diag_subset(MOD_NC, 2, TOK_LOC, "ACCEPT ... FROM DAY-OF-WEEK");
                    accept_stmt->name = strdup("DAY-OF-WEEK");
                }
                else if (tok.kind == TOK_TIME) accept_stmt->name = strdup("TIME");
                else if (diag_later_element(TOK_LOC, LATER_PHRASE, tok.lexeme, tok.length)) {
                    ast_free(accept_stmt);
                    next();             // (ENVIRONMENT would stop the recovery)
                    synchronize();
                    return NULL;
                }
                else if (tok.kind != TOK_IDENT) { ast_free(accept_stmt); arith_error("Expected DATE, DAY, DAY-OF-WEEK, TIME or a device"); return NULL; }
                next();
                // DATE YYYYMMDD, DAY YYYYDDD: the year in four digits (an
                // extension, flagged by the lexer)
                if (accept_stmt->name && strcmp(accept_stmt->name, "DATE") == 0 && tok.kind == TOK_YYYYMMDD) {
                    free(accept_stmt->name);
                    accept_stmt->name = strdup("DATE YYYYMMDD");
                    next();
                } else if (accept_stmt->name && strcmp(accept_stmt->name, "DAY") == 0 && tok_spelled("YYYYDDD")) {
                    diag_feature(F_ACCEPT_DAY_YYYYDDD, TOK_LOC, NULL);
                    free(accept_stmt->name);
                    accept_stmt->name = strdup("DAY YYYYDDD");
                    next();
                }
            }
        } else {
            diag_error(TOK_LOC, "ACCEPT statement requires a variable name");
            synchronize(); // Error, enter panic mode
            return NULL;
        }

        return accept_stmt;
    }
    else if (tok.kind == TOK_CONTINUE) {
        next();
        return ast_new(AST_STATEMENT_CONTINUE, NULL, loc);
    }
    // ─── Report Writer: INITIATE report..., GENERATE name, TERMINATE report...,
    //     SUPPRESS PRINTING. Report and report group names are resolved in IR
    //     lowering.
    else if (tok.kind == TOK_INITIATE || tok.kind == TOK_TERMINATE) {
        AstNode *st = ast_new(tok.kind == TOK_INITIATE ? AST_STATEMENT_INITIATE : AST_STATEMENT_TERMINATE, NULL, loc);
        const char *verb = tok.kind == TOK_INITIATE ? "INITIATE" : "TERMINATE";
        next();
        AstNode *tail = NULL;
        while (tok.kind == TOK_IDENT) {
            char *name = tok_to_str();
            list_append(&st->children, &tail, ast_new(AST_IDENTIFIER, name, TOK_LOC));
            free(name);
            next();
        }
        if (!st->children) {
            ast_free(st);
            char msg[64];
            snprintf(msg, sizeof(msg), "Expected a report name after %s", verb);
            arith_error(msg);
            return NULL;
        }
        return st;
    }
    else if (tok.kind == TOK_GENERATE) {
        next();
        if (tok.kind != TOK_IDENT) { arith_error("Expected a detail report group or a report name after GENERATE"); return NULL; }
        AstNode *st = ast_new(AST_STATEMENT_GENERATE, NULL, loc);
        char *name = tok_to_str();
        st->extra = ast_new(AST_IDENTIFIER, name, TOK_LOC);
        free(name);
        next();
        return st;
    }
    else if (tok.kind == TOK_SUPPRESS) {
        next();
        if (tok_spelled("PRINTING")) next();
        return ast_new(AST_STATEMENT_SUPPRESS, NULL, loc);
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

    if (tok.kind == TOK_SEND || tok.kind == TOK_RECEIVE || tok.kind == TOK_ENABLE || tok.kind == TOK_DISABLE ||
        tok.kind == TOK_PURGE) {
        communication_unsupported(TOK_LOC, tok_desc());
        synchronize();
        return NULL;
    }
    if (tok.kind != TOK_EOF && !later_statement(NULL, TOK_LOC)) {
        diag_error(TOK_LOC, "Expected a statement, found %s", tok_desc());
        synchronize(); // Try to recover
    }
    return NULL;
}

// An entry that begins in Area A (fixed format): division and section
// headers, FD / SD / RD, level 01 and 77, DECLARATIVES
static void check_area_a(SourceLocation loc, const char *what) {
    if (!current_lexer->mode->free_format && loc.column > 11)
        diag_warning(W_AREA, loc, "%s begins in Area B (column %d); it belongs in Area A, columns 8 to 11",
                     what, loc.column);
}

// A statement starting in Area A (fixed format)
static void check_statement_area(void) {
    if (!current_lexer->mode->free_format && tok.column >= 8 && tok.column <= 11 && tok.kind != TOK_EOF)
        diag_warning(W_AREA, TOK_LOC, "The statement %s begins in Area A (column %d); statements belong in Area B, from column 12",
                     tok_desc(), tok.column);
}

// Past an EXEC ... END-EXEC block (after its error)
static void skip_exec_block(void) {
    while (tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
        if (tok_is_word("END-EXEC")) { next(); return; }
        next();
    }
}

// A statement of a later standard or a vendor at the current token (or the
// word already read, name / loc): reported as such, then skipped
static bool later_statement(const char *name, SourceLocation loc) {
    const char *word = name ? name : tok.lexeme;
    size_t len = name ? strlen(name) : tok.length;
    if (!name && (tok.kind == TOK_STRING || tok.kind == TOK_NUMBER || tok.kind == TOK_EOF)) return false;
    if (!diag_later_element(name ? loc : TOK_LOC, LATER_STATEMENT, word, len)) return false;
    if (len == 4 && strncasecmp(word, "EXEC", 4) == 0) skip_exec_block();
    else synchronize();
    return true;
}

// The token after statement last is a phrase of a later standard or a
// vendor (CALL ... RETURNING, DISPLAY ... AT): reported as such
static bool later_phrase_after(const AstNode *last) {
    if (!last || tok.kind == TOK_EOF || tok.kind == TOK_STRING || tok.kind == TOK_NUMBER) return false;
    bool call = last->kind == AST_STATEMENT_CALL;
    bool console = last->kind == AST_STATEMENT_DISPLAY || last->kind == AST_STATEMENT_ACCEPT;
    if (tok_is_word("RETURNING") && !call) return false;
    if (tok.kind == TOK_AT && !console) return false;
    if (!tok_is_word("RETURNING") && tok.kind != TOK_AT) return false;
    return diag_later_element(TOK_LOC, LATER_PHRASE, tok.lexeme, tok.length);
}

// Sentence: statements up to a period. AST_SENTENCE with the statements as children.
static AstNode *parse_sentence(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    AstNode *sent = ast_new(AST_SENTENCE, NULL, loc);
    AstNode *tail = NULL;
    int errors_before = error_count;

    while (!at_block_end() && !parser_panic_mode) {
        check_statement_area();
        AstNode *stmt = parse_statement();
        if (!stmt) break;
        list_append(&sent->children, &tail, stmt);
    }
    if (tok.kind == TOK_PERIOD) {
        next();
        synced_in_sentence = false;
    } else if (synced_at_period) {
        // error recovery already went past the period that ends the sentence
    } else if (tok.kind != TOK_IDENT && tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
               !(tok.kind == TOK_END && end_program_follows())) {
        // A token that cannot continue the sentence (e.g. a stray ELSE)
        if (error_count == errors_before && !synced_in_sentence) {
            if (tok.kind == TOK_ELSE)
                diag_error(TOK_LOC, "ELSE without a matching IF");
            else if (is_scope_terminator(tok.kind))
                diag_error(TOK_LOC, "%s does not end any statement here", tok_desc());
            else if (tok.kind == TOK_WHEN)
                diag_error(TOK_LOC, "WHEN outside an EVALUATE or SEARCH statement");
            else if (!later_phrase_after(tail))
                diag_error(TOK_LOC, "Unexpected %s in a sentence", tok_desc());
            if (prev_tok.kind == TOK_PERIOD &&
                (tok.kind == TOK_ELSE || is_scope_terminator(tok.kind) || tok.kind == TOK_WHEN))
                diag_note_plain("a period ends every statement still open, including IF and EVALUATE");
        }
        synchronize();
        if (tok.kind != TOK_EOF && !sent->children && !parser_panic_mode && at_block_end() &&
            tok.kind != TOK_IDENT && !is_division_start(tok.kind)) next();
    } else if (error_count == errors_before && sent->children) {
        if (later_phrase_after(tail)) synchronize();
        else diag_error(AFTER_PREV, "Expected '.' at the end of the sentence, before %s", tok_desc());
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
// END PROGRAM follows (tok is END)
static bool end_program_follows(void) {
    Token t = peek_token();
    return t.kind == TOK_IDENT && t.length == 7 && strncasecmp(t.lexeme, "PROGRAM", 7) == 0;
}

// USE statement, the first sentence of a declarative section:
//   USE [GLOBAL] AFTER STANDARD {EXCEPTION|ERROR} PROCEDURE [ON]
//       {file-name... | INPUT | OUTPUT | I-O | EXTEND}
//   USE FOR DEBUGGING [ON] {ALL PROCEDURES | [ALL REFERENCES OF] name...}
//   USE [GLOBAL] BEFORE REPORTING identifier
static AstNode *parse_use(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    next();
    AstNode *u = ast_new(AST_USE, NULL, loc);
    AstNode *tail = NULL;
    if (tok_is_word("GLOBAL")) { diag_subset(MOD_IC, 2, TOK_LOC, "USE GLOBAL"); u->is_signed = true; next(); }
    if (tok.kind == TOK_AFTER) {
        u->name = strdup("ERROR");
        next();
        if (tok_is_word("STANDARD")) next();
        if (tok.kind == TOK_EXCEPTION || tok.kind == TOK_ERROR) next();
        else arith_error("Expected EXCEPTION or ERROR");
        if (tok.kind == TOK_PROCEDURE) next();
        if (tok.kind == TOK_ON) next();
        if (tok.kind == TOK_INPUT || tok.kind == TOK_OUTPUT || tok.kind == TOK_I_O || tok.kind == TOK_EXTEND) {
            u->text = tok_to_str();
            next();
        } else {
            while (tok.kind == TOK_IDENT) list_append(&u->children, &tail, parse_procedure_name());
            if (!u->children) arith_error("Expected file names or INPUT, OUTPUT, I-O or EXTEND");
        }
    } else if (tok.kind == TOK_FOR) {
        flag_obsolete(loc.line, loc.column, "USE FOR DEBUGGING (the Debug module)");
        diag_subset(MOD_DB, 1, loc, "USE FOR DEBUGGING");
        u->name = strdup("DEBUGGING");
        next();
        if (tok_is_word("DEBUGGING")) next();
        if (tok.kind == TOK_ON) next();
        while (tok.kind == TOK_IDENT || tok.kind == TOK_ALL || is_procedure_name_token()) {
            if (tok.kind == TOK_ALL) {
                next();
                if (tok_is_word("PROCEDURES")) { free(u->text); u->text = strdup("ALL"); next(); continue; }
                if (tok_is_word("REFERENCES")) next();
                if (tok.kind == TOK_OF) next();
                if (tok.kind != TOK_IDENT) { arith_error("Expected an identifier"); break; }
                AstNode *id = parse_forward_identifier();
                id->is_signed = true;
                list_append(&u->children, &tail, id);
                continue;
            }
            list_append(&u->children, &tail, parse_forward_identifier());
        }
    } else if (tok.kind == TOK_BEFORE) {
        diag_subset(MOD_RW, 1, loc, "USE BEFORE REPORTING");
        u->name = strdup("REPORTING");
        next();
        if (tok_is_word("REPORTING")) next();
        if (tok.kind == TOK_IDENT) list_append(&u->children, &tail, parse_forward_identifier());
    } else {
        arith_error("Expected AFTER, FOR DEBUGGING or BEFORE REPORTING after USE");
    }
    expect(TOK_PERIOD, ".");
    parser_panic_mode = false;
    return u;
}

static void parse_procedure_body(AstNode *div) {
    AstNode *div_tail = NULL, *sec = NULL, *sec_tail = NULL;
    bool in_declaratives = false;

    while (tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
        const char *before = tok.lexeme;
        AstNode *node = NULL;
        if (tok_is_word("DECLARATIVES") && peek_token().kind == TOK_PERIOD && !in_declaratives) {
            check_area_a(TOK_LOC, "DECLARATIVES");
            next();
            next();
            in_declaratives = true;
            continue;
        }
        if (tok.kind == TOK_END && end_program_follows()) break;
        if (tok.kind == TOK_END && peek_token().kind == TOK_IDENT && in_declaratives) {
            check_area_a(TOK_LOC, "END DECLARATIVES");
            next();
            if (!tok_is_word("DECLARATIVES")) { arith_error("Expected END DECLARATIVES"); continue; }
            next();
            expect(TOK_PERIOD, ".");
            parser_panic_mode = false;
            in_declaratives = false;
            sec = NULL;
            continue;
        }
        if (is_procedure_name_token() && !at_goback()) {
            SourceLocation loc = {tok.filename, tok.line, tok.column};
            char *name = tok_to_str();
            next();
            if (tok.kind == TOK_SECTION) {
                next();
                if (!current_lexer->mode->free_format && loc.column > 11)
                    diag_warning(W_AREA, loc, "The section name '%s' begins in Area B (column %d); it belongs in Area A, columns 8 to 11",
                                 name, loc.column);
                sec = ast_new(AST_SECTION, name, loc);
                if (tok.kind == TOK_NUMBER) {
                    // segment-number (Segmentation module): kept in digits
                    flag_obsolete(tok.line, tok.column, "segment-number (the Segmentation module)");
                    char *seg = tok_to_str();
                    int number = atoi(seg);
                    diag_subset(MOD_SG, 1, TOK_LOC, "A segment-number");
                    if (segment_limit && number >= segment_limit && number < 50)
                        diag_subset(MOD_SG, 2, TOK_LOC, "An overlayable fixed segment (at or above SEGMENT-LIMIT)");
                    sec->digits = atoi(seg);
                    sec->decimal_places = 1;      // a segment number was given
                    free(seg);
                    next();
                }
                expect(TOK_PERIOD, ".");
                parser_panic_mode = false;
                sec_tail = NULL;
                sec->is_signed = in_declaratives;
                if (in_declaratives && tok.kind == TOK_USE) sec->extra = parse_use();
                else if (in_declaratives)
                    diag_error(TOK_LOC, "A declarative section must start with a USE statement");
                declare_procedure(name, SYM_SECTION, loc);
                list_append(&div->children, &div_tail, sec);
                free(name);
                continue;
            }
            if (tok.kind != TOK_PERIOD) {
                // A word followed by something else: a misspelled statement
                // rather than a paragraph name
                const char *near = diag_closest(name, statement_verbs, -1);
                if (later_statement(name, loc)) {
                    free(name);
                    parser_panic_mode = false;
                    continue;
                }
                if (near || loc.column > 11 || current_lexer->mode->free_format) {
                    diag_error(loc, "Expected a statement, found '%s'", name);
                    if (near) diag_note_plain("did you mean '%s'?", near);
                    else diag_note_plain("a paragraph name is followed by a period");
                } else {
                    diag_error(AFTER_PREV, "Expected '.' after the paragraph name '%s'", name);
                }
                free(name);
                synchronize();
                parser_panic_mode = false;
                continue;
            }
            expect(TOK_PERIOD, ".");
            parser_panic_mode = false;
            if (!current_lexer->mode->free_format && loc.column > 11)
                diag_warning(W_AREA, loc, "The paragraph name '%s' begins in Area B (column %d); it belongs in Area A, columns 8 to 11",
                             name, loc.column);
            node = ast_new(AST_PARAGRAPH, name, loc);
            declare_procedure(name, SYM_PARAGRAPH, loc);
            free(name);
            AstNode *ptail = NULL;
            while (!is_procedure_name_token() && tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
                   !(tok.kind == TOK_END && peek_token().kind == TOK_IDENT &&
                     (in_declaratives || end_program_follows()))) {
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

// COMMUNICATION SECTION or REPORT SECTION: another section of the DATA DIVISION
static bool data_section_header(void) {
    return (tok_spelled("COMMUNICATION") || tok_spelled("REPORT")) && peek_token().kind == TOK_SECTION;
}

// ── Report Writer: the REPORT SECTION ───────────────────────────────────────
// Each RD becomes an AST_REPORT (see ast.h). The storage is declared in the
// WORKING-STORAGE SECTION as ordinary records, so the PICTURE, VALUE and
// editing of report fields work as for any item:
//   01 report-name.  05 PAGE-COUNTER PIC 9(6).  05 LINE-COUNTER PIC 9(6).
//   01 $RWL<n>: each line of a report group, its fields at their COLUMNs
//               (FILLER VALUE SPACES between them)
//   01 $RWH<n>: the entries of a group that are not printed (no COLUMN)
// and the FD that names the report gets 01 $RWR-<file> PIC X(widest line).

static struct { AstNode *fd; char *report; } fd_reports[64];
static int num_fd_reports;
static int rw_counter;

// Character positions of a PICTURE (S, V and P take none; CR and DB two)
static int pic_positions(const char *pic) {
    int n = 0;
    for (const char *p = pic; *p; ) {
        char c = (char)toupper((unsigned char)*p++);
        int rep = 1;
        if (*p == '(') {
            rep = atoi(p + 1);
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
        }
        if (c == 'S' || c == 'V' || c == 'P') continue;
        n += rep;
    }
    return n;
}

// The data description entries in order, with their levels: an item is used
// when a group that holds it is (-Wunused)
typedef struct { Symbol *sym; int level; } DeclaredItem;
static DeclaredItem *declared;
static int num_declared, cap_declared;

static void record_declaration(Symbol *sym, int level) {
    if (num_declared == cap_declared) {
        cap_declared = cap_declared ? cap_declared * 2 : 256;
        declared = realloc(declared, (size_t)cap_declared * sizeof(DeclaredItem));
    }
    declared[num_declared++] = (DeclaredItem){ sym, level };
}

// Mark the items whose group is used
static void mark_used_through_groups(void) {
    int levels[64];
    bool used[64];
    int depth = 0;
    for (int k = 0; k < num_declared; k++) {
        int level = declared[k].level;
        if (level == 88 || level == 66) continue;
        if (level == 1 || level == 77) depth = 0;
        while (depth > 0 && levels[depth - 1] >= level) depth--;
        bool via = depth > 0 && used[depth - 1];
        Symbol *s = declared[k].sym;
        if (s && via) s->is_used = true;
        if (depth < 64) {
            levels[depth] = level;
            used[depth] = via || (s && s->is_used);
            depth++;
        }
    }
    num_declared = 0;
}

static AstNode *rw_decl(const char *name, const char *level, const char *pic, AstNode *value, SourceLocation loc) {
    AstNode *d = ast_new(AST_VAR_DECL, name, loc);
    d->text = strdup(level);
    AstNode *tail = NULL;
    if (pic) list_append(&d->children, &tail, ast_new(AST_PIC_CLAUSE, pic, loc));
    if (value) list_append(&d->children, &tail, value);
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    if (!sym && strcmp(name, "FILLER") != 0) sym = symtab_declare_symbol(current_symtab, name, SYM_VAR, loc);
    if (sym) {
        sym->level = atoi(level);
        symtab_mark_symbol_as_used(sym);
    }
    return d;
}

// The WORKING-STORAGE SECTION of a DATA DIVISION (made if there is none)
static AstNode *rw_working_storage(AstNode *div) {
    AstNode *last = NULL;
    for (AstNode *c = div->children; c; c = c->next) {
        if (c->kind == AST_SECTION && c->name && strcasecmp(c->name, "WORKING-STORAGE") == 0) return c;
        last = c;
    }
    AstNode *ws = ast_new(AST_SECTION, "WORKING-STORAGE", div->loc);
    if (last) last->next = ws;
    else div->children = ws;
    return ws;
}

static void rw_add_record(AstNode *ws, AstNode *decls) {
    AstNode **tail = &ws->children;
    while (*tail) tail = &(*tail)->next;
    *tail = decls;
}

static int rw_int(void) {
    if (tok.kind != TOK_NUMBER) { arith_error("Expected an integer"); return 0; }
    int v = atoi(tok_value_tmp());
    next();
    return v;
}

// LINE NUMBER or NEXT GROUP operand: integer [ON NEXT PAGE] | PLUS integer | NEXT PAGE
static void rw_position(int *kind, int *value) {
    if (tok.kind == TOK_IS) next();
    if (tok_spelled("PLUS")) {
        next();
        *kind = RW_AT_PLUS;
        *value = rw_int();
    } else if (tok.kind == TOK_NEXT) {
        next();
        if (tok.kind == TOK_PAGE) next();
        *kind = RW_AT_NEXT_PAGE;
        *value = 0;
    } else {
        *kind = RW_AT_LINE;
        *value = rw_int();
        if (tok.kind == TOK_ON && peek_token().kind == TOK_NEXT) {
            next(); next();
            if (tok.kind == TOK_PAGE) next();
            *kind = RW_AT_NEXT_PAGE;            // (the line on the next page)
        }
    }
}

typedef struct {
    int level;
    char *name;                 // data-name, or NULL
    SourceLocation loc;
    char *pic;
    AstNode *value;             // VALUE literal
    bool justified, blank_zero, has_pic;
    RwDesc rw;                  // LINE, NEXT GROUP, TYPE, COLUMN, SOURCE, SUM...
} RwEntry;

// One report group description entry, up to its period
static bool rw_entry(RwEntry *e) {
    memset(e, 0, sizeof(*e));
    e->loc = TOK_LOC;
    if (tok.kind == TOK_NUMBER && atoi(tok_value_tmp()) == 1) check_area_a(TOK_LOC, "A report group (level 01)");
    e->level = rw_int();
    if (tok.kind == TOK_IDENT && !tok_spelled("LINE") && !tok_spelled("TYPE") && !tok_spelled("COLUMN") &&
        !tok_spelled("SOURCE") && !tok_spelled("SUM") && !tok_spelled("GROUP") && !tok_spelled("NEXT")) {
        e->name = tok_to_str();
        e->loc = TOK_LOC;
        next();
    } else if (tok.kind == TOK_FILLER) {
        next();
    }
    AstNode *sum_tail = NULL;
    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
        if (tok.kind == TOK_LINE || tok_spelled("LINE")) {
            diag_subset(MOD_RW, 1, TOK_LOC, "The LINE clause");
            next();
            if (tok_spelled("NUMBER")) next();
            rw_position(&e->rw.line_kind, &e->rw.line);
        } else if (tok.kind == TOK_NEXT) {
            next();
            if (tok_spelled("GROUP")) next();
            rw_position(&e->rw.next_kind, &e->rw.next);
        } else if (tok_spelled("TYPE")) {
            diag_subset(MOD_RW, 1, TOK_LOC, "The TYPE clause");
            next();
            if (tok.kind == TOK_IS) next();
            bool control = false;
            if (tok_spelled("REPORT")) {
                next();
                e->rw.type = tok_spelled("HEADING") ? RW_RH : RW_RF;
                next();
            } else if (tok.kind == TOK_PAGE) {
                next();
                e->rw.type = tok_spelled("HEADING") ? RW_PH : RW_PF;
                next();
            } else if (tok_spelled("CONTROL")) {
                next();
                e->rw.type = tok_spelled("HEADING") ? RW_CH : RW_CF;
                next();
                control = true;
            } else if (tok_spelled("DETAIL") || tok_spelled("DE")) { e->rw.type = RW_DE; next(); }
            else if (tok_spelled("RH")) { e->rw.type = RW_RH; next(); }
            else if (tok_spelled("RF")) { e->rw.type = RW_RF; next(); }
            else if (tok_spelled("PH")) { e->rw.type = RW_PH; next(); }
            else if (tok_spelled("PF")) { e->rw.type = RW_PF; next(); }
            else if (tok_spelled("CH")) { e->rw.type = RW_CH; next(); control = true; }
            else if (tok_spelled("CF")) { e->rw.type = RW_CF; next(); control = true; }
            else { arith_error("Expected a report group type (REPORT HEADING, PAGE HEADING, CONTROL HEADING, "
                               "DETAIL, CONTROL FOOTING, PAGE FOOTING or REPORT FOOTING)"); return false; }
            if (control) {
                if (tok.kind != TOK_IDENT) { arith_error("Expected a control data-name or FINAL"); return false; }
                e->rw.control = tok_to_str();
                next();
            }
        } else if (tok.kind == TOK_USAGE) {
            next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind == TOK_DISPLAY) next();
        } else if (tok.kind == TOK_DISPLAY) {
            next();
        } else if (tok_spelled("COLUMN")) {
            diag_subset(MOD_RW, 1, TOK_LOC, "The COLUMN clause");
            next();
            if (tok_spelled("NUMBER")) next();
            if (tok.kind == TOK_IS) next();
            e->rw.column = rw_int();
        } else if (tok.kind == TOK_PIC) {
            next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_PIC_STRING) { arith_error("Expected a PICTURE character-string"); return false; }
            e->pic = tok_to_str();
            e->has_pic = true;
            next();
        } else if (tok_spelled("SOURCE")) {
            diag_subset(MOD_RW, 1, TOK_LOC, "The SOURCE clause");
            next();
            if (tok.kind == TOK_IS) next();
            e->rw.source = parse_identifier();
        } else if (tok_spelled("SUM")) {
            diag_subset(MOD_RW, 1, TOK_LOC, "The SUM clause");
            next();
            while (tok.kind == TOK_IDENT && !tok_spelled("UPON") && !tok_spelled("RESET") && !tok_spelled("LINE") &&
                   !tok_spelled("COLUMN") && !tok_spelled("SOURCE") && !tok_spelled("GROUP") && !tok_spelled("TYPE"))
                list_append(&e->rw.sum, &sum_tail, parse_identifier());
            if (tok.kind == TOK_UPON) {
                next();
                if (tok.kind == TOK_IDENT) { e->rw.upon = tok_to_str(); next(); }
            }
        } else if (tok_spelled("RESET")) {
            next();
            if (tok.kind == TOK_ON) next();
            if (tok.kind == TOK_IDENT) { e->rw.reset = tok_to_str(); next(); }
        } else if (tok_spelled("GROUP")) {
            next();
            if (tok_spelled("INDICATE")) next();
            e->rw.group_indicate = true;
        } else if (tok.kind == TOK_VALUE) {
            next();
            if (tok.kind == TOK_IS) next();
            SourceLocation vloc = TOK_LOC;
            char *val = tok_value();
            if (tok.kind == TOK_STRING) e->value = ast_new(AST_STRING_LITERAL, val, vloc);
            else if (tok.kind == TOK_NUMBER) e->value = ast_new(AST_LITERAL_NUMBER, val, vloc);
            else if (is_figurative(tok.kind)) e->value = ast_new(AST_FIGURATIVE_CONSTANT, val, vloc);
            else if (tok.kind == TOK_ALL) {
                free(val);
                e->value = parse_all_literal();
                if (!e->value) return false;
                continue;
            }
            else { free(val); arith_error("Expected a literal after VALUE"); return false; }
            free(val);
            next();
        } else if (tok.kind == TOK_JUSTIFIED) {
            next();
            if (tok.kind == TOK_RIGHT) next();
            e->justified = true;
        } else if (tok.kind == TOK_BLANK) {
            next();
            if (tok.kind == TOK_WHEN) next();
            if (tok.kind == TOK_ZEROS) next();
            e->blank_zero = true;
        } else {
            diag_error(TOK_LOC, "Unexpected %s in a report group description entry", tok_desc());
            synchronize();
            return false;
        }
    }
    if (tok.kind == TOK_PERIOD) next();
    return true;
}

// The data item of a report field, named for the entry
static AstNode *rw_field_decl(RwEntry *e, const char *name) {
    AstNode *d = rw_decl(name, "05", e->pic, e->value ? ast_clone(e->value) : NULL, e->loc);
    if (e->justified) {
        AstNode **t = &d->children;
        while (*t) t = &(*t)->next;
        *t = ast_new(AST_JUSTIFIED, NULL, e->loc);
    }
    d->blank_when_zero = e->blank_zero;
    return d;
}

// One report group: its entries become lines and fields
static AstNode *rw_group(RwEntry *entries, int n, AstNode *ws) {
    RwEntry *g = &entries[0];
    AstNode *group = ast_new(AST_REPORT_GROUP, g->name, g->loc);
    group->rw = calloc(1, sizeof(RwDesc));
    *group->rw = g->rw;
    g->rw.control = NULL;
    group->rw->source = NULL;           // (fields only)
    group->rw->sum = NULL;
    group->rw->upon = NULL;
    group->rw->reset = NULL;
    AstNode *unprinted = NULL, *unprinted_tail = NULL;
    if (group->rw->type == RW_NONE) {
        diag_error(g->loc, "The report group '%s' has no TYPE clause", g->name ? g->name : "(unnamed)");
        group->rw->type = RW_DE;
    }
    AstNode *line_tail = NULL, *line = NULL, *hidden = NULL, *hidden_tail = NULL;
    AstNode *line_decls = NULL, *line_decl_tail = NULL;
    int line_end = 0;               // the last column of the current line
    int pending_kind = RW_AT_NONE, pending_line = 0;  // an empty line's position
    for (int k = 0; k < n; k++) {
        RwEntry *e = &entries[k];
        if (e->rw.line_kind != RW_AT_NONE) {
            // a new line (an empty one before it only gives its position)
            if (line && !line->children) {
                pending_kind = line->rw->line_kind;
                pending_line = line->rw->line;
            }
            if (line_decls) { rw_add_record(ws, line_decls); line_decls = NULL; line_decl_tail = NULL; }
            if (line && line->children) list_append(&group->children, &line_tail, line);
            else if (line) ast_free(line);
            char lname[32];
            snprintf(lname, sizeof(lname), "$RWL%d", ++rw_counter);
            line = ast_new(AST_REPORT_LINE, NULL, e->loc);
            line->text = strdup(lname);
            line->rw = calloc(1, sizeof(RwDesc));
            line->rw->line_kind = e->rw.line_kind;
            line->rw->line = e->rw.line;
            if (pending_kind == RW_AT_LINE && e->rw.line_kind == RW_AT_PLUS) {
                line->rw->line_kind = RW_AT_LINE;
                line->rw->line = pending_line + e->rw.line;
            }
            pending_kind = RW_AT_NONE;
            line_decls = rw_decl(lname, "01", NULL, NULL, e->loc);
            line_decl_tail = line_decls;
            line_end = 0;
        }
        if (!e->has_pic) {
            if (e->rw.column || e->rw.source || e->rw.sum || e->value)
                diag_error(e->loc, "A report entry with %s needs a PICTURE clause",
                           e->rw.column ? "COLUMN" : e->rw.source ? "SOURCE" : e->rw.sum ? "SUM" : "VALUE");
            continue;
        }
        char fname[64];
        if (e->name) snprintf(fname, sizeof(fname), "%s", e->name);
        else snprintf(fname, sizeof(fname), "$RWF%d", ++rw_counter);
        AstNode *field = ast_new(AST_REPORT_FIELD, fname, e->loc);
        field->rw = calloc(1, sizeof(RwDesc));
        *field->rw = e->rw;
        e->rw.source = NULL; e->rw.sum = NULL; e->rw.upon = NULL; e->rw.reset = NULL; e->rw.control = NULL;
        if (e->value) field->children = ast_clone(e->value);
        int width = pic_positions(e->pic);
        if (e->rw.column > 0) {
            if (!line) {
                diag_error(e->loc, "The report field at COLUMN %d is not on a line: no LINE clause comes before it",
                           e->rw.column);
                ast_free(field);
                continue;
            }
            if (e->rw.column <= line_end) {
                diag_error(e->loc, "COLUMN %d overlaps the field before it, which ends at column %d", e->rw.column, line_end);
                ast_free(field);
                continue;
            }
            if (e->rw.column > line_end + 1) {
                char gap[32];
                snprintf(gap, sizeof(gap), "X(%d)", e->rw.column - line_end - 1);
                AstNode *f = rw_decl("FILLER", "05", gap, ast_new(AST_FIGURATIVE_CONSTANT, "SPACES", e->loc), e->loc);
                line_decl_tail->next = f;
                line_decl_tail = f;
            }
            AstNode *d = rw_field_decl(e, fname);
            line_decl_tail->next = d;
            line_decl_tail = d;
            line_end = e->rw.column + width - 1;
            AstNode **t = &line->children;
            while (*t) t = &(*t)->next;
            *t = field;
        } else {
            // not printed (a sum counter used by other entries, say)
            if (!hidden) {
                char hname[32];
                snprintf(hname, sizeof(hname), "$RWH%d", ++rw_counter);
                hidden = rw_decl(hname, "01", NULL, NULL, e->loc);
                hidden_tail = hidden;
            }
            AstNode *d = rw_field_decl(e, fname);
            hidden_tail->next = d;
            hidden_tail = d;
            list_append(&unprinted, &unprinted_tail, field);
        }
    }
    if (line_decls) rw_add_record(ws, line_decls);
    if (line && line->children) list_append(&group->children, &line_tail, line);
    else if (line) ast_free(line);
    if (hidden) rw_add_record(ws, hidden);
    // unprinted fields after the lines
    if (unprinted) list_append(&group->children, &line_tail, unprinted);
    if (!group->children)
        diag_error(g->loc, "The report group '%s' has nothing to print: no LINE clause, or no field with COLUMN",
                   g->name ? g->name : "(unnamed)");
    for (int k = 0; k < n; k++) {
        free(entries[k].name);
        free(entries[k].pic);
        ast_free(entries[k].value);
        ast_free(entries[k].rw.source);
        ast_free(entries[k].rw.sum);
        free(entries[k].rw.upon);
        free(entries[k].rw.reset);
        free(entries[k].rw.control);
    }
    return group;
}

// Lines of a record: the longest line record of a report (for the FD)
static int rw_line_width(AstNode *ws, const char *line_name) {
    int width = 0;
    bool in = false;
    for (AstNode *d = ws->children; d; d = d->next) {
        if (d->text && atoi(d->text) == 1) { if (in) break; in = d->name && strcmp(d->name, line_name) == 0; continue; }
        if (!in) continue;
        for (AstNode *c = d->children; c; c = c->next)
            if (c->kind == AST_PIC_CLAUSE) width += pic_positions(c->name);
    }
    return width;
}

static void parse_report_section(AstNode *div, AstNode **sec_tail) {
    SourceLocation loc = TOK_LOC;
    diag_subset(MOD_RW, 1, loc, "The REPORT SECTION");
    check_area_a(loc, "The REPORT SECTION header");
    next();                                     // REPORT
    next();                                     // SECTION
    expect(TOK_PERIOD, ".");
    parser_panic_mode = false;
    AstNode *sec = ast_new(AST_SECTION, "REPORT", loc);
    AstNode *ws = rw_working_storage(div);
    AstNode *rd_tail = NULL;
    while (tok_spelled("RD")) {
        diag_subset(MOD_RW, 1, TOK_LOC, "An RD entry");
        check_area_a(TOK_LOC, "An RD entry");
        next();
        SourceLocation rloc = TOK_LOC;          // (at the report name)
        if (tok.kind != TOK_IDENT) { arith_error("Expected a report name after RD"); break; }
        char *name = tok_to_str();
        AstNode *rd = ast_new(AST_REPORT, name, rloc);
        rd->rw = calloc(1, sizeof(RwDesc));
        next();
        AstNode *ctl_tail = NULL;
        bool page_seen = false;
        while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
            if (tok_spelled("CODE")) {
                next();
                if (tok.kind == TOK_IS) next();
                next();
            } else if (tok_spelled("CONTROL") || tok_spelled("CONTROLS")) {
                next();
                if (tok.kind == TOK_IS) next();
                while (tok.kind == TOK_IDENT && !tok_spelled("PAGE") && !tok_spelled("CODE")) {
                    if (tok_spelled("FINAL")) { rd->rw->final_control = true; next(); continue; }
                    list_append(&rd->extra, &ctl_tail, parse_identifier());
                }
            } else if (tok.kind == TOK_PAGE) {
                next();
                if (tok_spelled("LIMIT") || tok_spelled("LIMITS")) next();
                if (tok.kind == TOK_IS) next();
                rd->rw->page = rw_int();
                if (tok.kind == TOK_LINE || tok.kind == TOK_LINES || tok_spelled("LINE") || tok_spelled("LINES")) next();
                page_seen = true;
            } else if (tok_spelled("HEADING")) {
                next(); rd->rw->heading = rw_int();
            } else if (tok_spelled("FIRST")) {
                next(); if (tok_spelled("DETAIL")) next(); rd->rw->first_detail = rw_int();
            } else if (tok_spelled("LAST")) {
                next(); if (tok_spelled("DETAIL")) next(); rd->rw->last_detail = rw_int();
            } else if (tok_spelled("FOOTING")) {
                next(); rd->rw->footing = rw_int();
            } else {
                diag_error(TOK_LOC, "Unexpected %s in the RD entry of '%s'", tok_desc(), name);
                synchronize();
                break;
            }
        }
        if (tok.kind == TOK_PERIOD) next();
        // Defaults of the PAGE clause
        RwDesc *r = rd->rw;
        if (page_seen) {
            if (!r->heading) r->heading = 1;
            if (!r->first_detail) r->first_detail = r->heading;
            if (!r->last_detail) r->last_detail = r->footing ? r->footing : r->page;
            if (!r->footing) r->footing = r->last_detail;
            if (!(r->heading <= r->first_detail && r->first_detail <= r->last_detail &&
                  r->last_detail <= r->footing && r->footing <= r->page))
                diag_error(rloc, "The PAGE clause of '%s' needs HEADING <= FIRST DETAIL <= LAST DETAIL <= FOOTING <= PAGE LIMIT",
                           name);
        }
        // PAGE-COUNTER and LINE-COUNTER
        AstNode *ctr = rw_decl(name, "01", NULL, NULL, rloc);
        ctr->next = rw_decl("PAGE-COUNTER", "05", "9(6)", ast_new(AST_FIGURATIVE_CONSTANT, "ZERO", rloc), rloc);
        ctr->next->next = rw_decl("LINE-COUNTER", "05", "9(6)", ast_new(AST_FIGURATIVE_CONSTANT, "ZERO", rloc), rloc);
        rw_add_record(ws, ctr);
        for (int k = 0; k < num_fd_reports; k++)
            if (strcasecmp(fd_reports[k].report, name) == 0) r->file = strdup(fd_reports[k].fd->name);
        if (!r->file) diag_error(rloc, "No FD names the report '%s' in a REPORT clause", name);
        // Its report groups
        AstNode *group_tail = NULL;
        while (tok.kind == TOK_NUMBER) {
            RwEntry entries[256];
            int n = 0;
            do {
                if (n == 256) { diag_error(TOK_LOC, "A report group has more than 256 entries"); break; }
                const char *before = tok.lexeme;
                if (!rw_entry(&entries[n])) { if (tok.lexeme == before) next(); continue; }
                n++;
            } while (tok.kind == TOK_NUMBER && atoi(tok_value_tmp()) != 1);
            if (n == 0) continue;
            if (entries[0].level != 1)
                diag_error(entries[0].loc, "A report group description starts with level 01");
            list_append(&rd->children, &group_tail, rw_group(entries, n, ws));
        }
        list_append(&sec->children, &rd_tail, rd);
        free(name);
    }
    if (tok.kind != TOK_EOF && !is_division_start(tok.kind) && tok.kind != TOK_PROCEDURE)
        diag_error(TOK_LOC, "Expected an RD entry or the end of the REPORT SECTION, found %s", tok_desc());
    // The record of each report file: as wide as its widest line
    for (int k = 0; k < num_fd_reports; k++) {
        int width = 1;
        for (AstNode *rd = sec->children; rd; rd = rd->next) {
            if (strcasecmp(rd->name, fd_reports[k].report) != 0) continue;
            for (AstNode *g = rd->children; g; g = g->next)
                for (AstNode *l = g->children; l; l = l->next)
                    if (l->kind == AST_REPORT_LINE) {
                        int w = rw_line_width(ws, l->text);
                        if (w > width) width = w;
                    }
        }
        AstNode *fd = fd_reports[k].fd;
        bool has = false;
        for (AstNode *c = fd->children; c; c = c->next) has |= c->kind == AST_VAR_DECL;
        char rname[96], pic[32];
        snprintf(rname, sizeof(rname), "$RWR-%s", fd->name);
        snprintf(pic, sizeof(pic), "X(%d)", width);
        // one record per file: the widest of its reports
        AstNode *rec = NULL;
        for (AstNode *c = fd->children; c; c = c->next) if (c->name && strcmp(c->name, rname) == 0) rec = c;
        if (rec) {
            for (AstNode *c = rec->children; c; c = c->next)
                if (c->kind == AST_PIC_CLAUSE && pic_positions(c->name) < width) { free(c->name); c->name = strdup(pic); }
        } else if (!has) {
            AstNode **t = &fd->children;
            while (*t) t = &(*t)->next;
            *t = rw_decl(rname, "01", pic, NULL, fd->loc);
        } else {
            diag_error(fd->loc, "The FD of '%s' names a report, so it cannot have record descriptions", fd->name);
        }
    }
    num_fd_reports = 0;
    AstNode *last = div->children;
    while (last && last->next) last = last->next;
    if (last) last->next = sec;
    else div->children = sec;
    *sec_tail = sec;
}

// DATA DIVISION section body: data description entries
static AstNode *parse_section(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    expect(TOK_SECTION, "SECTION");
    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) return NULL;

    AstNode *sec = ast_new(AST_SECTION, NULL, loc);
    AstNode *last = NULL;

    while (tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
           tok.kind != TOK_FILE && tok.kind != TOK_LINKAGE && tok.kind != TOK_WORKING_STORAGE &&
           !data_section_header()) {
        const char *before = tok.lexeme;
        parser_panic_mode = false;
        if (tok.kind == TOK_NUMBER) {
            AstNode *decl = parse_var_decl();
            if (decl) list_append(&sec->children, &last, decl);
        } else if (tok.kind == TOK_PERIOD) {
            next();
        } else {
            if (!(peek_token().kind == TOK_SECTION && diag_later_element(TOK_LOC, LATER_SECTION, tok.lexeme, tok.length)))
                diag_error(TOK_LOC, "Expected a data description entry, found %s", tok_desc());
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

// The current token is this word, a keyword or not
static bool tok_spelled(const char *word) {
    size_t n = strlen(word);
    return tok.kind != TOK_STRING && tok.length == n && strncasecmp(tok.lexeme, word, n) == 0;
}

// A word that starts another SPECIAL-NAMES clause
static bool special_names_clause_start(void) {
    static const char *words[] = { "SYMBOLIC", "ALPHABET", "CLASS", "CURRENCY", "DECIMAL-POINT",
                                   "CONSOLE", "SYSIN", "SYSOUT", "SYSERR", NULL };
    for (int k = 0; words[k]; k++)
        if (tok_is_word(words[k])) return true;
    return tok.kind == TOK_IDENT && tok.length > 7 && strncasecmp(tok.lexeme, "SWITCH-", 7) == 0;
}

static void declare_special_name(const char *name, int level, SourceLocation loc) {
    Symbol *sym = symtab_resolve_symbol(current_symtab, name);
    if (!sym) sym = symtab_declare_symbol(current_symtab, name, SYM_VAR, loc);
    if (sym) {
        sym->level = level;
        symtab_mark_symbol_as_used(sym);
    }
}

// Character specification of ALPHABET and CLASS:
// {literal [THRU literal | {ALSO literal}...]}...
static void parse_char_specs(AstNode *entry, bool allow_also) {
    AstNode *tail = NULL;
    while (tok.kind == TOK_STRING || tok.kind == TOK_NUMBER || is_figurative(tok.kind) ||
           (allow_also && tok.kind == TOK_ALSO)) {
        bool also = false;
        if (tok.kind == TOK_ALSO) {
            also = true;
            next();
        }
        AstNode *lit = parse_rel_operand();
        if (!lit) break;
        lit->is_signed = also;
        if (tok.kind == TOK_THROUGH) {
            next();
            lit->extra = parse_rel_operand();
        }
        list_append(&entry->children, &tail, lit);
    }
}

// SPECIAL-NAMES clauses up to the period
static void parse_special_names(AstNode *div, AstNode **tail) {
    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
           !tok_is_word("INPUT-OUTPUT") && !tok_is_word("FILE-CONTROL")) {
        SourceLocation loc = {tok.filename, tok.line, tok.column};
        if (tok_is_word("ALPHABET") || tok_is_word("CLASS")) {
            bool alphabet = tok_is_word("ALPHABET");
            next();
            if (tok.kind != TOK_IDENT) { arith_error("Expected a name"); return; }
            AstNode *e = ast_new(AST_SPECIAL, alphabet ? "ALPHABET" : "CLASS", loc);
            e->text = tok_to_str();
            declare_special_name(e->text, alphabet ? -3 : -2, loc);
            next();
            if (tok.kind == TOK_IS) next();
            if (alphabet && (tok_is_word("NATIVE") || tok_is_word("STANDARD-1") || tok_is_word("STANDARD-2")))
                next();                     // the native (ASCII) order
            else {
                diag_subset(MOD_NC, 2, TOK_LOC, alphabet ? "An ALPHABET of literals" : "CLASS");
                parse_char_specs(e, alphabet);
            }
            list_append(&div->children, tail, e);
        } else if (tok_is_word("CURRENCY")) {
            next();
            if (tok.kind == TOK_SIGN) next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_STRING) { arith_error("Expected the currency literal"); return; }
            AstNode *e = ast_new(AST_SPECIAL, "CURRENCY", loc);
            e->text = tok_value();
            list_append(&div->children, tail, e);
            next();
        } else if (tok_is_word("DECIMAL-POINT")) {
            next();
            if (tok.kind == TOK_IS) next();
            if (tok_is_word("COMMA")) next();
            list_append(&div->children, tail, ast_new(AST_SPECIAL, "DECIMAL-POINT", loc));
            current_lexer->decimal_comma = true;
        } else if (tok_is_word("SYMBOLIC")) {
            // SYMBOLIC [CHARACTERS] {name+ {IS|ARE} integer+}+ [IN alphabet]
            // children: the names, each with its ordinal in digits
            diag_subset(MOD_NC, 2, TOK_LOC, "SYMBOLIC CHARACTERS");
            next();
            if (tok_is_word("CHARACTERS") || tok_is_word("CHARACTER") || tok.kind == TOK_CHARACTERS ||
                tok.kind == TOK_CHARACTER) next();
            AstNode *e = ast_new(AST_SPECIAL, "SYMBOLIC", loc);
            AstNode *names_tail = NULL;
            while (tok.kind == TOK_IDENT && !special_names_clause_start() && !parser_panic_mode) {
                AstNode *first = NULL;
                while (tok.kind == TOK_IDENT) {
                    SourceLocation nloc = {tok.filename, tok.line, tok.column};
                    char *name = tok_to_str();
                    declare_special_name(name, -4, nloc);
                    AstNode *n = ast_new(AST_IDENTIFIER, name, nloc);
                    free(name);
                    list_append(&e->children, &names_tail, n);
                    if (!first) first = n;
                    next();
                }
                if (tok.kind == TOK_IS || tok_is_word("ARE")) next();
                for (AstNode *n = first; n; n = n->next) {
                    if (tok.kind != TOK_NUMBER) { arith_error("Expected an ordinal position"); break; }
                    char *num = tok_to_str();
                    n->digits = atoi(num);
                    free(num);
                    next();
                }
            }
            if (tok.kind == TOK_IN) {
                next();
                if (tok.kind == TOK_IDENT) { e->text = tok_to_str(); next(); }
            }
            list_append(&div->children, tail, e);
        } else if (tok.kind == TOK_IDENT) {
            // implementor-name [IS mnemonic-name] [ON [STATUS] [IS] cond] [OFF [STATUS] [IS] cond]
            AstNode *e = ast_new(AST_SPECIAL, "SWITCH", loc);
            e->text = tok_to_str();
            next();
            if (tok.kind == TOK_IS) {
                next();
                if (tok.kind == TOK_IDENT) {
                    char *mnemonic = tok_to_str();
                    declare_special_name(mnemonic, -1, loc);
                    e->extra = ast_new(AST_IDENTIFIER, mnemonic, loc);
                    free(mnemonic);
                    next();
                }
            }
            while (tok.kind == TOK_ON || tok_is_word("OFF")) {
                bool on = tok.kind == TOK_ON;
                next();
                if (tok.kind == TOK_STATUS) next();
                if (tok.kind == TOK_IS) next();
                if (tok.kind != TOK_IDENT) { arith_error("Expected a condition-name"); ast_free(e); return; }
                AstNode *cond = parse_procedure_name();
                declare_special_name(cond->name, 88, loc);
                if (on) e->on_error = cond;
                else e->not_on_error = cond;
            }
            list_append(&div->children, tail, e);
        } else {
            next();
        }
    }
    if (tok.kind == TOK_PERIOD) next();
}

// Data items named in FILE-CONTROL (declared later): used
static char **forward_names;
static int num_forward_names, cap_forward_names;

// SELECT [OPTIONAL] file ASSIGN [TO] name [ORGANIZATION [IS] [LINE|RECORD]
//   SEQUENTIAL] [ACCESS [MODE] [IS] SEQUENTIAL] [[FILE] STATUS [IS] item]
//   [RESERVE n [AREA|AREAS]] ... .
// A data-name [OF|IN data-name]... declared later in the DATA DIVISION
// (FILE STATUS, keys): resolved during IR lowering
static AstNode *parse_forward_identifier(void) {
    AstNode *id = parse_procedure_name();
    if (id->name) {
        if (num_forward_names == cap_forward_names) {
            cap_forward_names = cap_forward_names ? cap_forward_names * 2 : 16;
            forward_names = realloc(forward_names, cap_forward_names * sizeof(char *));
        }
        forward_names[num_forward_names++] = strdup(id->name);
    }
    if (id->extra) {
        id->extra->next = NULL;
        AstNode *tail = id->extra;
        while ((tok.kind == TOK_OF || tok.kind == TOK_IN) && peek_token().kind == TOK_IDENT) {
            next();
            char *q = tok_to_str();
            list_append(&id->extra, &tail, ast_new(AST_IDENTIFIER, q, (SourceLocation){tok.filename, tok.line, tok.column}));
            free(q);
            next();
        }
    }
    return id;
}

// SELECT [OPTIONAL] file-name, then its clauses in any order: ASSIGN,
// ORGANIZATION, ACCESS MODE, RELATIVE / RECORD / ALTERNATE RECORD KEY,
// FILE STATUS, RESERVE, PADDING CHARACTER, RECORD DELIMITER
// Subset flagging of files: the organization of each file (and of the
// file of each record), for the statements that name them
static struct { char *name; int org; } file_orgs[256];
static int num_file_orgs;

static void note_file_org(const char *name, int org) {
    if (num_file_orgs < 256) {
        file_orgs[num_file_orgs].name = strdup(name);
        file_orgs[num_file_orgs++].org = org;
    }
}

static int file_org_of(const char *name) {
    for (int k = num_file_orgs - 1; k >= 0; k--)
        if (strcasecmp(file_orgs[k].name, name) == 0) return file_orgs[k].org;
    return FILE_ORG_SEQUENTIAL;
}

static CobolModule org_module(int org) {
    return org == FILE_ORG_RELATIVE ? MOD_RL : org == FILE_ORG_INDEXED ? MOD_IX : MOD_SQ;
}

static AstNode *parse_select(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    next();
    AstNode *sel = ast_new(AST_SELECT, NULL, loc);
    AstNode *key_tail = NULL;
    // flags of the entry, of its file's module: known at its end
    struct { SourceLocation loc; const char *what; int level; int module; } pend[16];
    int npend = 0;
    #define PEND(l, w, lev, mod) do { if (npend < 16) pend[npend++] = (typeof(pend[0])){ l, w, lev, mod }; } while (0)
    if (tok.kind == TOK_OPTIONAL) {
        PEND(TOK_LOC, "OPTIONAL", 2, -1);
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
    bool assigned = false;

    while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
           tok.kind != TOK_SELECT) {
        if (tok.kind == TOK_ASSIGN) {
            next();
            if (tok.kind == TOK_TO) next();
            if (tok.kind != TOK_STRING && tok.kind != TOK_IDENT) {
                ast_free(sel);
                arith_error("Expected file name or literal after ASSIGN");
                return NULL;
            }
            sel->is_numeric = tok.kind == TOK_STRING;
            sel->text = tok_value();
            next();
            while (tok.kind == TOK_STRING ||
                   (tok.kind == TOK_IDENT && !tok_is_word("RESERVE") && !tok_is_word("PADDING") &&
                    !tok_is_word("ALTERNATE") && !tok_is_word("ORGANIZATION") && !tok_is_word("LOCK") &&
                    !tok_is_word("SHARING")))
                next();     // further ASSIGN names
            assigned = true;
        } else if (tok.kind == TOK_ORGANIZATION || tok.kind == TOK_LINE || tok.kind == TOK_SEQUENTIAL ||
                   tok.kind == TOK_INDEXED ||
                   (tok.kind == TOK_RELATIVE && peek_token().kind != TOK_KEY &&
                    peek_token().kind != TOK_IS && peek_token().kind != TOK_IDENT) ||
                   (tok.kind == TOK_RECORD && peek_token().kind == TOK_SEQUENTIAL)) {
            if (tok.kind == TOK_ORGANIZATION) next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind == TOK_LINE) {
                diag_feature(F_LINE_SEQUENTIAL, TOK_LOC, NULL);
                sel->org = FILE_ORG_LINE_SEQUENTIAL;
                sel->decimal_places = 1;
                next();
            }
            else if (tok.kind == TOK_RECORD) next();
            if (tok.kind == TOK_INDEXED) { PEND(TOK_LOC, "ORGANIZATION IS INDEXED", 1, MOD_IX); sel->org = FILE_ORG_INDEXED; next(); }
            else if (tok.kind == TOK_RELATIVE) { PEND(TOK_LOC, "ORGANIZATION IS RELATIVE", 1, MOD_RL); sel->org = FILE_ORG_RELATIVE; next(); }
            else if (tok.kind == TOK_SEQUENTIAL) next();
            else { ast_free(sel); arith_error("Expected SEQUENTIAL, RELATIVE or INDEXED"); return NULL; }
        } else if (tok.kind == TOK_ACCESS) {
            next();
            if (tok.kind == TOK_MODE) next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind == TOK_RANDOM) { PEND(TOK_LOC, "ACCESS MODE IS RANDOM", 1, -1); sel->access = FILE_ACCESS_RANDOM; }
            else if (tok.kind == TOK_DYNAMIC) { PEND(TOK_LOC, "ACCESS MODE IS DYNAMIC", 2, -1); sel->access = FILE_ACCESS_DYNAMIC; }
            else if (tok.kind != TOK_SEQUENTIAL) { ast_free(sel); arith_error("Expected SEQUENTIAL, RANDOM or DYNAMIC"); return NULL; }
            next();
        } else if (tok.kind == TOK_RELATIVE || (tok.kind == TOK_RECORD && peek_token().kind != TOK_DELIMITER) ||
                   tok_is_word("ALTERNATE")) {
            const char *what = tok.kind == TOK_RELATIVE ? "RELATIVE" : tok.kind == TOK_RECORD ? "RECORD" : "ALTERNATE";
            SourceLocation kloc = {tok.filename, tok.line, tok.column};
            if (what[0] == 'A') PEND(kloc, "ALTERNATE RECORD KEY", 2, MOD_IX);
            else if (what[0] == 'R' && what[2] == 'L') PEND(kloc, "RELATIVE KEY", 1, MOD_RL);
            else PEND(kloc, "RECORD KEY", 1, MOD_IX);
            next();
            if (what[0] == 'A' && tok.kind == TOK_RECORD) next();
            if (tok.kind == TOK_KEY) next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_IDENT) { ast_free(sel); arith_error("Expected the key data item"); return NULL; }
            AstNode *key = ast_new(AST_FILE_KEY, what, kloc);
            key->extra = parse_forward_identifier();
            if (tok.kind == TOK_WITH) next();
            if (tok_is_word("DUPLICATES")) { key->is_signed = true; next(); }
            list_append(&sel->children, &key_tail, key);
        } else if (tok.kind == TOK_FILE || tok.kind == TOK_STATUS) {
            if (tok.kind == TOK_FILE) next();
            if (tok.kind != TOK_STATUS) { ast_free(sel); arith_error("Expected STATUS"); return NULL; }
            next();
            if (tok.kind == TOK_IS) next();
            if (tok.kind != TOK_IDENT) { ast_free(sel); arith_error("Expected FILE STATUS data item"); return NULL; }
            // Declared later in the DATA DIVISION: resolved during IR lowering
            sel->extra = parse_forward_identifier();
        } else if (tok_is_word("RESERVE")) {
            PEND(TOK_LOC, "RESERVE ... AREAS", 2, -1);
            next();
            if (tok.kind == TOK_NUMBER || tok_is_word("NO")) next();
            if (tok_is_word("ALTERNATE")) next();
            if (tok_is_word("AREA") || tok_is_word("AREAS")) next();
        } else if (tok_is_word("PADDING")) {
            PEND(TOK_LOC, "PADDING CHARACTER", 2, MOD_SQ);
            next();
            if (tok_spelled("CHARACTER")) next();
            if (tok.kind == TOK_IS) next();
            next();
        } else if (tok.kind == TOK_RECORD) {
            PEND(TOK_LOC, "RECORD DELIMITER", 2, MOD_SQ);
            next();                         // RECORD DELIMITER IS STANDARD-1 / name
            if (tok.kind == TOK_DELIMITER) next();
            if (tok.kind == TOK_IS) next();
            next();
        } else {
            diag_error(TOK_LOC, "Unexpected %s in the SELECT entry of '%s'", tok_desc(), sel->name);
            next();
        }
    }
    if (!assigned) {
        diag_error(loc, "SELECT '%s' has no ASSIGN clause", sel->name);
    }
    for (int k = 0; k < npend; k++)
        diag_subset(pend[k].module >= 0 ? (CobolModule)pend[k].module : org_module(sel->org), pend[k].level,
                    pend[k].loc, pend[k].what);
    #undef PEND
    if (sel->name) note_file_org(sel->name, sel->org);
    if (tok.kind == TOK_PERIOD) next();
    return sel;
}

// LINAGE operand: an integer, or a data item declared later
static AstNode *parse_linage_operand(void) {
    if (tok.kind == TOK_IDENT) return parse_forward_identifier();
    return parse_value_operand();
}

// FILE SECTION. {FD file-name [clauses]. record-description-entries}...
static AstNode *parse_file_section(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    check_area_a(loc, "The FILE SECTION header");
    next();                                 // FILE
    expect(TOK_SECTION, "SECTION");
    expect(TOK_PERIOD, ".");
    parser_panic_mode = false;
    AstNode *sec = ast_new(AST_SECTION, "FILE", loc);
    AstNode *fd_tail = NULL;

    while (tok.kind == TOK_FD || tok.kind == TOK_SD) {
        SourceLocation fd_loc = {tok.filename, tok.line, tok.column};
        bool sort_file = tok.kind == TOK_SD;
        if (sort_file) diag_subset(MOD_ST, 1, fd_loc, "An SD entry");
        check_area_a(fd_loc, sort_file ? "An SD entry" : "An FD entry");
        next();
        if (tok.kind != TOK_IDENT) { arith_error("Expected file name after FD"); break; }
        char *name = tok_to_str();
        AstNode *fd = ast_new(AST_FILE_DECL, name, fd_loc);
        fd->is_numeric = sort_file;         // SD: a sort-merge file
        free(name);
        next();
        // RECORD CONTAINS / VARYING: digits = shortest record, decimal_places =
        // longest (0: from the record descriptions), is_signed = variable,
        // extra = DEPENDING ON item; LINAGE: loop = AST_SPECIAL "LINAGE" with
        // on_error = lines, not_on_error = FOOTING, extra = TOP, loop = BOTTOM.
        // BLOCK, LABEL, VALUE OF, DATA RECORDS, CODE-SET: no effect here
        while (tok.kind != TOK_PERIOD && tok.kind != TOK_EOF &&
               !(is_division_start(tok.kind) && peek_token().kind == TOK_DIVISION)) {
            if (tok.kind == TOK_RECORD) {
                next();
                if (tok_is_word("CONTAINS")) next();
                if (tok.kind == TOK_IS) next();
                if (tok.kind == TOK_VARYING) {
                    diag_subset(org_module(file_org_of(fd->name)), 2, TOK_LOC, "RECORD IS VARYING");
                    fd->is_signed = true;
                    next();
                    if (tok.kind == TOK_IN) next();
                    if (tok.kind == TOK_SIZE) next();
                    if (tok.kind == TOK_FROM) next();
                    if (tok.kind == TOK_NUMBER) { fd->digits = atoi(tok_value_tmp()); next(); }
                    if (tok.kind == TOK_TO) {
                        next();
                        if (tok.kind == TOK_NUMBER) { fd->decimal_places = atoi(tok_value_tmp()); next(); }
                    }
                    if (tok.kind == TOK_CHARACTERS) next();
                    if (tok.kind == TOK_DEPENDING) {
                        next();
                        if (tok.kind == TOK_ON) next();
                        if (tok.kind == TOK_IDENT) fd->extra = parse_forward_identifier();
                    }
                } else if (tok.kind == TOK_NUMBER) {
                    int first = atoi(tok_value_tmp());
                    next();
                    if (tok.kind == TOK_TO) {
                        next();
                        fd->digits = first;
                        if (tok.kind == TOK_NUMBER) { fd->decimal_places = atoi(tok_value_tmp()); next(); }
                        fd->is_signed = fd->digits != fd->decimal_places;
                    } else {
                        fd->digits = fd->decimal_places = first;
                    }
                    if (tok.kind == TOK_CHARACTERS) next();
                }
                continue;
            }
            if (tok_is_word("LABEL") || (tok.kind == TOK_VALUE && peek_token().kind == TOK_OF) ||
                (tok.kind == TOK_DATA && (peek_token().kind == TOK_RECORD || peek_token().kind == TOK_IDENT))) {
                flag_obsolete(tok.line, tok.column, tok.kind == TOK_VALUE ? "VALUE OF" :
                              tok.kind == TOK_DATA ? "DATA RECORDS" : "LABEL RECORDS");
                if (tok.kind == TOK_VALUE) {
                    // VALUE OF implementor-name IS {literal | data-name}: a
                    // data-name is level 2
                    next(); next();
                    while (tok.kind == TOK_IDENT && peek_token().kind != TOK_IS && peek_token().kind != TOK_PERIOD) next();
                    if (tok.kind == TOK_IDENT) next();
                    if (tok.kind == TOK_IS) next();
                    if (tok.kind == TOK_IDENT) diag_subset(MOD_SQ, 2, TOK_LOC, "VALUE OF with a data-name");
                    continue;
                }
                next();
                continue;
            }
            if (tok_is_word("BLOCK")) {
                next();
                if (tok_is_word("CONTAINS")) next();
                if (tok.kind == TOK_NUMBER) next();
                if (tok.kind == TOK_TO) diag_subset(MOD_SQ, 2, TOK_LOC, "BLOCK CONTAINS ... TO");
                continue;
            }
            if (tok_spelled("REPORT") || tok_spelled("REPORTS")) {
                // REPORT IS / REPORTS ARE report-name...: the Report Writer
                diag_subset(MOD_RW, 1, TOK_LOC, "The REPORT clause");
                next();
                if (tok.kind == TOK_IS) next();
                while (tok.kind == TOK_IDENT && !tok_spelled("LABEL") && !tok_spelled("GLOBAL") &&
                       !tok_spelled("EXTERNAL") && !tok_spelled("BLOCK") && !tok_spelled("CODE-SET")) {
                    if (num_fd_reports < 64) {
                        fd_reports[num_fd_reports].fd = fd;
                        fd_reports[num_fd_reports++].report = tok_to_str();
                    }
                    next();
                }
                continue;
            }
            if (tok_is_word("GLOBAL") || tok_is_word("EXTERNAL")) {
                if (tok_is_word("GLOBAL")) fd->is_global = true;
                else fd->is_external = true;
                next();
                continue;
            }
            if (tok.kind == TOK_LINAGE) {
                SourceLocation lloc = {tok.filename, tok.line, tok.column};
                diag_subset(MOD_SQ, 2, lloc, "LINAGE");
                next();
                if (tok.kind == TOK_IS) next();
                AstNode *lin = ast_new(AST_SPECIAL, "LINAGE", lloc);
                lin->on_error = parse_linage_operand();
                if (tok.kind == TOK_LINES) next();
                for (;;) {
                    if (tok.kind == TOK_WITH) next();
                    if (tok_is_word("FOOTING")) {
                        next();
                        if (tok.kind == TOK_AT) next();
                        lin->not_on_error = parse_linage_operand();
                    } else if (tok.kind == TOK_LINES || tok.kind == TOK_AT || tok_is_word("TOP") ||
                               tok_is_word("BOTTOM")) {
                        if (tok.kind == TOK_LINES) next();
                        if (tok.kind == TOK_AT) next();
                        bool top = tok_is_word("TOP");
                        if (!top && !tok_is_word("BOTTOM")) break;
                        next();
                        if (top) lin->extra = parse_linage_operand();
                        else lin->loop = parse_linage_operand();
                    } else {
                        break;
                    }
                }
                fd->loop = lin;
                declare_special_name("LINAGE-COUNTER", 1, lloc);
                continue;
            }
            next();
        }
        if (tok.kind == TOK_PERIOD) next();

        AstNode *tail = NULL;
        while (tok.kind == TOK_NUMBER) {
            const char *before = tok.lexeme;
            parser_panic_mode = false;
            AstNode *decl = parse_var_decl();
            if (decl) list_append(&fd->children, &tail, decl);
            if (decl && decl->name && decl->text && atoi(decl->text) == 1)
                note_file_org(decl->name, file_org_of(fd->name));
            if (tok.lexeme == before) next();
        }
        list_append(&sec->children, &fd_tail, fd);
    }
    return sec;
}

static AstNode *parse_division(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    char *name = tok_to_str();
    if (peek_token().kind == TOK_DIVISION) {
        char what[64];
        snprintf(what, sizeof(what), "The %s DIVISION header", name);
        check_area_a(loc, what);
    }
    next();
    expect(TOK_DIVISION, "DIVISION");
    AstNode *using = NULL, *using_tail = NULL;
    if (strcasecmp(name, "PROCEDURE") == 0 && tok.kind == TOK_USING) {
        // PROCEDURE DIVISION USING data-name...
        next();
        while (tok.kind == TOK_IDENT || tok.kind == TOK_BY || tok_is_word("REFERENCE")) {
            if (tok.kind == TOK_BY || tok_is_word("REFERENCE")) { next(); continue; }
            list_append(&using, &using_tail, parse_forward_identifier());
        }
    }
    expect(TOK_PERIOD, ".");
    if (parser_panic_mode) { free(name); ast_free(using); return NULL; }

    AstNode *div = ast_new(AST_DIVISION, name, loc);
    div->extra = using;
    free(name);

    if (strcasecmp(div->name, "IDENTIFICATION") == 0) {
        expect(TOK_PROGRAM_ID, "PROGRAM-ID");
        expect(TOK_PERIOD, ".");
        char *prog_name_str = tok.kind == TOK_STRING ? tok_value() : tok_to_str();
        if (tok.kind == TOK_STRING) next();
        else expect(TOK_IDENT, "program name");
        // [IS] [COMMON] [INITIAL] [PROGRAM]: digits 1 COMMON, 2 INITIAL
        while (tok.kind == TOK_IS || tok_is_word("COMMON") || tok.kind == TOK_INITIAL ||
               tok_is_word("PROGRAM")) {
            if (tok_is_word("COMMON")) { diag_subset(MOD_IC, 2, TOK_LOC, "A COMMON program"); div->digits |= 1; }
            if (tok.kind == TOK_INITIAL) { diag_subset(MOD_IC, 2, TOK_LOC, "An INITIAL program"); div->digits |= 2; }
            next();
        }
        expect(TOK_PERIOD, ".");
        div->text = prog_name_str; // Assign to AST node

        // Declare the PROGRAM-ID in the symbol table
        symtab_declare_symbol(current_symtab, prog_name_str, SYM_PROGRAM, (SourceLocation){tok.filename, tok.line, tok.column});

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
        int errors = error_count, names = parser_name_errors;
        parse_procedure_body(div);
        parser_statement_errors += (error_count - errors) - (parser_name_errors - names);
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
            if (tok_is_word("MEMORY") || tok_is_word("RERUN") || tok_is_word("MULTIPLE") ||
                tok_is_word("SEGMENT-LIMIT")) {
                flag_obsolete(tok.line, tok.column, tok_is_word("MEMORY") ? "MEMORY SIZE" :
                              tok_is_word("RERUN") ? "RERUN" : tok_is_word("MULTIPLE") ? "MULTIPLE FILE TAPE" :
                              "SEGMENT-LIMIT (the Segmentation module)");
                if (tok_is_word("MULTIPLE")) diag_subset(MOD_SQ, 2, TOK_LOC, "MULTIPLE FILE TAPE");
                if (tok_is_word("SEGMENT-LIMIT")) {
                    diag_subset(MOD_SG, 2, TOK_LOC, "SEGMENT-LIMIT");
                    Token p = peek_token();
                    if (p.kind == TOK_IS || p.kind == TOK_NUMBER) {
                        next();
                        if (tok.kind == TOK_IS) next();
                        if (tok.kind == TOK_NUMBER) segment_limit = atoi(tok_value_tmp());
                    }
                }
                if (tok_is_word("RERUN")) diag_subset(MOD_SQ, 2, TOK_LOC, "RERUN");
                next();
                continue;
            }
            if (tok_is_word("DEBUGGING") && peek_token().kind == TOK_MODE) {
                // SOURCE-COMPUTER ... WITH DEBUGGING MODE
                diag_subset(MOD_DB, 1, TOK_LOC, "WITH DEBUGGING MODE");
                list_append(&div->children, &sel_tail,
                            ast_new(AST_SPECIAL, "DEBUGGING", (SourceLocation){tok.filename, tok.line, tok.column}));
                next();
                next();
                continue;
            }
            if (tok_is_word("SAME")) {
                // SAME RECORD AREA FOR file...: the files share their record area;
                // SAME [SORT | SORT-MERGE] AREA: storage sharing, no effect
                SourceLocation sloc = {tok.filename, tok.line, tok.column};
                next();
                bool record = tok.kind == TOK_RECORD;
                if (record) {
                    // the module of the files named (sequential unless one is relative or indexed)
                    Lexer copy = *current_lexer;
                    copy.quiet = true;
                    copy.prev = NULL;
                    int org = FILE_ORG_SEQUENTIAL;
                    for (Token t = lexer_next(&copy); t.kind != TOK_PERIOD && t.kind != TOK_EOF; t = lexer_next(&copy))
                        if (t.kind == TOK_IDENT) {
                            char nm[64];
                            snprintf(nm, sizeof(nm), "%.*s", (int)t.length, t.lexeme);
                            if (file_org_of(nm) != FILE_ORG_SEQUENTIAL) org = file_org_of(nm);
                        }
                    diag_subset(org_module(org), 2, sloc, "SAME RECORD AREA");
                } else {
                    diag_subset(MOD_ST, 1, sloc, "SAME SORT AREA");
                }
                while (tok.kind == TOK_RECORD || tok.kind == TOK_SORT || tok_is_word("SORT-MERGE") ||
                       tok_is_word("AREA") || tok.kind == TOK_FOR) next();
                AstNode *same = record ? ast_new(AST_SPECIAL, "SAME RECORD", sloc) : NULL;
                AstNode *same_tail = NULL;
                while (tok.kind == TOK_IDENT && !tok_is_word("SAME") && !tok_is_word("MULTIPLE") &&
                       !tok_is_word("RERUN") && !tok_is_word("APPLY")) {
                    if (same) list_append(&same->children, &same_tail, parse_procedure_name());
                    else next();
                }
                if (same) list_append(&div->children, &sel_tail, same);
                continue;
            }
            if (tok_is_word("SPECIAL-NAMES")) {
                next();
                if (tok.kind == TOK_PERIOD) next();
                parse_special_names(div, &sel_tail);
                continue;
            }
            if (tok_is_word("PROGRAM") && peek_token().kind == TOK_IDENT) {
                // OBJECT-COMPUTER: PROGRAM COLLATING SEQUENCE IS alphabet-name
                SourceLocation loc = {tok.filename, tok.line, tok.column};
                next();
                if (tok_is_word("COLLATING")) next();
                if (tok_is_word("SEQUENCE")) next();
                if (tok.kind == TOK_IS) next();
                if (tok.kind == TOK_IDENT) {
                    AstNode *coll = ast_new(AST_SPECIAL, "COLLATING", loc);
                    coll->text = tok_to_str();
                    list_append(&div->children, &sel_tail, coll);
                    next();
                }
                continue;
            }
            next();
        }
    }
    else if (strcasecmp(div->name, "DATA") == 0) {
        // FILE SECTION, WORKING-STORAGE SECTION, LINKAGE SECTION in this order
        AstNode *sec_tail = NULL;
        while (tok.kind != TOK_EOF && !is_division_start(tok.kind)) {
            if (tok.kind == TOK_WORKING_STORAGE) {
                check_area_a(TOK_LOC, "The WORKING-STORAGE SECTION header");
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
            if (tok.kind == TOK_LINKAGE) {
                check_area_a(TOK_LOC, "The LINKAGE SECTION header");
                next();
                AstNode *sec = parse_section();
                if (sec) sec->name = strdup("LINKAGE");
                if (sec) list_append(&div->children, &sec_tail, sec);
                continue;
            }
            if (tok_spelled("REPORT") && peek_token().kind == TOK_SECTION) {
                parse_report_section(div, &sec_tail);
                continue;
            }
            if (tok_is_word("COMMUNICATION")) {
                communication_unsupported(TOK_LOC, "The COMMUNICATION SECTION");
            } else {
                diag_error(TOK_LOC, "Expected a DATA DIVISION section, found %s", tok_desc());
            }
            // Skip to the next section of this division
            next();
            while (tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
                   tok.kind != TOK_WORKING_STORAGE && tok.kind != TOK_FILE && tok.kind != TOK_LINKAGE) next();
        }
    }

    return div;
}

// One program: its divisions, its nested programs, up to END PROGRAM (or
// the end of the source)
static AstNode *parse_one_program(void) {
    SourceLocation loc = {tok.filename, tok.line, tok.column};
    AstNode *prog = ast_new(AST_PROGRAM, NULL, loc);
    bool outer_uses_return_code = uses_return_code;
    uses_return_code = false;
    AstNode *last = NULL;
    int has_proc = 0;
    int error_count_before = error_count;

    if (tok.kind == TOK_IDENTIFICATION) {
        AstNode *div = parse_division();
        if (div) {
            prog->children = div;
            last = div;
            if (div->text) {
                prog->name = div->text;
                div->text = NULL;
            }
            prog->digits = div->digits;
        }
    } else {
        diag_error(TOK_LOC, "Missing IDENTIFICATION DIVISION");
        synchronize();
    }

    while (tok.kind != TOK_EOF) {
        if (tok.kind == TOK_END && end_program_follows()) {
            diag_subset(MOD_IC, 2, TOK_LOC, "END PROGRAM");
            next();
            next();
            if (tok.kind == TOK_IDENT || tok.kind == TOK_STRING) {
                char *name = tok.kind == TOK_STRING ? tok_value() : tok_to_str();
                if (prog->name && strcasecmp(name, prog->name) != 0)
                    diag_error(TOK_LOC, "END PROGRAM %s does not end program %s", name, prog->name);
                free(name);
                next();
            }
            if (tok.kind == TOK_PERIOD) next();
            break;
        }
        if (tok.kind == TOK_IDENTIFICATION) {
            // A nested program (before this program's END PROGRAM), or with
            // no END PROGRAM before it, the next separately compiled program
            if (!has_proc) {
                diag_error(TOK_LOC, "Expected a PROCEDURE DIVISION before the next program");
            }
            diag_subset(MOD_IC, 2, TOK_LOC, "A nested program");
            AstNode *nested = parse_one_program();
            if (last) last->next = nested;
            else prog->children = nested;
            last = nested;
            continue;
        }
        parser_panic_mode = false;
        if (is_division_start(tok.kind)) {
            AstNode *div = parse_division();
            if (div) {
                if (last) last->next = div;
                else prog->children = div;
                last = div;
                if (strcasecmp(div->name, "PROCEDURE") == 0) has_proc = 1;
            }
        } else if (tok.kind == TOK_PERIOD) {
            next();
        } else {
            diag_error(TOK_LOC, "Expected a DIVISION start");
            next();
            while (tok.kind != TOK_EOF && !is_division_start(tok.kind) &&
                   !(tok.kind == TOK_END && end_program_follows())) next();
        }
    }

    if (!has_proc && error_count == error_count_before)
        diag_error(prog->loc, "Program '%s' has no PROCEDURE DIVISION", prog->name ? prog->name : "");
    if (uses_return_code) declare_return_code(prog);
    uses_return_code = outer_uses_return_code;
    return prog;
}

// A source: one or more programs (each with its nested programs), as a
// list of AST_PROGRAM nodes
AstNode *parse_program(Lexer *l) {
    current_lexer = l;
    parser_panic_mode = false;
    next();
    current_symtab = symtab_init();
    // The DEBUG-ITEM special register (the Debug module)
    static const char *debug_names[] = { "DEBUG-ITEM", "DEBUG-LINE", "DEBUG-NAME", "DEBUG-SUB-1",
                                         "DEBUG-SUB-2", "DEBUG-SUB-3", "DEBUG-CONTENTS", NULL };
    for (int k = 0; debug_names[k]; k++)
        declare_special_name(debug_names[k], 1, (SourceLocation){current_lexer->filename, 0, 0});

    AstNode *first = NULL, *last = NULL;
    while (tok.kind != TOK_EOF) {
        AstNode *prog = parse_one_program();
        if (!first) first = prog;
        else last->next = prog;
        last = prog;
    }

    for (int k = 0; k < num_forward_names; k++) {
        symtab_mark_symbol_as_used(symtab_resolve_symbol(current_symtab, forward_names[k]));
        free(forward_names[k]);
    }
    num_forward_names = 0;
    mark_used_through_groups();
    symtab_check_unused_symbols(current_symtab);
    symtab_free(current_symtab);
    return first;
}
