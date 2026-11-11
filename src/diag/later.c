/* src/diag/later.c
 * Elements of later COBOL standards and of vendor dialects that cobalt does
 * not support: recognized where they appear, so the message names what they
 * are instead of a syntax error. (Those cobalt accepts as extensions, such as
 * GOBACK and EXIT PERFORM, are handled where they are parsed.)
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "diag.h"

typedef struct {
    LaterContext where;
    const char *word;           // as written, upper case
    const char *what;           // what it is, for the message
    const char *origin;         // where it comes from
} LaterElement;

static const LaterElement elements[] = {
    // statements
    { LATER_STATEMENT, "EXEC", "EXEC ... END-EXEC (embedded SQL, CICS)", "a precompiler's input" },
    { LATER_STATEMENT, "ALLOCATE", "The ALLOCATE statement", "COBOL 2002" },
    { LATER_STATEMENT, "FREE", "The FREE statement", "COBOL 2002" },
    { LATER_STATEMENT, "RAISE", "The RAISE statement", "COBOL 2002" },
    { LATER_STATEMENT, "RESUME", "The RESUME statement", "COBOL 2002" },
    { LATER_STATEMENT, "VALIDATE", "The VALIDATE statement", "COBOL 2002" },
    { LATER_STATEMENT, "INVOKE", "The INVOKE statement (object orientation)", "COBOL 2002" },
    { LATER_STATEMENT, "ENTRY", "The ENTRY statement", "an IBM and Micro Focus extension" },
    { LATER_STATEMENT, "XML", "The XML statements", "an IBM extension" },
    { LATER_STATEMENT, "JSON", "The JSON statements", "an IBM extension" },
    { LATER_STATEMENT, "COMMIT", "The COMMIT statement", "a vendor extension" },
    { LATER_STATEMENT, "ROLLBACK", "The ROLLBACK statement", "a vendor extension" },
    { LATER_STATEMENT, "UNLOCK", "The UNLOCK statement", "COBOL 2002" },
    // usages
    { LATER_USAGE, "COMP-1", "USAGE COMP-1 (single precision floating point)", "an IBM extension" },
    { LATER_USAGE, "COMPUTATIONAL-1", "USAGE COMPUTATIONAL-1 (single precision floating point)", "an IBM extension" },
    { LATER_USAGE, "COMP-2", "USAGE COMP-2 (double precision floating point)", "an IBM extension" },
    { LATER_USAGE, "COMPUTATIONAL-2", "USAGE COMPUTATIONAL-2 (double precision floating point)", "an IBM extension" },
    { LATER_USAGE, "COMP-X", "USAGE COMP-X", "a Micro Focus extension" },
    { LATER_USAGE, "POINTER", "USAGE POINTER", "COBOL 2002" },
    { LATER_USAGE, "PROCEDURE-POINTER", "USAGE PROCEDURE-POINTER", "an IBM extension" },
    { LATER_USAGE, "FUNCTION-POINTER", "USAGE FUNCTION-POINTER", "an IBM extension" },
    { LATER_USAGE, "PROGRAM-POINTER", "USAGE PROGRAM-POINTER", "COBOL 2002" },
    { LATER_USAGE, "OBJECT", "USAGE OBJECT REFERENCE", "COBOL 2002" },
    { LATER_USAGE, "NATIONAL", "USAGE NATIONAL", "COBOL 2002" },
    { LATER_USAGE, "BINARY-CHAR", "USAGE BINARY-CHAR", "COBOL 2002" },
    { LATER_USAGE, "BINARY-SHORT", "USAGE BINARY-SHORT", "COBOL 2002" },
    { LATER_USAGE, "BINARY-LONG", "USAGE BINARY-LONG", "COBOL 2002" },
    { LATER_USAGE, "BINARY-DOUBLE", "USAGE BINARY-DOUBLE", "COBOL 2002" },
    { LATER_USAGE, "FLOAT-SHORT", "USAGE FLOAT-SHORT", "COBOL 2002" },
    { LATER_USAGE, "FLOAT-LONG", "USAGE FLOAT-LONG", "COBOL 2002" },
    { LATER_USAGE, "FLOAT-EXTENDED", "USAGE FLOAT-EXTENDED", "COBOL 2002" },
    { LATER_USAGE, "FLOAT-DECIMAL-16", "USAGE FLOAT-DECIMAL-16", "COBOL 2014" },
    { LATER_USAGE, "FLOAT-DECIMAL-34", "USAGE FLOAT-DECIMAL-34", "COBOL 2014" },
    { LATER_USAGE, "BIT", "USAGE BIT", "COBOL 2002" },
    { LATER_USAGE, "UTF-8", "USAGE UTF-8", "an IBM extension" },
    { LATER_USAGE, "BASED", "The BASED clause", "COBOL 2002" },
    { LATER_USAGE, "TYPEDEF", "The TYPEDEF clause", "COBOL 2002" },
    { LATER_USAGE, "TYPE", "The TYPE clause", "COBOL 2002" },
    { LATER_USAGE, "ANY", "ANY LENGTH", "COBOL 2002" },
    { LATER_USAGE, "DYNAMIC", "DYNAMIC LENGTH", "COBOL 2014" },
    { LATER_USAGE, "GROUP-USAGE", "The GROUP-USAGE clause", "COBOL 2002" },
    { LATER_USAGE, "VOLATILE", "The VOLATILE clause", "COBOL 2014" },
    // sections and division entries
    { LATER_SECTION, "LOCAL-STORAGE", "The LOCAL-STORAGE SECTION", "COBOL 2002" },
    { LATER_SECTION, "SCREEN", "The SCREEN SECTION", "COBOL 2002" },
    { LATER_SECTION, "78", "Level 78 (a constant)", "a Micro Focus and GnuCOBOL extension" },
    // intrinsic functions of COBOL 2002 and 2014 (and the commonest vendor ones)
    { LATER_FUNCTION, "TRIM", "FUNCTION TRIM", "COBOL 2002" },
    { LATER_FUNCTION, "CONCATENATE", "FUNCTION CONCATENATE", "COBOL 2014" },
    { LATER_FUNCTION, "SUBSTITUTE", "FUNCTION SUBSTITUTE", "COBOL 2014" },
    { LATER_FUNCTION, "SUBSTITUTE-CASE", "FUNCTION SUBSTITUTE-CASE", "COBOL 2014" },
    { LATER_FUNCTION, "LOCALE-DATE", "FUNCTION LOCALE-DATE", "COBOL 2002" },
    { LATER_FUNCTION, "LOCALE-TIME", "FUNCTION LOCALE-TIME", "COBOL 2002" },
    { LATER_FUNCTION, "ABS", "FUNCTION ABS", "COBOL 2002" },
    { LATER_FUNCTION, "SIGN", "FUNCTION SIGN", "COBOL 2002" },
    { LATER_FUNCTION, "PI", "FUNCTION PI", "COBOL 2002" },
    { LATER_FUNCTION, "E", "FUNCTION E", "COBOL 2002" },
    { LATER_FUNCTION, "EXP", "FUNCTION EXP", "COBOL 2002" },
    { LATER_FUNCTION, "EXP10", "FUNCTION EXP10", "COBOL 2002" },
    { LATER_FUNCTION, "FRACTION-PART", "FUNCTION FRACTION-PART", "COBOL 2002" },
    { LATER_FUNCTION, "BOOLEAN-OF-INTEGER", "FUNCTION BOOLEAN-OF-INTEGER", "COBOL 2002" },
    { LATER_FUNCTION, "BYTE-LENGTH", "FUNCTION BYTE-LENGTH", "COBOL 2002" },
    { LATER_FUNCTION, "DISPLAY-OF", "FUNCTION DISPLAY-OF", "COBOL 2002" },
    { LATER_FUNCTION, "NATIONAL-OF", "FUNCTION NATIONAL-OF", "COBOL 2002" },
    { LATER_FUNCTION, "HIGHEST-ALGEBRAIC", "FUNCTION HIGHEST-ALGEBRAIC", "COBOL 2002" },
    { LATER_FUNCTION, "LOWEST-ALGEBRAIC", "FUNCTION LOWEST-ALGEBRAIC", "COBOL 2002" },
    { LATER_FUNCTION, "TEST-DATE-YYYYMMDD", "FUNCTION TEST-DATE-YYYYMMDD", "COBOL 2002" },
    { LATER_FUNCTION, "TEST-DAY-YYYYDDD", "FUNCTION TEST-DAY-YYYYDDD", "COBOL 2002" },
    { LATER_FUNCTION, "TEST-NUMVAL", "FUNCTION TEST-NUMVAL", "COBOL 2014" },
    { LATER_FUNCTION, "TEST-NUMVAL-C", "FUNCTION TEST-NUMVAL-C", "COBOL 2014" },
    { LATER_FUNCTION, "NUMVAL-F", "FUNCTION NUMVAL-F", "COBOL 2002" },
    { LATER_FUNCTION, "STANDARD-COMPARE", "FUNCTION STANDARD-COMPARE", "COBOL 2002" },
    { LATER_FUNCTION, "CURRENT-DATE", NULL, NULL },     // (COBOL-85's: stops the search)
    { LATER_FUNCTION, "SECONDS-PAST-MIDNIGHT", "FUNCTION SECONDS-PAST-MIDNIGHT", "COBOL 2014" },
    { LATER_FUNCTION, "FORMATTED-DATE", "FUNCTION FORMATTED-DATE", "COBOL 2014" },
    { LATER_FUNCTION, "FORMATTED-TIME", "FUNCTION FORMATTED-TIME", "COBOL 2014" },
    { LATER_FUNCTION, "FORMATTED-DATETIME", "FUNCTION FORMATTED-DATETIME", "COBOL 2014" },
    { LATER_FUNCTION, "INTEGER-OF-FORMATTED-DATE", "FUNCTION INTEGER-OF-FORMATTED-DATE", "COBOL 2014" },
    { LATER_FUNCTION, "HEX-OF", "FUNCTION HEX-OF", "an IBM extension" },
    { LATER_FUNCTION, "HEX-TO-CHAR", "FUNCTION HEX-TO-CHAR", "an IBM extension" },
    { LATER_FUNCTION, "ULENGTH", "FUNCTION ULENGTH", "an IBM extension" },
    { LATER_FUNCTION, "UPOS", "FUNCTION UPOS", "an IBM extension" },
    { LATER_FUNCTION, "USUBSTR", "FUNCTION USUBSTR", "an IBM extension" },
    { LATER_FUNCTION, "UVALID", "FUNCTION UVALID", "an IBM extension" },
    { LATER_FUNCTION, "UWIDTH", "FUNCTION UWIDTH", "an IBM extension" },
    // phrases of COBOL-85 statements
    { LATER_PHRASE, "RETURNING", "CALL ... RETURNING", "COBOL 2002" },
    { LATER_PHRASE, "AT", "DISPLAY / ACCEPT ... AT (screen positions)", "a vendor extension" },
    { LATER_PHRASE, "ENVIRONMENT", "ACCEPT ... FROM ENVIRONMENT", "a Micro Focus and GnuCOBOL extension" },
    { LATER_PHRASE, "ENVIRONMENT-NAME", "ENVIRONMENT-NAME", "a Micro Focus and GnuCOBOL extension" },
    { LATER_PHRASE, "ENVIRONMENT-VALUE", "ENVIRONMENT-VALUE", "a Micro Focus and GnuCOBOL extension" },
    { LATER_PHRASE, "COMMAND-LINE", "ACCEPT ... FROM COMMAND-LINE", "a Micro Focus and GnuCOBOL extension" },
    { LATER_PHRASE, "ARGUMENT-VALUE", "ARGUMENT-VALUE", "a Micro Focus and GnuCOBOL extension" },
    { LATER_PHRASE, "ARGUMENT-NUMBER", "ARGUMENT-NUMBER", "a Micro Focus and GnuCOBOL extension" },
    { LATER_PHRASE, "VALUE", "BY VALUE", "COBOL 2002" },
};

bool diag_later_element(SourceLocation loc, LaterContext where, const char *word, size_t len) {
    if (len == (size_t)-1) len = strlen(word);
    for (size_t k = 0; k < sizeof(elements) / sizeof(elements[0]); k++) {
        const LaterElement *e = &elements[k];
        if (e->where != where || strlen(e->word) != len || strncasecmp(e->word, word, len) != 0) continue;
        if (!e->what) return false;
        diag_error(loc, "%s is %s, not COBOL-85; cobalt does not support it", e->what, e->origin);
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
    diag_error(loc, "%s is not COBOL-85; cobalt does not support it", what);
    diag_note_plain("cobalt takes its options from the command line (cobalt --help)");
    return true;
}
