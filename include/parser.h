#ifndef COBALT_PARSER_H
#define COBALT_PARSER_H

#include "ast.h"
#include "config.h"
#include "lexer.h" // Include lexer.h for Lexer struct

AstNode *parse_program(Lexer *l);

// Errors for names that are not defined: they leave a sound syntax tree, so
// the later stages run anyway (and find more)
extern int parser_name_errors;

// Syntax errors in the statements of the PROCEDURE DIVISION: recovery drops
// the statement, the data and the rest of the tree are sound, so the later
// stages run too (without the warnings a missing statement could make wrong)
extern int parser_statement_errors;

#endif
