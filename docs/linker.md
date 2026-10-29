# The COBALT linker

cobalt links programs itself (`include/link.h`, `src/link/link.c`): the
program's object, assembled in memory, and the runtime's objects, which are
embedded in the cobalt binary when it is built. `cobalt -i prog.cbl -o prog`
therefore needs no assembler, linker or archiver on the system.

## What it handles

- Input: ELF64 x86-64 relocatable objects (cobalt's own, or nasm's).
- Library objects (the runtime) are included only when they define a symbol
  that is still undefined, as members of an archive are.
- Relocations: `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_PLT32` (treated as
  PC32: there is no dynamic linking), `R_X86_64_32`, `R_X86_64_32S`, with
  range checks.
- Symbols: local, global and weak; a duplicate global definition, an
  undefined reference and a missing entry symbol are errors.

## Output

A static, non-PIE Linux executable:

| Part | Address | Contents |
|---|---|---|
| Read/execute segment | 0x400000 | ELF and program headers, code (`.text`), read-only data (`.rodata`) |
| Read/write segment | next page | `.data`, then `.bss` (memory only) |
| `GNU_STACK` | | non-executable stack |

Sections of each kind are concatenated in object order with their
alignment. The generated code uses absolute 32-bit addresses for data, so
the program must fit below 2 GiB. Unless `--strip` is given, the executable
has section headers and a symbol table (every label of the objects), so
`gdb` and `objdump` show names.

## The embedded runtime

The build (Makefile) assembles the runtime with the first-stage cobalt
(`cobalt as`), writes a file that includes each object with `incbin` behind a
table `cobalt_runtime_<target>` of `{data, size}` pairs ending with `{0, 0}`,
assembles it and links it into the final cobalt. Each target gets its own
table, so a cobalt built for several targets carries each target's runtime.
A cobalt without the table (the first stage) still produces assembly (`-A`)
and objects (`-c`).
