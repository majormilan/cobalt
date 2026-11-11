/* include/util.h: small data structures shared by the compiler's parts */
#ifndef COBALT_UTIL_H
#define COBALT_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

// A stream written to memory: after mem_stream_close, *buf (malloc'ed,
// NUL-terminated) and *len hold what was written. (open_memstream, or a
// temporary file where the C library has none.) mem_stream_close returns 0
// or EOF like fclose.
FILE *mem_stream_open(char **buf, size_t *len);
int mem_stream_close(FILE *f);

#endif
