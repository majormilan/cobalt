/* include/diag.h
 * Diagnostics: errors, warnings and the notes that follow them.
 *
 * Every diagnostic has a location (file, line, column) and shows the source
 * line with a caret under the column:
 *
 *   PAYROLL.cbl:15:22: error: 'GROSS-PAY' is not defined
 *      15 |            MOVE A TO GROSS-PAY.
 *         |                      ^~~~~~~~~
 *
 * Library texts brought in by COPY are files of their own: a diagnostic in
 * one says where it was copied ("In text copied at PAYROLL.cbl:12:").
 *
 * Warnings have names (-Wname, -Wno-name, -Werror=name). Extensions to the
 * standard are reported by the strictness level (-X): relaxed accepts them,
 * normal warns (-Wextension), pedantic rejects them, ibm-strict rejects all
 * but those of IBM Enterprise COBOL.
 */
#ifndef COBALT_DIAG_H
#define COBALT_DIAG_H

#include <stdbool.h>

typedef struct {
    const char *filename; // File where the token/symbol is located
    int line;
    int column;
} SourceLocation;

// Named warnings. Each has a default (on, or only with -Wextra)
typedef enum {
#define WARNING(id, name, extra, text) id,
#include "warnings.def"
#undef WARNING
    WARN_COUNT
} Warning;

// Who allows an extension without complaint: everybody's extension, or IBM's
// (accepted by ibm-strict as well)
typedef enum { EXT_ANY, EXT_IBM } ExtensionOwner;

// Global diagnostic counters
extern int error_count;
extern int warning_count;

// Reset counters for a new compilation run
void reset_diagnostics(void);

// The text of a source file or library text, kept for showing its lines.
// parent: the file holding the COPY statement (-1 for a source); at: where
// that statement is. Returns the file's number.
int diag_add_file(const char *name, const char *text, int parent, SourceLocation at);
const char *diag_file_name(int id);
// Whether a location is in text brought in by COPY
bool diag_in_copied_text(SourceLocation loc);

// Options
void diag_set_strictness(int strictness);      // a Strictness (config.h)
bool diag_option(const char *arg);             // -W..., -w, -fmax-errors=, -fdiagnostics-color=
void diag_print_warning_help(void);            // the warning names for --help

// FIPS PUB 21-4 subsets of COBOL-85 (--subset): the level of each module
// they contain. An element of a higher level (or of a module the subset
// leaves out) is reported "NON-CONFORMING STANDARD" (-Wsubset).
enum { SUBSET_HIGH, SUBSET_INTERMEDIATE, SUBSET_MINIMUM };
typedef enum {
    MOD_NC, MOD_SQ, MOD_RL, MOD_IX, MOD_ST, MOD_SM, MOD_IC, MOD_SG, MOD_DB, MOD_RW, MOD_IF, MOD_COUNT
} CobolModule;
void diag_set_subset(int subset);
bool diag_subset_active(void);
// An element of level `level` of a module; what names it ("COMPUTE")
void diag_subset(CobolModule module, int level, SourceLocation loc, const char *what);

// Diagnostics at a location (filename NULL: the current source)
void diag_error(SourceLocation loc, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
bool diag_warning(Warning w, SourceLocation loc, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void diag_note(SourceLocation loc, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void diag_note_plain(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   // a note without a location
// An element outside the standard: false when it is rejected (an error)
bool diag_extension(ExtensionOwner owner, SourceLocation loc, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
bool diag_warning_enabled(Warning w);
// Errors and warnings so far, printed at the end ("2 errors and 1 warning generated.")
void diag_summary(void);

// The closest of the candidates to a misspelled word, or NULL (n < 0: the
// candidates are NULL-terminated)
const char *diag_closest(const char *word, const char *const *candidates, int n);

// Diagnostics without a location (command line, files)
void cobalt_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void cobalt_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
