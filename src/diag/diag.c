/* src/diag/diag.c */
#include "diag.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ANSI color codes
#define CLR_RESET   "\x1b[0m"
#define CLR_BOLD    "\x1b[1m"
#define CLR_RED     "\x1b[31m"
#define CLR_YELLOW  "\x1b[33m"
#define CLR_CYAN    "\x1b[36m"

extern char *current_filename;

// Global diagnostic counters
int error_count = 0;
int warning_count = 0;

void reset_diagnostics(void) {
    error_count = 0;
    warning_count = 0;
}

static void print_context(const char *source, int line, int col, const char *color) {
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

    fprintf(stderr, "  %s%s", CLR_BOLD, color);
    for (int j = 1; j < col; j++) {
        fputc(' ', stderr);
    }
    fprintf(stderr, "^" CLR_RESET "\n");
}

void cobalt_error(const char *fmt, ...) {
    error_count++;
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, CLR_BOLD "%s: " CLR_RESET, current_filename);
    fprintf(stderr, CLR_BOLD CLR_RED "error: " CLR_RESET CLR_BOLD);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, CLR_RESET "\n");
    va_end(ap);
}

void cobalt_warn(const char *fmt, ...) {
    warning_count++;
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, CLR_BOLD "%s: " CLR_RESET, current_filename);
    fprintf(stderr, CLR_BOLD CLR_YELLOW "warning: " CLR_RESET CLR_BOLD);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, CLR_RESET "\n");
    va_end(ap);
}

void cobalt_error_loc(const char *source, int line, int col, const char *fmt, ...) {
    error_count++;
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, CLR_BOLD "%s:", current_filename);
    fprintf(stderr, "%d:%d: " CLR_RESET, line, col);
    fprintf(stderr, CLR_BOLD CLR_RED "error: " CLR_RESET CLR_BOLD);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, CLR_RESET "\n");
    va_end(ap);
    print_context(source, line, col, CLR_RED);
}

void cobalt_warn_loc(const char *source, int line, int col, const char *fmt, ...) {
    warning_count++;
    va_list ap;
    va_start(ap, fmt);
    if (current_filename) fprintf(stderr, CLR_BOLD "%s:", current_filename);
    fprintf(stderr, "%d:%d: " CLR_RESET, line, col);
    fprintf(stderr, CLR_BOLD CLR_YELLOW "warning: " CLR_RESET CLR_BOLD);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, CLR_RESET "\n");
    va_end(ap);
    print_context(source, line, col, CLR_YELLOW);
}
