/* src/lexer/pp.c
 * Source text manipulation: COPY and REPLACE (see include/pp.h).
 *
 * The text is split into text-words: literals, separators other than spaces
 * ("(", ")", ":", a period followed by a space, the pseudo-text delimiter
 * "=="), and the character-strings between them. A comma or semicolon
 * followed by a space counts as a space. COPY statements are expanded first
 * (their REPLACING phrase applied to the library text), then REPLACE
 * statements to the whole result. Matching compares text-words, COBOL words
 * without regard to case; replaced text is not scanned again.
 */
#include "pp.h"
#include "lexer.h"
#include "diag.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    char *buf;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2 + 64;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void puts_(Buf *b, const char *s) { put(b, s, strlen(s)); }

enum { W_WORD, W_LIT, W_PERIOD, W_PAREN_L, W_PAREN_R, W_COLON, W_PSEUDO };

typedef struct {
    int kind;
    size_t start, end;          // offsets in the text
} Word;

typedef struct {
    Word *w;
    int n, cap;
} Words;

static bool is_sep_space(const char *t, size_t p) {
    char c = t[p];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == PP_MARK ||
        c == PP_DEBUG_BEGIN || c == PP_DEBUG_END) return true;
    if ((c == ',' || c == ';') && (t[p + 1] == '\0' || isspace((unsigned char)t[p + 1]) || t[p + 1] == PP_MARK))
        return true;
    return false;
}

static void tokenize(const char *t, Words *ws) {
    size_t p = 0;
    ws->n = 0;
    while (t[p]) {
        if (t[p] == PP_MARK) {
            p++;
            while (t[p] && t[p] != PP_MARK) p++;
            if (t[p]) p++;
            continue;
        }
        if (is_sep_space(t, p)) { p++; continue; }
        Word w = { W_WORD, p, p };
        char c = t[p];
        if (c == '=' && t[p + 1] == '=') {
            w.kind = W_PSEUDO;
            p += 2;
        } else if (c == '"' || c == '\'' ||
                   (strchr("XxNnZzBbGg", c) && (t[p + 1] == '"' || t[p + 1] == '\''))) {
            w.kind = W_LIT;
            if (c != '"' && c != '\'') p++;
            char q = t[p++];
            while (t[p] && t[p] != '\n') {
                if (t[p] == q) {
                    if (t[p + 1] == q) { p += 2; continue; }
                    p++;
                    break;
                }
                p++;
            }
        } else if (c == '(') { w.kind = W_PAREN_L; p++; }
        else if (c == ')') { w.kind = W_PAREN_R; p++; }
        else if (c == ':') { w.kind = W_COLON; p++; }
        else if (c == '.' && (t[p + 1] == '\0' || is_sep_space(t, p + 1))) { w.kind = W_PERIOD; p++; }
        else {
            while (t[p] && !is_sep_space(t, p) && t[p] != '(' && t[p] != ')' && t[p] != ':' &&
                   t[p] != '"' && t[p] != '\'' && !(t[p] == '=' && t[p + 1] == '=') &&
                   !(t[p] == '.' && (t[p + 1] == '\0' || is_sep_space(t, p + 1))))
                p++;
            if (p == w.start) p++;      // a lone character
        }
        w.end = p;
        if (ws->n == ws->cap) {
            ws->cap = ws->cap ? ws->cap * 2 : 256;
            ws->w = realloc(ws->w, ws->cap * sizeof(Word));
        }
        ws->w[ws->n++] = w;
    }
}

static bool word_is(const char *t, const Word *w, const char *s) {
    size_t n = strlen(s);
    return w->kind == W_WORD && w->end - w->start == n && strncasecmp(t + w->start, s, n) == 0;
}

static bool words_equal(const char *ta, const Word *a, const char *tb, const Word *b) {
    if (a->kind != b->kind) return false;
    size_t n = a->end - a->start;
    if (n != b->end - b->start) return false;
    if (a->kind == W_LIT) return memcmp(ta + a->start, tb + b->start, n) == 0;
    return strncasecmp(ta + a->start, tb + b->start, n) == 0;
}

// Line (1-based) and column of offset p in t, counting line markers
static void position(const char *t, size_t p, int *line, int *col) {
    int l = 1;
    size_t ls = 0;
    for (size_t k = 0; k < p && t[k]; k++) {
        if (t[k] == '\n') { l++; ls = k + 1; }
    }
    *line = l;
    *col = (int)(p - ls) + 1;
}

