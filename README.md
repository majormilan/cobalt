<div align="center">

# Cobalt

**A self-contained cross-compiler for COBOL**

[![CI](https://github.com/majormilan/cobalt/actions/workflows/ci.yml/badge.svg)](https://github.com/majormilan/cobalt/actions/workflows/ci.yml)
[![Release](https://img.shields.io/badge/release-0.1.0--alpha-orange)](https://github.com/majormilan/cobalt/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/language-COBOL-005f87)](docs/guide/03-language.md)
[![Targets](https://img.shields.io/badge/targets-Linux%20%7C%20Windows-lightgrey)](docs/guide/08-platforms.md)

[Getting Started](docs/guide/01-getting-started.md) ·
[User Guide](docs/guide/README.md) ·
[Examples](examples) ·
[Downloads](https://github.com/majormilan/cobalt/releases) ·
[Release Notes](CHANGELOG.md)

</div>

## Overview

Cobalt is a general-purpose COBOL compiler designed for cross-compilation.
A single executable contains the compiler, an assembler, a linker and a
runtime library for each supported target. Any installation of Cobalt can
therefore build native programs for every supported operating system and
architecture, with no external toolchain, system libraries or target-side
runtime installation.

Generated programs are self-contained native executables. They include only
the runtime components they use and do not depend on a COBOL runtime on the
target system.

```
$ cobalt payroll.cbl                                   # native executable for this system
$ cobalt -t x86_64-pc-windows-msvc payroll.cbl         # Windows executable, built on any host
```

## Current Release

Version **0.1.0-alpha** is the first public release. Its scope is the COBOL-85
standard on two targets. Support for additional COBOL standards, vendor
dialects, architectures and operating systems is planned for subsequent
releases (see [Roadmap](#roadmap)).

> **Alpha software.** Command-line options, diagnostic messages and file
> formats may change before version 1.0. This release is not intended for
> production use.

## Contents

- [Support Matrix](#support-matrix)
- [Downloads](#downloads)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Language Support](#language-support)
- [Diagnostics](#diagnostics)
- [Runtime](#runtime)
- [Building from Source](#building-from-source)
- [Quality Assurance](#quality-assurance)
- [Examples](#examples)
- [Known Limitations](#known-limitations)
- [Roadmap](#roadmap)
- [Documentation](#documentation)
- [License](#license)

## Support Matrix

### Language standards

| Standard | Status in 0.1.0-alpha |
|---|---|
| COBOL-85 (ANSI X3.23-1985, ISO 1989:1985) with the 1989 Intrinsic Function amendment | Supported |
| COBOL 2002 (ISO/IEC 1989:2002) | Planned; selected elements available as extensions |
| COBOL 2014 (ISO/IEC 1989:2014) | Planned |

### Targets

| Target triple | Architecture | Operating system | Output | Status |
|---|---|---|---|---|
| `x86_64-linux-elf` | x86-64 | Linux | Static ELF executable | Supported |
| `x86_64-pc-windows-msvc` | x86-64 | Windows | PE32+ console executable, `KERNEL32.dll` only | Supported |
| Additional architectures, beginning with AArch64 | | | | Planned |

### Hosts

| Host | Distribution | Status |
|---|---|---|
| Linux, x86-64 | `cobalt` | Supported |
| Windows, x86-64 | `cobalt.exe` | Supported |

Every host can build for every target.

## Downloads

Release packages are available on the
[Releases](https://github.com/majormilan/cobalt/releases) page.

| Host | Package |
|---|---|
| Linux, x86-64 | `cobalt-<version>-linux-x86_64.tar.gz` (statically linked; runs on any distribution) |
| Windows, x86-64 | `cobalt-<version>-windows-x86_64.zip` |

Each package contains the compiler, the user guide, the release notes, the
license and the example programs. Checksums are published in `SHA256SUMS`.

## Installation

**Linux**

```
tar xzf cobalt-0.1.0-alpha-linux-x86_64.tar.gz
sudo cp cobalt-0.1.0-alpha-linux-x86_64/cobalt /usr/local/bin/
```

**Windows (PowerShell)**

```
Expand-Archive cobalt-0.1.0-alpha-windows-x86_64.zip
Copy-Item cobalt-0.1.0-alpha-windows-x86_64\cobalt.exe "$env:LOCALAPPDATA\Programs\cobalt\"
```

Add the destination directory to the `PATH`. To install from a source build,
see [Building from Source](#building-from-source).

## Quick Start

```
cobalt hello.cbl                        # compile and link ./hello (hello.exe on Windows)
cobalt main.cbl sub.cbl -o payroll      # link several programs into one executable
cobalt -I copylib ledger.cbl            # search copylib/ for copybooks
cobalt -X pedantic -Wextra ledger.cbl   # strict standard conformance, all warnings
cobalt -fcheck ledger.cbl               # enable run-time checks
cobalt -t x86_64-pc-windows-msvc ledger.cbl -o ledger.exe   # cross-compile for Windows
cobalt -fsyntax-only ledger.cbl         # check without producing output
cobalt --help                           # list all options
```

The [Getting Started](docs/guide/01-getting-started.md) chapter of the user
guide provides a complete introduction.

## Language Support

Cobalt 0.1.0-alpha implements COBOL-85 at its highest level in the following
modules: Nucleus, Sequential I-O, Relative I-O, Indexed I-O, Sort-Merge,
Inter-Program Communication, Source Text Manipulation, Intrinsic Functions,
Segmentation, Debug and Report Writer. The Communication module is not
supported.

| Capability | Option | Description |
|---|---|---|
| Extension sets | `-fext=common \| ibm \| gnucobol \| cobol2002 \| none` | Select which elements beyond the standard are accepted: the common extensions (default), those of IBM Enterprise COBOL or GnuCOBOL, or COBOL 2002 elements |
| Strictness levels | `-X relaxed \| normal \| pedantic \| ibm-strict` | Report accepted extensions silently or with a warning; `pedantic` accepts only the explicitly enabled sets |
| Implementation profiles | `-D standard \| ibm \| gnucobol \| microfocus` | Select implementation-defined behavior, such as numeric display format and sign encoding |
| FIPS flagging | `--flag-obsolete`, `--subset=minimum \| intermediate` | Report obsolete elements and elements outside a FIPS subset |
| Reference formats | `--fixed`, `-F` | Fixed (default) and free source format |

Frequently used elements of later standards and vendor dialects are
identified by name in diagnostics. GOBACK, EXIT PERFORM, EXIT PARAGRAPH, EXIT
SECTION and floating comment indicators are accepted as extensions.

## Diagnostics

- Messages identify the file, line and column, display the source line and
  mark the relevant position.
- Explanatory notes refer to related declarations; misspelled names and
  statements receive suggested corrections.
- Errors in copybooks are reported with the chain of COPY statements that
  included them.
- Warnings are named and individually configurable (`-W`, `-Wall`,
  `-Wextra`, `-Werror`).
- Compilation continues after syntax errors, so that a single run reports the
  errors of the entire program.

## Runtime

- Run-time errors identify the program and the source line.
- Optional run-time checks (`-fcheck`) cover subscripts, OCCURS DEPENDING ON
  values, reference modification and the validity of numeric data.
- File assignments can be redirected through environment variables
  (`DD_name`, `dd_name`, `name`, `COB_FILE_PATH`).
- The process exit status reflects the RETURN-CODE special register.

## Building from Source

| Purpose | Requirement |
|---|---|
| Compiler | `make` and a C compiler (GCC or Clang) |
| Windows compiler (`cobalt.exe`) | MinGW-w64 |
| NIST validation suite | Python 3 |

```
git clone https://github.com/majormilan/cobalt.git
cd cobalt
make                    # build bin/cobalt
make test               # run the test suite
make windows            # cross-build bin/cobalt.exe
sudo make install       # install to /usr/local (PREFIX and DESTDIR are supported)
make dist               # build release packages in obj/dist
```

No external assembler or linker is required. The build process is described
in the [Platforms](docs/guide/08-platforms.md) chapter of the user guide.

## Quality Assurance

| Suite | Scope |
|---|---|
| Test programs | Execution results of language features on each target |
| Diagnostics tests | Exact compiler messages for errors and warnings |
| Run-time error tests | Messages and exit status of run-time failures |
| Example programs | Complete applications with expected output |
| NIST CCVS85 | The COBOL-85 validation suite, all supported modules |
| Fuzz testing | Mutated sources compiled under AddressSanitizer and UBSan |

Continuous integration builds the compiler, runs the test suites and the NIST
validation suite on Linux, and verifies the Windows compiler on Windows for
every change.

### Code Quality

The compiler source is reviewed and restructured on an ongoing basis. The
largest parts of the parser, the IR lowering and the x86-64 code generator are
organized as small functions (one per statement, per data-division phase,
per group of instructions), dispatched through tables and flat switches
rather than long, deeply nested conditional chains. Reviews include builds
with strict compiler warnings and static analysis, and restructurings are
checked by comparing the generated assembly and diagnostics for the test
programs and the NIST suite before and after the change.

## Examples

| Example | Demonstrates |
|---|---|
| [hello](examples/hello) | Minimal program |
| [payroll](examples/payroll) | Copybooks, SORT with input and output procedures, CALL, edited output |
| [inventory](examples/inventory) | Indexed files: loading, random access, REWRITE, START and sequential reading |
| [sales-report](examples/sales-report) | Report Writer: page headings, control breaks and totals |

## Known Limitations

- The Communication module is not supported.
- SORT and MERGE operate in memory; indexed files build their indexes in
  memory when opened.
- A program, including its data, is limited to 2 GiB.
- Object files produced with `-c` are linked by Cobalt only.
- In the Report Writer, the data-name of a SUM entry refers to the printed
  field rather than the sum counter.
- Exponentiation with a non-integer exponent raises a size error.

See [Limits](docs/guide/09-limits.md) for the complete list.

## Roadmap

| Area | Planned work |
|---|---|
| Language standards | COBOL 2002 and COBOL 2014 |
| Targets | Additional architectures, beginning with AArch64, and additional operating systems |
| Interoperability | Calling and being called from C; dynamic CALL of separately compiled modules; COFF object files |
| Data and files | COMP-1 and COMP-2 floating point; sorting and indexed files larger than memory |
| Tooling | Debugging information (DWARF line numbers); compiler listings with cross-reference and data map |

## Documentation

| Document | Contents |
|---|---|
| [User Guide](docs/guide/README.md) | Installation, command line, language reference, strictness and profiles, diagnostics, files, execution, platforms and limits |
| [Release Notes](CHANGELOG.md) | Changes in each release |
| [Runtime ABI](runtime/ABI.md) | Data representation, files, inter-program calls and operating system interface |
| [Assembler](docs/assembler.md) | The integrated assembler |
| [Linker](docs/linker.md) | The integrated linker |
| [Code Generation](docs/optimization.md) | Code generation and optimization |

## License

Cobalt is licensed under the [MIT License](LICENSE).

Copyright © 2026 Milán Major.
