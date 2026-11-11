# cobalt

cobalt is a compiler for COBOL-85. It turns COBOL source into native
executables by itself: the assembler and the linker are built in, and the
runtime is hand-written x86-64 assembly. A compiled program is a small static
executable with no dependencies.

```
$ cobalt hello.cbl
$ ./hello
Hello, world!
```

**Status: alpha (0.1.0-alpha in preparation).** The goal of the first
release is all of COBOL-85 on Linux and Windows; see
[docs/release-checklist.md](docs/release-checklist.md) for what is done and
what is left.

## What it does

- **COBOL-85** (ANSI X3.23-1985 with the 1989 intrinsic functions
  amendment). Measured with the NIST CCVS85 test suite: the Nucleus,
  Sequential, Relative and Indexed I-O, Sort-Merge, Inter-Program
  Communication, Source Text Manipulation (COPY, REPLACE), Intrinsic
  Functions, Segmentation and Debug modules compile and pass. Report Writer
  is not done yet; Communication is not supported.
- **Strictness levels** (`-X relaxed | normal | pedantic | ibm-strict`):
  extensions to the standard are accepted, warned about or rejected, and
  `pedantic` flags the obsolete elements (FIPS flagging).
- **Dialects** (`-D standard | ibm | gnucobol | microfocus`) for what the
  standard leaves to the implementor, such as the format of numeric DISPLAY.
- **Diagnostics** that show the source line, explain the error with notes
  (where an item is declared, the candidates of an ambiguous name, "did you
  mean"), follow COPY into the copybook, and named warnings in the style of
  gcc and clang (`-Wall`, `-Wextra`, `-Werror`). See
  [docs/diagnostics.md](docs/diagnostics.md).
- **Fast builds.** On a 64,000-line program (`tools/bench/compile.py`)
  cobalt builds the executable in 2.5 s when cobalt itself is compiled with
  `-O2` (5 s with the default debug build), against 12 to 15 s for
  GnuCOBOL 3.2; the build time grows linearly with the size of the program.

## Platforms

| Platform | Status |
|---|---|
| Linux x86-64 | supported |
| Windows x86-64 | in progress (for 0.1.0-alpha) |

## Building

cobalt is written in C; the runtime is assembled by cobalt's own assembler
during the build, so no assembler or linker is needed. You need `make` and
a C compiler (gcc or clang):

```
make            # builds bin/cobalt
make test       # runs the test programs and the diagnostics tests
```

The build makes `bin/cobalt`, a single executable with the runtime inside;
copy it anywhere on your PATH.

## Using it

```
cobalt hello.cbl                       # executable ./hello
cobalt main.cbl sub.cbl -o payroll     # several programs, one executable (CALL)
cobalt -I copylib ledger.cbl           # copybooks from copylib/
cobalt -X pedantic -Wextra ledger.cbl  # strict COBOL-85, all warnings
cobalt -c ledger.cbl                   # object file ledger.o
cobalt --help                          # every option
```

Sources are in fixed reference format (columns 8-72) unless `-F` is given.

## Documentation

- [docs/diagnostics.md](docs/diagnostics.md): errors, warnings, strictness levels
- [docs/release-checklist.md](docs/release-checklist.md): the road to 0.1.0-alpha
- [runtime/ABI.md](runtime/ABI.md): the runtime's interface (data formats, files, CALL)
- [docs/assembler.md](docs/assembler.md), [docs/linker.md](docs/linker.md): the built-in toolchain
- [docs/optimization.md](docs/optimization.md): code generation

## Testing against NIST CCVS85

`tools/nist/run.sh MODULE` compiles and runs the programs of a module (NC,
SM, SQ, RL, IX, ST, IC, IF, SG, DB) and prints a summary. The first run
fetches the NIST suite (`tools/nist/fetch.sh`) and extracts it under
`obj/nist/`. Python 3 is needed for the extraction and scoring scripts.

## License

MIT; see [LICENSE](LICENSE).
