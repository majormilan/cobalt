/* include/ast.h */
#ifndef COBALT_AST_H
#define COBALT_AST_H
#include <stdio.h>
#include <stdbool.h> // For bool type
#include <stddef.h>  // For size_t type

// New enum for UsageType
typedef enum {
    USAGE_DISPLAY,
    USAGE_COMP,
    // Add other COMP types like COMP-1, COMP-2, COMP-3, COMP-5 later
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

    struct AstNode *children;
    struct AstNode *next;
} AstNode;

AstNode *ast_new(AstKind kind, const char *name, SourceLocation loc);
void ast_free(AstNode *node);
void ast_dump(AstNode *node, FILE *out, int indent);

#endif
