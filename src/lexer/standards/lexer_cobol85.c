#include "lexer_cobol85.h" // Corrected include path
#include "lexer.h" // Corrected include path
#include "diag.h" // Corrected include path
#include <string.h>
#include <strings.h> // for strncasecmp
#include <ctype.h>

Token lexer_cobol85_keyword_or_ident(Lexer *l, const char *start, size_t len) {
    #define KW(s, k) if (len == strlen(s) && strncasecmp(start, s, len) == 0) return make_token_public(l, k, start, len)

    KW("IDENTIFICATION", TOK_IDENTIFICATION);
    KW("DIVISION",        TOK_DIVISION);
    KW("PROGRAM-ID",      TOK_PROGRAM_ID);
    KW("ENVIRONMENT",     TOK_ENVIRONMENT);
    KW("DATA",            TOK_DATA);
    KW("WORKING-STORAGE", TOK_WORKING_STORAGE);
    KW("LINKAGE",         TOK_LINKAGE);
    KW("FILE",            TOK_FILE);
    // LOCAL-STORAGE is COBOL 2002+, not in COBOL85
    // KW("LOCAL-STORAGE",   TOK_LOCAL_STORAGE);
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
    KW("END-IF",          TOK_END_IF); // END-IF is technically COBOL 85, but its mandatory usage can vary
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

    KW("IS",              TOK_IS); // Added for USAGE IS COMP/DISPLAY

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
    KW("ZEROS",           TOK_ZEROS);
    KW("ZERO",            TOK_ZEROS);
    KW("ZEROES",          TOK_ZEROS);
    KW("SPACES",          TOK_SPACES);
    KW("SPACE",           TOK_SPACES);
    KW("COPY",            TOK_COPY); // Add COPY keyword
    #undef KW

    if (len == 1) {
        switch (toupper(start[0])) {
            case 'X': return make_token_public(l, TOK_X, start, len);
            case 'V': return make_token_public(l, TOK_V, start, len);
            case 'S': return make_token_public(l, TOK_S, start, len);
            case 'P': return make_token_public(l, TOK_P, start, len);
        }
    }

    return make_token_public(l, TOK_IDENT, start, len);
}