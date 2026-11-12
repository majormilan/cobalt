# 2. The command line

```
cobalt [options] source... [object.o...]
cobalt as [options] source.asm
cobalt embed SYMBOL object...
```

The sources (`.cbl` or any other name) and objects (names ending in `.o`)
given together make one **run unit**: by default an executable. The first
program of the first source is the main program, where the run starts. Options
may come before or after the files.

## Input

| Option | Meaning |
|---|---|
| `source` | A COBOL source. Several may be given. |
| `-i FILE`, `--input FILE` | The same as giving FILE as an argument. |
| `object.o` | An object made by `cobalt -c`, linked into the executable. Its programs can be called by the others. |
| `--fixed` | Fixed reference format: sequence area in columns 1 to 6, indicator in column 7, program text in columns 8 to 72. The default. |
| `-F`, `--free` | Free-form reference format: program text anywhere on the line. An extension (chapter 4). |
| `-I DIR` | A directory of copybooks. May be repeated; searched in order, after the directory of the source. |
| `--module` | None of the programs is a main program: every one is a program to be called. For objects of called programs. |

The `COBCPY` environment variable gives more copybook directories, searched
after the `-I` ones: separated by `:` on Linux and by `;` on Windows.

## Output

| Option | Meaning |
|---|---|
| (none) | An executable. |
| `-o FILE`, `--output FILE` | The name of the output file. |
| `-c` | An object file instead of an executable. |
| `-A`, `--asm` | Assembly (NASM syntax) instead of an executable. |
| `--strip` | An executable without a symbol table (smaller; debuggers show no names). |
| `-fsyntax-only` | Checks the program (every error and warning) and writes nothing. |
| `-E` | Writes the source as the compiler reads it, after COPY and REPLACE, to the `-o` file or standard output. |
| `-MD` | Also writes `OUTPUT.d` (the output's name without its extension, plus `.d`): a make rule whose prerequisites are the sources and every copybook read. |
| `-MP` | With `-MD`: an empty rule for each copybook, so make does not fail when one is removed. |
| `-t TRIPLE`, `--target TRIPLE` | The system to build for: `x86_64-linux-elf` or `x86_64-pc-windows-msvc`. The default is the system Cobalt runs on. |

Without `-o`, the output is named after the first source, without its
directory and its extension, in the current directory: `src/ledger.cbl`
gives `ledger` (`ledger.exe` for Windows), `ledger.o` with `-c` and
`ledger.asm` with `-A`.

Each program is compiled into an object of its own. With `-c`, a source
holding one program gives an object file; with several programs, the object
file is an archive (`ar` format) of their objects, which Cobalt links as one.
With `-A`, the assembly of the first program goes to the output file and that
of each other program to `<output>.<PROGRAM-ID>`.

## Language

| Option | Meaning |
|---|---|
| `-S STANDARD`, `--std STANDARD`, `-std=STANDARD` | The COBOL standard. `COBOL-85` (also `COBOL85`, `85`), the only one of this release. |
| `-fext=SET[,SET...]` | Extension sets: the elements beyond the standard that are accepted. `common` (the default set), `ibm`, `gnucobol`, `cobol2002`; `none` drops the default set. Chapter 4. |
| `-X LEVEL`, `--strictness LEVEL` | How accepted extensions are reported and which sets apply: `relaxed`, `normal` (the default), `pedantic` or `ibm-strict` (`-X pedantic -fext=ibm`). Chapter 4. |
| `-D NAME`, `--dialect NAME` | How what the standard leaves to the implementor behaves: `standard` (the default), `ibm`, `gnucobol` or `microfocus`. Chapter 4. |
| `-f KEY=VALUE` | One setting of the dialect, after `-D`: `display=stored\|pretty`, `sign=ascii\|ebcdic`, `binary-truncate=yes\|no`. Chapter 4. |

## Run time

| Option | Meaning |
|---|---|
| `-fcheck` | The program checks its subscripts, its OCCURS DEPENDING ON values and its reference modifications when it runs; a failed check ends the run with the source line. Chapter 7. |
| `-fno-check` | No such checks (the default). |

## Diagnostics

| Option | Meaning |
|---|---|
| `-Wname`, `-Wno-name` | Turns the warning `name` on or off. Chapter 5 lists them. |
| `-Wall` | The warnings that are on by default. |
| `-Wextra` | Every warning. |
| `-Werror` | Warnings are errors. `-Werror=name`: this warning is an error. `-Wno-error`, `-Wno-error=name` undo them. |
| `-w` | No warnings. |
| `--flag-obsolete` | Reports the obsolete elements of COBOL-85 (`-Wobsolete`). |
| `--subset=SUBSET` | Reports the elements outside a FIPS subset of COBOL-85: `minimum`, `intermediate`, or `high` (the default: nothing reported). Chapter 4. |
| `-fmax-errors=N` | Stops after N errors. |
| `-fdiagnostics-color[=WHEN]` | Colors: `always`, `never`, or `auto` (the default: on a terminal, unless `NO_COLOR` is set). `-fno-diagnostics-color` is `never`. |

## Other

| Option | Meaning |
|---|---|
| `--version` | The version and the targets. |
| `--help` | A summary of the options and the warnings. |
| `-a` | Prints the syntax tree (for working on Cobalt). |
| `-g` | Accepted for compatibility, with a warning: this release writes no debugging information. |

## Exit status

0 when the output was written, 1 after an error (in the program or on the
command line). Warnings do not change the exit status, unless `-Werror` makes
them errors.

The programs Cobalt builds have exit statuses of their own (chapter 7).

## `cobalt as`

`cobalt as` is the built-in x86-64 assembler, for NASM-syntax sources:

```
cobalt as [-f elf64] [-D NAME[=value]] [-I dir/] [-MD file] [-MP] [-o out.o] source.asm
```

| Option | Meaning |
|---|---|
| `-f elf64` | The object format (the only one). |
| `-D NAME[=value]` | Defines NAME, as `%define`. |
| `-I dir/` | A directory searched for `%include` and `incbin` (a prefix, as in nasm). |
| `-MD file`, `-MP` | Writes the dependencies of the object in make's format; `-MP` adds an empty rule for each included file. |
| `-o out.o` | The object file. |

[docs/assembler.md](../assembler.md) describes the syntax it accepts.

## `cobalt embed`

`cobalt embed SYMBOL object...` writes C source of a table of the objects to
standard output. The build of Cobalt uses it to put the runtimes inside
Cobalt; it is of no use for COBOL programs.
