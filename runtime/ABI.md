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

- Procedures (sections and paragraphs) are laid out in source order and
  entered by jumps, so control falls through from one procedure to the next.
  Each procedure has a return slot `R_<n>` in `.bss`. `PERFORM A THRU B`
  saves B's slot on the stack, points it at the return label and jumps to A;
  the end of every procedure jumps through its slot when it is set. This
  supports GO TO, PERFORM THRU, sections and nested PERFORMs, as in IBM
  compilers. Leaving a PERFORM range with GO TO leaves its saved slot on the
  stack, which the standard permits.
- `rsp` is 16-byte aligned everywhere: `_start` is entered aligned and every
  value generated code pushes takes a 16-byte slot, so every runtime call is
  made with an aligned stack.
- `rbx` holds the `PERFORM ... TIMES` counter. Each loop saves the previous
  value in a stack slot before it starts and restores it at the end, so loops
  nest.
- Pending numeric values (the numeric stack machine, section 6) live on the
  machine stack in 16-byte slots, which keeps `rsp` aligned.
- Two bytes in the program's `.bss` track size errors: `cobrt_tgt_err`
  collects errors while one receiver's value is computed, `cobrt_stmt_err`
  collects them for the whole statement (for ON SIZE ERROR).
- Data lives in records: every 01 or 77 item, together with any 01 that
  redefines it, is one block `D_<n>` in `.bss`, and subordinate items are at
  fixed offsets in it (`D_3+12`). Index-names (INDEXED BY) are 8-byte binary
  occurrence numbers in records of their own.
- A field whose address depends on subscripts or reference modifiers is
  addressed through an address slot `A_<k>` (and, for a length known only at
  run time, a length slot `N_<k>`) in `.bss`, computed just before use.
  Subscripts are not range-checked at run time; constant subscripts out of
  range are compile-time errors.
- Other labels derived from COBOL names (literals) are upper-cased and
  prefixed with `c_`, because COBOL names are case-insensitive and may collide
  with assembler reserved words such as `DS`.
- Any other value that must survive a runtime call lives in a callee-saved
  register or on the stack.

x86_64 runtime code uses `rdi` and `rsi` as scratch registers (for
`rep movsb` and similar). They are caller-saved under SysV but callee-saved
under Win64, so a Win64 port must save them in each routine's prologue.

## 3. Runtime routines

The authoritative list is `runtime/routines.def`. The compiler generates its
`extern` declarations from it, and `make check-runtime` checks that a runtime
exports every routine. Signatures below use: `ptr` for a field or literal
address, `size` and `len` in bytes, `usage` for the usage code and `flags`
for the numeric flags (both in section 5), `scale` for the number of digits
after the implied decimal point, and `digits` for the number of digit
positions in the PICTURE.

