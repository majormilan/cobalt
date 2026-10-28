# COBALT runtime ABI

This document is the contract between the COBALT compiler and every target
runtime. A runtime for a new target is correct when it implements everything
here; generated code may rely on nothing else.

Status markers: **[implemented]** matches the current code, **[planned]** is a
decision already taken that the code does not implement yet. Code written from
now on must follow the planned layouts, not invent new ones.

## 1. Targets and runtime layout

Each target triple maps to three parts (see `TargetInfo` in
`include/target.h` and the `TARGET` mapping in the `Makefile`, which must stay
in sync):

| Part | Location | Contains |
|---|---|---|
| Arch layer | `runtime/<arch>/common/` | All COBOL semantics: DISPLAY formatting, MOVE, arithmetic, ACCEPT. No OS calls. |
| ABI include | `runtime/<arch>/abi/<abi>.inc` | Register names `ARG0..ARG5`, `RET0` for the calling convention. |
| OS layer | `runtime/<arch>/<os>/os.asm` | The `cobrt_os_*` primitives (section 4). The only place that talks to the OS. |

| Triple | Arch | OS | ABI | nasm `-f` | Entry symbol | Status |
|---|---|---|---|---|---|---|
| `x86_64-linux-elf` | x86_64 | linux | SysV | `elf64` | `_start` | [implemented] |
| `x86_64-pc-windows-msvc` | x86_64 | windows | Win64 | `win64` | `mainCRTStartup` | [planned] |

The runtime is built as a static library, `obj/runtime/<triple>/libcobrt.a`,
so a program links only the routines it uses.

### Adding a target

1. Add a row to `target_infos` in `src/target.c` and a backend in
   `src/backend/backend.c`.
2. Add the triple to the `TARGET` mapping in the `Makefile` and in
   `test_runner.sh`.
3. New OS on an existing arch: write `runtime/<arch>/<os>/os.asm` (and an ABI
   include if the calling convention differs). New arch: write the whole arch
   layer as well.
4. `make test TARGET=<triple>` must pass with the same `tests/*.expected` files.
   Expected output is target-independent by design (section 5).

## 2. Calling convention

Runtime routines are called like C functions in the target's native ABI
(x86_64 Linux: System V AMD64). All integer arguments are passed as full
64-bit values. Arch-layer code reads arguments only through `ARG0..ARG5` and
returns values in `RET0`, so it can be assembled for another ABI on the same
architecture.

Rules for runtime code:

- Callee-saved registers (SysV: `rbx`, `rbp`, `r12` to `r15`) must be
  preserved. Caller-saved registers may be clobbered.
- The direction flag is clear on entry and must be clear on return.
- A routine must not assume any particular stack alignment on entry. If it
  calls into a system library it must realign the stack itself (this applies
  mainly to OS layers).

Rules for generated code (x86_64, see `src/backend/x86_64-linux-elf.c`):

- `rsp` is 16-byte aligned throughout `_start` and every paragraph body, so
  every runtime call is made with an aligned stack. `_start` is entered
  aligned; a paragraph call subtracts 8 from `rsp` before `call` so the
  paragraph is entered the same way.
- Paragraphs are subroutines (`call` / `ret`) and PERFORMs nest, so any value
  that must survive a paragraph call is saved on the stack.
- `rbx` holds the `PERFORM ... TIMES` counter. Each loop pushes the previous
  value before it starts and pops it at the end; the pushed value also serves
  as the 8-byte call padding.
- `r12` holds the left operand of a comparison across the runtime call that
  loads the right one. It is never live across a paragraph call.
- Any other value that must survive a runtime call lives in a callee-saved
  register or on the stack.

## 3. Runtime routines

The authoritative list is `runtime/routines.def`. The compiler generates its
`extern` declarations from it, and `make check-runtime` checks that a runtime
exports every routine. Signatures below use: `ptr` for a field or literal
address, `size` and `len` in bytes, `usage` as the `AstUsageType` value
(0 = DISPLAY, 1 = COMP).

| Routine | Arguments | Behavior |
|---|---|---|
| `runtime_init` | none | Called once at program start. |
| `runtime_exit` | none | Ends the program with status 0. Does not return. |
| `runtime_display_string` | `ptr` (NUL-terminated) | Writes a literal to stdout. |
| `runtime_display_numeric` | `ptr, size, decimals, usage` | Writes a numeric field: inserts the decimal point, drops leading zeros (keeps one zero before the point). |
| `runtime_display_var_string` | `ptr, size, usage` | Writes an alphanumeric field as is (`size` bytes). |
| `runtime_display_fill` | `char, size, usage` | Writes `size` copies of `char` (figurative constants). |
| `runtime_display_newline` | none | Ends the DISPLAY line. |
| `runtime_move_alphanum` | `dest, src, src_len, dest_size, usage` | Left-justified copy, space padded, truncated on the right. |
| `runtime_move_numeric` | `dest, src_digits, src_len, dest_size, usage` | Right-justified digit copy, zero padded. Requires `src_len <= dest_size`. |
| `runtime_move_fill` | `dest, char, size, usage` | Fills a field with `char`. |
| `runtime_accept` | `dest, size, decimals, usage, pad_char` | Reads one line from stdin. `pad_char` `'0'`: right-justify and zero pad; otherwise left-justify and space pad. Excess input is discarded, `\r` is ignored, EOF ends the line. |
| `runtime_load_int` | `ptr, size` | Returns the unsigned DISPLAY field as an integer in `RET0`. |
| `runtime_add_to` | `dest, dest_size, src, src_size` | `dest += src` |
| `runtime_sub_from` | same | `dest -= src`, clamped at 0 |
| `runtime_mul_by` | same | `dest *= src` |
| `runtime_div_into` | same | `dest /= src` (integer); unchanged if `src` is 0 |
| `runtime_mod_into` | same | `dest %= src`; unchanged if `src` is 0 |

