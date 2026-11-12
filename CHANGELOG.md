# Changelog

## Unreleased

### Added

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
