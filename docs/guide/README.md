# The cobalt User Guide

This guide describes cobalt 0.1.0-alpha: how to install it, how to compile
and run COBOL programs with it, what of COBOL-85 it implements and how its
programs behave when they run.

| Chapter | Contents |
|---|---|
| [1. Getting started](01-getting-started.md) | installing, a first program, several programs, copybooks, files |
| [2. The command line](02-command-line.md) | every option, output files, exit status |
| [3. The language](03-language.md) | COBOL-85 as cobalt implements it, module by module; what is not supported |
| [4. Strictness, dialects and FIPS flagging](04-strictness-and-dialects.md) | `-X`, `-D`, `-f`, `--flag-obsolete`, `--subset`, the extensions cobalt knows |
| [5. Diagnostics](05-diagnostics.md) | the messages, every warning, colors and limits |
| [6. Files](06-files.md) | file names, organizations, FILE STATUS codes, formats on disk, LINAGE, SORT and MERGE |
| [7. Running programs](07-running-programs.md) | the console, numbers, the environment, run-time errors and checks, exit status, CALL |
| [8. Platforms](08-platforms.md) | Linux and Windows, building for the other system, building cobalt |
| [9. Limits](09-limits.md) | the sizes cobalt handles and the known limitations |

For people working on cobalt itself, the internals are described in
[runtime/ABI.md](../../runtime/ABI.md) (the runtime and the code it is called
from), [docs/assembler.md](../assembler.md), [docs/linker.md](../linker.md)
and [docs/optimization.md](../optimization.md).

## Conventions

Examples of COBOL are in fixed reference format: columns 1 to 6 are the
sequence area, column 7 the indicator, columns 8 to 11 area A and columns 12
to 72 area B. Shell examples use `$` for the prompt of Linux (or of a shell
on Windows); `cobalt` stands for `bin/cobalt` or `cobalt.exe`, wherever it is
installed.
