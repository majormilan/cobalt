/* include/diag.h */
#ifndef COBALT_DIAG_H
#define COBALT_DIAG_H

// Global diagnostic counters
extern int error_count;
extern int warning_count;

// Reset counters for a new compilation run
void reset_diagnostics(void);

void cobalt_error(const char *fmt, ...);
void cobalt_warn(const char *fmt, ...);
void cobalt_error_loc(const char *source, int line, int col, const char *fmt, ...);
void cobalt_warn_loc(const char *source, int line, int col, const char *fmt, ...);

#endif
