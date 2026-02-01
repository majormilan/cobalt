/* src/diag/diag.c */
#include "diag.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

extern char *current_filename;

static void print_context(const char *source, int line, int col) {
    if (!source) return;

    const char *line_start = source;
    for (int i = 1; i < line; i++) {
        const char *next = strchr(line_start, '\n');
        if (!next) return;
        line_start = next + 1;
    }

    const char *line_end = strchr(line_start, '\n');
    if (!line_end) line_end = line_start + strlen(line_start);

    fprintf(stderr, "  %.*s\n", (int)(line_end - line_start), line_start);

    fprintf(stderr, "  ");
    for (int j = 1; j < col; j++) {
        fputc(' ', stderr);
    }
    fprintf(stderr, "^\n");
}

void cobalt_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, "%s: ", current_filename);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void cobalt_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, "%s: ", current_filename);
    fprintf(stderr, "warning: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void cobalt_error_loc(const char *source, int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, "%s:", current_filename);
    fprintf(stderr, "%d:%d: error: ", line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    print_context(source, line, col);
}

void cobalt_warn_loc(const char *source, int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, "%s:", current_filename);
    fprintf(stderr, "%d:%d: warning: ", line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    print_context(source, line, col);
}
