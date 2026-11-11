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

// The text to compile, of the source registered as file (diag_add_file).
// Lines of the result keep the line numbers of the source; copied text
// starts with a line marker (PP_MARK line:column:file PP_MARK) naming its
// library text (registered too), and a marker after it sets the position
// back. Returns NULL after an error (reported).
char *pp_process(const char *text, int file, const PpOptions *opt);

#define PP_MARK '\x01'
#define PP_DEBUG_BEGIN '\x02'     // a debugging line of library text (pp.c)
#define PP_DEBUG_END '\x03'

#endif
