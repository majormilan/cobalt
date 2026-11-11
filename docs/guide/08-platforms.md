# 8. Platforms

## Targets and hosts

Cobalt runs on a **host** and builds executables for a **target**. Every
Cobalt carries the runtimes of all its targets, so any installation of Cobalt builds for any
target:

| Target (`-t`) | Executables | Runtime |
|---|---|---|
| `x86_64-linux-elf` | static ELF executables for Linux on x86-64 | system calls only: no libc, no dynamic libraries |
| `x86_64-pc-windows-msvc` | PE32+ console executables for 64-bit Windows | kernel32.dll only: no C runtime, no DLLs to ship |

| Host | Cobalt |
|---|---|
| Linux on x86-64 | `bin/cobalt`, built with `make` |
| Windows on x86-64 | `bin/cobalt.exe`, cross-built on Linux with `make windows` |

The default target is that of the host. The programs behave the same on
both targets: the same results, the same file formats on disk (chapter 6),
the same messages. The NIST suite gives the same results on both.

## Linux

The executables are static: copy them to any x86-64 Linux and they run.
They hold only the runtime code the program uses; `--strip` removes the
symbol table. The symbol table names the procedures and
runtime routines, not the compiler's internal labels.

The shell sees the exit status modulo 256 (chapter 7).

## Windows

- cobalt.exe and the executables it builds run in a console (cmd.exe,
  PowerShell, Windows Terminal) and from scripts.
- Sources may have CR LF line ends, and so may the input of ACCEPT and LINE
  SEQUENTIAL files. Files Cobalt's programs write end their lines with LF
  alone, as on Linux.
- Paths may use `\` or `/`. `COBCPY` separates its directories with `;`.
- Without `-o`, the executable is `NAME.exe`.
- The environment variables of chapter 7 are set with `set` in cmd.exe or
  `$env:COB_SWITCH_1 = "ON"` in PowerShell.

From Linux, build for Windows with `-t`:

```
$ cobalt -t x86_64-pc-windows-msvc ledger.cbl -o ledger.exe
```

On WSL the result runs directly (`./ledger.exe`); WSL passes only the
environment variables listed in `WSLENV` to it, so use
`WSLENV=COB_SWITCH_1 COB_SWITCH_1=ON ./ledger.exe`.

## Building Cobalt

| To | You need |
|---|---|
| build Cobalt | `make` and a C compiler (gcc or clang) |
| build cobalt.exe | MinGW-w64 (`x86_64-w64-mingw32-gcc`) |
| run the NIST suite | Python 3 |

No assembler is needed: Cobalt assembles its runtimes with its own.

```
$ make                  # bin/cobalt
$ make windows          # bin/cobalt.exe
```

The build has two stages: a first-stage compiler without runtimes assembles the
runtime sources, `cobalt embed` turns their objects into C tables, and the
final compiler is linked with them inside. `make windows` builds the runtimes
with the Linux compiler, then cobalt.exe with MinGW-w64.

The default build is unoptimized (`-g`), for working on cobalt. For a faster
compiler:

```
$ make CFLAGS="-O2 -Iinclude -Iruntime -include compat.h"
```

`make dist` builds the release packages in `obj/dist`: an optimized, static
Cobalt for Linux and cobalt.exe for Windows, each with the README, the
license, the release notes, this guide and the examples, and their SHA-256
sums; the examples are run with the Linux package's cobalt.

`make clean` removes `obj/` and `bin/`, including the NIST suite under
`obj/nist/` (downloaded again when needed).

`make install` copies `bin/cobalt` to `$(PREFIX)/bin` (`PREFIX` is
`/usr/local` unless given, `DESTDIR` is put before it for packaging) and the
README, the license and this guide to `$(PREFIX)/share/doc/cobalt`;
`make uninstall` removes them. On Windows, copy `bin/cobalt.exe` to a
directory on the PATH.

## Testing Cobalt

| Command | Runs |
|---|---|
| `make test` | the test programs of `tests/` (each output compared with its `.expected` file), the diagnostics tests of `tests/diag/` and the run-time error tests |
| `make examples` | the programs of `examples/`, built and run, their output and the files they write compared with `expected/` (also part of `make test`) |
| `make test-windows` | the test programs built for Windows and run there (on WSL directly, elsewhere through Wine) |
| `tools/nist/run.sh MODULE` | one module of the NIST CCVS85 suite: `NC`, `SM`, `SQ`, `RL`, `IX`, `ST`, `IC`, `IF`, `SG`, `DB`, `RW` |

The NIST suite is downloaded on the first run into `obj/nist/`. `TARGET=
x86_64-pc-windows-msvc tools/nist/run.sh NC` runs a module built for
Windows; `COBALT_FLAGS` passes options to cobalt. Each module's flagging
programs (FIPS subsets and obsolete elements) are compiled with the
matching `--subset` and `--flag-obsolete`, and scored on the messages they
expect.

`.github/workflows/ci.yml` runs the build, `make test` and the NIST suite on
Linux for every push, cross-builds cobalt.exe, and runs the test programs and
the examples with it on Windows.

A diagnostics test is `tests/diag/NAME.cbl` with its options in
`NAME.flags` and the expected messages in `NAME.err` (`NAME.src` names
another source, to check one program with other options).
`UPDATE=1 tools/diag_test.sh NAME` writes `NAME.err` from the output; read
it before committing.
