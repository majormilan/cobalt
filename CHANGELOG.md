# Changelog

## 0.1.0-alpha (unreleased)

The first release: a COBOL-85 compiler for Linux and Windows on x86-64,
feature complete for the standard except the Communication module. It is an
alpha: options, messages and file formats may still change.

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
- The elements of later standards and vendor dialects that cobalt does not
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

### Toolchain and platforms

- One executable: the compiler, an x86-64 assembler, a linker for ELF and
  PE, and the runtime (hand-written assembly) of both systems.
- Static Linux executables using only system calls; Windows executables
  using only kernel32.dll. Every cobalt builds for both (`-t`).
- `-c` objects (archives for several programs), `-A` assembly,
  `-fsyntax-only`, `-E`, `-MD`.

### Not in this release

- The Communication module.
- See the [known limitations](docs/guide/09-limits.md): ASSIGN to a data
  item, floating point (COMP-1, COMP-2), calling programs in other
  languages, debugging information, files and sorts larger than memory.
