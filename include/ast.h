/* include/ast.h */
#ifndef COBALT_AST_H
#define COBALT_AST_H
#include <stdio.h>

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
    AST_IDENTIFICATION_PARAGRAPH,
    AST_LITERAL_NUMBER,     // numeric literal
    AST_FILE_DECL,          // FD/SD
    AST_SELECT,
    AST_CONDITION,
    AST_PIC_CLAUSE,
} AstKind;

typedef struct {
    int line;
    int column;
} SourceLocation;

typedef struct AstNode {
    AstKind kind;
    char *name;             // owned
    char *text;             // owned
    SourceLocation loc;     // ← new: where this node starts in source
    struct AstNode *children;
    struct AstNode *next;
} AstNode;

AstNode *ast_new(AstKind kind, const char *name, SourceLocation loc);
void ast_free(AstNode *node);
void ast_dump(AstNode *node, FILE *out, int indent);

#endif
