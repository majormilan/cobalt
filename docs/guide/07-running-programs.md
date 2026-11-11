# 7. Running programs

An executable built by cobalt needs nothing else: no runtime library, no
environment variables, no configuration. It runs the main program from the
start of its PROCEDURE DIVISION until STOP RUN or the end of the PROCEDURE
DIVISION.

## The console

**DISPLAY** writes its operands one after the other on standard output, then
a newline (none `WITH NO ADVANCING`). Numeric items are shown as the dialect
says (chapter 4): by default their stored characters. DISPLAY UPON any
mnemonic-name (CONSOLE, SYSOUT, SYSERR) writes to standard output as well.

**ACCEPT** reads one line from standard input:

- an alphanumeric item takes the line left-justified, padded with spaces;
  the characters beyond the item's length are discarded;
- a numeric item takes the digits right-justified, padded with zeros
  (`42` into `PIC 9(4)` is `0042`);
- a carriage return before the newline is ignored, so input from Windows
  works on Linux and the other way round;
- a line of 80 characters or more is a full console record: if it does not
  fill an alphanumeric item, the next line continues it;
- at the end of the input the item is filled with spaces (or zeros).

**ACCEPT FROM** gives the date and time:

| Phrase | Format | Example |
|---|---|---|
| `FROM DATE` | YYMMDD | `261111` |
| `FROM DATE YYYYMMDD` (an extension) | YYYYMMDD | `20261111` |
| `FROM DAY` | YYDDD (the day of the year) | `26315` |
| `FROM DAY YYYYDDD` (an extension) | YYYYDDD | `2026315` |
| `FROM DAY-OF-WEEK` | 1 digit, Monday is 1 | `3` |
| `FROM TIME` | HHMMSSss (hundredths) | `14302517` |

The date and time are local. FUNCTION CURRENT-DATE gives the date and time
with hundredths; its offset from UTC is `00000`.

## Numbers

Numeric items hold up to 18 digits, the limit of COBOL-85. Their storage
follows their USAGE:

| USAGE | Storage |
|---|---|
| DISPLAY (the default) | one ASCII digit per byte; a sign (S) overpunched on the last digit, as chapter 4 describes |
| DISPLAY with SIGN LEADING / TRAILING SEPARATE | the digits and a `+` or `-` character |
| BINARY, COMP, COMP-4 | two's complement, big-endian (as on IBM systems; data files read the same on every target), 2 bytes for 1 to 4 digits, 4 for 5 to 9, 8 for 10 to 18; the value is limited by the PICTURE's digits unless `-f binary-truncate=no` |
| COMP-5 | the same sizes in the machine's byte order (little-endian); the value uses all the bits |
| PACKED-DECIMAL, COMP-3 | two digits per byte, the sign in the low half of the last byte (C positive, D negative, F unsigned) |

Arithmetic is decimal and exact:

- an intermediate result keeps the decimals of its operands and at least two
  more than the receiver's, so ROUNDED always sees the next digit;
- without ROUNDED, decimals beyond the receiver's are dropped; ROUNDED
  rounds half away from zero (2.345 to 2.35, -2.345 to -2.35);
- a result with more integer digits than the receiver holds loses the
  high-order digits, as the standard requires; with ON SIZE ERROR the
  receiver keeps its value and the SIZE ERROR phrase runs;
- division by zero is a size error: the receiver keeps its value, with or
  without ON SIZE ERROR;
- an unsigned receiver takes the absolute value of a negative result;
- exponentiation (`**`) takes an integer exponent, a literal or an item;
  an exponent with decimals is a size error in this release.

A byte that is not a digit in a numeric DISPLAY item counts as 0 when the
item is used in arithmetic. `IF item IS NUMERIC` tells whether the item
holds valid data.

## Programs and CALL

The programs of a run unit are those of the sources and objects given to
cobalt (chapter 2). CALL finds a program by its PROGRAM-ID, written as a
literal or held in an alphanumeric item; case and trailing spaces do not
matter.

```cobol
           CALL "COMPUTE-TAX" USING BY REFERENCE EMPLOYEE-RECORD
                                    BY CONTENT TAX-YEAR
               ON EXCEPTION
                   DISPLAY "COMPUTE-TAX is not available"
           END-CALL.
```

- BY REFERENCE passes the item itself, BY CONTENT a copy. The called
  program receives them in its LINKAGE SECTION, named in PROCEDURE DIVISION
  USING.
