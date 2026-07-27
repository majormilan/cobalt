/* src/ast/ast.c */
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AstNode *ast_new(AstKind kind, const char *name, SourceLocation loc) {
    AstNode *n = calloc(1, sizeof(AstNode));
    if (!n) return NULL;
    n->kind = kind;
    if (name) {
        n->name = strdup(name);
    }
    n->loc = loc;
    // Initialize new semantic fields. calloc already zeros out the memory.
    n->data_size = 0;
    n->usage_type = USAGE_DISPLAY; // Default COBOL usage type
    n->is_numeric = false;
    n->is_signed = false;
    n->decimal_places = 0;
    return n;
}

void ast_free(AstNode *node) {
    if (!node) return;

    free(node->name);
    free(node->text);

    ast_free(node->children);
    ast_free(node->next);

    free(node);
}

void ast_dump(AstNode *node, FILE *out, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; i++) fprintf(out, "  ");

    const char *kind_name = "???";
    switch (node->kind) {
        case AST_PROGRAM:                   kind_name = "PROGRAM"; break;
        case AST_DIVISION:                  kind_name = "DIVISION"; break;
        case AST_SECTION:                   kind_name = "SECTION"; break;
        case AST_STATEMENT_DISPLAY:         kind_name = "DISPLAY"; break;
        case AST_STATEMENT_STOP:            kind_name = "STOP-RUN"; break;
        case AST_VAR_DECL:                  kind_name = "VAR-DECL"; break;
        case AST_STRING_LITERAL:            kind_name = "STRING"; break;
        case AST_IDENTIFIER:                kind_name = "IDENT"; break;
        case AST_LITERAL_NUMBER:            kind_name = "NUMBER"; break;
        case AST_FIGURATIVE_CONSTANT:       kind_name = "FIG-CONST"; break;
        case AST_PARAGRAPH:                 kind_name = "PARAGRAPH"; break;
        case AST_STATEMENT_MOVE:            kind_name = "MOVE"; break;
        case AST_STATEMENT_ADD:             kind_name = "ADD"; break;
        case AST_STATEMENT_SUBTRACT:        kind_name = "SUBTRACT"; break;
        case AST_STATEMENT_MULTIPLY:        kind_name = "MULTIPLY"; break;
        case AST_STATEMENT_DIVIDE:          kind_name = "DIVIDE"; break;
        case AST_STATEMENT_COMPUTE:         kind_name = "COMPUTE"; break;
        case AST_STATEMENT_IF:              kind_name = "IF"; break;
        case AST_STATEMENT_PERFORM:         kind_name = "PERFORM"; break;
        case AST_STATEMENT_ACCEPT:          kind_name = "ACCEPT"; break;
        case AST_STATEMENT_CONTINUE:        kind_name = "CONTINUE"; break;
        case AST_IDENTIFICATION_PARAGRAPH:  kind_name = "ID-PARAGRAPH"; break;
        case AST_CONDITION:                 kind_name = "CONDITION"; break;
        case AST_PIC_CLAUSE:                kind_name = "PIC"; break;
        case AST_USAGE_CLAUSE:              kind_name = "USAGE"; break;
        case AST_USAGE_TYPE_COMP:           kind_name = "USAGE-COMP"; break;
        case AST_USAGE_TYPE_DISPLAY:        kind_name = "USAGE-DISPLAY"; break;
        case AST_THEN_BLOCK:                kind_name = "THEN-BLOCK"; break;
        case AST_ELSE_BLOCK:                kind_name = "ELSE-BLOCK"; break;
        case AST_EXPR_ADD:                  kind_name = "EXPR-ADD"; break;
        case AST_EXPR_SUB:                  kind_name = "EXPR-SUB"; break;
        case AST_EXPR_MUL:                  kind_name = "EXPR-MUL"; break;
        case AST_EXPR_DIV:                  kind_name = "EXPR-DIV"; break;
        default:                            kind_name = "UNKNOWN"; break;
    }

    fprintf(out, "%s", kind_name);

    if (node->name) fprintf(out, " name=\"%s\"", node->name);
    if (node->text) fprintf(out, " text=\"%s\"", node->text);

    fprintf(out, " [%d:%d]", node->loc.line, node->loc.column);

    fprintf(out, "\n");

    ast_dump(node->children, out, indent + 1);
    ast_dump(node->next, out, indent);
}