// ── Comment entries ─────────────────────────────────────────────────────────
// AUTHOR, INSTALLATION, DATE-WRITTEN, DATE-COMPILED and SECURITY take any
// text up to the next line with something in area A. It becomes spaces, so
// that the lexer does not see it.

static bool area_a_word(const char *line, size_t len, const char *w) {
    size_t n = strlen(w);
    size_t p = 7;
    while (p < len && p < 11 && line[p] == ' ') p++;
    if (p >= 11 || p + n > len || strncasecmp(line + p, w, n) != 0) return false;
    p += n;
    while (p < len && line[p] == ' ') p++;
    return p < len && line[p] == '.';
}

static bool area_a_used(const char *line, size_t len) {
    for (size_t p = 7; p < len && p < 11; p++)
        if (line[p] != ' ') return true;
    return false;
}

static bool line_has(const char *line, size_t len, const char *w) {
    size_t n = strlen(w);
    for (size_t p = 0; p + n <= len; p++)
        if (strncasecmp(line + p, w, n) == 0) return true;
    return false;
}

static void blank_comment_entries(char *t) {
    static const char *paras[] = { "AUTHOR", "INSTALLATION", "DATE-WRITTEN", "DATE-COMPILED",
                                   "SECURITY", "REMARKS", NULL };
    bool in_id = false, in_entry = false;
    char *line = t;
    while (*line) {
        char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (in_entry && area_a_used(line, len)) in_entry = false;
        if (in_entry) {
            for (size_t k = 0; k < len; k++) line[k] = ' ';
        } else if (area_a_used(line, len)) {
            if (line_has(line, len, " DIVISION"))
                in_id = line_has(line, len, "IDENTIFICATION") || area_a_word(line, len, "ID DIVISION");
            if (in_id) {
                for (int k = 0; paras[k]; k++) {
                    if (area_a_word(line, len, paras[k])) {
                        char *dot = memchr(line, '.', len);
                        for (char *c = dot + 1; c < line + len; c++) *c = ' ';
                        in_entry = true;
                        break;
                    }
                }
            }
        }
        if (!end) break;
        line = end + 1;
    }
}

// ── Library texts ───────────────────────────────────────────────────────────

static char *read_whole(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1);
    if (s && fread(s, 1, n, f) != (size_t)n) { free(s); s = NULL; }
    if (s) s[n] = '\0';
    fclose(f);
    return s;
}

// The file of text-name in library (NULL: the default library): searched in
// the including file's directory, then the -I directories, then COBCPY
static char *find_text(const char *name, const char *library, const char *including,
                       const PpOptions *opt) {
    static const char *exts[] = { "", ".cpy", ".CPY", ".cbl", ".CBL", ".cob", ".COB", NULL };
    const char *dirs[64];
    int nd = 0;
    char incdir[1024];
    snprintf(incdir, sizeof(incdir), "%s", including ? including : "");
    char *slash = strrchr(incdir, '/');
    if (slash) *slash = '\0';
    else strcpy(incdir, ".");
    dirs[nd++] = incdir;
    for (int k = 0; k < opt->ndirs && nd < 48; k++) dirs[nd++] = opt->dirs[k];
    char cobcpy[2048] = "";
    const char *env = getenv("COBCPY");
    if (env) {
        snprintf(cobcpy, sizeof(cobcpy), "%s", env);
        for (char *d = strtok(cobcpy, ":"); d && nd < 63; d = strtok(NULL, ":")) dirs[nd++] = d;
    }
    if (name[0] == '/') {
        for (int e = 0; exts[e]; e++) {
            char path[2048];
            snprintf(path, sizeof(path), "%s%s", name, exts[e]);
            FILE *f = fopen(path, "rb");
            if (f) { fclose(f); return strdup(path); }
        }
        return NULL;
    }
    char lower[256], upper[256];
    size_t n = strlen(name);
    if (n >= sizeof(lower)) return NULL;
    for (size_t k = 0; k <= n; k++) {
        lower[k] = tolower((unsigned char)name[k]);
        upper[k] = toupper((unsigned char)name[k]);
    }
    const char *names[] = { name, upper, lower };
    char lib_lower[256] = "", lib_upper[256] = "";
    if (library) {
        size_t m = strlen(library);
        if (m >= sizeof(lib_lower)) return NULL;
        for (size_t k = 0; k <= m; k++) {
            lib_lower[k] = tolower((unsigned char)library[k]);
            lib_upper[k] = toupper((unsigned char)library[k]);
        }
    }
    const char *libs[] = { library, lib_upper, lib_lower };
    for (int d = 0; d < nd; d++) {
        for (int l = 0; l < (library ? 3 : 1); l++) {
            for (int c = 0; c < 3; c++) {
                for (int e = 0; exts[e]; e++) {
                    char path[2048];
                    if (library)
                        snprintf(path, sizeof(path), "%s/%s/%s%s", dirs[d], libs[l], names[c], exts[e]);
                    else
                        snprintf(path, sizeof(path), "%s/%s%s", dirs[d], names[c], exts[e]);
                    FILE *f = fopen(path, "rb");
                    if (f) { fclose(f); return strdup(path); }
                }
            }
        }
    }
    return NULL;
}

