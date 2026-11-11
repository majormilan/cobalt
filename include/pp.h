/* include/pp.h
 * Source text manipulation (the COBOL-85 SM module): the COPY and REPLACE
 * statements, and the comment entries of the IDENTIFICATION DIVISION. Runs
 * on the program text after reference-format conversion, before the lexer.
 */
#ifndef COBALT_PP_H
#define COBALT_PP_H

#include <stdbool.h>

typedef struct {
    const char **dirs;          // copy library directories (-I), searched in order
    int ndirs;
    bool free_format;           // the source (and library texts) are in free format
} PpOptions;

// The text to compile. Lines of the result keep the line numbers of the
// source: where copied text is inserted, the lexer finds a line marker
// (PP_MARK line:column PP_MARK) that sets its position back. Returns NULL
// after an error (reported).
char *pp_process(const char *text, const char *filename, const PpOptions *opt);

#define PP_MARK '\x01'
#define PP_DEBUG_BEGIN '\x02'     // a debugging line of library text (pp.c)
#define PP_DEBUG_END '\x03'

#endif
