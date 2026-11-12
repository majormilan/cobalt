/* src/diag/features.c
 * The language elements that differ between COBOL standards and vendor
 * dialects (include/features.def), and what using one gives in this
 * compile: the selected standard, the enabled extension sets (-fext) and
 * the strictness level (-X) decide between nothing, an extension warning,
 * and an error that names where the element comes from.
 *
 *   -X relaxed   the common set and the -fext sets, silently
 *   -X normal    the same, each use a warning (-Wextension)   [default]
 *   -X pedantic  only the -fext sets, silently; the rest are errors
 *   -X ibm-strict  -X pedantic with -fext=ibm
 *
 * Elements Cobalt does not implement yet are errors in every case, saying
 * so; the parser also recognizes them by word where they would appear
 * (diag_later_element), so they are named instead of being syntax errors.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "diag.h"

typedef struct {
    LaterContext where;
    const char *word;
    const char *text;
    unsigned standards, sets;
    bool implemented;
    const char *origin;
} FeatureInfo;

static const FeatureInfo features[F_COUNT] = {
#define FEATURE(id, context, word, text, standards, sets, implemented, origin) \
    { context, word, text, standards, sets, implemented, origin },
#include "features.def"
#undef FEATURE
};

static unsigned enabled_sets = EXT_COMMON;
static const unsigned standard = STD_85;       // the only standard of this release

static const struct { const char *name; unsigned set; } set_names[] = {
    { "common", EXT_COMMON }, { "ibm", EXT_IBM }, { "gnucobol", EXT_GNUCOBOL },
    { "cobol2002", EXT_COBOL2002 }, { NULL, 0 } };

unsigned diag_extension_set(const char *name) {
    for (int k = 0; set_names[k].name; k++)
        if (!strcasecmp(name, set_names[k].name)) return set_names[k].set;
    return 0;
}

void diag_set_extensions(unsigned named, bool no_default) {
    int s = diag_strictness();
    if (s == STRICT_PEDANTIC || s == STRICT_IBM || no_default) enabled_sets = named;
    else enabled_sets = EXT_COMMON | named;
    if (s == STRICT_IBM) enabled_sets |= EXT_IBM;
}

// Where an element comes from: "COBOL 2002", "an IBM extension", ...
static void origin_text(const FeatureInfo *f, char *buf, size_t n) {
    if (f->origin) { snprintf(buf, n, "%s", f->origin); return; }
    if (f->standards & STD_2002) { snprintf(buf, n, "COBOL 2002"); return; }
    if (f->standards & STD_2014) { snprintf(buf, n, "COBOL 2014"); return; }
    const char *v[3];
    int k = 0;
    if (f->sets & EXT_IBM) v[k++] = "IBM";
    if (f->sets & EXT_GNUCOBOL) v[k++] = "GnuCOBOL";
    if (f->sets & EXT_MF) v[k++] = "Micro Focus";
    if (k == 0) snprintf(buf, n, "an extension");
    else if (k == 1) snprintf(buf, n, "a%s %s extension", v[0][0] == 'I' ? "n" : "", v[0]);
    else if (k == 2) snprintf(buf, n, "a%s %s and %s extension", v[0][0] == 'I' ? "n" : "", v[0], v[1]);
    else snprintf(buf, n, "a%s %s, %s and %s extension", v[0][0] == 'I' ? "n" : "", v[0], v[1], v[2]);
}

// The set to suggest for an element: the most specific that has it
static const char *suggested_set(unsigned sets) {
    if (sets & EXT_COBOL2002) return "cobol2002";
    if (sets & EXT_IBM) return "ibm";
    if (sets & EXT_GNUCOBOL) return "gnucobol";
    if (sets & EXT_COMMON) return "common";
    return NULL;
}

bool diag_feature(Feature id, SourceLocation loc, const char *fmt, ...) {
    const FeatureInfo *f = &features[id];
    char text[512];
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(text, sizeof(text), fmt, ap);
        va_end(ap);
    } else {
        snprintf(text, sizeof(text), "%s", f->text);
    }
    text[0] = (char)toupper((unsigned char)text[0]);
    char origin[96];
    origin_text(f, origin, sizeof(origin));

    if (f->standards & standard) {
        if (f->implemented) return true;
        diag_error(loc, "%s is not supported by Cobalt yet", text);
        return false;
    }
    if (!f->implemented) {
        diag_error(loc, "%s is %s, not COBOL-85; Cobalt does not support it yet", text, origin);
        return false;
    }
    if (f->sets & enabled_sets) {
        if (diag_strictness() == STRICT_NORMAL)
            diag_warning(W_EXTENSION, loc, "%s is an extension to COBOL-85", text);
        return true;
    }
    diag_error(loc, "%s is not COBOL-85", text);
    const char *set = suggested_set(f->sets);
    if (set) diag_note_plain("it is %s; -fext=%s accepts it", origin, set);
    return false;
}

bool diag_later_element(SourceLocation loc, LaterContext where, const char *word, size_t len) {
    if (len == (size_t)-1) len = strlen(word);
    for (int k = 0; k < F_COUNT; k++) {
        const FeatureInfo *f = &features[k];
        if (f->where != where || where == LATER_NONE || strlen(f->word) != len ||
            strncasecmp(f->word, word, len) != 0)
            continue;
        diag_feature((Feature)k, loc, NULL);
        return true;
    }
    return false;
}

// A compiler-directing line: >>directive (COBOL 2002), $SET (Micro Focus),
// PROCESS / CBL (IBM compiler options); its text from the first non-space
bool diag_later_directive(SourceLocation loc, const char *text, size_t len) {
    const char *what = NULL;
    if (len >= 2 && text[0] == '>' && text[1] == '>') what = "A >> compiler directive (COBOL 2002)";
    else if (len >= 4 && strncasecmp(text, "$SET", 4) == 0) what = "A $SET directive (Micro Focus)";
    else if ((len >= 7 && strncasecmp(text, "PROCESS", 7) == 0 && (len == 7 || text[7] == ' ')) ||
             (len >= 3 && strncasecmp(text, "CBL", 3) == 0 && (len == 3 || text[3] == ' ')))
        what = "A PROCESS / CBL line of IBM compiler options";
    if (!what) return false;
    diag_error(loc, "%s is not COBOL-85; Cobalt does not support it", what);
    diag_note_plain("Cobalt takes its options from the command line (cobalt --help)");
    return true;
}
