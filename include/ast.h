/* include/ast.h */
#ifndef COBALT_AST_H
#define COBALT_AST_H
#include <stdio.h>
#include <stdbool.h> // For bool type
#include <stddef.h>  // For size_t type

// Storage usage of a data item. The values are the runtime usage codes
// (runtime/ABI.md section 5), so they are passed to the runtime unchanged.
typedef enum {
    USAGE_DISPLAY = 0,
    USAGE_COMP    = 1,      // COMP, COMP-4, BINARY: big-endian binary
    USAGE_COMP3   = 2,      // COMP-3, PACKED-DECIMAL
    USAGE_COMP5   = 3,      // COMP-5: native binary
} AstUsageType;

typedef enum {
    AST_PROGRAM,
    AST_DIVISION,
    AST_SECTION,
    AST_STATEMENT_DISPLAY,
    AST_STATEMENT_STOP,
    AST_VAR_DECL,
    AST_STRING_LITERAL,
    AST_IDENTIFIER,

    AST_PARAGRAPH,
    AST_STATEMENT_MOVE,
    AST_STATEMENT_ADD,
    AST_STATEMENT_SUBTRACT,
    AST_STATEMENT_MULTIPLY,
    AST_STATEMENT_DIVIDE,
    AST_STATEMENT_COMPUTE,
    AST_STATEMENT_IF,
    AST_STATEMENT_PERFORM,
    AST_STATEMENT_ACCEPT,
    AST_STATEMENT_CONTINUE,
    AST_IDENTIFICATION_PARAGRAPH,
    AST_LITERAL_NUMBER,     // numeric literal
    AST_FIGURATIVE_CONSTANT, // e.g., ZEROS, SPACES
    AST_FILE_DECL,          // FD/SD
    AST_SELECT,
    AST_CONDITION,
    AST_PIC_CLAUSE,
    AST_USAGE_CLAUSE,
    AST_USAGE_TYPE_COMP,
    AST_USAGE_TYPE_DISPLAY,
    AST_THEN_BLOCK,   // wrapper for THEN-branch statements inside AST_STATEMENT_IF
    AST_ELSE_BLOCK,   // wrapper for ELSE-branch statements inside AST_STATEMENT_IF
    // Arithmetic expression nodes (used by COMPUTE)
    // children[0] = left operand, children[0]->next = right operand
    AST_EXPR_ADD,
    AST_EXPR_SUB,
    AST_EXPR_MUL,
    AST_EXPR_DIV,
    AST_EXPR_POW,           // children[0] ** children[1]
    AST_EXPR_NEG,           // unary minus: children[0]
} AstKind;


typedef struct {
    const char *filename; // File where the token/symbol is located
    int line;
    int column;
} SourceLocation;

typedef struct AstNode {
    AstKind kind;
    char *name;             // owned
    char *text;             // owned
    SourceLocation loc;     // ← new: where this node starts in source

    // Semantic information for VAR_DECL nodes
    size_t data_size;       // Size in bytes
    AstUsageType usage_type; // e.g., DISPLAY, COMP
    bool is_numeric;        // True if the data item is numeric
    bool is_signed;         // True if the data item is signed
    int decimal_places;     // Number of decimal places for numeric items (V in PIC)
    int digits;             // Number of digit positions (9s) for numeric items

    // Arithmetic statements
    bool rounded;           // ROUNDED on this receiving identifier
    struct AstNode *on_error;     // ON SIZE ERROR statements (NULL if absent)
    struct AstNode *not_on_error; // NOT ON SIZE ERROR statements (NULL if absent)

    struct AstNode *children;
    struct AstNode *next;
} AstNode;

AstNode *ast_new(AstKind kind, const char *name, SourceLocation loc);
void ast_free(AstNode *node);
void ast_dump(AstNode *node, FILE *out, int indent);

#endif
