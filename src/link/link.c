/* src/link/link.c: the COBALT linker.
 *
 * Links ELF64 x86-64 relocatable objects into a static, non-PIE Linux
 * executable. Loaded sections are grouped by kind: code, read-only data,
 * data and bss. Code and read-only data form one read/execute segment that
 * starts with the ELF and program headers at 0x400000; data and bss form a
 * read/write segment on the next page boundary. A GNU_STACK header makes the
 * stack non-executable. Library objects are included only when they define
 * a symbol still undefined, as with an archive. docs/linker.md has details. */

#include "link.h"
#include "util.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8
#define SHF_WRITE    1
#define SHF_ALLOC    2
#define SHF_EXEC     4
#define STB_LOCAL    0
#define STB_GLOBAL   1
#define STB_WEAK     2
#define STT_SECTION  3
#define STT_FILE     4
#define SHN_UNDEF    0
#define SHN_ABS      0xFFF1
#define SHN_COMMON   0xFFF2

#define R_X86_64_64    1
#define R_X86_64_PC32  2
#define R_X86_64_PLT32 4
#define R_X86_64_32    10
#define R_X86_64_32S   11

#define BASE_ADDRESS 0x400000ULL
#define PAGE         4096ULL

enum { CLS_TEXT, CLS_RODATA, CLS_DATA, CLS_BSS, NCLS };

typedef struct {
    uint32_t type;
    uint64_t flags, offset, size, align;
    uint32_t link, info;
    int cls;                    // output class, or -1 when not loaded
    uint64_t out_off;           // offset within its class
    uint64_t addr;              // address in the executable
} InSection;

typedef struct {
    const LinkObject *in;
    InSection *sec;
    int nsec;
    int symtab;                 // section index, or -1
    const uint8_t *syms;
    int nsym;
    const char *strtab;
    size_t strtab_size;
    bool included;
} InObject;

typedef struct {
    InObject *objs;
    int nobjs;
    NameMap globals;            // name -> index into def_obj / def_sym
    int *def_obj, *def_sym;     // definition of each global, -1 while undefined
    char **global_names;
    int nglobals, cglobals;
    int errors;
} Linker;