| Routine | Arguments | Behavior |
|---|---|---|
| `runtime_init` | none | Called once at program start. |
| `runtime_exit` | none | Ends the program with status 0. Does not return. |
| `runtime_display_string` | `ptr` (NUL-terminated) | Writes a literal to stdout. |
| `runtime_display_numeric` | `ptr, size, scale, usage, flags` | Writes a numeric field: no leading zeros (one zero before the point), all `scale` decimals, a leading `-` when negative. |
| `runtime_display_var_string` | `ptr, size, usage` | Writes an alphanumeric field as is (`size` bytes). |
| `runtime_display_fill` | `char, size, usage` | Writes `size` copies of `char` (figurative constants). |
| `runtime_display_newline` | none | Ends the DISPLAY line. |
| `runtime_move_alphanum` | `dest, src, src_len, dest_size, justified` | Left-justified copy, space padded, truncated on the right. With `justified` non-zero (JUSTIFIED RIGHT): right-justified, space padded on the left, truncated on the left. |
| `runtime_move_numeric` | `dest, src_digits, src_len, dest_size, usage` | Right-justified digit copy, zero padded, for an alphanumeric source moved to an unsigned integer DISPLAY field. Requires `src_len <= dest_size`. |
| `runtime_move_fill` | `dest, char, size, usage` | Fills a field with `char`. |
| `runtime_replicate` | `ptr, size, count` | Copies the first `size` bytes at `ptr` over the following `count - 1` occurrences (INITIALIZE of a table). |
| `runtime_move_pattern` | `ptr, size, pattern, pattern_len` | Fills a field with a pattern repeated (MOVE ALL "literal"). |
| `runtime_compare_pattern` | `ptr, len, pattern, pattern_len` | Compares a field with a pattern repeated. Returns -1, 0 or 1. |
| `runtime_file_open` | `fcb, mode` | OPEN (section 7). Returns the status code. |
| `runtime_file_close` | `fcb` | CLOSE. Returns the status code. |
| `runtime_file_read` | `fcb, area` | READ the next record into the record area. Returns the status code. |
| `runtime_file_write` | `fcb, record, length, adv_mode, adv_lines` | WRITE, with ADVANCING (section 7). Returns the status code. |
| `runtime_file_rewrite` | `fcb, record, length` | REWRITE the record last read. Returns the status code. |
| `runtime_accept` | `dest, size, decimals, usage, pad_char` | Reads one line from stdin. `pad_char` `'0'`: right-justify and zero pad; otherwise left-justify and space pad. Excess input is discarded, `\r` is ignored, EOF ends the line. |
| `runtime_accept_numeric` | `ptr, size, digits, scale, usage, flags` | Reads one line and stores it as a number: optional spaces, optional sign, digits and an optional point; the first other character ends the number; extra decimals are dropped. |
| `runtime_num_load` | `ptr, size, usage, flags` | Returns the field's value as an unscaled signed integer in `RET0`. |
| `runtime_num_store` | `ptr, size, digits, usage, flags, value` | Stores an unscaled value (already at the field's scale). Returns 1 in `RET0` on size error, after truncating, or without storing when `NUM_KEEP` is set. |
| `runtime_num_rescale` | `value, shift, rounded` | `value * 10^shift`; a negative shift truncates, or rounds half away from zero when `rounded` is non-zero. `RET1` = 1 on overflow. |
| `runtime_num_mul` | `a, b, shift` | `(a * b) / 10^shift` with a 128-bit product, truncated. `RET1` = 1 on overflow. |
| `runtime_num_div` | `a, b, shift` | `(a * 10^shift) / b` with a 128-bit dividend, truncated. `RET1` = 1 on overflow or division by zero. |
| `runtime_compare_alnum` | `a, a_len, b, b_len` | Compares in the ASCII collating sequence, padding the shorter operand with spaces. Returns -1, 0 or 1. |
| `runtime_compare_fill` | `ptr, len, char` | Compares a field with a figurative constant (`char` repeated): ZERO `'0'`, SPACE `' '`, HIGH-VALUE `0xFF`, LOW-VALUE `0x00`, QUOTE `'"'`. Returns -1, 0 or 1. |
| `runtime_class_test` | `ptr, len, class` | Returns 1 if every character is in the class: 0 NUMERIC (digits), 1 NUMERIC signed DISPLAY (trailing overpunch allowed), 2 ALPHABETIC, 3 ALPHABETIC-LOWER, 4 ALPHABETIC-UPPER (letters and spaces), 5 NUMERIC packed decimal (digit nibbles, sign C, D or F). |

`RET1` is the second return register (`rdx` under SysV).

## 4. OS layer interface

Implemented per OS in `runtime/<arch>/<os>/os.asm`, declared in
`runtime/<arch>/runtime.inc`. These are internal to the runtime: generated code
never calls them. The `cobrt_` prefix keeps them apart from COBOL names.

| Routine | Arguments | Returns |
|---|---|---|
| `cobrt_os_write` | `fd, buf, len` | bytes written, or negative on error |
| `cobrt_os_read` | `fd, buf, len` | bytes read, 0 at EOF, negative on error |
| `cobrt_os_exit` | `status` | does not return |
| `cobrt_os_open` | `path, mode` | handle, or `OS_ERR_NOT_FOUND` (-1) / `OS_ERR_OTHER` (-2) |
| `cobrt_os_close` | `handle` | 0, or negative on error |
| `cobrt_os_seek` | `handle, offset` | new position (offset relative to the current one), or negative |

`cobrt_os_open` modes are OS independent: `OS_OPEN_READ` (0, existing file),
`OS_OPEN_WRITE` (1, create or truncate), `OS_OPEN_READWRITE` (2, existing),
`OS_OPEN_APPEND` (3, create if missing) and `OS_OPEN_RW_CREATE` (4).

File descriptors are 0 (stdin), 1 (stdout) and 2 (stderr), exposed as
`COBRT_STDIN`, `COBRT_STDOUT` and `COBRT_STDERR`. An OS layer without numeric descriptors maps them to its own
handles.

## 5. Data layout

The compiler decides storage layout, not the runtime, and it is the same on
every target. A given COBOL program produces the same bytes in memory and the
same output on every target. The one deliberate exception is COMP-5 (native
binary).

Usage codes passed to the runtime: 0 = DISPLAY, 1 = COMP (also COMP-4 and
BINARY), 2 = COMP-3 (also PACKED-DECIMAL), 3 = COMP-5. Numeric flags:
`NUM_SIGNED` (1) when the PICTURE has `S`, `NUM_KEEP` (2) when the statement
has ON SIZE ERROR and the receiver must stay unchanged on size error.

Character data is ASCII, one byte per character position. Bytes outside ASCII
(for example UTF-8 in literals) are copied unchanged and count as one position
per byte. EBCDIC is not supported.

### Alphanumeric (`PIC X`) [implemented]

`n` bytes, left-justified, padded with spaces (right-justified with
JUSTIFIED RIGHT).

### Groups and tables [implemented]

A group item is the concatenation of its subordinate items with no padding
(SYNCHRONIZED is accepted and ignored) and is alphanumeric. A REDEFINES item
starts at the offset of the item it redefines; the group is as long as its
longest member. An item with `OCCURS n` takes `n` consecutive copies of its
size; element `(i, j, ...)` is at the first occurrence's offset plus
`(i-1) * outer size + (j-1) * inner size ...`.

### Numeric DISPLAY (`PIC 9`, `S`, `V`)

- One ASCII digit `'0'` to `'9'` per byte, most significant first. Size =
  number of digit positions. [implemented]
- The implied decimal point (`V`) is not stored. The scale (digits after `V`)
  is a compile-time property passed to the runtime. [implemented]
- **Signed fields (`S`) use a trailing overpunch** in the last digit's byte,
  the default IBM position. [implemented] Encoding (the ASCII convention used by
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
  sign for positive ones. [implemented]

### Binary (`COMP`, `COMP-4`, `BINARY`) [implemented]

Two's complement, **big-endian on every target**: the standard and IBM
behavior, and it keeps binary data files portable between targets. Size by
digit count, as `sema.c` already allocates:

| Digits | Bytes |
|---|---|
| 1 to 4 | 2 |
| 5 to 9 | 4 |
| 10 to 18 | 8 |

The value is limited by the PIC digits (for example `PIC 9(3) COMP` holds 0 to
999), not by the byte size.

### Native binary (`COMP-5`) [implemented]

Same sizes, two's complement, **native byte order of the target**. The value
range is the full binary range of the size, not the PIC digits. Meant for
interfacing with non-COBOL code.

### Packed decimal (`COMP-3`, `PACKED-DECIMAL`) [implemented]

Two digits per byte, high nibble first, with the sign in the low nibble of the
last byte: `0xC` positive, `0xD` negative, `0xF` unsigned. Size =
`digits / 2 + 1` bytes (integer division). On input `0xA`, `0xE` are also
accepted as positive and `0xB` as negative.

## 6. Numeric core [implemented]

All arithmetic, numeric MOVE, VALUE initialization, numeric comparison and
PERFORM counts go through a single representation:

- A value is a **signed 64-bit integer scaled by a power of ten**:
  `value = n * 10^-scale`. The scale of every value is known at compile time.
- Maximum precision is **18 digits**, the COBOL-85 limit, which always fits in
  64 bits. The compiler rejects PICTUREs with more digits.
- `runtime_num_load` and `runtime_num_store` convert between storage and the
  core, so each usage (DISPLAY, COMP, COMP-3, COMP-5) touches only them.
- Generated code is a **stack machine** (`IR_NUM_*` in `include/ir.h`): push
  fields and literals, rescale, add, subtract, multiply, divide, negate, then
  store or compare. Add, subtract and negate are inline instructions; scaling,
  multiplication and division call the runtime.
- **Scale alignment is done by the compiler**: operands of `+`, `-` and
  comparisons are brought to the larger scale before the operation.
- **Working scale W** = largest scale among the operands and the receiver,
  plus 2 (at most 18). Products are cut back to W decimals, quotients are
  computed to W decimals, so `ROUNDED` always sees at least one digit beyond
  the receiver's scale.
- Multiplication and division use a 128-bit intermediate (`rdx:rax` on
  x86_64), so `a * b` and the scaling before a division do not overflow before
  the result is truncated. Other arches must give the same results.
- **Truncation (default):** extra decimal digits are dropped and high-order
  digits that do not fit are lost, as the standard requires.
- **ROUNDED:** round half away from zero.
- **Size error:** set when high-order digits would be lost, on overflow of an
  intermediate result, or on division by zero. An overflow or division by
  zero always leaves the receiver unchanged. Lost high-order digits leave it
  unchanged with `ON SIZE ERROR`, and store the truncated value without it.
- Unsigned receivers store the absolute value of a negative result (standard
  behavior).
- Every arithmetic statement is computed as one expression per receiver:
  `ADD a b TO x` stores `x + a + b` into `x`, `DIVIDE a INTO b GIVING q
  REMAINDER r` stores `b / a` into `q` and then `b - q * a` into `r`.
- Exponentiation (`**`) supports integer literal exponents from 0 to 18.

## 7. Sequential files [implemented]

Every file named in a SELECT has a 64-byte file control block (FCB) in the
program's `.data`, labelled `F_<n>`, that generated code passes to the file
routines:

| Offset | Field | Contents |
|---|---|---|
| 0 | handle | OS handle while the file is open |
| 8 | mode | 0 closed, 1 INPUT, 2 OUTPUT, 3 I-O, 4 EXTEND |
| 16 | size | record area size (the longest record description) |
| 24 | flags | 1 LINE SEQUENTIAL, 2 OPTIONAL |
| 32 | name | NUL-terminated file name (from ASSIGN) |
| 40 | status | address of the 2-character FILE STATUS item, or 0 |
| 48 | state | runtime state bits (AT END reached, last READ succeeded, text line open, OPTIONAL file absent) |
| 56 | reserved | |

All record descriptions of one FD share one record area. Each file routine
returns the I-O status code and stores it as two digits in the FILE STATUS
item, if there is one. A status of 30 or more on a file without a FILE STATUS
item ends the run with a message on stderr and exit status 1.

| Status | Meaning |
|---|---|
| 00 | success |
| 05 | OPEN of an OPTIONAL file that does not exist |
| 10 | AT END |
| 30 | other I-O error |
| 35 | OPEN INPUT / I-O of a file that does not exist |
| 41 | OPEN of an open file |
| 42 | CLOSE of a file that is not open |
| 43 | REWRITE without a successful READ |
| 46 | READ after AT END |
| 47 | READ of a file not open INPUT or I-O |
| 48 | WRITE to a file not open OUTPUT or EXTEND |
| 49 | REWRITE of a file not open I-O |

**File formats.** ORGANIZATION SEQUENTIAL (record sequential, the default)
files hold fixed-length records of the record area size with no separators;
a short last record reads space padded. LINE SEQUENTIAL files, and any file
written with ADVANCING, are text: each record is written without trailing
spaces and ends with a newline; READ fills the record area from one line and
pads it with spaces.

**ADVANCING** follows printer semantics on text: `AFTER n` moves down `n`
lines and then prints (the line stays open), `BEFORE n` prints and then moves
down `n` lines, `PAGE` writes a form feed. WRITE without ADVANCING is
`BEFORE 1`. A printer would overprint when `BEFORE` follows `AFTER` on the
same line; in a text file the record starts a new line instead. CLOSE ends an
open line.

Not yet supported: RELATIVE and INDEXED organization, ACCESS RANDOM and
DYNAMIC, LINAGE, SD and SORT/MERGE, USE declaratives, and ASSIGN to a data
item.
