# The COBALT assembler

cobalt's built-in assembler assembles the subset of NASM syntax that the COBALT compiler
generates and the runtime (`runtime/<arch>/`) is written in, into ELF64
relocatable objects for x86-64. It replaces nasm, which is not available on
every system, and is much faster on the large files the compiler produces.
The assembler (`include/asm.h`, `src/asm/`) is part of cobalt: compiling a
program assembles its generated code in memory, and the linker
(docs/linker.md) makes the executable. `cobalt as` is its command line, used
to build the runtime:

```
cobalt as [-f elf64] [-D NAME[=value]] [-I dir/] [-MD depfile] [-MP] [-o out.o] input.asm
```

The options are nasm's, so nasm can still stand in: `make ASSEMBLER=nasm`
assembles the runtime with nasm, and `ASSEMBLER=nasm` makes the test runner
and the NIST harness build programs with nasm and ld (`cobalt -A`, then nasm
and ld; needs `make runtime-lib`).

## Output

Objects match nasm's: the same code bytes, data, relocations and section
sizes. `tools/asm/compare.sh file.asm...` checks this by assembling with both
and comparing the disassembly, data and section sizes. The one known
difference is in cobalt's favour: it always uses the short (2-byte) form of
a jump that reaches its target, where nasm occasionally keeps a near jump.

Encoding choices (nasm's defaults): the shortest immediate form (sign-extended
8-bit where it fits), the accumulator short forms (`add rax, imm32`),
`mov r64, imm` as `mov r32, imm32` when the value fits in 32 unsigned bits
(else a sign-extended 32-bit or a 64-bit immediate), shifts by 1 without an
immediate, and short jumps where they reach (layout is repeated until no jump
changes).

References to labels of the same object are relocated against the section's
symbol, references to `extern` symbols against the symbol. Relocation types:
`R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_32`, `R_X86_64_32S`. The object has
an empty `.note.GNU-stack` (non-executable stack).

## Source syntax

- **Lines**: `[label:] [instruction or directive] [; comment]`. A label without
  a colon is accepted before `db`/`dw`/`dd`/`dq`, `resb`... and `times`.
  Labels starting with `.` are local to the previous plain label
  (`.loop` after `runtime_accept:` is `runtime_accept.loop`). Labels starting
  with `..@` are not written to the object.
- **Sections**: `section .text|.data|.bss|.rodata|name [align=N] [nobits]`
  (`segment` is a synonym). The default is `.text`. Standard sections get
  nasm's attributes and alignment (.text 16, the others 4); `align N` raises a
  section's alignment.
- **Symbols**: `global name[, ...]`, `extern name[, ...]` (a `:type` suffix is
  ignored), `name equ expression`.
- **Data**: `incbin "file"` (the file's bytes, searched like `%include`),
  `db`, `dw`, `dd`, `dq` with numbers, character constants, strings
  (padded with zeros to the item width) and, for `dd`/`dq`, symbol addresses;
  `resb`, `resw`, `resd`, `resq`; `times count statement`;
  `align N` (filled with `nop` in code and data); `alignb N`.
- **Other directives**: `default rel` / `default abs`, `bits 64`.
- **Preprocessor**: `%define NAME [text]` (text substituted for the name
  outside strings, repeatedly), `%undef`, `%include "file"` (searched next to
  the including file, then in each `-I` directory, which is a prefix as in
  nasm), `%ifdef` / `%ifndef` / `%else` / `%endif`, `%error text`.
  `-D NAME[=value]` defines a name.
- **Expressions**: decimal, `0x` / `h` hexadecimal, `0o` / `q` octal, `0b`
  binary, character constants (`'COB_SWIT'`, little-endian), `$` (the current
  position), symbols, `+ - * / % << >> & | ^ ~`, parentheses. A value may add
  one symbol and subtract one label of the same section (a label difference,
  such as `msg_len equ $ - msg`, known after layout).
- **Operands**: registers (8, 16, 32 and 64-bit, including `spl`, `sil`...
  and `ah`...), immediates, and memory `[base + index*scale + displacement]`
  with an optional size (`byte`, `word`, `dword`, `qword`). `[rel x]` is
  RIP-relative; `[x]` without registers is an absolute 32-bit address
  (static, non-PIE executables). `short`, `near` and `strict` are accepted
  and ignored.

## Instructions

`add or adc sbb and sub xor cmp test mov lea movzx movsx movsxd xchg`,
`inc dec not neg mul imul (1, 2 and 3 operands) div idiv`,
`shl sal shr sar rol ror rcl rcr`, `bt`,
`push pop call jmp jcc setcc cmovcc` (all condition codes and their aliases), `bswap`,
`ret leave cqo cdq cdqe syscall cld std nop hlt`,
`movsb movsq stosb stosq` with `rep` / `repe` / `repne`.

An instruction outside this list, or a form x86-64 does not have, is reported
as an error with the file and line.
