/* src/diag/diag.c
 * Diagnostics (see include/diag.h)
 */
#include "diag.h"
#include "config.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char *current_filename;

int error_count = 0;
int warning_count = 0;

// ---------------------------------------------------------------- options

static const struct { const char *name; bool extra; const char *text; } warnings[] = {
#define WARNING(id, name, extra, text) { name, extra, text },
#include "warnings.def"
#undef WARNING
};

static bool enabled[WARN_COUNT];
static bool as_error[WARN_COUNT];
static bool explicit_setting[WARN_COUNT];      // -W / -Wno- given: strictness leaves it alone
static bool all_errors;                        // -Werror
static bool no_warnings;                       // -w
static int max_errors;                         // -fmax-errors (0: no limit)
static int color = -1;                         // -fdiagnostics-color: -1 auto, 0 never, 1 always
static int strictness = STRICT_NORMAL;
static bool initialized;

static void init(void) {
    if (initialized) return;
    initialized = true;
    for (int w = 0; w < WARN_COUNT; w++) enabled[w] = !warnings[w].extra;
}

void reset_diagnostics(void) {
    init();
    error_count = 0;
    warning_count = 0;
}

static bool use_color(void) {
    if (color < 0) {
        const char *term = getenv("TERM");
        color = isatty(2) && !getenv("NO_COLOR") && term && strcmp(term, "dumb") != 0;
    }
    return color;
}

#define C(code) (use_color() ? code : "")
#define RESET   C("\x1b[0m")
#define BOLD    C("\x1b[1m")
#define RED     C("\x1b[1;31m")
#define MAGENTA C("\x1b[1;35m")
#define CYAN    C("\x1b[1;36m")
#define GREEN   C("\x1b[1;32m")

void diag_set_strictness(int s) {
    init();
    strictness = s;
    // Pedantic flags the obsolete elements too
    if ((s == STRICT_PEDANTIC || s == STRICT_IBM) && !explicit_setting[W_OBSOLETE]) enabled[W_OBSOLETE] = true;
    if (s == STRICT_RELAXED) {
        // Relaxed: the reference format is not checked
        if (!explicit_setting[W_AREA]) enabled[W_AREA] = false;
        if (!explicit_setting[W_COLUMN]) enabled[W_COLUMN] = false;
    }
}

static int warning_by_name(const char *name) {
    for (int w = 0; w < WARN_COUNT; w++)
        if (strcmp(warnings[w].name, name) == 0) return w;
    return -1;
}

static void unknown_warning(const char *arg, const char *name) {
    const char *names[WARN_COUNT];
    for (int w = 0; w < WARN_COUNT; w++) names[w] = warnings[w].name;
    const char *near = diag_closest(name, names, WARN_COUNT);
    if (near) cobalt_error("Unknown warning option '%s'; did you mean '-W%s'?", arg, near);
    else cobalt_error("Unknown warning option '%s' (see --help for the warnings)", arg);
}

bool diag_option(const char *arg) {
    init();
    if (strcmp(arg, "-w") == 0) { no_warnings = true; return true; }
    if (strncmp(arg, "-fmax-errors=", 13) == 0) {
        char *end;
        long n = strtol(arg + 13, &end, 10);
        if (*end || n < 0) { cobalt_error("Invalid number in '%s'", arg); return false; }
        max_errors = (int)n;
        return true;
    }
    if (strcmp(arg, "-fdiagnostics-color") == 0 || strcmp(arg, "-fdiagnostics-color=always") == 0) { color = 1; return true; }
    if (strcmp(arg, "-fno-diagnostics-color") == 0 || strcmp(arg, "-fdiagnostics-color=never") == 0) { color = 0; return true; }
    if (strcmp(arg, "-fdiagnostics-color=auto") == 0) { color = -1; return true; }
    if (strncmp(arg, "-W", 2) != 0) return false;
    const char *w = arg + 2;
    if (strcmp(w, "all") == 0) {
        for (int k = 0; k < WARN_COUNT; k++)
            if (!warnings[k].extra && !explicit_setting[k]) enabled[k] = true;
        return true;
    }
    if (strcmp(w, "extra") == 0) {
        for (int k = 0; k < WARN_COUNT; k++)
            if (!explicit_setting[k]) enabled[k] = true;
        return true;
    }
    if (strcmp(w, "error") == 0) { all_errors = true; return true; }
    if (strcmp(w, "no-error") == 0) { all_errors = false; return true; }
    bool error = strncmp(w, "error=", 6) == 0, no_error = strncmp(w, "no-error=", 9) == 0;
    bool off = !error && !no_error && strncmp(w, "no-", 3) == 0;
    const char *name = error ? w + 6 : no_error ? w + 9 : off ? w + 3 : w;
    int k = warning_by_name(name);
    if (k < 0) { unknown_warning(arg, name); return false; }
    if (error) { as_error[k] = true; enabled[k] = true; }
    else if (no_error) as_error[k] = false;
    else { enabled[k] = !off; explicit_setting[k] = true; }
    return true;
}

