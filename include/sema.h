#ifndef COBALT_SEMA_H
#define COBALT_SEMA_H

#include "ast.h"
#include "config.h"

typedef struct {
    CompilerMode *mode;
    // future: symbol table, error list, etc.
} SemaContext;

void sema_basic_check(AstNode *program, SemaContext *ctx);

#endif
