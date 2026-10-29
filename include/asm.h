/* include/asm.h */
#ifndef COBALT_ASM_H
#define COBALT_ASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The COBALT assembler: the subset of NASM syntax that the compiler and the
// runtime use (docs/assembler.md), assembled into an x86-64 ELF relocatable
// object. It replaces nasm, which is not available on every system.

typedef struct AsmObject AsmObject;

typedef struct {
    const char *const *include_dirs;    // NULL-terminated, searched by %include
    const char *const *defines;         // NULL-terminated "NAME" or "NAME=value"
} AsmOptions;

// Assemble source text. Reports errors on stderr as file:line and returns
// NULL when there were any.
AsmObject *asm_assemble(const char *source, const char *filename, const AsmOptions *opts);

// Write the object as an ELF64 relocatable file; false on an I/O error.
bool asm_write_elf64(const AsmObject *obj, const char *path);

// The same ELF64 relocatable image in memory (malloc'd, for the linker)
bool asm_elf64_image(const AsmObject *obj, uint8_t **data, size_t *size);

// Files read through %include, NULL-terminated (for dependency files)
const char *const *asm_included_files(const AsmObject *obj);

void asm_free(AsmObject *obj);

// `cobalt as`: the assembler's command line (nasm's options: -f elf64,
// -D NAME[=value], -I dir/, -MD depfile, -MP, -o out.o). argv[0] is "as".
int asm_command(int argc, char **argv);

#endif