void diag_print_warning_help(void) {
    for (int w = 0; w < WARN_COUNT; w++)
        printf("        %-20s %s%s\n", warnings[w].name, warnings[w].text, warnings[w].extra ? " (-Wextra)" : "");
}

// After statements were dropped by error recovery: the warnings that
// depend on every statement being there (a dropped MOVE would make an item
// "never given a value", a dropped PERFORM a paragraph "never reached")
void diag_quiet_flow_warnings(void) {
    init();
    static const Warning flow[] = { W_UNUSED, W_UNUSED_FILE, W_UNUSED_PROCEDURE, W_UNINITIALIZED,
                                    W_UNREACHABLE, W_UNCHECKED_IO, W_PERFORM_RANGE };
    for (size_t k = 0; k < sizeof(flow) / sizeof(flow[0]); k++) enabled[flow[k]] = false;
}

bool diag_warning_enabled(Warning w) {
    init();
    return !no_warnings && enabled[w];
}

// ---------------------------------------------------------------- files

typedef struct {
    char *name;                 // also the identity of this copy of a text
    char *text;
    int parent;
    SourceLocation at;
    const char **lines;         // start of each line (built when first shown)
    int nlines;
} File;

static File *files;
static int nfiles, cap_files;

int diag_add_file(const char *name, const char *text, int parent, SourceLocation at) {
    if (nfiles == cap_files) {
        cap_files = cap_files ? cap_files * 2 : 16;
        files = realloc(files, cap_files * sizeof(File));
    }
    while (name[0] == '.' && name[1] == '/') name += 2;            // "./X.cpy": X.cpy
    files[nfiles] = (File){ strdup(name), text ? strdup(text) : NULL, parent, at, NULL, 0 };
    return nfiles++;
}

int diag_file_parent(int id) {
    return id >= 0 && id < nfiles ? files[id].parent : -1;
}

const char *diag_file_name(int id) {
    return id >= 0 && id < nfiles ? files[id].name : NULL;
}

// The file a location names: the same string (one copy of a library text),
// else the last file of that name
static File *file_of(const char *name) {
    if (!name) return NULL;
    for (int k = nfiles - 1; k >= 0; k--)
        if (files[k].name == name) return &files[k];
    for (int k = nfiles - 1; k >= 0; k--)
        if (strcmp(files[k].name, name) == 0) return &files[k];
    return NULL;
}

bool diag_in_copied_text(SourceLocation loc) {
    File *f = file_of(loc.filename);
    return f && f->parent >= 0;
}

static const char *line_of(File *f, int line, int *len) {
    if (!f || !f->text || line < 1) return NULL;
    if (!f->lines) {
        int cap = 256;
        f->lines = malloc(cap * sizeof(char *));
        f->lines[f->nlines++] = f->text;
        for (const char *p = f->text; *p; p++)
            if (*p == '\n' && p[1]) {
                if (f->nlines == cap) { cap *= 2; f->lines = realloc(f->lines, cap * sizeof(char *)); }
                f->lines[f->nlines++] = p + 1;
            }
    }
    if (line > f->nlines) return NULL;
    const char *s = f->lines[line - 1];
    const char *e = strchr(s, '\n');
    *len = e ? (int)(e - s) : (int)strlen(s);
    if (*len > 0 && s[*len - 1] == '\r') (*len)--;
    return s;
}

// ---------------------------------------------------------------- output

static bool last_shown;         // whether notes of the last diagnostic are shown
static bool any_located;        // a diagnostic about a source was given (for the summary)

static bool word_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

