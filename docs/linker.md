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

## Windows (PE)

With `-t x86_64-pc-windows-msvc` the linker writes a PE32+ console
executable from the same ELF objects:

| Section | Contents |
|---|---|
| headers | DOS header, PE signature, COFF and optional headers, section table (0x400 bytes) |
| `.text` | code (execute, read) |
| `.rdata` | read-only data |
| `.data` | data, then the bss as uninitialized memory at its end |
| `.idata` | imports: the directory, lookup and address tables, hint/name entries, `KERNEL32.dll` |

The image is based at 0x400000 with no base relocations (relocations
stripped), so the absolute 32-bit addresses of the generated code stay valid;
the sections are aligned to 0x1000 in memory and 0x200 in the file. A
reference to an undefined symbol `__imp_<name>` imports the function `<name>`
from `KERNEL32.dll`: it resolves to the function's slot in the import address
table, which the Windows loader fills. The stack reserve is 8 MiB. Objects
from `-c` stay ELF: cobalt links them for either system.

## The embedded runtime

The build (Makefile) assembles the runtime with the first-stage cobalt
(`cobalt as`; the first stage is cobalt without runtimes, `main.c` built with
`COBALT_STAGE0`), then `cobalt embed` writes C source of a table
`cobalt_runtime_<target>` of `{data, size}` pairs ending with `{0, 0}`, which
is compiled and linked into the final cobalt (C, so any compiler builds
cobalt for any host: `make windows` cross-builds `bin/cobalt.exe` with
MinGW-w64). Each target gets its own
table, so a cobalt built for several targets carries each target's runtime.
A cobalt without the table (the first stage) still produces assembly (`-A`)
and objects (`-c`).
