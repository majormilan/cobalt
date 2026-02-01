#include "lexer.h"
#include "diag.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

void lexer_init(Lexer *l, const char *source, const CompilerMode *mode) {
    l->original_source = source;
    l->src = source;
    l->current_line = 1;
    l->current_col = 1;
    l->mode = mode;
}

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

static int is_ident_char(char c) {
    return isalnum(c) || c == '-' || c == '_';
}

static Token make_token(Lexer *l, TokenKind kind, const char *start, size_t length) {
    Token t;
    t.kind = kind;
    t.lexeme = start;
    t.length = length;
    t.line = l->current_line;
    t.column = l->current_col - length;
    return t;
}

static Token keyword_or_ident(Lexer *l, const char *start, size_t len) {
    #define KW(s, k) if (len == strlen(s) && strncasecmp(start, s, len) == 0) return make_token(l, k, start, len)

    KW("IDENTIFICATION", TOK_IDENTIFICATION);
    KW("DIVISION",        TOK_DIVISION);
    KW("PROGRAM-ID",      TOK_PROGRAM_ID);
    KW("ENVIRONMENT",     TOK_ENVIRONMENT);
    KW("DATA",            TOK_DATA);
    KW("WORKING-STORAGE", TOK_WORKING_STORAGE);
    KW("LINKAGE",         TOK_LINKAGE);
    KW("FILE",            TOK_FILE);
    // LOCAL-STORAGE is COBOL 2002+
    if (l->mode->std >= STD_COBOL2002) {
        KW("LOCAL-STORAGE", TOK_LOCAL_STORAGE);
    }
    KW("PROCEDURE",       TOK_PROCEDURE);
    KW("VALUE",           TOK_VALUE);
    KW("SECTION",         TOK_SECTION);

    KW("DISPLAY",         TOK_DISPLAY);
    KW("STOP",            TOK_STOP);
    KW("RUN",             TOK_RUN);
    KW("MOVE",            TOK_MOVE);
    KW("TO",              TOK_TO);
    KW("ADD",             TOK_ADD);
    KW("SUBTRACT",        TOK_SUBTRACT);
    KW("MULTIPLY",        TOK_MULTIPLY);
    KW("DIVIDE",          TOK_DIVIDE);
    KW("COMPUTE",         TOK_COMPUTE);
    KW("IF",              TOK_IF);
    KW("THEN",            TOK_THEN);
    KW("ELSE",            TOK_ELSE);
    KW("END-IF",          TOK_END_IF);
    KW("PERFORM",         TOK_PERFORM);
    KW("UNTIL",           TOK_UNTIL);
    KW("THROUGH",         TOK_THROUGH);
    KW("VARYING",         TOK_VARYING);
    KW("FROM",            TOK_FROM);
    KW("BY",              TOK_BY);
    KW("OPEN",            TOK_OPEN);
    KW("CLOSE",           TOK_CLOSE);
    KW("READ",            TOK_READ);
    KW("WRITE",           TOK_WRITE);
    KW("INPUT",           TOK_INPUT);
    KW("OUTPUT",          TOK_OUTPUT);
    KW("I-O",             TOK_I_O);
    KW("EXTEND",          TOK_EXTEND);

    KW("PIC",             TOK_PIC);
    KW("USAGE",           TOK_USAGE);
    KW("COMP",            TOK_COMP);
    KW("COMP-1",          TOK_COMP_1);
    KW("COMP-2",          TOK_COMP_2);
    KW("COMP-3",          TOK_COMP_3);
    KW("COMP-5",          TOK_COMP_5);

    KW("ACCEPT",          TOK_ACCEPT);
    KW("INTO",            TOK_INTO);
    KW("GIVING",          TOK_GIVING);
    KW("ROUNDED",         TOK_ROUNDED);
    KW("SIZE",            TOK_SIZE);
    KW("ERROR",           TOK_ERROR);
    KW("AT",              TOK_AT);
    KW("END",             TOK_END);
    KW("DATE",            TOK_DATE);
    KW("TIME",            TOK_TIME);
    KW("DAY",             TOK_DAY);
    KW("DAY-OF-WEEK",     TOK_DAY_OF_WEEK);
    KW("YEAR",            TOK_YEAR);
    KW("YYYYMMDD",        TOK_YYYYMMDD);
    KW("YYDDD",           TOK_YYDDD);

    #undef KW

    if (len == 1) {
        switch (toupper(start[0])) {
            case 'X': return make_token(l, TOK_X, start, len);
            case 'V': return make_token(l, TOK_V, start, len);
            case 'S': return make_token(l, TOK_S, start, len);
            case 'P': return make_token(l, TOK_P, start, len);
        }
    }

    return make_token(l, TOK_IDENT, start, len);
}

Token lexer_next(Lexer *l) {
    skip_ws(l);

    const char *start = l->src;

    if (*l->src == '\0') {
        return make_token(l, TOK_EOF, start, 0);
    }

    // TODO: Handle fixed/free format here

    if (*l->src == '"') {
        advance(l);  // skip opening "
        start = l->src;
        while (*l->src && *l->src != '"') advance(l);
        size_t len = l->src - start;
        if (*l->src == '"') advance(l);  // skip closing "
        return make_token(l, TOK_STRING, start, len);
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
        return make_token(l, TOK_NUMBER, start, len);
    } else if (negative) {
        // If - not followed by number, treat as minus operator
        return make_token(l, TOK_MINUS, start, 1);
    }

    if (isalpha(*l->src)) {
        while (is_ident_char(*l->src)) advance(l);
        return keyword_or_ident(l, start, l->src - start);
    }

    if (*l->src == '(') {
        advance(l);
        return make_token(l, TOK_LPAREN, start, 1);
    }

    if (*l->src == ')') {
        advance(l);
        return make_token(l, TOK_RPAREN, start, 1);
    }

    if (*l->src == '.') {
        advance(l);
        return make_token(l, TOK_PERIOD, start, 1);
    }

    if (*l->src == '>') {
        advance(l);
        if (*l->src == '=') {
            advance(l);
            return make_token(l, TOK_GE, start, 2);
        }
        return make_token(l, TOK_GT, start, 1);
    }

    if (*l->src == '<') {
        advance(l);
        if (*l->src == '>') {
            advance(l);
            return make_token(l, TOK_NE, start, 2);
        }
        if (*l->src == '=') {
            advance(l);
            return make_token(l, TOK_LE, start, 2);
        }
        return make_token(l, TOK_LT, start, 1);
    }

    if (*l->src == '=') {
        advance(l);
        return make_token(l, TOK_EQ, start, 1);
    }

    if (*l->src == '+') {
        advance(l);
        return make_token(l, TOK_PLUS, start, 1);
    }

    if (*l->src == '-') {
        advance(l);
        return make_token(l, TOK_MINUS, start, 1);
    }

    if (*l->src == '*') {
        advance(l);
        return make_token(l, TOK_MUL, start, 1);
    }

    if (*l->src == '/') {
        advance(l);
        return make_token(l, TOK_DIV, start, 1);
    }

    cobalt_error_loc(l->original_source, l->current_line, l->current_col, "Unexpected character '%c'", *l->src);
    advance(l);
    return lexer_next(l);  // recovery
}