- A CALL of a program that is not in the run unit runs ON EXCEPTION (or ON
  OVERFLOW); without one, the run ends with an error.
- A called program keeps its storage from one CALL to the next. CANCEL puts
  it back to its initial state for the next CALL; an INITIAL program starts
  in that state at every CALL.
- EXIT PROGRAM returns to the caller (in the main program it does nothing);
  GOBACK (an extension) returns to the caller, or ends the run in the main
  program;
  STOP RUN ends the run unit, in any program.
- Nested programs are called by their containing program, or, if COMMON, by
  their siblings. GLOBAL items and files are visible in the contained
  programs; EXTERNAL items and files are shared by every program of the run
  unit that declares them with the same name.

Programs in other languages cannot be called in this release.

## The environment

| Variable | Effect |
|---|---|
| `COB_SWITCH_n` (n from 1 to 9) | `ON` starts SWITCH-n on; otherwise it starts off. SET mnemonic-name TO ON / OFF changes it while the program runs. |
| `DD_name`, `dd_name`, `name` | the path of the file assigned to `name` (chapter 6) |
| `COB_FILE_PATH` | the directory of the files whose names have no directory and no variable of their own (chapter 6) |
| `COB_SET_DEBUG` | `Y`, `y`, `1` or `ON` turns on the debugging switch: the USE FOR DEBUGGING procedures run (in a program compiled WITH DEBUGGING MODE). |

```
$ COB_SWITCH_1=ON ./ledger
```

## Run-time errors

When something that the program cannot go on from happens, it writes one
line to standard error, closes its open files and ends with exit status 1:

```
ledger.cbl:31: run-time error in program LEDGER: I-O error on file 'CUSTOMERS': status 35 (the file does not exist)
```

The line of the source and the program are those of the statement that
failed. The errors:

| Error | When |
|---|---|
| `CALL of a program that is not in the run unit: 'NAME'` | a CALL without ON EXCEPTION or ON OVERFLOW |
| `I-O error on file 'NAME': status NN (meaning)` | a status of 30 or more on a file with no FILE STATUS item and no USE procedure (chapter 6) |
| a run-time check (below) | compiled with `-fcheck` |

## Run-time checks (`-fcheck`)

COBOL-85 leaves undefined what happens when a subscript, a reference
modification or an OCCURS DEPENDING ON value is out of range. By default
cobalt does not check them, which is the fastest. Compiled with `-fcheck`,
the program checks:

- each subscript (and index) against the OCCURS of its table, or against
  the current value of the DEPENDING ON item;
- the DEPENDING ON item, wherever the length of its table is used, against
  its `OCCURS min TO max`;
- the start, length and end of a reference modification against the length
  of the item;
- the contents of each numeric DISPLAY item its value is taken from: a
  byte that is not a digit (spaces moved through a group, a REDEFINES,
  data read from a file) ends the run instead of counting as 0.

```
ledger.cbl:42: run-time error in program LEDGER: the subscript of 'ENTRY' is 11, out of range 1 to 10
ledger.cbl:57: run-time error in program LEDGER: the reference modification start of 'NAME' is 0, out of range 1 to 29
ledger.cbl:63: run-time error in program LEDGER: a numeric item holds '    ', which is not a number
```

The check of numeric items is on for the whole run unit when its main
program is compiled with `-fcheck`.

Literal subscripts and reference modifications are checked when compiling,
with or without `-fcheck`.

## Exit status

| Status | When |
|---|---|
| 0 | STOP RUN, or the end of the main program |
| RETURN-CODE | when a program of the run unit uses the RETURN-CODE special register (an extension): its value at STOP RUN |
| 1 | a run-time error |

```cobol
           IF ERROR-COUNT > 0
               MOVE 8 TO RETURN-CODE
           END-IF
           STOP RUN.
```

On Linux the shell sees the status modulo 256. RETURN-CODE is one item for
the whole run unit: a called program that sets it sets it for its caller.

## Debugging

A program with `WITH DEBUGGING MODE` in its SOURCE-COMPUTER paragraph
compiles its debugging lines (D in column 7) and its USE FOR DEBUGGING
declaratives. The declaratives run only while the debugging switch is on
(`COB_SET_DEBUG`); the debugging lines always run. Without DEBUGGING MODE
both are comments. DEBUG-ITEM holds the line, the name, the subscripts and
the contents of what was referred to, as the standard describes.

Executables have a symbol table (unless `--strip`), but its names are
those of the generated code and the runtime, not the COBOL names: there is
no source-level debugging information in this release.