static void show_line(SourceLocation loc, const char *color_code) {
    File *f = file_of(loc.filename);
    int len;
    const char *s = line_of(f, loc.line, &len);
    if (!s || loc.column < 1) return;
    int col = loc.column - 1;
    if (col > len) col = len;
    // Underline the word or literal at the column
    int end = col + 1;
    if (col < len && word_char(s[col])) {
        end = col;
        while (end < len && word_char(s[end])) end++;
        while (end > col + 1 && s[end - 1] == '-') end--;
    } else if (col < len && (s[col] == '"' || s[col] == '\'')) {
        end = col + 1;
        while (end < len && s[end] != s[col]) end++;
        if (end < len) end++;
    }
    fprintf(stderr, " %5d | %.*s\n", loc.line, len, s);
    fprintf(stderr, "       | ");
    for (int k = 0; k < col; k++) fputc(s[k] == '\t' ? '\t' : ' ', stderr);
    fprintf(stderr, "%s^", color_code);
    for (int k = col + 1; k < end; k++) fputc('~', stderr);
    fprintf(stderr, "%s\n", RESET);
}

// Where the location's library text was copied, before the message:
//   In text copied at BADREC.cpy:5:12,
//                  from LEDGER.cbl:7:8:
static void show_inclusion(SourceLocation loc) {
    File *f = file_of(loc.filename);
    bool first = true;
    for (File *p = f; p && p->parent >= 0; p = &files[p->parent]) {
        fprintf(stderr, "%s%s:%d:%d", first ? "In text copied at " : "                 from ",
                p->at.filename ? p->at.filename : "?", p->at.line, p->at.column);
        fprintf(stderr, "%s\n", files[p->parent].parent >= 0 ? "," : ":");
        first = false;
    }
}

static void location_prefix(SourceLocation loc) {
    any_located = true;
    const char *name = loc.filename ? loc.filename : current_filename;
    if (name && loc.line > 0) fprintf(stderr, "%s%s:%d:%d:%s ", BOLD, name, loc.line, loc.column, RESET);
    else if (name) fprintf(stderr, "%s%s:%s ", BOLD, name, RESET);
}

static void too_many_errors(void) {
    if (max_errors && error_count >= max_errors) {
        fprintf(stderr, "%sfatal error:%s too many errors emitted, stopping now [-fmax-errors=%d]\n", RED, RESET, max_errors);
        exit(1);              // diag_summary runs at exit

    }
}

static void report(const char *kind, const char *color_code, SourceLocation loc, const char *option,
                   const char *fmt, va_list ap) {
    show_inclusion(loc);
    location_prefix(loc);
    fprintf(stderr, "%s%s:%s %s", color_code, kind, RESET, BOLD);
    vfprintf(stderr, fmt, ap);
    if (option) fprintf(stderr, " [%s]", option);
    fprintf(stderr, "%s\n", RESET);
    show_line(loc, color_code);
}

// An error reported again at the same place (by a later stage of the
// compiler): shown once
static bool repeated(SourceLocation loc, const char *text) {
    static char **seen;
    static int nseen, cap;
    if (!loc.line) return false;
    char key[640];
    snprintf(key, sizeof(key), "%s:%d:%d:%s", loc.filename ? loc.filename : "", loc.line, loc.column, text);
    for (int k = 0; k < nseen; k++)
        if (strcmp(seen[k], key) == 0) return true;
    if (nseen == cap) { cap = cap ? cap * 2 : 64; seen = realloc(seen, cap * sizeof(char *)); }
    seen[nseen++] = strdup(key);
    return false;
}

void diag_error(SourceLocation loc, const char *fmt, ...) {
    init();
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (repeated(loc, text)) { last_shown = false; return; }
    // A second error at a place that already has one follows from the first
    // (a later stage tripping over what an earlier one rejected)
    if (repeated(loc, "\1error here")) { last_shown = false; return; }
    error_count++;
    show_inclusion(loc);
    location_prefix(loc);
    fprintf(stderr, "%serror:%s %s%s%s\n", RED, RESET, BOLD, text, RESET);
    show_line(loc, RED);
    last_shown = true;
    too_many_errors();
}

static bool repeated(SourceLocation loc, const char *text);

static bool warn_v(Warning w, SourceLocation loc, const char *fmt, va_list ap) {
    if (!diag_warning_enabled(w)) { last_shown = false; return false; }
    char text[512];
    va_list copy;
    va_copy(copy, ap);
    vsnprintf(text, sizeof(text), fmt, copy);
    va_end(copy);
    if (repeated(loc, text)) { last_shown = false; return false; }
    char option[64];
    bool err = all_errors || as_error[w];
    snprintf(option, sizeof(option), err ? "-Werror=%s" : "-W%s", warnings[w].name);
    if (err) error_count++;
    else warning_count++;
    report(err ? "error" : "warning", err ? RED : MAGENTA, loc, option, fmt, ap);
    last_shown = true;
    if (err) too_many_errors();
    return true;
}

