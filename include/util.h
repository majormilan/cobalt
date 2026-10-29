/* include/util.h: small data structures shared by the compiler's parts */
#ifndef COBALT_UTIL_H
#define COBALT_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Growable byte buffer
typedef struct {
    uint8_t *data;
    size_t len, cap;
} Bytes;

void bytes_put(Bytes *b, const void *p, size_t n);
void bytes_byte(Bytes *b, uint8_t v);

// Name table: string -> int (open addressing)
typedef struct {
    char **keys;
    int *values;
    size_t cap, count;
} NameMap;

int namemap_get(const NameMap *m, const char *key, size_t len);     // -1 if absent
void namemap_put(NameMap *m, const char *key, size_t len, int value);
void namemap_free(NameMap *m);

#endif
