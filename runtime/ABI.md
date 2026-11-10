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
- Pending numeric values (the numeric stack machine, section 6) are tracked
  at compile time: the top ones are constants or live in `r12` to `r15`, the
  rest on the machine stack in 16-byte slots, which keeps `rsp` aligned. At
  every label, jump and non-numeric statement all of them are on the machine
  stack, so every path into a label sees the same stack.
- In a loop whose numeric fields are promoted to registers (section 6),
  `rbp`, `rbx`, `r15` and `r14` hold up to four fields; `rbx` is saved on
  the stack around the loop. Every exit from the loop writes the fields back
  to memory first.
- Paths taken only on errors (stores that do not fit, overflow) are emitted
  out of line, after the program's code.
- Two bytes in the program's `.bss` track size errors: `cobrt_tgt_err`
  collects errors while one receiver's value is computed, `cobrt_stmt_err`
  collects them for the whole statement (for ON SIZE ERROR). They carry
  information only within one statement: the compiler skips writes it can
  prove unnecessary (clearing a byte known to be zero, clearing
  `cobrt_stmt_err` when no ON SIZE ERROR test reads it before the next
  statement clears it again).
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
| `runtime_init` | `entry_sp, settings` | Called once at program start with the stack pointer at program entry (the OS layer finds the environment there) and the program-wide settings of the dialect: bit 0 `RT_SIGN_EBCDIC` (section 5). The runtime keeps them in `cobrt_runtime_flags`. |
| `runtime_exit` | none | Ends the program with status 0. Does not return. |
| `runtime_display_string` | `ptr` (NUL-terminated) | Writes a literal to stdout. |
| `runtime_display_var_string` | `ptr, size, usage` | Writes a field as stored (`size` bytes). DISPLAY uses it for every item: a numeric DISPLAY item shows its stored characters (all digit positions, an overpunched or separate sign, no decimal point); a binary or packed item is first stored as the DISPLAY item of the same PICTURE (sign overpunched on the last digit). With the `gnucobol` dialect (`display=pretty`) the compiler edits numeric items instead (sign, every digit, decimal point) with `runtime_edit_numeric` and writes the result. |
| `runtime_display_fill` | `char, size, usage` | Writes `size` copies of `char` (figurative constants). |
| `runtime_display_newline` | none | Ends the DISPLAY line. |
| `runtime_move_alphanum` | `dest, src, src_len, dest_size, justified` | Left-justified copy, space padded, truncated on the right. With `justified` non-zero (JUSTIFIED RIGHT): right-justified, space padded on the left, truncated on the left. |
| `runtime_move_numeric` | `dest, src_digits, src_len, dest_size, usage` | Right-justified digit copy, zero padded, for an alphanumeric source moved to an unsigned integer DISPLAY field. Requires `src_len <= dest_size`. |
| `runtime_move_fill` | `dest, char, size, usage` | Fills a field with `char`. |
| `runtime_replicate` | `ptr, size, count` | Copies the first `size` bytes at `ptr` over the following `count - 1` occurrences (INITIALIZE of a table). |
| `runtime_move_pattern` | `ptr, size, pattern, pattern_len` | Fills a field with a pattern repeated (MOVE ALL "literal"). |
| `runtime_compare_pattern` | `ptr, len, pattern, pattern_len` | Compares a field with a pattern repeated. Returns -1, 0 or 1. |
| `runtime_edit_numeric` | `ptr, mask, mask_len, value, flags` | Edits a value (at the item's scale) into a numeric edited item (section 8). Returns 1 on size error. |
| `runtime_edit_alnum` | `ptr, src, src_len, mask, mask_len` | Alphanumeric editing: source characters fill the `X` positions, `B 0 /` are inserted. |
| `runtime_deedit` | `ptr, len` | Value of a numeric edited item's digits, negative if it shows `-`, `CR` or `DB` (MOVE numeric edited to numeric). |
| `runtime_file_open` | `fcb, mode` | OPEN (section 7). Returns the status code. |
| `runtime_file_close` | `fcb` | CLOSE. Returns the status code. |
| `runtime_file_read` | `fcb, area` | READ the next record into the record area. Returns the status code. |
| `runtime_file_write` | `fcb, record, length, adv_mode, adv_lines` | WRITE, with ADVANCING (section 7). Returns the status code. |
| `runtime_file_rewrite` | `fcb, record, length` | REWRITE the record last read. Returns the status code. |
| `runtime_accept` | `dest, size, decimals, usage, pad_char` | Reads one line from stdin. `pad_char` `'0'`: right-justify and zero pad; otherwise left-justify and space pad. Excess input is discarded, `\r` is ignored, EOF ends the line. A line of at least `COBRT_CONSOLE_RECORD` (80) characters is a full device record: when it does not fill an alphanumeric field, the next line continues the field. |
| `runtime_accept_date` | `buffer, kind` | Current UTC date or time as digits: 0 DATE `YYMMDD`, 1 DAY `YYDDD`, 2 TIME `HHMMSShh`, 3 DAY-OF-WEEK (1 Monday to 7 Sunday). |
| `runtime_accept_numeric` | `ptr, size, digits, scale, usage, flags` | Reads one line and stores it as a number: optional spaces, optional sign, digits and an optional point; the first other character ends the number; extra decimals are dropped. |
| `runtime_num_load` | `ptr, size, usage, flags` | Returns the field's value as an unscaled signed integer in `RET0`. |
| `runtime_num_store` | `ptr, size, digits, usage, flags, value` | Stores an unscaled value (already at the field's scale). Returns 1 in `RET0` on size error, after truncating, or without storing when `NUM_KEEP` is set. |
| `runtime_packed_load` | `ptr, size` | Value of a COMP-3 item of 1 to 10 bytes in `RET0` (as `runtime_num_load`). Changes only caller-saved registers. |
| `runtime_packed_store` | `ptr, size, value, flags` | Stores a value into a COMP-3 item of 1 to 10 bytes; the value must fit (below `10^digits`, at most 18 digits). Sign nibble C or D with `NUM_SIGNED`, else F and the magnitude. No result. |
| `runtime_num_to_digits` | `buffer, count, value` | Writes `|value|` as `count` digits, zero padded on the left (the low-order digits when it has more). |
| `runtime_num_rescale` | `value, shift, rounded` | `value * 10^shift`; a negative shift truncates, or rounds half away from zero when `rounded` is non-zero. `RET1` = 1 on overflow. |
| `runtime_num_mul` | `a, b, shift` | `(a * b) / 10^shift` with a 128-bit product, truncated. `RET1` = 1 on overflow. |
| `runtime_num_div` | `a, b, shift` | `(a * 10^shift) / b` with a 128-bit dividend, truncated. `RET1` = 1 on overflow or division by zero. |
| `runtime_compare_alnum` | `a, a_len, b, b_len` | Compares in the program collating sequence (section 10), padding the shorter operand with spaces. Returns -1, 0 or 1. |
| `runtime_compare_fill` | `ptr, len, char` | Compares a field with a figurative constant (`char` repeated): ZERO `'0'`, SPACE `' '`, HIGH-VALUE `0xFF`, LOW-VALUE `0x00`, QUOTE `'"'`. Returns -1, 0 or 1. |
| `runtime_class_test` | `ptr, len, class` | Returns 1 if every character is in the class: 0 NUMERIC (digits), 1 NUMERIC signed DISPLAY (trailing overpunch allowed), 2 ALPHABETIC, 3 ALPHABETIC-LOWER, 4 ALPHABETIC-UPPER (letters and spaces), 5 NUMERIC packed decimal (digit nibbles, sign C, D or F), 6 NUMERIC SIGN LEADING (overpunch on the first digit), 7 NUMERIC SIGN TRAILING SEPARATE, 8 NUMERIC SIGN LEADING SEPARATE, 9 user CLASS (`members, count` follow: every character must be one of the members). |
| `runtime_inspect` | `subject, len, table, entries, counters` | INSPECT TALLYING / REPLACING / CONVERTING (section 9). |
| `runtime_set_collation` | `table` | Sets the program collating sequence: a 256-byte weight table, or 0 for native (section 10). |
| `runtime_switch_status` | `n` | Returns 1 if SWITCH-`n` (1 to 9) is on (section 10). |
| `runtime_switch_set` | `n, on` | SET mnemonic-name TO ON (`on` 1) or OFF (0) for SWITCH-`n`. |
| `runtime_num_pow` | `base, base_scale, exp, exp_scale, scale` | `base ** exp` at `scale` for an exponent known only at run time. The exponent must be an integer (it may be negative). `RET1` = 1 on a size error: a fractional exponent, `0 ** n` with `n <= 0`, or overflow. |
| `runtime_string` | `target, len, table, entries, pointer` | STRING (section 11). Returns the new pointer; `RET1` = 1 on overflow. |
| `runtime_unstring_start` | `state, pointer` | Starts an UNSTRING (section 11). Returns 0 when the pointer is out of range. |
| `runtime_unstring_next` | `state, size` | The next UNSTRING field (section 11). Returns 0 when the sending item is exhausted. |

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
| `cobrt_os_time` | `buffer` (16 bytes) | seconds since 1970-01-01 UTC and nanoseconds, as two qwords |
| `cobrt_os_init` | `entry_sp` | nothing; records what the OS layer needs from the entry stack |
| `cobrt_os_getenv` | `name` (NUL-terminated) | the value (NUL-terminated), or 0 when not set |

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
has ON SIZE ERROR and the receiver must stay unchanged on size error,
`NUM_SIGN_LEADING` (4) and `NUM_SIGN_SEPARATE` (8) for the SIGN clause, and
`NUM_NOTRUNC` (16) for a COMP item limited by its bytes rather than its
PICTURE digits (dialect setting `binary-truncate=no`).

Character data is ASCII, one byte per character position. Bytes outside ASCII
(for example UTF-8 in literals) are copied unchanged and count as one position
per byte. EBCDIC is not supported.

### Alphanumeric (`PIC X`) [implemented]

`n` bytes, left-justified, padded with spaces (right-justified with
JUSTIFIED RIGHT).

### Edited items and P scaling [implemented]

Numeric edited and alphanumeric edited items are stored as characters, one
byte per position of the PICTURE. `P` (scaling) has no position: `99PPP`
stores two digits worth thousands (scale -3) and `PPP99` two digits after
three implied zeros (scale 5).

### Groups and tables [implemented]

A group item is the concatenation of its subordinate items with no padding
(SYNCHRONIZED is accepted and ignored) and is alphanumeric. A REDEFINES item
starts at the offset of the item it redefines; the group is as long as its
longest member. An item with `OCCURS n` takes `n` consecutive copies of its
size; element `(i, j, ...)` is at the first occurrence's offset plus
`(i-1) * outer size + (j-1) * inner size ...`.

`OCCURS min TO max DEPENDING ON item` reserves `max` occurrences; the table
ends its record. A group holding the table has a length known only at run
time (the bytes before the table plus the current occurrences, in a length
slot), except as the receiver of a MOVE when the group also holds the
DEPENDING ON item: then it has its maximum length.

A level-66 RENAMES item covers the storage from the first renamed item to
the end of the THRU item and is a group; renaming one elementary item gives
it that item's attributes.

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

  The EBCDIC-derived convention overpunches both signs: `'{'`, `'A'` to
  `'I'` for +0 to +9 and `'}'`, `'J'` to `'R'` for -0 to -9. The runtime reads
  both conventions, so fields from other systems read correctly, and writes
  the one the dialect selects (`RT_SIGN_EBCDIC` from `runtime_init`: the
  `ibm` dialect, or `sign=ebcdic`). Negative zero is stored as positive zero.
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
  store or compare. The backend runs the stack at compile time (constants
  folded, values in registers) and knows a bound on every value's magnitude:
  an operation that cannot overflow 64 bits gets no overflow check, a value
  that fits its receiver no size check. Add, subtract, negate, and scaling
  or multiplication that cannot overflow are inline instructions; the others
  call the runtime.
- Binary items (COMP, COMP-5 of 2, 4 or 8 bytes) are loaded and stored
  inline; COMP-3 items of up to 10 bytes through `runtime_packed_load` and
  `runtime_packed_store`, which convert 16 digits at a time.
- **Register promotion**: in an innermost loop (a label and a jump back to
  it, entered only by falling into it, with no calls other than numeric
  ones), fields that only the loop's numeric loads and stores touch, whose
  record is not addressed through subscripts there, are kept in registers
  (`src/ir/promote.c` finds them). A store that fits sets the register; one
  that does not takes the normal path to memory (truncation, size error) and
  loads the register again. Decimal fields have a dirty byte, so only a
  field that was stored is written back; this leaves invalid data that the
  loop never replaced untouched.
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

## 8. Editing [implemented]

The compiler turns an edited PICTURE into an edit mask with one symbol per
character position (repeat factors expanded, `V` and `P` dropped):

| Mask | PICTURE | Output |
|---|---|---|
| `9` | `9` | the digit |
| `Z` | `Z` | the digit, or a space while zeros are suppressed |
| `*` | `*` | the digit, or `*` while zeros are suppressed (check protection) |
| `F` `f` | floating `$$..`, `++..`, `--..` | `F` is the leftmost symbol of the floating string, `f` its other (digit) positions; the floating symbol is printed in the last suppressed position |
| `B` `0` `/` `,` | same | inserted (space for `B`); inside a suppressed string replaced by the suppression character |
| `.` | `.` | the decimal point; suppression ends here |
| `S` | fixed `+` | `+` or `-` |
| `M` | fixed `-` | space or `-` |
| `$` | fixed `$` | `$` |
| `c r` / `d b` | `CR` / `DB` | `CR` / `DB` when negative, else spaces |
| `X` | `X` `A` `9` in alphanumeric edited | the next source character |

`runtime_edit_numeric` takes the value already scaled to the item (digits
after the point or `V`) and flags: bit 0 BLANK WHEN ZERO, bit 1 keep the item
on size error, bits 8-15 the floating character, bits 16-23 the CURRENCY SIGN
character (0 for `$`), bit 24 DECIMAL-POINT IS COMMA (`.` and `,` exchange
roles in the output). The mask always uses `.` for the decimal point and `$`
for the currency symbol. Zero suppression ends at the
first non-zero digit, a `9` position or the decimal point. A zero value in an
item without `9` positions is all spaces, or all `*` except the decimal point
with check protection. A numeric DISPLAY item with BLANK WHEN ZERO is edited
with a mask of `9`s.

## 9. INSPECT [implemented]

`runtime_inspect` takes a table of operand entries, 96 bytes each, in source
order:

| Offset | Field |
|---|---|
| 0 | kind: 0 CHARACTERS, 1 ALL, 2 LEADING, 3 FIRST |
| 8 | TALLYING counter index, or -1 for a REPLACING operand |
| 16, 24 | pattern pointer and length (for CHARACTERS: the length is 1) |
| 32 | replacement pointer (pattern length characters) |
| 40, 48 | BEFORE INITIAL delimiter pointer and length, or 0 |
| 56, 64 | AFTER INITIAL delimiter pointer and length, or 0 |
| 72-95 | scratch for the runtime |

`counters` points to one signed qword per TALLYING counter; the runtime adds
the tallies, and the compiler stores the counters back into their items. The
regions are fixed from the BEFORE / AFTER delimiters before the scan. The scan
moves left to right; at each position the first entry (in table order) whose
pattern matches inside its region is applied and the scan skips the matched
characters. A program with both TALLYING and REPLACING calls the routine
twice, tallying first. CONVERTING is compiled as one ALL entry per character
of the first operand (a character listed twice keeps its first replacement).

## 10. SPECIAL-NAMES [implemented]

PROGRAM COLLATING SEQUENCE: the compiler builds a 256-byte weight table from
the alphabet (STANDARD-1 and STANDARD-2 are ASCII, NATIVE is the table being
absent; a literal alphabet gives listed characters ascending weights in order,
ALSO giving equal weights, and unlisted characters follow in native order) and
calls `runtime_set_collation` at program start. Alphanumeric comparisons
compare weights; class tests, INSPECT and moves are not affected.

User CLASS: the compiler emits the list of member characters (THROUGH ranges
expanded) and tests with `runtime_class_test` class 9.

Switches: SWITCH-1 to SWITCH-9. A condition-name for ON STATUS is true when
`runtime_switch_status` returns 1, OFF STATUS when it returns 0. A switch
starts in the state the OS layer reports (on Linux: on when the environment
variable `COB_SWITCH_n` is `ON`) and `runtime_switch_set` changes it (SET
mnemonic-name TO ON / OFF).

SYMBOLIC CHARACTERS are resolved by the compiler: the character at the given
ordinal position (from 1) of the native set or of the named alphabet, used as
a figurative constant.

CURRENCY SIGN and DECIMAL-POINT IS COMMA are resolved by the compiler (PICTURE
analysis and numeric literals) and passed to `runtime_edit_numeric` in its
flags (section 8). DISPLAY UPON a mnemonic name writes to stdout.

## 11. STRING and UNSTRING [implemented]

`runtime_string` takes a table of 32-byte entries, one per sending operand in
source order:

| Offset | Field |
|---|---|
| 0, 8 | sending operand pointer and length |
| 16, 24 | delimiter pointer and length, or 0 for DELIMITED BY SIZE |

Each operand is sent up to the first occurrence of its delimiter (or whole)
into the receiving item from `pointer` (from 1). An out-of-range pointer
sends nothing; a full receiving item with characters left to send stops the
statement. Both are an overflow.

UNSTRING keeps its state in an 80-byte block the compiler sets up:

| Offset | Field |
|---|---|
| 0, 8 | sending item pointer and length |
| 16, 24 | delimiter table pointer and number of delimiters |
| 32 | next position to examine (from 0) |
| 40, 48 | out: the field found (pointer, length) |
| 56, 64 | out: the delimiter found (pointer, length; 0 when none) |
| 72 | overflow flag (set by the compiled code) |

Delimiter table entries are 24 bytes: pointer, length and ALL (1: adjacent
occurrences are one delimiter). `runtime_unstring_next` finds the next field:
at each position the first delimiter (in table order) that matches ends it;
without delimiters the field is as long as the receiving item. The compiled
code moves each field and delimiter to the receivers through an address and
length slot, so the receivers' MOVE rules apply, then stores COUNT IN,
TALLYING IN and the pointer. Characters left unexamined after the last
receiver are an overflow.

