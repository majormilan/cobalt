/* include/config.h */
#ifndef COBALT_CONFIG_H
#define COBALT_CONFIG_H

#include <stdbool.h>

typedef enum {
    STD_COBOL85,            // ANSI COBOL-85 — our current minimal baseline
    STD_COBOL2002,
    STD_COBOL2014,
    STD_GNUCOBOL,
    STD_IBM_ENTERPRISE
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

typedef struct {
    Standard     std;
    Strictness   strictness;
    Extensions   extensions;
    bool         fixed_format;      // default true for COBOL-85
    bool         free_format;       // default false
} CompilerMode;

// Helpers for printing / errors
const char *standard_name(Standard std);
const char *strictness_name(Strictness s);
const char *extensions_name(Extensions e);

#endif
