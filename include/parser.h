#ifndef COBALT_PARSER_H
#define COBALT_PARSER_H

#include "ast.h"
#include "config.h"

AstNode *parse_program(CompilerMode *mode, const char *source);

#endif
