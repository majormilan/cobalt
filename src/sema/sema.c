/* src/sema/sema.c */
#include "sema.h"
#include "diag.h"
#include "ast.h"
#include <strings.h>
#include <stdbool.h>

static bool has_stop_run(AstNode *node) {
    if (!node) return false;

    if (node->kind == AST_STATEMENT_STOP) {
        return true;
    }

    // Depth-first: check children
    if (has_stop_run(node->children)) {
        return true;
    }

    // Then siblings
    if (has_stop_run(node->next)) {
        return true;
    }

    return false;
}

static AstNode *find_procedure_division(AstNode *program) {
    AstNode *div = program->children;
    while (div) {
        if (div->kind == AST_DIVISION &&
            strcasecmp(div->name, "PROCEDURE") == 0) {
            return div;
        }
        div = div->next;
    }
    return NULL;
}

void sema_basic_check(AstNode *program, SemaContext *ctx) {
    (void)ctx;  // unused for now

    AstNode *proc_div = find_procedure_division(program);
    if (!proc_div) {
        // Parser already reports missing PROCEDURE DIVISION
        return;
    }

    if (!has_stop_run(proc_div)) {
        cobalt_error("No STOP RUN found in PROCEDURE DIVISION");
    }

    // Future checks can be added here:
    // - Undeclared identifiers
    // - PERFORM targets exist
    // - Type compatibility in arithmetic
    // - etc.
}
