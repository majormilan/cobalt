/* src/config.c */
#include "config.h"
#include "diag.h"
#include <string.h>
#include <strings.h>

// Built-in dialect profiles
static const Dialect dialects[] = {
    //  name          display          sign encoding          binary truncate
    { "standard",   DISPLAY_STORED, SIGN_ENCODING_ASCII,  true },
    { "ibm",        DISPLAY_STORED, SIGN_ENCODING_EBCDIC, true },
    { "gnucobol",   DISPLAY_PRETTY, SIGN_ENCODING_ASCII,  true },
    { "microfocus", DISPLAY_STORED, SIGN_ENCODING_ASCII,  true },
};

const char *const dialect_names[] = { "standard", "ibm", "gnucobol", "microfocus", NULL };

bool dialect_select(Dialect *d, const char *name) {
    for (size_t k = 0; k < sizeof(dialects) / sizeof(dialects[0]); k++) {
        if (strcasecmp(dialects[k].name, name) == 0) {
            *d = dialects[k];
            return true;
        }
    }
    return false;
}

static int option_choice(const char *value, const char *const choices[]) {
    for (int k = 0; choices[k]; k++)
        if (strcasecmp(value, choices[k]) == 0) return k;
    return -1;
}

bool dialect_set_option(Dialect *d, const char *setting) {
    const char *eq = strchr(setting, '=');
    if (!eq) {
        cobalt_error("Expected key=value, got '%s'", setting);
        return false;
    }
    size_t klen = (size_t)(eq - setting);
    const char *value = eq + 1;
    #define KEY(k) (klen == strlen(k) && strncasecmp(setting, k, klen) == 0)
    if (KEY("display")) {
        static const char *const choices[] = { "stored", "pretty", NULL };
        int c = option_choice(value, choices);
        if (c >= 0) { d->display = (DisplayStyle)c; return true; }
    } else if (KEY("sign")) {
        static const char *const choices[] = { "ascii", "ebcdic", NULL };
        int c = option_choice(value, choices);
        if (c >= 0) { d->sign_encoding = (SignEncoding)c; return true; }
    } else if (KEY("binary-truncate")) {
        static const char *const choices[] = { "no", "yes", NULL };
        int c = option_choice(value, choices);
        if (c >= 0) { d->binary_truncate = c == 1; return true; }
    } else {
        cobalt_error("Unknown dialect setting '%.*s' (known: display, sign, binary-truncate)", (int)klen, setting);
        return false;
    }
    #undef KEY
    cobalt_error("Invalid value '%s' for dialect setting '%.*s'", value, (int)klen, setting);
    return false;
}

const char *standard_name(Standard std) {
    switch (std) {
        case STD_COBOL85:           return "COBOL-85";
        case STD_COBOL2002:         return "COBOL-2002";
        case STD_COBOL2014:         return "COBOL-2014";
        default:                    return "unknown";
    }
}

const char *strictness_name(Strictness s) {
    switch (s) {
        case STRICT_RELAXED:        return "relaxed";
        case STRICT_NORMAL:         return "normal";
        case STRICT_PEDANTIC:       return "pedantic";
        case STRICT_IBM:            return "ibm-strict";
        default:                    return "unknown";
    }
}

const char *extensions_name(Extensions e) {
    switch (e) {
        case EXT_NONE:              return "none";
        case EXT_COBALT_BASIC:      return "cobalt-basic";
        default:                    return "unknown";
    }
}
