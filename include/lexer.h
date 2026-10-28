#ifndef COBALT_LEXER_H
#define COBALT_LEXER_H
#include <stddef.h>
#include "config.h" // Include config.h for CompilerMode

// Forward declaration
struct Lexer;

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_STRING,
    TOK_PERIOD,
    TOK_NUMBER,

    /* keywords */
    TOK_IDENTIFICATION,
    TOK_DIVISION,
    TOK_PROGRAM_ID,
    TOK_ENVIRONMENT,
    TOK_DATA,
    TOK_WORKING_STORAGE,
    TOK_LINKAGE,
    TOK_FILE,
    TOK_LOCAL_STORAGE,      // COBOL 2002+, but useful for future
    TOK_PROCEDURE,
    TOK_VALUE,

    /* Paragraph/Section */
    TOK_SECTION,

    /* Statements */
    TOK_DISPLAY,
    TOK_STOP,
    TOK_RUN,
    TOK_MOVE,
    TOK_TO,
    TOK_ADD,
    TOK_SUBTRACT,
    TOK_MULTIPLY,
    TOK_DIVIDE,
    TOK_COMPUTE,
    TOK_IF,
    TOK_THEN,
    TOK_ELSE,
    TOK_END_IF,
    TOK_PERFORM,
    TOK_UNTIL,
    TOK_THROUGH,
    TOK_VARYING,
    TOK_FROM,
    TOK_BY,
    TOK_OPEN,
    TOK_CLOSE,
    TOK_READ,
    TOK_WRITE,
    TOK_INPUT,
    TOK_OUTPUT,
    TOK_I_O,
    TOK_EXTEND,

    /* PIC clause symbols */
    TOK_PIC,
    TOK_PIC_STRING, // the character-string after PIC/PICTURE [IS]
    TOK_X,
    TOK_9,
    TOK_V,
    TOK_S,
    TOK_P,
    TOK_USAGE,
    TOK_IS,
    TOK_COMP,
    TOK_COMP_1,
    TOK_COMP_2,
    TOK_COMP_3,
    TOK_COMP_5,
    TOK_BINARY,

    /* Parentheses (used in PIC clauses and expressions) */
    TOK_LPAREN,     // (
    TOK_RPAREN,     // )

    /* Misc */
    TOK_INTO,
    TOK_GIVING,
    TOK_ROUNDED,
    TOK_SIZE,
    TOK_ERROR,
    TOK_AT,
    TOK_END,
    TOK_ACCEPT,
    TOK_DATE,
    TOK_TIME,
    TOK_DAY,
    TOK_DAY_OF_WEEK,
    TOK_YEAR,
    TOK_YYYYMMDD,
    TOK_YYDDD,
    TOK_ZEROS,
    TOK_SPACES,
    TOK_COPY,
    TOK_CONTINUE,
    TOK_TIMES,
    TOK_REMAINDER,
    TOK_NOT,
    TOK_ON,
    TOK_END_ADD,
    TOK_END_SUBTRACT,
    TOK_END_MULTIPLY,
    TOK_END_DIVIDE,
    TOK_END_COMPUTE,

    // Operators
    TOK_GT,
    TOK_LT,
    TOK_GE,
    TOK_LE,
    TOK_EQ,
    TOK_NE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MUL,
    TOK_DIV,
    TOK_POW,        // **

    // Add more as needed...
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *lexeme;
    size_t length;
    const char *filename; // File where the token/symbol is located
    int line;
    int column;
} Token;

typedef struct Lexer {
    const char *src;
    int current_line;
    int current_col;
    const char *original_source; // The original source string, used for error reporting
    const CompilerMode *mode;    // Add CompilerMode to lexer state
    Token (*get_keyword_or_ident)(struct Lexer *l, const char *start, size_t len); // Function pointer for standard-specific keyword check

    // For COPY statement handling
    const char *filename;           // The name of the current file being lexed
    char *file_content;             // Owned content if read from a file, NULL otherwise
    bool free_content_on_pop;       // Whether to free `file_content` when this lexer is popped
    struct Lexer *prev;             // Pointer to the previous lexer state (for COPY)

    bool pic_next;                  // next word is a PICTURE character-string
} Lexer;

void lexer_init(Lexer *l, const char *source, const CompilerMode *mode, const char *filename);
Token lexer_next(Lexer *l);

// Public helper functions for standard-specific lexers
int is_ident_char(char c);
Token make_token_public(Lexer *l, TokenKind kind, const char *start, size_t length);

// Functions for managing lexer input source stack
void lexer_push_source(Lexer *l, const char *new_source_content, const char *new_filename, bool free_content);
void lexer_pop_source(Lexer *l);

#endif