// ── Replacement ─────────────────────────────────────────────────────────────

typedef struct {
    const char *text;           // the text holding the operands' words
    Word *from;                 // pseudo-text-1 (or the operand's words)
    int nfrom;
    Word *to;
    int nto;
} Pattern;

typedef struct {
    Pattern *p;
    int n;
} Patterns;

static void patterns_free(Patterns *ps) {
    for (int k = 0; k < ps->n; k++) { free(ps->p[k].from); free(ps->p[k].to); }
    free(ps->p);
    ps->p = NULL;
    ps->n = 0;
}

// One REPLACING / REPLACE operand at ws[*i]: ==pseudo-text==, or a literal,
// word or identifier (with qualifiers and subscripts)
static bool parse_operand(const char *t, const Words *ws, int *i, Word **out, int *nout,
                          bool pseudo_only) {
    int k = *i;
    if (k >= ws->n) return false;
    int s, e;
    if (ws->w[k].kind == W_PSEUDO) {
        s = ++k;
        while (k < ws->n && ws->w[k].kind != W_PSEUDO) k++;
        if (k >= ws->n) return false;
        e = k++;
    } else {
        if (pseudo_only) return false;
        if (ws->w[k].kind != W_WORD && ws->w[k].kind != W_LIT) return false;
        s = k++;
        if (ws->w[s].kind == W_WORD) {
            while (k + 1 < ws->n && (word_is(t, &ws->w[k], "OF") || word_is(t, &ws->w[k], "IN")) &&
                   ws->w[k + 1].kind == W_WORD)
                k += 2;
            if (k < ws->n && ws->w[k].kind == W_PAREN_L) {
                int depth = 0;
                while (k < ws->n) {
                    if (ws->w[k].kind == W_PAREN_L) depth++;
                    else if (ws->w[k].kind == W_PAREN_R && --depth == 0) { k++; break; }
                    k++;
                }
            }
        }
        e = k;
    }
    *nout = e - s;
    *out = malloc((e - s + 1) * sizeof(Word));
    memcpy(*out, ws->w + s, (e - s) * sizeof(Word));
    *i = k;
    return true;
}

// {operand-1 BY operand-2}... at ws[*i]
static bool parse_pairs(const char *t, const Words *ws, int *i, Patterns *ps, bool pseudo_only) {
    while (*i < ws->n && ws->w[*i].kind != W_PERIOD) {
        Pattern p = { t, NULL, 0, NULL, 0 };
        if (!parse_operand(t, ws, i, &p.from, &p.nfrom, pseudo_only)) return false;
        if (*i >= ws->n || !word_is(t, &ws->w[*i], "BY")) { free(p.from); return false; }
        (*i)++;
        if (!parse_operand(t, ws, i, &p.to, &p.nto, pseudo_only)) { free(p.from); return false; }
        ps->p = realloc(ps->p, (ps->n + 1) * sizeof(Pattern));
        ps->p[ps->n++] = p;
    }
    return ps->n > 0;
}

// Line breaks and line markers of t[s, e), so that the lines after a
// replaced span keep their numbers
static void keep_breaks(Buf *out, const char *t, size_t s, size_t e) {
    for (size_t k = s; k < e; k++) {
        if (t[k] == '\n') put(out, "\n", 1);
        else if (t[k] == PP_MARK) {
            size_t m = k + 1;
            while (m < e && t[m] != PP_MARK) m++;
            put(out, t + k, m + 1 - k);
            k = m;
        }
    }
}

