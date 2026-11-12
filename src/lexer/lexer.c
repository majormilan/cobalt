#include "lexer.h"
#include "pp.h"
#include "diag.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h> // For file operations
#include <errno.h> // For errno and strerror

#include "lexer_cobol85.h" // Corrected include path
#include "lexer_cobol2002.h" // Corrected include path

// ── Reference format ─────────────────────────────────────────────────────────

typedef struct {
    char *buf;
    size_t len, cap;
} StrBuf;

static void sb_append(StrBuf *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        sb->cap = (sb->len + n + 1) * 2;
        sb->buf = realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

// Quote character of a nonnumeric literal left open at the end of text, or 0
static char open_literal(const char *text) {
    char q = 0;
    for (const char *p = text; *p; p++) {
        if (!q) {
            if (*p == '"' || *p == '\'') q = *p;
        } else if (*p == q) {
            if (p[1] == q) p++;     // doubled quote inside the literal
            else q = 0;
        }
    }
    return q;
}

// WITH DEBUGGING MODE in a SOURCE-COMPUTER paragraph of the source: its
// debugging lines (D in the indicator area) are compiled
static bool debugging_lines = false;

static bool word_eq(const char *w, size_t n, const char *s) {
    return n == strlen(s) && strncasecmp(w, s, n) == 0;
}

// Looking for WITH DEBUGGING MODE in a SOURCE-COMPUTER paragraph, one word
// at a time; true once found
typedef struct {
    bool in_paragraph;          // after SOURCE-COMPUTER, before the next paragraph
    bool debugging;             // the previous word was DEBUGGING
} DebugScan;

static bool debug_scan_word(DebugScan *d, const char *w, size_t n) {
    static const char *const ends[] = { "OBJECT-COMPUTER", "SPECIAL-NAMES", "INPUT-OUTPUT", "FILE-CONTROL",
                                        "I-O-CONTROL", "DATA", "PROCEDURE", "IDENTIFICATION", "ID", NULL };
    if (word_eq(w, n, "SOURCE-COMPUTER")) {
        *d = (DebugScan){ .in_paragraph = true };
        return false;
    }
    if (!d->in_paragraph) return false;
    for (int k = 0; ends[k]; k++)
        if (word_eq(w, n, ends[k])) { d->in_paragraph = false; return false; }
    if (d->debugging && word_eq(w, n, "MODE")) return true;
    d->debugging = word_eq(w, n, "DEBUGGING");
    return false;
}

// WITH DEBUGGING MODE in the SOURCE-COMPUTER paragraph of a program of the
// source (fixed format: the words of columns 8 to 72, outside comment
// lines, debugging lines and literals)
bool lexer_debugging_mode(const char *src) {
    DebugScan d = {0};
    for (const char *line = src; *line;) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        char ind = len > 6 ? line[6] : ' ';
        bool skip = ind == '*' || ind == '/' || ind == 'D' || ind == 'd';
        size_t end = len > 72 ? 72 : len;
        for (size_t k = 7; !skip && k < end;) {
            char c = line[k];
            if (c == '"' || c == '\'') {               // a literal: to its end on this line
                k++;
                while (k < end && line[k] != c) k++;
                k++;
            } else if (isalnum((unsigned char)c) || c == '-') {
                size_t start = k;
                while (k < end && (isalnum((unsigned char)line[k]) || line[k] == '-')) k++;
                if (debug_scan_word(&d, line + start, k - start)) return true;
            } else {
                k++;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
    return false;
}

void lexer_set_debugging_lines(bool on) { debugging_lines = on; }
bool lexer_debugging_lines(void) { return debugging_lines; }

// Library text keeps its debugging lines, between PP_DEBUG_BEGIN and
// PP_DEBUG_END, for COPY REPLACING to match (pp.c removes what is left)
static bool mark_debugging_lines = false;
void lexer_mark_debugging_lines(bool on) { mark_debugging_lines = on; }

// The file being converted (for its diagnostics)
static const char *format_file;
void lexer_set_format_file(const char *name) { format_file = name; }

char *lexer_reference_format(const char *src) {
    // Collect the converted lines first: a continuation line changes the
    // line it continues.
    size_t nlines = 1;
    for (const char *p = src; *p; p++) if (*p == '\n') nlines++;
    StrBuf *out = calloc(nlines, sizeof(StrBuf));
    if (!out) return NULL;

    size_t n = 0;
    int last = -1;          // last line holding program text
    const char *line = src;
    while (n < nlines) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        char indicator = len > 6 ? line[6] : ' ';
        size_t text_end = len > 72 ? 72 : len;
        const char *text = line + 7;
        size_t text_len = text_end > 7 ? text_end - 7 : 0;
        sb_append(&out[n], "", 0);

        if ((indicator == 'D' || indicator == 'd') && !debugging_lines && mark_debugging_lines && len > 7) {
            char b = PP_DEBUG_BEGIN, e = PP_DEBUG_END;
            sb_append(&out[n], "      ", 6);
            sb_append(&out[n], &b, 1);
            sb_append(&out[n], text, text_len);
            sb_append(&out[n], &e, 1);
            last = (int)n;
        } else if (len <= 6 || indicator == '*' || indicator == '/' ||
            ((indicator == 'D' || indicator == 'd') && !debugging_lines)) {
            // sequence area only, comment line or debugging line
        } else if (indicator == '-' && last >= 0) {
            StrBuf *prev = &out[last];
            size_t skip = 0;
            while (skip < text_len && text[skip] == ' ') skip++;
            char q = open_literal(prev->buf);
            // A literal ending in column 72 with a quote, continued by a line
            // starting with two quotes, splits a doubled quote: still open
            if (!q && prev->len == 72 && (prev->buf[71] == '"' || prev->buf[71] == '\'') &&
                skip + 1 < text_len && text[skip] == prev->buf[71] && text[skip + 1] == prev->buf[71])
                q = prev->buf[71];
            if (q) {
                // A literal continues: the previous line counts up to column
                // 72, and the continuation starts after a quote character
                while (prev->len < 72) sb_append(prev, " ", 1);
                if (skip < text_len && text[skip] == q) skip++;
            } else {
                // A word or numeric literal continues without a space
                while (prev->len > 0 && prev->buf[prev->len - 1] == ' ') prev->buf[--prev->len] = '\0';
                diag_subset(MOD_NC, 2, (SourceLocation){ format_file, (int)n + 1, 7 },
                            "The continuation of a word or numeric literal");
            }
            sb_append(prev, text + skip, text_len - skip);
        } else {
            sb_append(&out[n], "       ", 7);   // keep columns for diagnostics
            sb_append(&out[n], text, text_len);
            for (size_t k = 0; k < text_len; k++) {
                if (text[k] != ' ') { last = (int)n; break; }
            }
        }
        n++;
        if (!end) break;
        line = end + 1;
    }

    StrBuf all = {0};
    sb_append(&all, "", 0);
    for (size_t i = 0; i < n; i++) {
        sb_append(&all, out[i].buf, out[i].len);
        if (i + 1 < n) sb_append(&all, "\n", 1);
        free(out[i].buf);
    }
    free(out);
    return all.buf;
}

void lexer_check_directives(char *raw, const char *file, bool free_format) {
    bool program_started = false;
    int line = 1;
    for (char *p = raw; *p; line++) {
        char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        // the text of the line: from column 7 in fixed format (a directive
        // may start in the indicator area), anywhere in free format; PROCESS
        // and CBL may start in column 1
        size_t from = 0;
        while (from < len && (p[from] == ' ' || p[from] == '\t')) from++;
        bool is_comment = !free_format && len > 6 && (p[6] == '*' || p[6] == '/');
        size_t text = from;
        if (!free_format && from < 6 && !program_started) text = from;          // PROCESS / CBL
        else if (!free_format && len > 6) {
            text = 6;
            while (text < len && p[text] == ' ') text++;
        }
        if (!is_comment && text < len) {
            const char *t = p + text;
            size_t tl = len - text;
            bool directive = (tl >= 2 && t[0] == '>' && t[1] == '>') || (tl >= 4 && strncasecmp(t, "$SET", 4) == 0);
            bool options = !program_started &&
                           ((tl >= 7 && strncasecmp(t, "PROCESS", 7) == 0 && (tl == 7 || t[7] == ' ')) ||
                            (tl >= 3 && strncasecmp(t, "CBL", 3) == 0 && (tl == 3 || t[3] == ' ')));
            if ((directive || options) &&
                diag_later_directive((SourceLocation){ file, line, (int)text + 1 }, t, tl)) {
                for (size_t k = 0; k < len; k++) if (p[k] != '\r') p[k] = ' ';
            } else if (!directive && !options) {
                program_started = true;
            }
        }
        if (!end) break;
        p = end + 1;
    }
}

void lexer_strip_floating_comments(char *text, const char *file) {
    bool reported = false;
    int line = 1, col = 1;
    char quote = 0;
    for (char *p = text; *p; p++, col++) {
        if (*p == '\n') { line++; col = 0; quote = 0; continue; }
        if (quote) { if (*p == quote) quote = 0; continue; }
        if (*p == '"' || *p == '\'') { quote = *p; continue; }
        if (p[0] == '*' && p[1] == '>') {
            if (!reported)
                diag_feature(F_FLOATING_COMMENT, (SourceLocation){ file, line, col }, NULL);
            reported = true;
            while (*p && *p != '\n') *p++ = ' ';
            if (!*p) break;
            p--;
        }
    }
}

static void advance(Lexer *l) {
    if (*l->src == '\n') {
        l->current_line++;
        l->current_col = 1;
    } else {
        l->current_col++;
    }
    l->src++;
}

// Whitespace, and the separators ", " and "; " (a comma or semicolon followed
// by a space is equivalent to a space)
static void skip_ws(Lexer *l) {
    while (*l->src) {
        if (*l->src == PP_MARK) {
            // A line marker of the text manipulation (pp.c): line:column:file
            const char *p = l->src + 1;
            int line = (int)strtol(p, (char **)&p, 10);
            int col = *p == ':' ? (int)strtol(p + 1, (char **)&p, 10) : 1;
            if (*p == ':') {
                const char *name = diag_file_name((int)strtol(p + 1, (char **)&p, 10));
                if (name) l->filename = name;
            }
            if (*p == PP_MARK) p++;
            l->src = p;
            l->current_line = line;
            l->current_col = col;
            continue;
        }
        if (isspace((unsigned char)*l->src)) advance(l);
        else if ((*l->src == ',' || *l->src == ';') &&
                 (l->src[1] == '\0' || isspace((unsigned char)l->src[1]))) {
            l->comma_seen = true;
            advance(l);
        }
        else break;
    }
}

// Words beyond COBOL-85: '_' in a word, more than 30 characters. Each word
// is reported where it first appears.
static bool word_reported(const Token *t) {
    static char **seen;
    static int nseen, cap;
    for (int k = 0; k < nseen; k++)
        if (strlen(seen[k]) == t->length && strncasecmp(seen[k], t->lexeme, t->length) == 0) return true;
    if (nseen == cap) { cap = cap ? cap * 2 : 32; seen = realloc(seen, cap * sizeof(char *)); }
    seen[nseen++] = strndup(t->lexeme, t->length);
    return false;
}

static void check_word(const Lexer *l, const Token *t) {
    if (l->quiet) return;
    SourceLocation loc = { t->filename, t->line, t->column };
    if (t->kind != TOK_IDENT) return;
    bool underscore = memchr(t->lexeme, '_', t->length) != NULL;
    if ((underscore || t->length > 30) && word_reported(t)) return;
    if (underscore)
        diag_feature(F_UNDERSCORE, loc, "the underscore in '%.*s'", (int)t->length, t->lexeme);
    else if (t->length > 30) {
        diag_feature(F_LONG_WORD, loc, "the %d-character word '%.*s'", (int)t->length, (int)t->length, t->lexeme);
        diag_note_plain("COBOL-85 words have at most 30 characters");
    }
}

// NOTE: This helper is used by both lexer_cobol85.c and lexer_cobol2002.c
// and also by lexer.c. So it should not be static. It can be made non-static or moved to a common utility.
// For now, let's make it non-static.
int is_ident_char(char c) {
    return isalnum(c) || c == '-' || c == '_';
}

// NOTE: This helper is used by both lexer_cobol85.c and lexer_cobol2002.c.
// It also needs to be non-static or a common utility function.
// For now, let's keep it here and make it accessible.
Token make_token_public(Lexer *l, TokenKind kind, const char *start, size_t length) {
    Token t;
    t.after_comma = l->comma_seen;
    l->comma_seen = false;
    t.kind = kind;
    t.lexeme = start;
    t.length = length;
    t.filename = l->filename;
    t.line = l->current_line;
    t.column = l->current_col - length;
    return t;
}


void lexer_init(Lexer *l, const char *source, const CompilerMode *mode, const char *filename) {
    l->original_source = source;
    l->src = source;
    l->current_line = 1;
    l->current_col = 1;
    l->mode = mode;
    l->filename = filename;
    l->file_content = NULL; // Not owned by the initial lexer
    l->free_content_on_pop = false;
    l->prev = NULL;
    l->pic_next = false;
    l->decimal_comma = false;
    l->quiet = false;
    l->comma_seen = false;

    // Set standard-specific keyword checker
    switch (l->mode->std) {
        case STD_COBOL2002:
        case STD_COBOL2014:
            l->get_keyword_or_ident = lexer_cobol2002_keyword_or_ident;
            break;
        case STD_COBOL85:
        default:
            l->get_keyword_or_ident = lexer_cobol85_keyword_or_ident;
            break;
    }
}

void lexer_push_source(Lexer *l, const char *new_source_content, const char *new_filename, bool free_content) {
    Lexer *new_lexer_state = malloc(sizeof(Lexer));
    if (new_lexer_state == NULL) {
        cobalt_error("Failed to allocate memory for new lexer state.");
        return; // Or exit, depending on error handling strategy
    }

    // Save the current lexer's state into the new_lexer_state (which will become l->prev)
    memcpy(new_lexer_state, l, sizeof(Lexer));

    // Now, initialize the current lexer `l` with the new source
    l->original_source = new_source_content;
    l->src = new_source_content;
    l->current_line = 1;
    l->current_col = 1;
    // l->mode and l->get_keyword_or_ident remain the same
    l->filename = new_filename;
    l->file_content = (char*)new_source_content; // Store as char* if it's heap allocated
    l->free_content_on_pop = free_content;
    l->prev = new_lexer_state; // Link to the previous state
}

void lexer_pop_source(Lexer *l) {
    if (l->prev == NULL) {
        cobalt_error("Attempted to pop source from an empty lexer stack.");
        return; // Cannot pop the base lexer
    }

    if (l->free_content_on_pop && l->file_content != NULL) {
        free(l->file_content);
    }

    Lexer *old_lexer_state = l->prev;
    // Restore the lexer `l` to the state of `l->prev`
    memcpy(l, old_lexer_state, sizeof(Lexer));
    free(old_lexer_state); // Free the memory of the popped lexer state
}


Token lexer_next(Lexer *l) {
    // Keep trying to get a token until a non-EOF token is found
    // or we truly hit EOF on the base lexer.
    while (true) {
        // If we've reached the end of a pushed source, pop it.
        if (*l->src == '\0' && l->prev != NULL) {
            lexer_pop_source(l);
            // After popping, loop again to get the next token from the restored source.
            continue; 
        }
        
        skip_ws(l);

        const char *start = l->src;

        if (*l->src == '\0') {
            // If it's EOF on the base lexer, return TOK_EOF
            if (l->prev == NULL) {
                return make_token_public(l, TOK_EOF, start, 0);
            }
            // If it's EOF on a pushed source, continue the loop to pop it.
            continue;
        }

    // TODO: Handle fixed/free format here

    // PICTURE character-string: everything up to whitespace, except that a
    // '.', ',' or ';' followed by whitespace ends the clause, not the string
    if (l->pic_next) {
        const char *p = l->src;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (p - l->src == 2 && strncasecmp(l->src, "IS", 2) == 0) {
            advance(l); advance(l);
            return make_token_public(l, TOK_IS, start, 2);
        }
        l->pic_next = false;
        while (*l->src && !isspace((unsigned char)*l->src)) {
            if ((*l->src == '.' || *l->src == ',' || *l->src == ';') &&
                (l->src[1] == '\0' || isspace((unsigned char)l->src[1]))) break;
            advance(l);
        }
        return make_token_public(l, TOK_PIC_STRING, start, l->src - start);
    }

    // Nonnumeric literal: "..." or '...'; a doubled delimiter stands for one
    // (the parser collapses it, see the lexeme's preceding delimiter)
    if (*l->src == '"' || *l->src == '\'') {
        char q = *l->src;
        int line = l->current_line, col = l->current_col;
        advance(l);  // skip opening quote
        start = l->src;
        while (*l->src && *l->src != '\n') {
            if (*l->src == q) {
                if (l->src[1] != q) break;
                advance(l);
            }
            advance(l);
        }
        size_t len = l->src - start;
        if (*l->src == q) advance(l);  // skip closing quote
        else if (!l->quiet) {
            diag_error((SourceLocation){l->filename, line, col}, "Nonnumeric literal is not closed");
            diag_note_plain("a literal ends on its line; continue a long literal on the next line "
                            "with '-' in the indicator area and a quote before the rest");
        }
        Token t = make_token_public(l, TOK_STRING, start, len);
        t.line = line;
        t.column = col;                 // at the opening quote
        SourceLocation loc = { l->filename, line, col };
        if (l->quiet) return t;
        if (q == '\'') diag_feature(F_APOSTROPHE, loc, "a nonnumeric literal in apostrophes");
        if (len > 160) {
            diag_feature(F_LONG_LITERAL, loc, "a nonnumeric literal of %d characters", (int)len);
            diag_note_plain("COBOL-85 nonnumeric literals have at most 160 characters");
        }
        return t;
    }

    // A user-defined word may start with digits ("3-DIMENSION-TBL", "25ANS")
    // as long as it contains a letter; it cannot end with a hyphen
    if (isdigit((unsigned char)*l->src)) {
        const char *p = l->src;
        bool letter = false;
        while (isalnum((unsigned char)*p) || *p == '-') {
            if (isalpha((unsigned char)*p)) letter = true;
            p++;
        }
        while (p > l->src && p[-1] == '-') p--;
        if (letter) {
            while (l->src < p) advance(l);
            return l->get_keyword_or_ident(l, start, l->src - start);
        }
    }

    // Handle numbers (a sign directly before the digits belongs to the literal)
    bool negative = false;
    if (*l->src == '-') {
        negative = true;
        advance(l);
    } else if (*l->src == '+' && (isdigit((unsigned char)l->src[1]) ||
                                  (l->src[1] == (l->decimal_comma ? ',' : '.') &&
                                   isdigit((unsigned char)l->src[2])))) {
        char plus_point = l->decimal_comma ? ',' : '.';
        advance(l);
        if (*l->src == plus_point) { *(char *)l->src = '.'; advance(l); }
        while (isdigit(*l->src)) advance(l);
        if (*l->src == plus_point && isdigit(l->src[1])) {
            *(char *)l->src = '.';
            advance(l);
            while (isdigit(*l->src)) advance(l);
        }
        return make_token_public(l, TOK_NUMBER, start, l->src - start);
    }

    // The decimal point of a numeric literal is ',' with DECIMAL-POINT IS
    // COMMA; it is rewritten to '.' in the (writable) source buffer, so the
    // rest of the compiler sees one form
    char point = l->decimal_comma ? ',' : '.';
    if (isdigit(*l->src) || (*l->src == point && isdigit(l->src[1]))) {
        if (*l->src == point) { *(char *)l->src = '.'; advance(l); }
        while (isdigit(*l->src)) advance(l);
        if (*l->src == point && isdigit(l->src[1])) {
            *(char *)l->src = '.';
            advance(l);
            while (isdigit(*l->src)) advance(l);
        }
        // start already points at the '-' of a negative literal
        return make_token_public(l, TOK_NUMBER, start, l->src - start);
    } else if (negative) {
        // If - not followed by number, treat as minus operator
        return make_token_public(l, TOK_MINUS, start, 1);
    }

    if (isalpha(*l->src)) {
        while (is_ident_char(l->src[0])) advance(l); // Use non-static is_ident_char
        Token t = l->get_keyword_or_ident(l, start, l->src - start);
        if (t.kind == TOK_PIC) l->pic_next = true;
        check_word(l, &t);
        return t;
    }

    if (*l->src == ':') {
        advance(l);
        return make_token_public(l, TOK_COLON, start, 1);
    }

    if (*l->src == '(') {
        advance(l);
        return make_token_public(l, TOK_LPAREN, start, 1);
    }

    if (*l->src == ')') {
        advance(l);
        return make_token_public(l, TOK_RPAREN, start, 1);
    }

    if (*l->src == '.') {
        advance(l);
        return make_token_public(l, TOK_PERIOD, start, 1);
    }

    if (*l->src == '>') {
        advance(l);
        if (*l->src == '=') {
            advance(l);
            return make_token_public(l, TOK_GE, start, 2);
        }
        return make_token_public(l, TOK_GT, start, 1);
    }

    if (*l->src == '<') {
        advance(l);
        if (*l->src == '>') {
            advance(l);
            return make_token_public(l, TOK_NE, start, 2);
        }
        if (*l->src == '=') {
            advance(l);
            return make_token_public(l, TOK_LE, start, 2);
        }
        return make_token_public(l, TOK_LT, start, 1);
    }

    if (*l->src == '=') {
        advance(l);
        return make_token_public(l, TOK_EQ, start, 1);
    }

    if (*l->src == '+') {
        advance(l);
        return make_token_public(l, TOK_PLUS, start, 1);
    }

    if (*l->src == '-') {
        advance(l);
        return make_token_public(l, TOK_MINUS, start, 1);
    }

    if (*l->src == '*') {
        advance(l);
        if (*l->src == '*') {
            advance(l);
            return make_token_public(l, TOK_POW, start, 2);
        }
        return make_token_public(l, TOK_MUL, start, 1);
    }

    if (*l->src == '/') {
        advance(l);
        return make_token_public(l, TOK_DIV, start, 1);
    }

    if (!l->quiet) diag_error((SourceLocation){l->filename, l->current_line, l->current_col}, "Unexpected character '%c'", *l->src);
    advance(l);
    return lexer_next(l);  // recovery
    } // Closing brace for while(true)
}



// The identification area (columns 73-80) of a line, or "" (in buf)
static const char *ident_area(const char *p, size_t len, char *buf) {
    size_t n = 0;
    for (size_t k = 72; k < len && k < 80; k++) buf[n++] = p[k];
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = '\0';
    return buf;
}

void lexer_check_columns(const char *raw, const char *filename) {
    if (!diag_warning_enabled(W_COLUMN)) return;
    // A program that fills the identification area on purpose writes the same
    // text there on most lines: then nothing past column 72 is a mistake
    {
        char first[16] = "", buf[16];
        int lines = 0, same = 0;
        for (const char *p = raw; *p; ) {
            const char *end = strchr(p, '\n');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len > 0 && p[len - 1] == '\r') len--;
            if (len > 72) {
                ident_area(p, len, buf);
                if (lines == 0) snprintf(first, sizeof(first), "%s", buf);
                lines++;
                if (*buf && strcmp(buf, first) == 0) same++;
            }
            if (!end) break;
            p = end + 1;
        }
        if (lines >= 2 && same * 2 > lines) return;
    }
    int line = 1;
    for (const char *p = raw; *p; line++) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;
        // a word or literal cut at column 72: columns 72 and 73 both hold text
        // (not in a floating comment, which runs to the end of the line)
        bool floating = false;
        for (size_t k = 7; k + 1 < 72 && k + 1 < len; k++)
            if (p[k] == '*' && p[k + 1] == '>') { floating = true; break; }
        if (len > 72 && !floating && p[6] != '*' && p[6] != '/' && is_ident_char(p[71]) && is_ident_char(p[72])) {
            int end73 = 72, start = 71;
            while (end73 < (int)len && end73 < 80 && is_ident_char(p[end73])) end73++;
            while (start > 7 && is_ident_char(p[start - 1])) start--;
            if (diag_warning(W_COLUMN, (SourceLocation){filename, line, start + 1},
                             "The word '%.*s' runs past column 72, so only '%.*s' of it is read", end73 - start, p + start,
                             72 - start, p + start))
                diag_note_plain("in fixed format, columns 73 to 80 are the identification area; "
                                "continue the line on the next one");
        }
        if (!end) break;
        p = end + 1;
    }
}
