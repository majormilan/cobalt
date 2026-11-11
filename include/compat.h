/* include/compat.h
 * What the C library of a host lacks (included in every compilation unit,
 * see the Makefile). cobalt is written for POSIX; on Windows (MinGW-w64)
 * these fill the gaps.
 */
#ifndef COBALT_COMPAT_H
#define COBALT_COMPAT_H

#ifdef _WIN32
#include <stdlib.h>
#include <string.h>

static inline char *cobalt_strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *d = malloc(len + 1);
    if (d) {
        memcpy(d, s, len);
        d[len] = '\0';
    }
    return d;
}
#define strndup cobalt_strndup
#endif

#endif
