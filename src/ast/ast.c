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

AstNode *ast_clone(const AstNode *node) {
    if (!node) return NULL;
    AstNode *c = ast_new(node->kind, node->name, node->loc);
    if (!c) return NULL;
    if (node->text) c->text = strdup(node->text);
    c->data_size = node->data_size;
    c->usage_type = node->usage_type;
    c->is_numeric = node->is_numeric;
    c->is_signed = node->is_signed;
    c->decimal_places = node->decimal_places;
    c->digits = node->digits;
    c->rounded = node->rounded;
    c->children = ast_clone_list(node->children);
    c->on_error = ast_clone_list(node->on_error);
    c->not_on_error = ast_clone_list(node->not_on_error);
    c->extra = ast_clone_list(node->extra);
    c->loop = ast_clone_list(node->loop);
    return c;
}

AstNode *ast_clone_list(const AstNode *node) {
    AstNode *head = NULL, *tail = NULL;
    for (; node; node = node->next) {
        AstNode *c = ast_clone(node);
        if (!head) head = c;
        else tail->next = c;
        tail = c;
    }
    return head;
}

void ast_free(AstNode *node) {
    if (!node) return;

    free(node->name);
    free(node->text);

    ast_free(node->children);
    ast_free(node->on_error);
    ast_free(node->not_on_error);
    ast_free(node->extra);
    ast_free(node->loop);
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
        case AST_EXPR_POW:                  kind_name = "EXPR-POW"; break;
        case AST_EXPR_NEG:                  kind_name = "EXPR-NEG"; break;
        case AST_SENTENCE:                  kind_name = "SENTENCE"; break;
        case AST_STATEMENT_GOTO:            kind_name = "GO-TO"; break;
        case AST_STATEMENT_NEXT_SENTENCE:   kind_name = "NEXT-SENTENCE"; break;
        case AST_STATEMENT_EXIT:            kind_name = "EXIT"; break;
        case AST_VARYING:                   kind_name = "VARYING"; break;
        case AST_COND_AND:                  kind_name = "AND"; break;
        case AST_COND_OR:                   kind_name = "OR"; break;
        case AST_COND_NOT:                  kind_name = "NOT"; break;
        case AST_COND_CLASS:                kind_name = "CLASS"; break;
        case AST_COND_SIGN:                 kind_name = "SIGN"; break;
        case AST_COND_NAME:                 kind_name = "COND-NAME"; break;
        case AST_BOOL_LITERAL:              kind_name = "BOOL"; break;
        case AST_STATEMENT_EVALUATE:        kind_name = "EVALUATE"; break;
        case AST_WHEN:                      kind_name = "WHEN"; break;
        case AST_STATEMENT_SET:             kind_name = "SET"; break;
        case AST_OCCURS:                    kind_name = "OCCURS"; break;
        case AST_REDEFINES:                 kind_name = "REDEFINES"; break;
        case AST_JUSTIFIED:                 kind_name = "JUSTIFIED"; break;
        case AST_REFMOD:                    kind_name = "REFMOD"; break;
        case AST_STATEMENT_INITIALIZE:      kind_name = "INITIALIZE"; break;
        case AST_STATEMENT_SEARCH:          kind_name = "SEARCH"; break;
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