static void link_error(Linker *lk, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "cobalt link: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    lk->errors++;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }
static void wr64(uint8_t *p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }

static uint64_t align_up(uint64_t v, uint64_t a) { return a > 1 ? (v + a - 1) / a * a : v; }

// ── Reading objects ──────────────────────────────────────────────────────────

static bool read_object(Linker *lk, const LinkObject *in, InObject *o) {
    memset(o, 0, sizeof(*o));
    o->in = in;
    o->symtab = -1;
    const uint8_t *d = in->data;
    if (in->size < 64 || memcmp(d, "\x7F" "ELF", 4) != 0 || d[4] != 2 || d[5] != 1) {
        link_error(lk, "%s: not an ELF64 little-endian object", in->name);
        return false;
    }
    if (rd16(d + 16) != 1 || rd16(d + 18) != 62) {
        link_error(lk, "%s: not an x86-64 relocatable object", in->name);
        return false;
    }
    uint64_t shoff = rd64(d + 40);
    int shnum = rd16(d + 60);
    if (rd16(d + 58) != 64 || shoff + (uint64_t)shnum * 64 > in->size) {
        link_error(lk, "%s: bad section header table", in->name);
        return false;
    }
    o->nsec = shnum;
    o->sec = calloc((size_t)shnum, sizeof(InSection));
    for (int k = 0; k < shnum; k++) {
        const uint8_t *h = d + shoff + (uint64_t)k * 64;
        InSection *s = &o->sec[k];
        s->type = rd32(h + 4);
        s->flags = rd64(h + 8);
        s->offset = rd64(h + 24);
        s->size = rd64(h + 32);
        s->link = rd32(h + 40);
        s->info = rd32(h + 44);
        s->align = rd64(h + 48);
        s->cls = -1;
        if (s->type != SHT_NOBITS && s->offset + s->size > in->size) {
            link_error(lk, "%s: section %d lies outside the file", in->name, k);
            return false;
        }
        if (s->type == SHT_SYMTAB) o->symtab = k;
        if (s->flags & SHF_ALLOC)
            s->cls = s->type == SHT_NOBITS ? CLS_BSS : (s->flags & SHF_EXEC) ? CLS_TEXT :
                     (s->flags & SHF_WRITE) ? CLS_DATA : CLS_RODATA;
    }
    if (o->symtab >= 0) {
        InSection *st = &o->sec[o->symtab];
        o->syms = d + st->offset;
        o->nsym = (int)(st->size / 24);
        if (st->link >= (uint32_t)shnum) { link_error(lk, "%s: bad symbol table", in->name); return false; }
        o->strtab = (const char *)d + o->sec[st->link].offset;
        o->strtab_size = o->sec[st->link].size;
    }
    return true;
}

typedef struct {
    const char *name;
    int bind, type;
    uint16_t shndx;
    uint64_t value;
} Sym;

static Sym symbol(const InObject *o, int i) {
    const uint8_t *p = o->syms + (size_t)i * 24;
    Sym s;
    uint32_t name = rd32(p);
    s.name = name < o->strtab_size ? o->strtab + name : "";
    s.bind = p[4] >> 4;
    s.type = p[4] & 15;
    s.shndx = rd16(p + 6);
    s.value = rd64(p + 8);
    return s;
}

// ── Symbol resolution ────────────────────────────────────────────────────────

static int global_index(Linker *lk, const char *name) {
    size_t len = strlen(name);
    int k = namemap_get(&lk->globals, name, len);
    if (k >= 0) return k;
    if (lk->nglobals == lk->cglobals) {
        lk->cglobals = lk->cglobals ? lk->cglobals * 2 : 256;
        lk->def_obj = realloc(lk->def_obj, (size_t)lk->cglobals * sizeof(int));
        lk->def_sym = realloc(lk->def_sym, (size_t)lk->cglobals * sizeof(int));
        lk->global_names = realloc(lk->global_names, (size_t)lk->cglobals * sizeof(char *));
    }
    k = lk->nglobals++;
    lk->def_obj[k] = lk->def_sym[k] = -1;
    lk->global_names[k] = strdup(name);
    namemap_put(&lk->globals, name, len, k);
    return k;
}

static void include_object(Linker *lk, int idx) {
    InObject *o = &lk->objs[idx];
    o->included = true;
    for (int i = 1; i < o->nsym; i++) {
        Sym s = symbol(o, i);
        if (s.bind == STB_LOCAL) continue;
        int g = global_index(lk, s.name);
        if (s.shndx == SHN_UNDEF) continue;
        if (s.shndx == SHN_COMMON) {
            link_error(lk, "%s: common symbol '%s' is not supported", o->in->name, s.name);
            continue;
        }
        if (lk->def_obj[g] >= 0) {
            if (s.bind == STB_WEAK) continue;
            Sym prev = symbol(&lk->objs[lk->def_obj[g]], lk->def_sym[g]);
            if (prev.bind != STB_WEAK) {
                link_error(lk, "'%s' is defined in both %s and %s", s.name,
                           lk->objs[lk->def_obj[g]].in->name, o->in->name);
                continue;
            }
        }
        lk->def_obj[g] = idx;
        lk->def_sym[g] = i;
    }
}

// Address of symbol i of object o; false (with an error) if it has none
static bool symbol_address(Linker *lk, const InObject *o, int i, uint64_t *addr) {
    if (i <= 0 || i >= o->nsym) { link_error(lk, "%s: bad symbol index %d", o->in->name, i); return false; }
    Sym s = symbol(o, i);
    if (s.shndx == SHN_UNDEF) {
        int g = namemap_get(&lk->globals, s.name, strlen(s.name));
        if (g < 0 || lk->def_obj[g] < 0) {
            if (s.bind == STB_WEAK) { *addr = 0; return true; }
            link_error(lk, "undefined reference to '%s' (in %s)", s.name, o->in->name);
            return false;
        }
        return symbol_address(lk, &lk->objs[lk->def_obj[g]], lk->def_sym[g], addr);
    }
    if (s.shndx == SHN_ABS) { *addr = s.value; return true; }
    if (s.shndx >= o->nsec || o->sec[s.shndx].cls < 0) {
        link_error(lk, "%s: symbol '%s' is in a section that is not loaded", o->in->name, s.name);
        return false;
    }
    *addr = o->sec[s.shndx].addr + s.value;
    return true;
}

// ── Output ───────────────────────────────────────────────────────────────────

typedef struct {
    uint64_t size[NCLS], align[NCLS];
    uint64_t file_off[NCLS], addr[NCLS];
    uint64_t rx_end, rw_off, rw_addr, rw_filesz, rw_memsz;
} Layout;

static void put_phdr(uint8_t *p, uint32_t type, uint32_t flags, uint64_t off, uint64_t addr,
                     uint64_t filesz, uint64_t memsz, uint64_t align) {
    wr32(p, type);
    wr32(p + 4, flags);
    wr64(p + 8, off);
    wr64(p + 16, addr);
    wr64(p + 24, addr);
    wr64(p + 32, filesz);
    wr64(p + 40, memsz);
    wr64(p + 48, align);
}

// A code label that starts a function for debuggers and profilers: a runtime
// routine or a COBOL procedure, not a local (.x), internal (L_) or hidden label
static bool is_function_label(const char *name) {
    return !strchr(name, '.') && strncmp(name, "L_", 2) != 0 && strncmp(name, "..@", 3) != 0;
}

static int compare_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

// Symbol table and section headers, for debuggers and objdump. Function
// labels in code get the function type and a size reaching the next one.
static void append_section_headers(Linker *lk, Bytes *img, const Layout *L, uint8_t *ehdr) {
    Bytes syms = {0}, str = {0}, shstr = {0};
    uint8_t zero[24] = {0};
    bytes_byte(&str, 0);
    bytes_byte(&shstr, 0);
    bytes_put(&syms, zero, 24);

    // start addresses of the functions, sorted, to size each one
    uint64_t *starts = NULL;
    size_t nstarts = 0, cstarts = 0;
    for (int k = 0; k < lk->nobjs; k++) {
        InObject *o = &lk->objs[k];
        if (!o->included) continue;
        for (int i = 1; i < o->nsym; i++) {
            Sym s = symbol(o, i);
            if (s.type == STT_SECTION || s.type == STT_FILE || s.shndx == SHN_UNDEF || s.shndx >= o->nsec) continue;
            if (o->sec[s.shndx].cls != CLS_TEXT || !is_function_label(s.name)) continue;
            if (nstarts == cstarts) {
                cstarts = cstarts ? cstarts * 2 : 256;
                starts = realloc(starts, cstarts * sizeof(uint64_t));
            }
            starts[nstarts++] = o->sec[s.shndx].addr + s.value;
        }
    }
    if (nstarts) qsort(starts, nstarts, sizeof(uint64_t), compare_u64);
    uint64_t text_end = L->addr[CLS_TEXT] + L->size[CLS_TEXT];

    // section header index of each class: 1..4
    for (int pass = 0; pass < 2; pass++) {           // locals first, then globals
        for (int k = 0; k < lk->nobjs; k++) {
            InObject *o = &lk->objs[k];
            if (!o->included) continue;
            for (int i = 1; i < o->nsym; i++) {
                Sym s = symbol(o, i);
                if (s.type == STT_SECTION || s.type == STT_FILE || s.shndx == SHN_UNDEF) continue;
                if ((s.bind == STB_LOCAL) != (pass == 0)) continue;
                if (s.shndx >= o->nsec || o->sec[s.shndx].cls < 0) continue;
                uint8_t e[24] = {0};
                uint64_t addr = o->sec[s.shndx].addr + s.value;
                int type = s.type;
                uint64_t size = 0;
                if (o->sec[s.shndx].cls == CLS_TEXT && is_function_label(s.name)) {
                    // size: up to the next function start (binary search)
                    size_t lo = 0, hi = nstarts;
                    while (lo < hi) {
                        size_t mid = (lo + hi) / 2;
                        if (starts[mid] <= addr) lo = mid + 1; else hi = mid;
                    }
                    type = 2;                                   // STT_FUNC
                    size = (lo < nstarts ? starts[lo] : text_end) - addr;
                }
                wr32(e, (uint32_t)str.len);
                bytes_put(&str, s.name, strlen(s.name) + 1);
                e[4] = (uint8_t)(s.bind << 4 | type);
                wr16(e + 6, (uint16_t)(1 + o->sec[s.shndx].cls));
                wr64(e + 8, addr);
                wr64(e + 16, size);
                bytes_put(&syms, e, 24);
            }
        }
        if (pass == 0) {
            // first global symbol index, kept in the first symbol's size field
            wr64(syms.data + 16, syms.len / 24);
        }
    }
    uint64_t first_global = rd64(syms.data + 16);
    memset(syms.data + 16, 0, 8);
    free(starts);

    // section contents after the image
    while (img->len % 8) bytes_byte(img, 0);
    uint64_t symoff = img->len;
    bytes_put(img, syms.data, syms.len);
    uint64_t stroff = img->len;
    bytes_put(img, str.data, str.len);
    const char *names[8] = { "", ".text", ".rodata", ".data", ".bss", ".symtab", ".strtab", ".shstrtab" };
    uint32_t name_off[8];
    for (int k = 0; k < 8; k++) {
        name_off[k] = (uint32_t)shstr.len;
        bytes_put(&shstr, names[k], strlen(names[k]) + 1);
    }
    uint64_t shstroff = img->len;
    bytes_put(img, shstr.data, shstr.len);
    while (img->len % 8) bytes_byte(img, 0);
    uint64_t shoff = img->len;

    uint8_t h[64];
    memset(h, 0, 64);
    bytes_put(img, h, 64);                              // null section
    for (int c = 0; c < NCLS; c++) {
        memset(h, 0, 64);
        wr32(h, name_off[1 + c]);
        wr32(h + 4, c == CLS_BSS ? 8 : 1);
        wr64(h + 8, SHF_ALLOC | (c == CLS_TEXT ? SHF_EXEC : 0) | (c >= CLS_DATA ? SHF_WRITE : 0));
        wr64(h + 16, L->addr[c]);
        wr64(h + 24, L->file_off[c]);
        wr64(h + 32, L->size[c]);
        wr64(h + 48, L->align[c]);
        bytes_put(img, h, 64);
    }
    memset(h, 0, 64);
    wr32(h, name_off[5]); wr32(h + 4, SHT_SYMTAB); wr64(h + 24, symoff); wr64(h + 32, syms.len);
    wr32(h + 40, 6); wr32(h + 44, (uint32_t)first_global); wr64(h + 48, 8); wr64(h + 56, 24);
    bytes_put(img, h, 64);
    memset(h, 0, 64);
    wr32(h, name_off[6]); wr32(h + 4, SHT_STRTAB); wr64(h + 24, stroff); wr64(h + 32, str.len); wr64(h + 48, 1);
    bytes_put(img, h, 64);
    memset(h, 0, 64);
    wr32(h, name_off[7]); wr32(h + 4, SHT_STRTAB); wr64(h + 24, shstroff); wr64(h + 32, shstr.len); wr64(h + 48, 1);
    bytes_put(img, h, 64);

    ehdr = img->data;                                   // the image may have moved
    wr64(ehdr + 40, shoff);
    wr16(ehdr + 58, 64);
    wr16(ehdr + 60, 8);
    wr16(ehdr + 62, 7);
    free(syms.data);
    free(str.data);
    free(shstr.data);
}

bool link_executable(const LinkObject *objects, int nobjects,
                     const LinkObject *library, int nlibrary,
                     const LinkOptions *opts, const char *path) {
    Linker lk;
    memset(&lk, 0, sizeof(lk));
    lk.nobjs = nobjects + nlibrary;
    lk.objs = calloc((size_t)lk.nobjs, sizeof(InObject));
    for (int k = 0; k < lk.nobjs; k++)
        read_object(&lk, k < nobjects ? &objects[k] : &library[k - nobjects], &lk.objs[k]);
    if (lk.errors) goto done;

    // Every object, then the library members that define what is still needed
    for (int k = 0; k < nobjects; k++) include_object(&lk, k);
    NameMap provides = {0};                              // library: symbol -> object
    for (int k = nobjects; k < lk.nobjs; k++) {
        InObject *o = &lk.objs[k];
        for (int i = 1; i < o->nsym; i++) {
            Sym s = symbol(o, i);
            if (s.bind != STB_LOCAL && s.shndx != SHN_UNDEF && namemap_get(&provides, s.name, strlen(s.name)) < 0)
                namemap_put(&provides, s.name, strlen(s.name), k);
        }
    }
    global_index(&lk, opts->entry);
    for (bool changed = true; changed;) {
        changed = false;
        for (int g = 0; g < lk.nglobals; g++) {
            if (lk.def_obj[g] >= 0) continue;
            int k = namemap_get(&provides, lk.global_names[g], strlen(lk.global_names[g]));
            if (k >= 0 && !lk.objs[k].included) {
                include_object(&lk, k);
                changed = true;
            }
        }
    }
    namemap_free(&provides);
    int entry = namemap_get(&lk.globals, opts->entry, strlen(opts->entry));
    if (entry < 0 || lk.def_obj[entry] < 0) link_error(&lk, "entry symbol '%s' is not defined", opts->entry);
    if (lk.errors) goto done;

    // Layout: offsets within each class, then the segments
    Layout L;
    memset(&L, 0, sizeof(L));
    for (int c = 0; c < NCLS; c++) L.align[c] = 1;
    for (int k = 0; k < lk.nobjs; k++) {
        InObject *o = &lk.objs[k];
        if (!o->included) continue;
        for (int i = 0; i < o->nsec; i++) {
            InSection *s = &o->sec[i];
            if (s->cls < 0) continue;
            uint64_t a = s->align ? s->align : 1;
            if (a > L.align[s->cls]) L.align[s->cls] = a;
            s->out_off = align_up(L.size[s->cls], a);
            L.size[s->cls] = s->out_off + s->size;
        }
    }
    const int nphdr = 3;
    uint64_t headers = 64 + (uint64_t)nphdr * 56;
    L.file_off[CLS_TEXT] = align_up(headers, L.align[CLS_TEXT]);
    L.file_off[CLS_RODATA] = align_up(L.file_off[CLS_TEXT] + L.size[CLS_TEXT], L.align[CLS_RODATA]);
    L.rx_end = L.file_off[CLS_RODATA] + L.size[CLS_RODATA];
    L.rw_off = align_up(L.rx_end, PAGE);
    L.file_off[CLS_DATA] = align_up(L.rw_off, L.align[CLS_DATA]);
    for (int c = CLS_TEXT; c <= CLS_DATA; c++) L.addr[c] = BASE_ADDRESS + L.file_off[c];
    L.rw_addr = BASE_ADDRESS + L.rw_off;
    L.addr[CLS_BSS] = align_up(L.addr[CLS_DATA] + L.size[CLS_DATA], L.align[CLS_BSS]);
    L.file_off[CLS_BSS] = L.file_off[CLS_DATA] + L.size[CLS_DATA];
    L.rw_filesz = L.file_off[CLS_DATA] + L.size[CLS_DATA] - L.rw_off;
    L.rw_memsz = L.addr[CLS_BSS] + L.size[CLS_BSS] - L.rw_addr;
    if (L.addr[CLS_BSS] + L.size[CLS_BSS] > 0x7FFFFFFFULL) {
        link_error(&lk, "the program is larger than the 2 GiB its absolute addresses allow");
        goto done;
    }
    for (int k = 0; k < lk.nobjs; k++) {
        InObject *o = &lk.objs[k];
        if (!o->included) continue;
        for (int i = 0; i < o->nsec; i++)
            if (o->sec[i].cls >= 0) o->sec[i].addr = L.addr[o->sec[i].cls] + o->sec[i].out_off;
    }

    // The image: headers, code, read-only data, data
    Bytes img = {0};
    uint64_t file_size = L.rw_filesz ? L.rw_off + L.rw_filesz : L.rx_end;
    img.data = calloc(1, file_size);
    img.len = img.cap = file_size;
    for (int k = 0; k < lk.nobjs; k++) {
        InObject *o = &lk.objs[k];
        if (!o->included) continue;
        for (int i = 0; i < o->nsec; i++) {
            InSection *s = &o->sec[i];
            if (s->cls < 0 || s->cls == CLS_BSS) continue;
            memcpy(img.data + L.file_off[s->cls] + s->out_off, o->in->data + s->offset, s->size);
        }
    }

    // Relocations
    for (int k = 0; k < lk.nobjs; k++) {
        InObject *o = &lk.objs[k];
        if (!o->included) continue;
        for (int r = 0; r < o->nsec; r++) {
            InSection *rs = &o->sec[r];
            if (rs->type != SHT_RELA || rs->info >= (uint32_t)o->nsec) continue;
            InSection *t = &o->sec[rs->info];
            if (t->cls < 0) continue;
            if (t->cls == CLS_BSS) { link_error(&lk, "%s: relocation in a bss section", o->in->name); continue; }
            const uint8_t *e = o->in->data + rs->offset;
            for (uint64_t n = 0; n < rs->size / 24; n++, e += 24) {
                uint64_t off = rd64(e), info = rd64(e + 8);
                int64_t addend = (int64_t)rd64(e + 16);
                int sym = (int)(info >> 32), type = (int)(info & 0xFFFFFFFF);
                int width = type == R_X86_64_64 ? 8 : 4;
                if (off + (uint64_t)width > t->size) { link_error(&lk, "%s: relocation outside its section", o->in->name); continue; }
                uint64_t S;
                if (!symbol_address(&lk, o, sym, &S)) continue;
                uint64_t P = t->addr + off;
                uint8_t *at = img.data + L.file_off[t->cls] + t->out_off + off;
                int64_t v = (int64_t)(S + (uint64_t)addend);
                switch (type) {
                    case R_X86_64_64:
                        wr64(at, (uint64_t)v);
                        break;
                    case R_X86_64_PC32:
                    case R_X86_64_PLT32:
                        v -= (int64_t)P;
                        if (v < INT32_MIN || v > INT32_MAX) link_error(&lk, "%s: PC-relative relocation out of range", o->in->name);
                        wr32(at, (uint32_t)v);
                        break;
                    case R_X86_64_32:
                        if (v < 0 || v > (int64_t)UINT32_MAX) link_error(&lk, "%s: 32-bit relocation out of range", o->in->name);
                        wr32(at, (uint32_t)v);
                        break;
                    case R_X86_64_32S:
                        if (v < INT32_MIN || v > INT32_MAX) link_error(&lk, "%s: 32-bit relocation out of range", o->in->name);
                        wr32(at, (uint32_t)v);
                        break;
                    default:
                        link_error(&lk, "%s: unsupported relocation type %d", o->in->name, type);
                }
            }
        }
    }
    uint64_t entry_addr = 0;
    if (!lk.errors) symbol_address(&lk, &lk.objs[lk.def_obj[entry]], lk.def_sym[entry], &entry_addr);
    if (lk.errors) { free(img.data); goto done; }

    // ELF and program headers
    uint8_t *h = img.data;
    static const uint8_t ident[16] = { 0x7F, 'E', 'L', 'F', 2, 1, 1 };
    memcpy(h, ident, 16);
    wr16(h + 16, 2);                    // ET_EXEC
    wr16(h + 18, 62);                   // EM_X86_64
    wr32(h + 20, 1);
    wr64(h + 24, entry_addr);
    wr64(h + 32, 64);                   // program headers
    wr16(h + 52, 64);
    wr16(h + 54, 56);
    wr16(h + 56, (uint16_t)nphdr);
    put_phdr(h + 64, 1, 5, 0, BASE_ADDRESS, L.rx_end, L.rx_end, PAGE);                    // R X
    put_phdr(h + 64 + 56, 1, 6, L.rw_off, L.rw_addr, L.rw_filesz, L.rw_memsz, PAGE);       // R W
    put_phdr(h + 64 + 112, 0x6474E551, 6, 0, 0, 0, 0, 16);                                 // GNU_STACK
    if (!opts->strip) append_section_headers(&lk, &img, &L, h);

    FILE *f = fopen(path, "wb");
    if (!f || fwrite(img.data, 1, img.len, f) != img.len) link_error(&lk, "cannot write %s", path);
    if (f && fclose(f) != 0) link_error(&lk, "cannot write %s", path);
    if (!lk.errors) chmod(path, 0755);
    free(img.data);

done:
    for (int k = 0; k < lk.nobjs; k++) free(lk.objs[k].sec);
    free(lk.objs);
    for (int g = 0; g < lk.nglobals; g++) free(lk.global_names[g]);
    free(lk.global_names);
    free(lk.def_obj);
    free(lk.def_sym);
    namemap_free(&lk.globals);
    return lk.errors == 0;
}