All arithmetic routines are currently [implemented] for unsigned integer
DISPLAY fields only. High-order digits that do not fit are silently lost.
Phase 1 replaces their internals with the numeric core in section 6.

## 4. OS layer interface

Implemented per OS in `runtime/<arch>/<os>/os.asm`, declared in
`runtime/<arch>/runtime.inc`. These are internal to the runtime: generated code
never calls them. The `cobrt_` prefix keeps them apart from COBOL names.

| Routine | Arguments | Returns |
|---|---|---|
| `cobrt_os_write` | `fd, buf, len` | bytes written, or negative on error |
| `cobrt_os_read` | `fd, buf, len` | bytes read, 0 at EOF, negative on error |
| `cobrt_os_exit` | `status` | does not return |

File descriptors are 0 (stdin) and 1 (stdout), exposed as `COBRT_STDIN` and
`COBRT_STDOUT`. An OS layer without numeric descriptors maps them to its own
handles.

## 5. Data layout

The compiler decides storage layout, not the runtime, and it is the same on
every target. A given COBOL program produces the same bytes in memory and the
same output on every target. The one deliberate exception is COMP-5 (native
binary).

Character data is ASCII, one byte per character position. Bytes outside ASCII
(for example UTF-8 in literals) are copied unchanged and count as one position
per byte. EBCDIC is not supported.

### Alphanumeric (`PIC X`) [implemented]

`n` bytes, left-justified, padded with spaces.

### Numeric DISPLAY (`PIC 9`, `S`, `V`)

- One ASCII digit `'0'` to `'9'` per byte, most significant first. Size =
  number of digit positions. [implemented]
- The implied decimal point (`V`) is not stored. The scale (digits after `V`)
  is a compile-time property passed to the runtime. [planned: arithmetic and
  MOVE ignore it today]
- **Signed fields (`S`) use a trailing overpunch** in the last digit's byte,
  the default IBM position. [planned] Encoding (the ASCII convention used by
  GnuCOBOL and Micro Focus):

  | Digit | Positive or unsigned | Negative |
  |---|---|---|
  | 0 to 9 | `'0'` to `'9'` | `'p'` to `'y'` (digit + 0x40) |

  On input the runtime also accepts the EBCDIC-derived convention, so
  fields from other systems read correctly: `'{'`, `'A'` to `'I'` for +0 to +9
  and `'}'`, `'J'` to `'R'` for -0 to -9. On output it always writes the
  first convention. Negative zero is stored as positive zero.
- `SIGN LEADING`, `SIGN SEPARATE` and their combinations are [planned] for
  later and will be documented here when implemented.
- DISPLAY of a signed field prints a leading `-` for negative values and no
  sign for positive ones.

### Binary (`COMP`, `COMP-4`, `BINARY`) [planned]

Two's complement, **big-endian on every target**: the standard and IBM
behavior, and it keeps binary data files portable between targets. Size by
digit count, as `sema.c` already allocates:

| Digits | Bytes |
|---|---|
| 1 to 4 | 2 |
| 5 to 9 | 4 |
| 10 to 18 | 8 |

The value is limited by the PIC digits (for example `PIC 9(3) COMP` holds 0 to
999), not by the byte size. Today COMP fields get these sizes but are still
stored as ASCII digits, which truncates them (`tests/usage_test.xfail`).

### Native binary (`COMP-5`) [planned]

Same sizes, two's complement, **native byte order of the target**. The value
range is the full binary range of the size, not the PIC digits. Meant for
interfacing with non-COBOL code.

### Packed decimal (`COMP-3`, `PACKED-DECIMAL`) [planned]

Two digits per byte, high nibble first, with the sign in the low nibble of the
last byte: `0xC` positive, `0xD` negative, `0xF` unsigned. Size =
`digits / 2 + 1` bytes (integer division). On input `0xA`, `0xE` are also
accepted as positive and `0xB` as negative.

## 6. Numeric core [planned]

All arithmetic and numeric MOVE go through a single internal representation:

- A value is a **signed 64-bit integer scaled by a power of ten**:
  `value = n * 10^-scale`. The scale is known at compile time.
- Maximum precision is **18 digits**, the COBOL-85 limit, which always fits in
  64 bits. The compiler rejects PICs with more digits.
- Two routines convert between storage and the core, so each new usage
  (DISPLAY, COMP, COMP-3) touches only them:
  - `load_num(ptr, size, scale, usage, signed)` returns the scaled integer.
  - `store_num(ptr, size, scale, usage, signed, value, value_scale, rounded)`
    stores it and returns a size-error flag.
- **Scale alignment is done by the compiler** where possible (multiplying by a
  constant power of ten), which keeps the runtime small on every arch.
- Multiplication and division use a 128-bit intermediate (`rdx:rax` on
  x86_64), so `a * b` and the scaling before a division do not overflow before
  the result is rounded or truncated. Other arches must give the same results.
- **Truncation (default):** extra decimal digits are dropped and high-order
  digits that do not fit are lost, as the standard requires.
- **ROUNDED:** round half away from zero.
- **Size error:** set when high-order digits would be lost or on division by
  zero. With `ON SIZE ERROR` the target keeps its old value and the handler
  runs. Without it, the truncated result is stored, except after division by
  zero, where the target is left unchanged.
- Unsigned targets store the absolute value of a negative result (standard
  behavior). This replaces today's clamp at 0 in `runtime_sub_from`.