// The replacement: operand-2 as written (its separators kept: "X(115)" must
// stay one character-string), with line breaks as spaces
static void put_words(Buf *out, const char *t, const Word *w, int n) {
    put(out, " ", 1);
    if (n > 0) {
        for (size_t k = w[0].start; k < w[n - 1].end; k++) {
            char c = t[k];
            if (c == PP_MARK) {
                while (k + 1 < w[n - 1].end && t[k + 1] != PP_MARK) k++;
                k++;
                continue;
            }
            put(out, c == '\n' || c == '\r' ? " " : &t[k], 1);
        }
    }
    put(out, " ", 1);
}

// Apply patterns to t; with statements, REPLACE statements in t set the
// patterns (and are removed). Returns the new text.
static char *replace_text(const char *t, Patterns *fixed, bool statements, const char *filename) {
    Words ws = {0};
    tokenize(t, &ws);
    Buf out = {0};
    put(&out, "", 0);
    size_t last = 0;
    Patterns active = {0};
    Patterns *pats = fixed;
    int i = 0;
    while (i < ws.n) {
        Word *w = &ws.w[i];
        if (statements && word_is(t, w, "REPLACE") && i + 1 < ws.n &&
            (ws.w[i + 1].kind == W_PSEUDO || word_is(t, &ws.w[i + 1], "OFF"))) {
            int j = i + 1;
            Patterns np = {0};
            bool ok;
            if (word_is(t, &ws.w[j], "OFF")) { j++; ok = true; }
            else ok = parse_pairs(t, &ws, &j, &np, true);
            if (!ok || j >= ws.n || ws.w[j].kind != W_PERIOD) {
                int line, col;
                position(t, w->start, &line, &col);
                cobalt_error_loc(t, line, col, "Malformed REPLACE statement");
                patterns_free(&np);
                i++;
                continue;
            }
            put(&out, t + last, w->start - last);
            keep_breaks(&out, t, w->start, ws.w[j].end);
            last = ws.w[j].end;
            patterns_free(&active);
            active = np;
            pats = &active;
            i = j + 1;
            continue;
        }
        bool matched = false;
        for (int k = 0; pats && k < pats->n && !matched; k++) {
            Pattern *p = &pats->p[k];
            if (p->nfrom == 0 || i + p->nfrom > ws.n) continue;
            int m = 0;
            while (m < p->nfrom && words_equal(t, &ws.w[i + m], p->text, &p->from[m])) m++;
            if (m < p->nfrom) continue;
            put(&out, t + last, w->start - last);
            put_words(&out, p->text, p->to, p->nto);
            keep_breaks(&out, t, w->start, ws.w[i + m - 1].end);
            last = ws.w[i + m - 1].end;
            i += m;
            matched = true;
        }
        if (!matched) i++;
    }
    put(&out, t + last, strlen(t + last));
    (void)filename;
    patterns_free(&active);
    free(ws.w);
    return out.buf;
}

// ── COPY ────────────────────────────────────────────────────────────────────

static char *word_text(const char *t, const Word *w) {
    size_t s = w->start, e = w->end;
    if (w->kind == W_LIT) { s++; if (e > s && (t[e - 1] == '"' || t[e - 1] == '\'')) e--; }
    return strndup(t + s, e - s);
}

static bool expand(Buf *out, const char *t, const char *filename, int depth, const PpOptions *opt);

static char *load_text(const char *path, const PpOptions *opt) {
    char *raw = read_whole(path);
    if (!raw) return NULL;
    if (opt->free_format) return raw;
    lexer_mark_debugging_lines(true);
    char *conv = lexer_reference_format(raw);
    lexer_mark_debugging_lines(false);
    free(raw);
    return conv;
}

