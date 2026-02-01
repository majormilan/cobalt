/* src/config.c */
#include "config.h"

const char *standard_name(Standard std) {
    switch (std) {
        case STD_COBOL85:           return "COBOL-85";
        case STD_COBOL2002:         return "COBOL-2002";
        case STD_COBOL2014:         return "COBOL-2014";
        case STD_GNUCOBOL:          return "GnuCOBOL";
        case STD_IBM_ENTERPRISE:    return "IBM Enterprise";
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
