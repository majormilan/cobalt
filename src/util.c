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

// ── Memory streams ───────────────────────────────────────────────────────────

#ifdef _WIN32
#include <windows.h>
static struct { FILE *f; char **buf; size_t *len; } mem_streams[8];

// A temporary file in %TEMP%, deleted when closed (tmpfile() of the C
// library writes to the root of the drive, which users may not write to)
static FILE *temp_file(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathA(sizeof(dir), dir) || !GetTempFileNameA(dir, "cob", 0, path)) return NULL;
    return fopen(path, "w+bTD");        // T: short-lived, D: deleted on close
}

FILE *mem_stream_open(char **buf, size_t *len) {
    for (int k = 0; k < 8; k++) {
        if (mem_streams[k].f) continue;
        FILE *f = temp_file();
        if (!f) return NULL;
        mem_streams[k].f = f;
        mem_streams[k].buf = buf;
        mem_streams[k].len = len;
        return f;
    }
    return NULL;
}

int mem_stream_close(FILE *f) {
    for (int k = 0; k < 8; k++) {
        if (mem_streams[k].f != f) continue;
        mem_streams[k].f = NULL;
        long n = ftell(f);
        char *b = malloc(n > 0 ? (size_t)n + 1 : 1);
        if (!b) { fclose(f); return EOF; }
        rewind(f);
        size_t got = n > 0 ? fread(b, 1, (size_t)n, f) : 0;
        b[got] = '\0';
        *mem_streams[k].buf = b;
        *mem_streams[k].len = got;
        return fclose(f) == 0 && got == (size_t)(n > 0 ? n : 0) ? 0 : EOF;
    }
    return fclose(f);
}
#else
FILE *mem_stream_open(char **buf, size_t *len) { return open_memstream(buf, len); }
int mem_stream_close(FILE *f) { return fclose(f); }
#endif
