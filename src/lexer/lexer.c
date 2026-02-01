#include "lexer.h"
#include "diag.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "lexer_cobol85.h" // Corrected include path
#include "lexer_cobol2002.h" // Corrected include path

// Token make_token (Lexer *l, ...)` is already in lexer_cobol85.c and lexer_cobol2002.c.
// To avoid duplication, we need a common helper or keep `make_token` here if it's truly generic.
// For now, let's keep it here and make the standard-specific keyword functions accept it.
// Actually, `make_token` is static, so it should be fine. The problem is with `is_ident_char`
// and `advance`, `skip_ws` which are also static. I should pass `l` to them as well.

static void advance(Lexer *l) {
    if (*l->src == '\n') {
        l->current_line++;
        l->current_col = 1;
    } else {
        l->current_col++;
    }
    l->src++;
}

static void skip_ws(Lexer *l) {
    while (*l->src && isspace(*l->src)) {
        advance(l);
    }
}

// NOTE: This helper is used by both lexer_cobol85.c and lexer_cobol2002.c
// and also by lexer.c. So it should not be static. It can be made non-static or moved to a common utility.
// For now, let's make it non-static.
int is_ident_char(char c) {
    return isalnum(c) || c == '-' || c == '_';
}

// NOTE: This helper is used by both lexer_cobol85.c and lexer_cobol2002.c.
// It also needs to be non-static or a common utility function.
// For now, let's keep it here and make it accessible.
Token make_token_public(Lexer *l, TokenKind kind, const char *start, size_t length) {
    Token t;
    t.kind = kind;
    t.lexeme = start;
    t.length = length;
    t.line = l->current_line;
    t.column = l->current_col - length;
    return t;
}


void lexer_init(Lexer *l, const char *source, const CompilerMode *mode) {
    l->original_source = source;
    l->src = source;
    l->current_line = 1;
    l->current_col = 1;
    l->mode = mode;

    // Set standard-specific keyword checker
    switch (l->mode->std) {
        case STD_COBOL2002:
        case STD_COBOL2014:
            l->get_keyword_or_ident = lexer_cobol2002_keyword_or_ident;
            break;
        case STD_COBOL85:
        case STD_GNUCOBOL: // Fallback to COBOL85 for GnuCOBOL for now
        case STD_IBM_ENTERPRISE: // Fallback to COBOL85 for IBM for now
        default:
            l->get_keyword_or_ident = lexer_cobol85_keyword_or_ident;
            break;
    }
}

Token lexer_next(Lexer *l) {
    skip_ws(l);

    const char *start = l->src;

    if (*l->src == '\0') {
        return make_token_public(l, TOK_EOF, start, 0);
    }

    // TODO: Handle fixed/free format here

    if (*l->src == '"') {
        advance(l);  // skip opening "
        start = l->src;
        while (*l->src && *l->src != '"') advance(l);
        size_t len = l->src - start;
        if (*l->src == '"') advance(l);  // skip closing "
        return make_token_public(l, TOK_STRING, start, len);
    }

    // Handle numbers
    bool negative = false;
    if (*l->src == '-') {
        negative = true;
        advance(l);
    }

    if (isdigit(*l->src) || (*l->src == '.' && isdigit(l->src[1]))) {
        if (*l->src == '.') advance(l);  // for .56 style, but rare in COBOL
        while (isdigit(*l->src)) advance(l);
        if (*l->src == '.' && isdigit(l->src[1])) {
            advance(l);
            while (isdigit(*l->src)) advance(l);
        }
        size_t len = l->src - start;
        if (negative) {
            start--;  // include the '-'
            len++;
        }
        return make_token_public(l, TOK_NUMBER, start, len);
    } else if (negative) {
        // If - not followed by number, treat as minus operator
        return make_token_public(l, TOK_MINUS, start, 1);
    }

    if (isalpha(*l->src)) {
        while (is_ident_char(l->src[0])) advance(l); // Use non-static is_ident_char
        return l->get_keyword_or_ident(l, start, l->src - start); // Use function pointer
    }

    if (*l->src == '(') {
        advance(l);
        return make_token_public(l, TOK_LPAREN, start, 1);
    }

    if (*l->src == ')') {
        advance(l);
        return make_token_public(l, TOK_RPAREN, start, 1);
    }

    if (*l->src == '.') {
        advance(l);
        return make_token_public(l, TOK_PERIOD, start, 1);
    }

    if (*l->src == '>') {
        advance(l);
        if (*l->src == '=') {
            advance(l);
            return make_token_public(l, TOK_GE, start, 2);
        }
        return make_token_public(l, TOK_GT, start, 1);
    }

    if (*l->src == '<') {
        advance(l);
        if (*l->src == '>') {
            advance(l);
            return make_token_public(l, TOK_NE, start, 2);
        }
        if (*l->src == '=') {
            advance(l);
            return make_token_public(l, TOK_LE, start, 2);
        }
        return make_token_public(l, TOK_LT, start, 1);
    }

    if (*l->src == '=') {
        advance(l);
        return make_token_public(l, TOK_EQ, start, 1);
    }

    if (*l->src == '+') {
        advance(l);
        return make_token_public(l, TOK_PLUS, start, 1);
    }

    if (*l->src == '-') {
        advance(l);
        return make_token_public(l, TOK_MINUS, start, 1);
    }

    if (*l->src == '*') {
        advance(l);
        return make_token_public(l, TOK_MUL, start, 1);
    }

    if (*l->src == '/') {
        advance(l);
        return make_token_public(l, TOK_DIV, start, 1);
    }

    cobalt_error_loc(l->original_source, l->current_line, l->current_col, "Unexpected character '%c'", *l->src);
    advance(l);
    return lexer_next(l);  // recovery
}
