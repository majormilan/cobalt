/* src/asm/elf64.c: ELF64 relocatable object output for the COBALT assembler.
 *
 * Layout: the ELF header, the contents of the assembled sections, a
 * .rela.<name> section for each one with relocations, .symtab, .strtab,
 * .shstrtab, then the section header table. References to symbols defined in
 * the object are relocated against their section's symbol (with the label's
 * offset in the addend), as nasm does; references to extern symbols use the
 * symbol itself. An empty .note.GNU-stack marks the stack non-executable. */

#include "asm_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8
#define SHF_WRITE    1
#define SHF_ALLOC    2
#define SHF_EXEC     4
#define SHF_INFO_LINK 0x40
#define STB_LOCAL    0
#define STB_GLOBAL   1
#define STT_NOTYPE   0
#define STT_SECTION  3
#define STT_FILE     4
#define SHN_UNDEF    0
#define SHN_ABS      0xFFF1

typedef struct {
    uint32_t name, type;
    uint64_t flags, addr, offset, size;
    uint32_t link, info;
    uint64_t align, entsize;
} SectionHeader;

static void le16(Bytes *b, uint16_t v) { uint8_t p[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; bytes_put(b, p, 2); }
static void le32(Bytes *b, uint32_t v) {
    uint8_t t[4];
    for (int k = 0; k < 4; k++) t[k] = (uint8_t)(v >> (8 * k));
    bytes_put(b, t, 4);
}
static void le64(Bytes *b, uint64_t v) {
    uint8_t t[8];
    for (int k = 0; k < 8; k++) t[k] = (uint8_t)(v >> (8 * k));
    bytes_put(b, t, 8);
}

static uint32_t add_string(Bytes *tab, const char *s) {
    uint32_t at = (uint32_t)tab->len;
    bytes_put(tab, s, strlen(s) + 1);
    return at;
}

static void pad_to(Bytes *b, uint64_t align) {
    while (align > 1 && b->len % align) bytes_byte(b, 0);
}

static void symbol_entry(Bytes *b, uint32_t name, int bind, int type, uint16_t shndx, uint64_t value) {
    le32(b, name);
    bytes_byte(b, (uint8_t)((bind << 4) | type));
    bytes_byte(b, 0);
    le16(b, shndx);
    le64(b, value);
    le64(b, 0);
}

bool asm_elf64_image(const AsmObject *o, uint8_t **data, size_t *size) {
    int ns = o->nsections;
    // Section header indexes: 0 null, 1..ns user sections, then .note.GNU-stack,
    // the .rela sections, .symtab, .strtab, .shstrtab
    int note_idx = ns + 1;
    int nrela = 0;
    for (int s = 0; s < ns; s++) if (o->sections[s].nrelocs) nrela++;
    int symtab_idx = note_idx + 1 + nrela;
    int strtab_idx = symtab_idx + 1, shstrtab_idx = symtab_idx + 2;
    int nheaders = shstrtab_idx + 1;

    Bytes shstr = {0}, str = {0}, syms = {0};
    bytes_byte(&shstr, 0);
    bytes_byte(&str, 0);

    // Symbols: null, file, section symbols, local labels, then globals
    int *elf_index = malloc(((size_t)o->nsymbols + 1) * sizeof(int));
    symbol_entry(&syms, 0, STB_LOCAL, STT_NOTYPE, SHN_UNDEF, 0);
    const char *base = strrchr(o->source_name, '/');
    symbol_entry(&syms, add_string(&str, base ? base + 1 : o->source_name), STB_LOCAL, STT_FILE, SHN_ABS, 0);
    int nsym = 2;
    for (int s = 0; s < ns; s++, nsym++) symbol_entry(&syms, 0, STB_LOCAL, STT_SECTION, (uint16_t)(s + 1), 0);
    for (int k = 0; k < o->nsymbols; k++) {
        const Symbol *sym = &o->symbols[k];
        elf_index[k] = -1;
        if (sym->defined && !sym->global && strncmp(sym->name, "..@", 3) != 0) {
            symbol_entry(&syms, add_string(&str, sym->name), STB_LOCAL, STT_NOTYPE,
                         (uint16_t)(sym->section + 1), sym->value);
            elf_index[k] = nsym++;
        }
    }
    int first_global = nsym;
    for (int k = 0; k < o->nsymbols; k++) {
        const Symbol *sym = &o->symbols[k];
        if (sym->defined && sym->global) {
            symbol_entry(&syms, add_string(&str, sym->name), STB_GLOBAL, STT_NOTYPE,
                         (uint16_t)(sym->section + 1), sym->value);
            elf_index[k] = nsym++;
        } else if (!sym->defined && sym->external && sym->referenced) {
            symbol_entry(&syms, add_string(&str, sym->name), STB_GLOBAL, STT_NOTYPE, SHN_UNDEF, 0);
            elf_index[k] = nsym++;
        }
    }

    Bytes out = {0};
    SectionHeader *sh = calloc((size_t)nheaders, sizeof(SectionHeader));
    // ELF header (completed at the end)
    for (int k = 0; k < 64; k++) bytes_byte(&out, 0);

    for (int s = 0; s < ns; s++) {
        const Section *sec = &o->sections[s];
        SectionHeader *h = &sh[s + 1];
        h->name = add_string(&shstr, sec->name);
        h->type = sec->nobits ? SHT_NOBITS : SHT_PROGBITS;
        h->flags = SHF_ALLOC | (sec->write ? SHF_WRITE : 0) | (sec->exec ? SHF_EXEC : 0);
        h->align = sec->align ? sec->align : 1;
        h->size = sec->size;
        if (!sec->nobits) {
            pad_to(&out, h->align);
            h->offset = out.len;
            bytes_put(&out, sec->data.data, sec->data.len);
        } else {
            h->offset = out.len;
        }
    }
    sh[note_idx].name = add_string(&shstr, ".note.GNU-stack");
    sh[note_idx].type = SHT_PROGBITS;
    sh[note_idx].align = 1;
    sh[note_idx].offset = out.len;

    int r = note_idx + 1;
    for (int s = 0; s < ns; s++) {
        const Section *sec = &o->sections[s];
        if (!sec->nrelocs) continue;
        char name[256];
        snprintf(name, sizeof(name), ".rela%s", sec->name);
        SectionHeader *h = &sh[r++];
        h->name = add_string(&shstr, name);
        h->type = SHT_RELA;
        h->flags = SHF_INFO_LINK;
        h->link = (uint32_t)symtab_idx;
        h->info = (uint32_t)(s + 1);
        h->align = 8;
        h->entsize = 24;
        pad_to(&out, 8);
        h->offset = out.len;
        for (int k = 0; k < sec->nrelocs; k++) {
            const Reloc *rel = &sec->relocs[k];
            const Symbol *sym = &o->symbols[rel->sym];
            uint64_t index;
            int64_t addend = rel->addend;
            if (sym->defined) {
                index = 2 + (uint64_t)sym->section;                 // the section's symbol
                addend += (int64_t)sym->value;
            } else {
                index = (uint64_t)elf_index[rel->sym];
            }
            le64(&out, rel->offset);
            le64(&out, (index << 32) | (uint32_t)rel->type);
            le64(&out, (uint64_t)addend);
        }
        h->size = out.len - h->offset;
    }

    SectionHeader *h = &sh[symtab_idx];
    h->name = add_string(&shstr, ".symtab");
    h->type = SHT_SYMTAB;
    h->link = (uint32_t)strtab_idx;
    h->info = (uint32_t)first_global;
    h->align = 8;
    h->entsize = 24;
    pad_to(&out, 8);
    h->offset = out.len;
    h->size = syms.len;
    bytes_put(&out, syms.data, syms.len);

    h = &sh[strtab_idx];
    h->name = add_string(&shstr, ".strtab");
    h->type = SHT_STRTAB;
    h->align = 1;
    h->offset = out.len;
    h->size = str.len;
    bytes_put(&out, str.data, str.len);

    h = &sh[shstrtab_idx];
    h->name = add_string(&shstr, ".shstrtab");
    h->type = SHT_STRTAB;
    h->align = 1;
    h->offset = out.len;
    h->size = shstr.len;
    bytes_put(&out, shstr.data, shstr.len);

    pad_to(&out, 8);
    uint64_t shoff = out.len;
    for (int k = 0; k < nheaders; k++) {
        le32(&out, sh[k].name);
        le32(&out, sh[k].type);
        le64(&out, sh[k].flags);
        le64(&out, sh[k].addr);
        le64(&out, sh[k].offset);
        le64(&out, sh[k].size);
        le32(&out, sh[k].link);
        le32(&out, sh[k].info);
        le64(&out, sh[k].align);
        le64(&out, sh[k].entsize);
    }

    // ELF header
    Bytes eh = {0};
    static const uint8_t ident[16] = { 0x7F, 'E', 'L', 'F', 2 /* 64-bit */, 1 /* little-endian */, 1 };
    bytes_put(&eh, ident, 16);
    le16(&eh, 1);                       // ET_REL
    le16(&eh, 62);                      // EM_X86_64
    le32(&eh, 1);
    le64(&eh, 0);                       // entry
    le64(&eh, 0);                       // program headers
    le64(&eh, shoff);
    le32(&eh, 0);                       // flags
    le16(&eh, 64);                      // header size
    le16(&eh, 0);
    le16(&eh, 0);
    le16(&eh, 64);                      // section header size
    le16(&eh, (uint16_t)nheaders);
    le16(&eh, (uint16_t)shstrtab_idx);
    memcpy(out.data, eh.data, 64);

    *data = out.data;
    *size = out.len;
    free(eh.data);
    free(shstr.data);
    free(str.data);
    free(syms.data);
    free(sh);
    free(elf_index);
    return true;
}

bool asm_write_elf64(const AsmObject *o, const char *path) {
    uint8_t *data;
    size_t size;
    if (!asm_elf64_image(o, &data, &size)) return false;
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(data, 1, size, f) == size;
    if (f && fclose(f) != 0) ok = false;
    free(data);
    return ok;
}
