# Changelog

## Unreleased

### Added

- Hexadecimal literals (`X"0D0A"`) and `LENGTH OF identifier`, in the
  `common`, `ibm` and `gnucobol` extension sets.
- ASSIGN TO DYNAMIC, CALL ... BY VALUE and RETURNING, INSPECT ...
  TRAILING and ACCEPT ... FROM ENVIRONMENT are named as extensions Cobalt
  does not support yet, without follow-on errors.
- Extension sets (`-fext=common | ibm | gnucobol | cobol2002 | none`) select
  which elements beyond the standard are accepted. The `common` set is the
  default; `-X pedantic` accepts only the sets named explicitly, and
  `-X ibm-strict` is `-X pedantic -fext=ibm`.

### Changed

- Messages about elements outside COBOL-85 name where the element comes
  from (a later standard or a vendor) and the extension set that accepts
  it; elements Cobalt does not implement yet say so.
- `make install` installs the existing build and no longer compiles; it
  stops with a message if the build is missing or out of date.
- The parser, IR lowering and x86-64 code generator are restructured into
  smaller functions (statement parsers dispatched by table, data-division
  phases, report setup, FD clauses, per-instruction emitters); generated
  code is unchanged.

### Fixed

- DISPLAY UPON SYSERR, or a mnemonic-name of SYSERR, writes to standard
  error.
- A program may contain more than 64 programs; past the limit of 512
  waiting to be compiled, an error says so instead of skipping them.
- A copy library directory given with a trailing slash (`-I ./`) no longer
  shows copybook paths starting with `/` in messages.
- COPY also searches the current directory, so `COPY "dir/name.cpy"`
  written relative to the project's top directory is found.
- MOVE of an item whose length is known only at run time (a reference
  modifier with variable start or length) to a numeric item is supported.
- Exponentiation takes an exponent with decimals (`9 ** 0.5` is 3); a
  negative base with such an exponent is a size error.
- MOVE of HIGH-VALUE, LOW-VALUE, QUOTE or an ALL literal to a numeric item
  is accepted, as the standard allows (only SPACE is not), and moves the
  characters as alphanumeric data (`MOVE ALL "5" TO N` with `N PIC 9(4)`
  gives 5555).
- VALUE of a figurative constant other than ZERO on a numeric item is an
  error instead of silently becoming zero.
- A PERFORM, or an inline PERFORM ... TIMES loop, left with GO TO no
  longer leaves data on the stack: each statement saves its return slot or
  outer loop counter in a static cell, as IBM's implementation does.
  Programs that leave a PERFORM this way repeatedly no longer crash, and
  PERFORM ranges that end out of order each restore their own slot.
- A name declared both as a condition-name and as a data item is told
  apart by its qualification (`IF ACTIVE OF REC-A`, `EVALUATE ACTIVE OF
  REC-B`), whatever order the declarations come in.
- An EVALUATE subject may be any conditional expression (`A = 3 AND B = 5`,
  `NOT A = 3`, `(A = 3)`, abbreviated combined relations).
- A reference modifier without a length on a group with an OCCURS
  DEPENDING ON table runs to the group's current end, not its maximum.
- A signed numeric literal moved to, or compared with, an alphanumeric
  item is treated as its unsigned digits (`MOVE -12 TO X` gives "12").
- The constant-condition warning for a loop condition (`PERFORM ... UNTIL
  I > 9` with `I PIC 9`) says the relation is always false, as written,
  instead of describing its negation.
- UNSTRING into a numeric receiver keeps the low-order digits of a
  delimited field longer than 18 characters.
- Intrinsic functions of large arguments: MAX, MIN, ORD-MAX, ORD-MIN,
  MEDIAN, MIDRANGE, RANGE, SUM, MEAN, STANDARD-DEVIATION, VARIANCE,
  PRESENT-VALUE and NUMVAL / NUMVAL-C of a literal give exact results up to
  18 digits; a value that cannot be represented raises the size error
  instead of giving a wrong result.
- MOVE evaluates the sending item's subscripts and reference modifier
  once, before the first receiver, as the standard requires
  (`MOVE TE(I) TO I J`).
- YEAR, YYYYMMDD and YYDDD are no longer reserved words; they can name
  data items (YYYYMMDD keeps its meaning after ACCEPT ... FROM DATE).
- Debugging lines are compiled only with WITH DEBUGGING MODE in the
  SOURCE-COMPUTER paragraph; the words elsewhere (in a literal, a comment
  line or USE FOR DEBUGGING) no longer turn them on.
