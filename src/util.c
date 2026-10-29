/* src/util.c: small data structures shared by the compiler's parts */

#include "util.h"
#include <stdlib.h>
#include <string.h>

void bytes_put(Bytes *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + n) cap *= 2;
        b->data = realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void bytes_byte(Bytes *b, uint8_t v) { bytes_put(b, &v, 1); }

static uint64_t hash(const char *s, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t k = 0; k < len; k++) h = (h ^ (uint8_t)s[k]) * 1099511628211ULL;
    return h;
}

int namemap_get(const NameMap *m, const char *key, size_t len) {
    if (!m->cap) return -1;
    for (size_t i = hash(key, len) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
        if (!m->keys[i]) return -1;
        if (strlen(m->keys[i]) == len && memcmp(m->keys[i], key, len) == 0) return m->values[i];
    }
}

void namemap_put(NameMap *m, const char *key, size_t len, int value) {
    if ((m->count + 1) * 2 > m->cap) {
        NameMap bigger = { calloc(m->cap ? m->cap * 2 : 64, sizeof(char *)),
                           calloc(m->cap ? m->cap * 2 : 64, sizeof(int)), m->cap ? m->cap * 2 : 64, 0 };
        for (size_t i = 0; i < m->cap; i++)
            if (m->keys[i]) {
                size_t j = hash(m->keys[i], strlen(m->keys[i])) & (bigger.cap - 1);
                while (bigger.keys[j]) j = (j + 1) & (bigger.cap - 1);
                bigger.keys[j] = m->keys[i];
                bigger.values[j] = m->values[i];
                bigger.count++;
            }
        free(m->keys);
        free(m->values);
        *m = bigger;
    }
    size_t i = hash(key, len) & (m->cap - 1);
    while (m->keys[i]) {
        if (strlen(m->keys[i]) == len && memcmp(m->keys[i], key, len) == 0) { m->values[i] = value; return; }
        i = (i + 1) & (m->cap - 1);
    }
    m->keys[i] = strndup(key, len);
    m->values[i] = value;
    m->count++;
}

void namemap_free(NameMap *m) {
    for (size_t i = 0; i < m->cap; i++) free(m->keys[i]);
    free(m->keys);
    free(m->values);
    memset(m, 0, sizeof(*m));
}