static bool expand(Buf *out, const char *t, const char *filename, int depth, const PpOptions *opt) {
    Words ws = {0};
    tokenize(t, &ws);
    size_t last = 0;
    bool pseudo = false, ok = true;
    for (int i = 0; i < ws.n; i++) {
        Word *w = &ws.w[i];
        if (w->kind == W_PSEUDO) { pseudo = !pseudo; continue; }
        if (pseudo || !word_is(t, w, "COPY")) continue;
        int line, col;
        position(t, w->start, &line, &col);
        int j = i + 1;
        if (j >= ws.n || (ws.w[j].kind != W_WORD && ws.w[j].kind != W_LIT)) {
            cobalt_error_loc(depth ? NULL : t, line, col, "Expected a text-name after COPY");
            ok = false;
            continue;
        }
        char *name = word_text(t, &ws.w[j++]);
        char *library = NULL;
        if (j + 1 < ws.n && (word_is(t, &ws.w[j], "OF") || word_is(t, &ws.w[j], "IN")) &&
            (ws.w[j + 1].kind == W_WORD || ws.w[j + 1].kind == W_LIT)) {
            library = word_text(t, &ws.w[j + 1]);
            j += 2;
        }
        if (j < ws.n && word_is(t, &ws.w[j], "SUPPRESS")) j++;
        Patterns ps = {0};
        bool good = true;
        if (j < ws.n && word_is(t, &ws.w[j], "REPLACING")) {
            j++;
            good = parse_pairs(t, &ws, &j, &ps, false);
        }
        if (!good || j >= ws.n || ws.w[j].kind != W_PERIOD) {
            cobalt_error_loc(depth ? NULL : t, line, col, "Malformed COPY statement");
            free(name); free(library); patterns_free(&ps);
            ok = false;
            continue;
        }
        char *path = find_text(name, library, filename, opt);
        char *lib = path ? load_text(path, opt) : NULL;
        if (!lib) {
            cobalt_error_loc(depth ? NULL : t, line, col, "Cannot find text '%s'%s%s for COPY", name,
                             library ? " in library " : "", library ? library : "");
            free(name); free(library); free(path); patterns_free(&ps);
            ok = false;
            i = j;
            continue;
        }
        if (depth > 50) {
            cobalt_error_loc(depth ? NULL : t, line, col, "COPY nested too deeply (recursive COPY of '%s'?)", name);
            free(name); free(library); free(path); free(lib); patterns_free(&ps);
            free(ws.w);
            return false;
        }
        put(out, t + last, w->start - last);
        if (ps.n) {
            char *r = replace_text(lib, &ps, false, path);
            free(lib);
            lib = r;
        }
        put(out, " ", 1);
        ok = expand(out, lib, path, depth + 1, opt) && ok;
        put(out, " ", 1);
        last = ws.w[j].end;
        if (depth == 0) {
            int l, c;
            position(t, last, &l, &c);
            char mark[48];
            snprintf(mark, sizeof(mark), "%c%d:%d%c", PP_MARK, l, c, PP_MARK);
            puts_(out, mark);
        }
        free(name); free(library); free(path); free(lib); patterns_free(&ps);
        i = j;
    }
    put(out, t + last, strlen(t + last));
    free(ws.w);
    return ok;
}

// Whether t may hold a COPY or REPLACE statement at all
static bool mentions(const char *t, const char *w) {
    size_t n = strlen(w);
    for (const char *p = t; *p; p++)
        if ((p == t || !isalnum((unsigned char)p[-1]) ) && strncasecmp(p, w, n) == 0 &&
            !isalnum((unsigned char)p[n]) && p[n] != '-' && (p == t || p[-1] != '-'))
            return true;
    return false;
}

char *pp_process(const char *text, const char *filename, const PpOptions *opt) {
    char *t = strdup(text);
    if (!opt->free_format) blank_comment_entries(t);
    if (mentions(t, "COPY")) {
        Buf out = {0};
        put(&out, "", 0);
        bool ok = expand(&out, t, filename, 0, opt);
        free(t);
        if (!ok) { free(out.buf); return NULL; }
        t = out.buf;
    }
    if (mentions(t, "REPLACE")) {
        char *r = replace_text(t, NULL, true, filename);
        free(t);
        t = r;
    }
    // Debugging lines of library text that remain: comments, unless the
    // program is compiled WITH DEBUGGING MODE
    bool in_debug = false;
    for (char *c = t; *c; c++) {
        if (*c == PP_DEBUG_BEGIN) { in_debug = true; *c = ' '; }
        else if (*c == PP_DEBUG_END) { in_debug = false; *c = ' '; }
        else if (in_debug && *c != '\n' && !lexer_debugging_lines()) *c = ' ';
    }
    return t;
}
