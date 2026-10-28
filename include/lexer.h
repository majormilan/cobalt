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
    TOK_END_PERFORM,
    TOK_GO,
    TOK_DEPENDING,
    TOK_NEXT,
    TOK_SENTENCE,
    TOK_EXIT,
    TOK_WITH,
    TOK_TEST,
    TOK_BEFORE,
    TOK_AFTER,
    TOK_GREATER,
    TOK_LESS,
    TOK_EQUAL,
    TOK_THAN,
    TOK_AND,
    TOK_OR,
    TOK_NUMERIC,
    TOK_ALPHABETIC,
    TOK_ALPHABETIC_LOWER,
    TOK_ALPHABETIC_UPPER,
    TOK_POSITIVE,
    TOK_NEGATIVE,
    TOK_EVALUATE,
    TOK_WHEN,
    TOK_ALSO,
    TOK_OTHER,
    TOK_ANY,
    TOK_TRUE,
    TOK_FALSE,
    TOK_END_EVALUATE,
    TOK_SET,
    TOK_HIGH_VALUES,
    TOK_LOW_VALUES,
    TOK_QUOTES,
    TOK_OF,
    TOK_IN,
    TOK_FILLER,
    TOK_REDEFINES,
    TOK_OCCURS,
    TOK_INDEXED,
    TOK_ASCENDING,
    TOK_DESCENDING,
    TOK_KEY,
    TOK_JUSTIFIED,
    TOK_RIGHT,
    TOK_LEFT,
    TOK_SYNCHRONIZED,
    TOK_BLANK,
    TOK_SIGN,
    TOK_INITIALIZE,
    TOK_SEARCH,
    TOK_ALL,
    TOK_END_SEARCH,
    TOK_UP,
    TOK_DOWN,
    TOK_INDEX,
    TOK_SELECT,
    TOK_ASSIGN,
    TOK_OPTIONAL,
    TOK_ORGANIZATION,
    TOK_SEQUENTIAL,
    TOK_LINE,
    TOK_ACCESS,
    TOK_MODE,
    TOK_STATUS,
    TOK_FD,
    TOK_SD,
    TOK_RECORD,
    TOK_REWRITE,
    TOK_END_READ,
    TOK_END_WRITE,
    TOK_END_REWRITE,
    TOK_ADVANCING,
    TOK_LINES,
    TOK_PAGE,
    TOK_END_OF_PAGE,
    TOK_LINAGE,
    TOK_RELATIVE,
    TOK_RANDOM,
    TOK_DYNAMIC,
    TOK_LEADING,
    TOK_TRAILING,
    TOK_SEPARATE,
    TOK_CHARACTER,
    TOK_CORRESPONDING,
    TOK_INSPECT,
    TOK_TALLYING,
    TOK_REPLACING,
    TOK_CONVERTING,
    TOK_CHARACTERS,
    TOK_FIRST,
    TOK_INITIAL,
    TOK_FOR,
    TOK_UPON,

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
    TOK_COLON,      // : (reference modification)

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
    bool decimal_comma;             // DECIMAL-POINT IS COMMA: ',' is the decimal point
} Lexer;

void lexer_init(Lexer *l, const char *source, const CompilerMode *mode, const char *filename);

// Convert fixed reference format (columns 1-6 sequence area, 7 indicator,
// 8-72 program text) to text the lexer reads as free format. Line numbers
// and columns are kept; comment lines become empty and continuation lines
// are joined to the line they continue. Returns a new buffer.
char *lexer_reference_format(const char *src);
Token lexer_next(Lexer *l);

// Public helper functions for standard-specific lexers
int is_ident_char(char c);
Token make_token_public(Lexer *l, TokenKind kind, const char *start, size_t length);

// Functions for managing lexer input source stack
void lexer_push_source(Lexer *l, const char *new_source_content, const char *new_filename, bool free_content);
void lexer_pop_source(Lexer *l);

#endif
