#ifndef COBALT_LEXER_COBOL2002_H
#define COBALT_LEXER_COBOL2002_H

#include "lexer.h" // Corrected include path

Token lexer_cobol2002_keyword_or_ident(Lexer *l, const char *start, size_t len);

#endif