- COPY REPLACING and REPLACE no longer put spaces around the
  replacement, so a replaced word inside a PICTURE string (`X(LEN)`) or
  part of a word (`:P:-REC`) gives one character-string.
- Pseudo-text that ends in a period (`==STOP RUN.==`) matches.
- `0 ** 0` is a size error also when the exponent is a literal.
- INITIALIZE ... REPLACING ... BY a figurative constant (SPACES, QUOTE,
  ALL literal) fills the items with it instead of storing its name.
- No false size error when a quotient or product of up to 18 digits is
  stored with no room for extra decimal places (DIVIDE and COMPUTE with
  18-digit operands, DIVIDE ... REMAINDER).
- INITIALIZE no longer changes USAGE INDEX data items inside the group.
- INSPECT TALLYING accepts a subscripted or qualified counter after the
  first one.
- DIVIDE ... REMAINDER ... ON SIZE ERROR leaves the remainder unchanged
  when the quotient has the size error.
- `sudo make install` could rebuild the compiler as root, leaving the build
  directories owned by root.

## 0.1.0-alpha (2026-11-11)

The first public release of Cobalt, a general-purpose COBOL cross-compiler.
This release supports the COBOL-85 standard, complete except for the
Communication module, on two targets: Linux and Windows on x86-64. Support
for further COBOL standards, architectures and operating systems is planned.
As an alpha release, command-line options, messages and file formats may
change before version 1.0.

### Language

- COBOL-85 (ANSI X3.23-1985, ISO 1989:1985) with the 1989 amendment of
  intrinsic functions, every module at its highest level: Nucleus,
  Sequential, Relative and Indexed I-O, Sort-Merge, Inter-Program
  Communication, Source Text Manipulation, Intrinsic Functions,
  Segmentation, Debug and Report Writer. Checked with the NIST CCVS85
  validation suite.
- The common extensions, each a warning under `-X normal`: COMP-3, COMP-4,
  COMP-5, LINE SEQUENTIAL files, apostrophe literals, RETURN-CODE, ACCEPT
  FROM DATE YYYYMMDD and DAY YYYYDDD, free format, and from COBOL 2002 GOBACK,
  EXIT PERFORM [CYCLE], EXIT PARAGRAPH, EXIT SECTION and `*>` comments.
- The elements of later standards and vendor dialects that Cobalt does not
  support are named as such (`FUNCTION TRIM is COBOL 2002, not COBOL-85`),
  instead of syntax errors.

### Strictness, dialects and flagging

- `-X relaxed | normal | pedantic | ibm-strict`: what is done with elements
  outside COBOL-85.
- `-D standard | ibm | gnucobol | microfocus` and `-f`: DISPLAY of numbers,
  sign encoding, binary truncation.
- FIPS flagging: `--flag-obsolete`, `--subset=minimum | intermediate`.

### Diagnostics

- Messages in the style of gcc and clang: the source line with the error
  underlined, notes on what explains it, "did you mean", errors in
  copybooks shown with their COPY chain, colors.
- Recovery after syntax errors: one compilation reports the errors of the
  whole program.
- Named warnings (`-Wall`, `-Wextra`, `-Werror`, `-Wno-...`): truncated
  literals, narrowing moves, constant conditions, unreachable statements,
  unused items and procedures, uninitialized items, unchecked I-O, numeric
  and nonnumeric comparisons, group moves, recursive PERFORM, and more.

### Running programs

- Run-time errors that name the source line and the program; `-fcheck` for
  subscripts, OCCURS DEPENDING ON values, reference modification and the
  contents of numeric items.
- File names mapped through the environment (`DD_name`, `dd_name`, `name`,
  `COB_FILE_PATH`); exit status from RETURN-CODE.

### Executables

- Small and self-contained: a program links only the runtime code it uses.
- Compiled with constant folding across statements, values kept in
  registers, initial values written into storage when compiling, compact
  calls to the runtime; numeric loops kept in registers.

### Toolchain and platforms

- One executable: the compiler, an x86-64 assembler, a linker for ELF and
  PE, and the runtime (hand-written assembly) of both systems.
- Static Linux executables using only system calls; Windows executables
  using only kernel32.dll. Every installation of Cobalt builds for both (`-t`).
- `-c` objects (archives for several programs), `-A` assembly,
  `-fsyntax-only`, `-E`, `-MD`.

### Not in this release

- The Communication module.
- See the [known limitations](docs/guide/09-limits.md): ASSIGN to a data
  item, floating point (COMP-1, COMP-2), calling programs in other
  languages, debugging information, files and sorts larger than memory.
