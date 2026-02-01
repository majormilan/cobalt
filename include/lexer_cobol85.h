#ifndef COBALT_LEXER_COBOL85_H
#define COBALT_LEXER_COBOL85_H

#include "lexer.h" // Corrected include path

Token lexer_cobol85_keyword_or_ident(Lexer *l, const char *start, size_t len);

#endif