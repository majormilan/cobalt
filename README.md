# cobalt

**A COBOL-85 compiler with its own assembler, linker and runtime.**

cobalt compiles COBOL source into native executables in one step, using only
itself: the x86-64 assembler and the linker are built in, and the runtime is
hand-written assembly embedded in the compiler. The programs it makes are
small self-contained executables: static ELF on Linux, PE using only
`KERNEL32.dll` on Windows.

```
$ cobalt hello.cbl
$ ./hello
Hello, world!
```

> **Status: under development.** cobalt is working toward its first release,
> **0.1.0-alpha**, a feature complete alpha for COBOL-85. The language is
> largely done and checked with the NIST test suite, but the release work
> (CI, packaging) is not finished, and options and formats may still
> change. It is not yet meant for production use.

## Contents

- [What works today](#what-works-today)
- [Targets and platforms](#targets-and-platforms)
- [Building from source](#building-from-source)
- [Installing](#installing)
- [Using cobalt](#using-cobalt)
- [Testing](#testing)
- [Roadmap](#roadmap)
- [Known limitations](#known-limitations)
- [Documentation](#documentation)
- [License](#license)

## What works today

### The language: COBOL-85

cobalt compiles COBOL-85 (ANSI X3.23-1985, with the 1989 amendment of
intrinsic functions), checked with the NIST CCVS85 validation suite. Every
module of the standard is implemented except Communication: Nucleus,
Sequential, Relative and Indexed I-O, Sort-Merge, Inter-Program
Communication, Source Text Manipulation (COPY, REPLACE), Intrinsic
Functions, Segmentation, Debug and Report Writer.

- **Diagnostics** in the style of gcc and clang: the source line with the
  error underlined, notes that explain it (where an item is declared, the
  candidates of an ambiguous name, "did you mean"), errors inside copybooks
  shown in the copybook with the COPY chain, named warnings (`-Wall`,
  `-Wextra`, `-Werror`), and every stage reporting what it finds in one run.
- **Run-time errors** that name the source line and the program, and
  run-time checks of subscripts and reference modification (`-fcheck`).
- **Fast builds**, in a time that grows linearly with the size of the
  program, and small executables that hold only the runtime code they use.

### The toolchain

- A built-in x86-64 assembler (NASM syntax, with `%define`, `%include` and
  `%macro`): `cobalt as`.
- A built-in linker for ELF and PE executables.
- The runtime, hand-written in x86-64 assembly: decimal arithmetic, editing,
  files of the three organizations, SORT and MERGE, CALL, the intrinsic
  functions, the Report Writer. The same code serves every system, with a
  thin OS layer for each.

No assembler, linker or C library is needed to compile COBOL programs.

## Targets and platforms

cobalt runs on a **host** and builds executables for a **target**. Every
cobalt can build for every target: the runtimes of all targets are built in.

| Target (`-t`) | Architecture | Operating system | Executable | Status |
|---|---|---|---|---|
| `x86_64-linux-elf` | x86-64 | Linux | static ELF, no dependencies | supported (the default on Linux) |
| `x86_64-pc-windows-msvc` | x86-64 | Windows (6.0 and later) | PE32+ console program, `KERNEL32.dll` only | supported (the default on Windows) |

| Host | cobalt | Status |
|---|---|---|
| Linux x86-64 | `bin/cobalt` | supported, built and tested |
| Windows x86-64 | `bin/cobalt.exe` | supported, cross-built with MinGW-w64 and tested; a native build on Windows is not tested yet |

The test programs and the NIST suite give the same results on both targets.

The runtime is split into an architecture layer and an OS layer
(`runtime/<arch>/common`, `runtime/<arch>/<os>`), so another operating system
on x86-64 needs only an OS layer. Other architectures are a future goal
(see the [roadmap](#roadmap)).

## Building from source

### Requirements

| To | You need |
|---|---|
| build cobalt | `make` and a C compiler (gcc or clang) |
| build `cobalt.exe` for Windows | MinGW-w64 (`x86_64-w64-mingw32-gcc`) |
| run the NIST suite, the benchmark | Python 3 |
| cross-check with nasm (optional) | `nasm` and `ld` |

No assembler is needed: the build assembles the runtime with cobalt's own
assembler.

### Linux

```
git clone https://github.com/majormilan/cobalt.git
cd cobalt
make            # bin/cobalt
make test       # the test programs, the diagnostics and run-time error tests
```

The build has two stages: a first cobalt without runtimes assembles the
runtime sources, `cobalt embed` turns the objects into C tables, and the
final `bin/cobalt` is linked with them inside.

The default build is unoptimized (`-g`). For a faster compiler:

```
make CFLAGS="-O2 -Iinclude -Iruntime -include compat.h"
```

### Windows

Cross-build `cobalt.exe` on Linux (or WSL) with MinGW-w64:

```
make windows    # bin/cobalt.exe (and the first stage and runtimes it needs)
```

`bin/cobalt.exe` is a single file; copy it to Windows. Building cobalt
natively on Windows (MSYS2, with `make` and MinGW gcc) should work the same
way but is not tested yet.

### Cleaning

`make clean` removes `obj/` and `bin/`, including the NIST suite downloaded
under `obj/nist/` (it is fetched again when needed).

## Installing

There is no `make install` yet. cobalt is one
executable with nothing else to install, so copy it to a directory on your
PATH:

```
# Linux
sudo cp bin/cobalt /usr/local/bin/

# Windows (PowerShell): any directory on the PATH
Copy-Item bin\cobalt.exe "$env:LOCALAPPDATA\Programs\cobalt\"
```

Copybooks are found next to the source, in `-I` directories, and in the
directories of the `COBCPY` environment variable (separated by `:` on Linux
and `;` on Windows).

## Using cobalt

```
cobalt hello.cbl                        # executable ./hello (hello.exe on Windows)
cobalt main.cbl sub.cbl -o payroll      # several programs, one executable (CALL)
cobalt -I copylib ledger.cbl            # copybooks from copylib/
cobalt -X pedantic -Wextra ledger.cbl   # strict COBOL-85, all warnings
cobalt -fcheck ledger.cbl               # subscripts checked at run time
cobalt -t x86_64-pc-windows-msvc ledger.cbl -o ledger.exe   # for Windows
cobalt -c ledger.cbl                    # object ledger.o
cobalt -A ledger.cbl                    # assembly ledger.asm
cobalt --help                           # every option
```

Sources are in fixed reference format (columns 8 to 72) unless `-F` (free
format, an extension) is given. The exit status is 0 on success and 1 after
errors.

Programs read their environment at run time: `COB_SWITCH_1` to
`COB_SWITCH_9` (`ON`) set the switches of SPECIAL-NAMES, and `COB_SET_DEBUG`
(`Y`, `1` or `ON`) turns on the Debug module of a program compiled WITH
DEBUGGING MODE.

## Testing

| Command | Runs |
|---|---|
| `make test` | the test programs (`tests/*.cbl`, output compared with `.expected`), the diagnostics tests (`tests/diag`), the run-time error tests (`tests/runerr`) |
| `make test-windows` | the test programs built for Windows (on WSL through its interop, elsewhere with Wine) |
| `tools/nist/run.sh MODULE` | a module of the NIST CCVS85 suite (NC, SQ, RL, IX, ST, IC, SM, IF, SG, DB, RW); `COBALT_FLAGS="-t x86_64-pc-windows-msvc"` for Windows |
| `python3 -I tools/bench/compile.py DIR` | writes the compile-time benchmark program into DIR |

## Roadmap

### For 0.1.0-alpha

The language and both platforms are done; what is left:

- error recovery after syntax errors, and clear messages for elements of
  later standards and vendor extensions;
- release notes with the known limitations;
- continuous integration on Linux and Windows;
- a fuzzing pass over the compiler (no crash on any input);
- mapping ASSIGN names through environment variables;
- `make install`, release builds for both systems, a version tag.

### After the alpha

- Debug information (DWARF line numbers) for stepping through COBOL source
  in gdb, and a compiler listing (source, cross-reference, data map).
- Calling C code and being called from C on both systems; dynamic CALL of
  separately compiled modules; COFF objects for other Windows linkers.
- Sorting and indexed files larger than memory.
- COMP-1 and COMP-2 (floating point).

### Longer term

- More architectures (AArch64 first) and operating systems, using the split
  of the runtime into architecture and OS layers.
- Newer COBOL standards (2002, 2014): `-S` accepts only COBOL-85 today.

## Known limitations

- The Communication module is not supported (it is rejected with a clear
  error).
- SORT and MERGE work in memory, and indexed files build their indexes in
  memory when opened.
- A program must fit in 2 GiB (the generated code uses absolute 32-bit
  addresses).
- Objects from `-c` are in cobalt's own ELF format: cobalt links them for
  either system, but other linkers cannot use them on Windows.
- In the Report Writer, the data-name of a SUM entry names its printed
  field, not the sum counter.
- An exponent with decimals (`2 ** 0.5`) is a size error.

[docs/guide/09-limits.md](docs/guide/09-limits.md) has the complete list.

## Documentation

| Document | About |
|---|---|
| [docs/guide](docs/guide/README.md) | the user guide: getting started, the command line, the language, strictness and dialects, diagnostics, files, running programs, platforms, limits |
| [runtime/ABI.md](runtime/ABI.md) | the runtime: data formats, files, CALL, run-time errors, the Report Writer, the OS layers |
| [docs/assembler.md](docs/assembler.md) | the built-in assembler |
| [docs/linker.md](docs/linker.md) | the built-in linker (ELF and PE) |
| [docs/optimization.md](docs/optimization.md) | code generation |

## License

MIT; see [LICENSE](LICENSE). Copyright (c) 2026 Milán Major.
