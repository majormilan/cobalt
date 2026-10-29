/* include/config.h */
#ifndef COBALT_CONFIG_H
#define COBALT_CONFIG_H

#include <stdbool.h>

typedef enum {
    STD_COBOL85,            // ANSI COBOL-85 — our current minimal baseline
    STD_COBOL2002,
    STD_COBOL2014
} Standard;

typedef enum {
    STRICT_RELAXED,
    STRICT_NORMAL,
    STRICT_PEDANTIC,
    STRICT_IBM
} Strictness;

typedef enum {
    EXT_NONE,
    EXT_COBALT_BASIC
} Extensions;

// A dialect: the habits of one vendor's compiler on top of the standard.
// Profiles are built in (config.c); -f key=value overrides one setting.
typedef enum {
    DISPLAY_STORED,         // numeric DISPLAY shows the stored characters (IBM)
    DISPLAY_PRETTY          // sign, all digits and the decimal point (GnuCOBOL)
} DisplayStyle;

typedef enum {
    SIGN_ENCODING_ASCII,    // overpunched negative digits 'p'..'y', positive plain
    SIGN_ENCODING_EBCDIC    // '{' 'A'..'I' positive, '}' 'J'..'R' negative
} SignEncoding;

typedef struct {
    const char   *name;
    DisplayStyle  display;
    SignEncoding  sign_encoding;
    bool          binary_truncate;  // COMP items hold PICTURE digits (else: their bytes)
} Dialect;

typedef struct {
    Standard     std;
    Dialect      dialect;
    Strictness   strictness;
    Extensions   extensions;
    bool         fixed_format;      // default true for COBOL-85
    bool         free_format;       // default false
} CompilerMode;

// Helpers for printing / errors
const char *standard_name(Standard std);
const char *strictness_name(Strictness s);
const char *extensions_name(Extensions e);

// Select a built-in dialect by name; false if there is none
bool dialect_select(Dialect *d, const char *name);
// Apply one "key=value" setting; false (with a message) if it is invalid
bool dialect_set_option(Dialect *d, const char *setting);
// Names of the built-in dialects, NULL-terminated
extern const char *const dialect_names[];

#endif
