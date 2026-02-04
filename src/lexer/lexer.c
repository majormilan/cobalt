#include "lexer.h"
#include "diag.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h> // For file operations
#include <errno.h> // For errno and strerror

#include "lexer_cobol85.h" // Corrected include path
#include "lexer_cobol2002.h" // Corrected include path

// Reads an entire file into a dynamically allocated string.
// Returns NULL on failure. Caller is responsible for freeing the memory.
static char *read_file_content(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        cobalt_error(NULL, "Could not open file '%s': %s", filepath, strerror(errno));
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (buffer == NULL) {
        cobalt_error(NULL, "Could not allocate memory for file '%s'", filepath);
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, length, file);
    if (read_bytes != (size_t)length) {
        cobalt_error(NULL, "Could not read entire file '%s'", filepath);
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[length] = '\0';

    fclose(file);
    return buffer;
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

static void skip_ws(Lexer *l) {
    while (*l->src && isspace(*l->src)) {
        advance(l);
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
    t.kind = kind;
    t.lexeme = start;
    t.length = length;
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

    // Set standard-specific keyword checker
    switch (l->mode->std) {
        case STD_COBOL2002:
        case STD_COBOL2014:
            l->get_keyword_or_ident = lexer_cobol2002_keyword_or_ident;
            break;
        case STD_COBOL85:
        case STD_GNUCOBOL: // Fallback to COBOL85 for GnuCOBOL for now
        case STD_IBM_ENTERPRISE: // Fallback to COBOL85 for IBM for now
        default:
            l->get_keyword_or_ident = lexer_cobol85_keyword_or_ident;
            break;
    }
}

void lexer_push_source(Lexer *l, const char *new_source_content, const char *new_filename, bool free_content) {
    Lexer *new_lexer_state = malloc(sizeof(Lexer));
    if (new_lexer_state == NULL) {
        cobalt_error(l->original_source, "Failed to allocate memory for new lexer state.");
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
        cobalt_error(l->original_source, "Attempted to pop source from an empty lexer stack.");
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

    if (*l->src == '"') {
        advance(l);  // skip opening "
        start = l->src;
        while (*l->src && *l->src != '"') advance(l);
        size_t len = l->src - start;
        if (*l->src == '"') advance(l);  // skip closing "
        return make_token_public(l, TOK_STRING, start, len);
    }

    // Handle numbers
    bool negative = false;
    if (*l->src == '-') {
        negative = true;
        advance(l);
    }

    if (isdigit(*l->src) || (*l->src == '.' && isdigit(l->src[1]))) {
        if (*l->src == '.') advance(l);  // for .56 style, but rare in COBOL
        while (isdigit(*l->src)) advance(l);
        if (*l->src == '.' && isdigit(l->src[1])) {
            advance(l);
            while (isdigit(*l->src)) advance(l);
        }
        size_t len = l->src - start;
        if (negative) {
            start--;  // include the '-'
            len++;
        }
        return make_token_public(l, TOK_NUMBER, start, len);
    } else if (negative) {
        // If - not followed by number, treat as minus operator
        return make_token_public(l, TOK_MINUS, start, 1);
    }

    if (isalpha(*l->src)) {
        while (is_ident_char(l->src[0])) advance(l); // Use non-static is_ident_char
        Token t = l->get_keyword_or_ident(l, start, l->src - start);
        if (t.kind == TOK_COPY) {
            // Found a COPY statement, now parse the filename
            // For simplicity, assume filename is a string literal immediately after COPY
            skip_ws(l);
            if (*l->src == '"') {
                advance(l); // skip opening "
                const char *filename_start = l->src;
                while (*l->src && *l->src != '"') advance(l);
                size_t filename_len = l->src - filename_start;
                if (*l->src == '"') advance(l); // skip closing "

                char *copied_filename = strndup(filename_start, filename_len);
                if (copied_filename == NULL) {
                    cobalt_error(l->original_source, "Failed to duplicate filename for COPY statement.");
                    return make_token_public(l, TOK_EOF, start, 0); // Error recovery
                }

                char *file_content = read_file_content(copied_filename);
                if (file_content == NULL) {
                    char error_msg[512]; // Buffer for error message
                    snprintf(error_msg, sizeof(error_msg), "Failed to read content of copied file '%s'.", copied_filename);
                    free(copied_filename);
                    cobalt_error(l->original_source, "%s", error_msg);
                    return make_token_public(l, TOK_EOF, start, 0); // Error recovery
                }
                
                lexer_push_source(l, file_content, copied_filename, true);
                // After pushing, we don't return TOK_COPY. We immediately get the next token
                // which will be from the newly pushed source.
                continue; // Continue the while(true) loop to get the first token from the new source.
            } else {
                cobalt_error(l->original_source, "Expected string literal after COPY statement.");
                return make_token_public(l, TOK_EOF, start, 0); // Error recovery
            }
        }
        return t;
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
        return make_token_public(l, TOK_MUL, start, 1);
    }

    if (*l->src == '/') {
        advance(l);
        return make_token_public(l, TOK_DIV, start, 1);
    }

    cobalt_error_loc(l->original_source, l->current_line, l->current_col, "Unexpected character '%c'", *l->src);
    advance(l);
    return lexer_next(l);  // recovery
    } // Closing brace for while(true)
}


