#ifndef COBALT_PARSER_H
#define COBALT_PARSER_H

#include "ast.h"
#include "config.h"
#include "lexer.h" // Include lexer.h for Lexer struct

AstNode *parse_program(Lexer *l);

#endif