bool diag_warning(Warning w, SourceLocation loc, const char *fmt, ...) {
    init();
    va_list ap;
    va_start(ap, fmt);
    bool shown = warn_v(w, loc, fmt, ap);
    va_end(ap);
    return shown;
}

void diag_note(SourceLocation loc, const char *fmt, ...) {
    if (!last_shown) return;
    va_list ap;
    va_start(ap, fmt);
    location_prefix(loc);
    fprintf(stderr, "%snote:%s ", CYAN, RESET);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    show_line(loc, GREEN);
}

void diag_note_plain(const char *fmt, ...) {
    if (!last_shown) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%snote:%s ", CYAN, RESET);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int diag_strictness(void) { return strictness; }

// ---------------------------------------------------------------- FIPS subsets

static int subset = SUBSET_HIGH;

void diag_set_subset(int s) { subset = s; }
bool diag_subset_active(void) { return subset != SUBSET_HIGH; }

void diag_subset(CobolModule module, int level, SourceLocation loc, const char *what) {
    //                                        NC SQ RL IX ST SM IC SG DB RW IF
    static const int levels[3][MOD_COUNT] = { { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },     // High
                                              { 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },     // Intermediate
                                              { 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0 } };   // Minimum
    static const char *const names[MOD_COUNT] = {
        "Nucleus", "Sequential I-O", "Relative I-O", "Indexed I-O", "Sort-Merge", "Source Text Manipulation",
        "Inter-Program Communication", "Segmentation", "Debug", "Report Writer", "Intrinsic Functions" };
    static const char *const subsets[3] = { "High", "Intermediate", "Minimum" };
    if (subset == SUBSET_HIGH || level <= levels[subset][module]) return;
    if (levels[subset][module] == 0)
        diag_warning(W_SUBSET, loc, "NON-CONFORMING STANDARD: %s is not in the %s subset (the %s module)",
                     what, subsets[subset], names[module]);
    else
        diag_warning(W_SUBSET, loc, "NON-CONFORMING STANDARD: %s is not in the %s subset (%s level %d)",
                     what, subsets[subset], names[module], level);
}

void diag_summary(void) {
    if (!any_located || (!error_count && !warning_count)) return;
    char e[32] = "", w[32] = "";
    if (error_count) snprintf(e, sizeof(e), "%d error%s", error_count, error_count == 1 ? "" : "s");
    if (warning_count) snprintf(w, sizeof(w), "%d warning%s", warning_count, warning_count == 1 ? "" : "s");
    fprintf(stderr, "%s%s%s generated.\n", e, *e && *w ? " and " : "", w);
}

// ---------------------------------------------------------------- spelling

static int distance(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 64 || lb > 64) return 99;
    int d[65][65];
    for (int i = 0; i <= la; i++) d[i][0] = i;
    for (int j = 0; j <= lb; j++) d[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++) {
            int cost = toupper((unsigned char)a[i - 1]) != toupper((unsigned char)b[j - 1]);
            int v = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < v) v = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < v) v = d[i - 1][j - 1] + cost;
            // a transposition counts as one
            if (i > 1 && j > 1 && toupper((unsigned char)a[i - 1]) == toupper((unsigned char)b[j - 2]) &&
                toupper((unsigned char)a[i - 2]) == toupper((unsigned char)b[j - 1]) && d[i - 2][j - 2] + 1 < v)
                v = d[i - 2][j - 2] + 1;
            d[i][j] = v;
        }
    return d[la][lb];
}

const char *diag_closest(const char *word, const char *const *candidates, int n) {
    int best = 99;
    const char *found = NULL;
    int len = (int)strlen(word);
    int limit = len <= 3 ? 1 : len <= 8 ? 2 : 3;
    if (n < 0) for (n = 0; candidates[n]; n++) {}       // NULL-terminated
    for (int k = 0; k < n; k++) {
        if (!candidates[k]) continue;
        int d = distance(word, candidates[k]);
        if (d > 0 && d < best) { best = d; found = candidates[k]; }
    }
    return best <= limit ? found : NULL;
}

// ---------------------------------------------------------------- no location

void cobalt_error(const char *fmt, ...) {
    init();
    error_count++;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%scobalt:%s %serror:%s ", BOLD, RESET, RED, RESET);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    last_shown = true;
}

void cobalt_warn(const char *fmt, ...) {
    init();
    warning_count++;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%scobalt:%s %swarning:%s ", BOLD, RESET, MAGENTA, RESET);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    last_shown = true;
}
