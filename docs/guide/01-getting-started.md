# 1. Getting started

## Installing

cobalt is a single executable with everything it needs inside: the
compiler, an assembler, a linker and the runtime of every system it builds
for. There is nothing else to install.

Build it from source (chapter 8 has the details):

```
$ git clone https://github.com/majormilan/cobalt.git
$ cd cobalt
$ make                  # bin/cobalt, for Linux
$ make windows          # bin/cobalt.exe, for Windows (needs MinGW-w64)
```

Then copy `bin/cobalt` (or `bin/cobalt.exe`) to a directory on your PATH:

```
$ sudo cp bin/cobalt /usr/local/bin/
$ cobalt --version
cobalt 0.1.0-alpha
```

## A first program

Save this as `hello.cbl`; the program text starts in column 8:

```cobol
       IDENTIFICATION DIVISION.
       PROGRAM-ID. HELLO.
       PROCEDURE DIVISION.
           DISPLAY "Hello, world!".
           STOP RUN.
```

Compile and run it:

```
$ cobalt hello.cbl
$ ./hello
Hello, world!
```

`cobalt hello.cbl` compiles, assembles and links in one step and writes the
executable `hello` (`hello.exe` on Windows) in the current directory. `-o`
names it otherwise:

```
$ cobalt hello.cbl -o bin/hello
```

## When something is wrong

cobalt shows the source line, underlines what is wrong and explains it:

```
$ cobalt payroll.cbl
payroll.cbl:16:37: error: 'TOTAL-AMONT' is not defined
    16 |            MOVE CUSTOMER-BALANCE TO TOTAL-AMONT.
       |                                     ^~~~~~~~~~~
payroll.cbl:11:12: note: did you mean 'TOTAL-AMOUNT'?
    11 |        01  TOTAL-AMOUNT          PIC S9(9)V99.
       |            ^~~~~~~~~~~~
1 error generated.
```

It reports everything it finds in one run, then stops with exit status 1
and writes no executable. Chapter 5 describes the messages and the warnings.

## Several programs

A run unit (the main program and the programs it calls) may be in several
sources. Give them all; the first program of the first source is the main
program:

```
$ cobalt payroll.cbl taxes.cbl reports.cbl -o payroll
```

A source may also hold several programs, one after the other or nested,
each ending with END PROGRAM. CALL finds the called programs by their
PROGRAM-ID (chapter 7).

To compile called programs separately, compile them to objects with `-c`
and give the objects when building the executable:

```
$ cobalt -c --module taxes.cbl       # taxes.o; --module: it has no main program
$ cobalt payroll.cbl taxes.o -o payroll
```

## Copybooks

COPY brings in a library text. cobalt looks for it next to the source that
holds the COPY statement, then in each `-I` directory, then in the
directories of the `COBCPY` environment variable, trying the name as given,
in upper case and in lower case, with no extension and with `.cpy`, `.CPY`,
`.cbl`, `.CBL`, `.cob` and `.COB`:

```cobol
       WORKING-STORAGE SECTION.
       COPY CUSTREC.
       COPY CUSTREC REPLACING CUST-RECORD BY SAVED-RECORD.
```

```
$ cobalt -I copylib ledger.cbl
```

Errors inside a copybook are shown in the copybook, after the COPY
statements that brought it in.

## Files

A file is named in its SELECT entry; the ASSIGN clause gives the name of the
file on disk:

```cobol
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT CUSTOMERS ASSIGN TO "customers.dat"
               ORGANIZATION IS INDEXED
               ACCESS MODE IS DYNAMIC
               RECORD KEY IS CUSTOMER-ID
               FILE STATUS IS CUSTOMER-STATUS.
```

A relative path is relative to the directory the program runs in. Chapter 6
describes the organizations, the status codes and the formats on disk.

## A stricter or looser compiler

By default cobalt accepts the common extensions to COBOL-85 with a warning.
`-X pedantic` rejects them; `-X relaxed` accepts them silently. `-Wextra`
turns on every warning:

```
$ cobalt -X pedantic -Wextra ledger.cbl
```

Chapter 4 has the levels and the dialects, which decide what the standard
leaves to the implementor (for example how DISPLAY shows a number).

## Checking a program while it runs

`-fcheck` makes the program check its subscripts and reference
modifications, and stop with the source line when one is out of range:

```
$ cobalt -fcheck ledger.cbl
$ ./ledger
ledger.cbl:42: run-time error in program LEDGER: the subscript of 'ENTRY' is 11, out of range 1 to 10
```

## Building for the other system

Every cobalt builds for Linux and for Windows; `-t` picks the target:

```
$ cobalt -t x86_64-pc-windows-msvc hello.cbl -o hello.exe
```
