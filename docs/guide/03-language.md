# 3. The language

In release 0.1.0-alpha, Cobalt compiles **COBOL-85**: ANSI X3.23-1985
(ISO 1989:1985) with its 1989 amendment, the intrinsic functions. Its modules are implemented at their
highest level (level 2), except the Communication module, which is not
supported. It is checked with the NIST CCVS85 validation suite.

This chapter describes the language as Cobalt implements it, with what it
adds and what it leaves out. Chapter 4 explains how the extensions are
treated; chapter 7 how programs behave when they run.

## Reference format

**Fixed format** (the default, `--fixed`):

| Columns | Contents |
|---|---|
| 1 to 6 | sequence area: ignored |
| 7 | indicator: space, `*` (comment), `/` (comment, new page in a listing), `-` (continuation), `D` (debugging line) |
| 8 to 11 | area A: division, section and paragraph headers, level 01 and 77, FD / SD / RD, DECLARATIVES |
| 12 to 72 | area B: everything else |
| 73 to 80 | identification area: ignored |

- A continuation line (`-`) continues the word or numeric literal at the end
  of the line before; a nonnumeric literal is continued from the quote that
  starts its continuation.
- Debugging lines are compiled only WITH DEBUGGING MODE; otherwise they are
  comments.
- Names and statements in the wrong area, and words cut at column 72, are
  warned about (`-Warea`, `-Wcolumn-overflow`; not with `-X relaxed`).
- Lines may end with LF or CR LF.

**Free format** (`-F`, an extension): the program text may start in column 1
and run to the end of the line. There are no indicators or debugging lines
in free format; comments are floating comments.

**Floating comments** (`*>`, an extension from COBOL 2002): from `*>` to the
end of the line, in either format, outside literals.

Words are case-insensitive; literals keep their case.

## IDENTIFICATION DIVISION

PROGRAM-ID (with IS COMMON, IS INITIAL) and the paragraphs AUTHOR,
INSTALLATION, DATE-WRITTEN, DATE-COMPILED and SECURITY, whose contents are
comments.

## ENVIRONMENT DIVISION

- **SOURCE-COMPUTER** (WITH DEBUGGING MODE turns on the Debug module) and
  **OBJECT-COMPUTER** (PROGRAM COLLATING SEQUENCE, SEGMENT-LIMIT; MEMORY
  SIZE is accepted with no effect).
- **SPECIAL-NAMES**: SWITCH-1 to SWITCH-9 with ON / OFF STATUS
  condition-names, CONSOLE, SYSIN, SYSOUT, SYSERR, the channel names C01 to
  C12 and TOP (for ADVANCING), ALPHABET (STANDARD-1, STANDARD-2, NATIVE, or
  literals with THRU and ALSO), SYMBOLIC CHARACTERS, CLASS, CURRENCY SIGN,
  DECIMAL-POINT IS COMMA.
- **FILE-CONTROL**: SELECT [OPTIONAL] with ASSIGN, ORGANIZATION
  (SEQUENTIAL, LINE SEQUENTIAL, RELATIVE, INDEXED), ACCESS MODE (SEQUENTIAL,
  RANDOM, DYNAMIC), RECORD KEY, ALTERNATE RECORD KEY [WITH DUPLICATES],
  RELATIVE KEY, FILE STATUS, PADDING CHARACTER and RECORD DELIMITER;
  RESERVE is accepted with no effect. LINE SEQUENTIAL is an extension.
- **I-O-CONTROL**: SAME RECORD AREA (the files share their record area),
  SAME SORT AREA and SAME SORT-MERGE AREA, MULTIPLE FILE TAPE and RERUN
  (accepted with no effect).

## DATA DIVISION

Sections in this order: FILE SECTION, WORKING-STORAGE SECTION, LINKAGE
SECTION, REPORT SECTION. (COMMUNICATION SECTION is rejected.)

**File description entries.** FD with BLOCK CONTAINS (no effect), RECORD
CONTAINS and RECORD IS VARYING [DEPENDING ON], LABEL RECORDS, VALUE OF and
DATA RECORDS (obsolete, no effect), LINAGE (with FOOTING, TOP, BOTTOM),
CODE-SET (no effect: data is ASCII), REPORT IS, GLOBAL, EXTERNAL. SD for sort
files.

**Data description entries.** Levels 01 to 49, 66 (RENAMES ... THRU), 77 and
88 (condition-names with VALUE literals and THRU ranges). The clauses:

| Clause | |
|---|---|
| PICTURE | `9 X A S V P Z * B 0 / , . + - CR DB $` and the currency sign; up to 18 digits in a numeric item |
| USAGE | DISPLAY, BINARY (COMPUTATIONAL, COMP), PACKED-DECIMAL, INDEX; COMP-3, COMP-4, COMP-5 as extensions |
| VALUE | literals, figurative constants, ALL literal |
| OCCURS | `n TIMES`, `m TO n TIMES DEPENDING ON`, ASCENDING / DESCENDING KEY, INDEXED BY; tables of up to 16 dimensions (COBOL-85 requires 7) |
| REDEFINES, RENAMES | |
| JUSTIFIED RIGHT, BLANK WHEN ZERO | |
| SIGN | LEADING or TRAILING, SEPARATE CHARACTER |
| SYNCHRONIZED | accepted; items are never aligned (no padding) |
| GLOBAL, EXTERNAL | shared with contained programs, or by name across the run unit |

Chapter 7 describes how each usage is stored.

## PROCEDURE DIVISION

Sections and paragraphs, DECLARATIVES with USE procedures, and these
statements (the scope terminators END-ADD ... END-WRITE are accepted where
the standard has them):

| Statements | |
|---|---|
| Arithmetic | ADD, SUBTRACT, MULTIPLY, DIVIDE (with REMAINDER), COMPUTE; ROUNDED; ON SIZE ERROR / NOT ON SIZE ERROR; CORRESPONDING |
| Data | MOVE (and MOVE CORRESPONDING), INITIALIZE (REPLACING), SET (index-names, TO TRUE, UP BY / DOWN BY, switches ON / OFF), INSPECT (TALLYING, REPLACING, CONVERTING), STRING, UNSTRING |
| Control | IF / ELSE, EVALUATE (with ALSO, THRU, ANY, TRUE, FALSE), PERFORM (procedures, THRU, TIMES, UNTIL, VARYING ... AFTER, WITH TEST BEFORE / AFTER, inline), GO TO (DEPENDING ON), ALTER, CONTINUE, EXIT, NEXT SENTENCE, STOP RUN, STOP literal |
| Tables | SEARCH, SEARCH ALL |
| Input and output | ACCEPT (from the console, DATE, DAY, DAY-OF-WEEK, TIME), DISPLAY (UPON, WITH NO ADVANCING) |
| Files | OPEN (INPUT, OUTPUT, I-O, EXTEND; REVERSED, NO REWIND), CLOSE (REEL / UNIT, FOR REMOVAL, NO REWIND, LOCK), READ (NEXT, INTO, KEY IS), WRITE (FROM, ADVANCING, AT END-OF-PAGE), REWRITE, DELETE, START, with AT END / INVALID KEY and their NOT phrases |
| Sort and merge | SORT (ASCENDING / DESCENDING KEY, WITH DUPLICATES IN ORDER, COLLATING SEQUENCE, USING / GIVING, INPUT / OUTPUT PROCEDURE), MERGE, RELEASE, RETURN |
| Programs | CALL (BY REFERENCE / BY CONTENT, ON EXCEPTION / OVERFLOW), CANCEL, EXIT PROGRAM; nested programs with COMMON and INITIAL |
| Report Writer | INITIATE, GENERATE, TERMINATE, SUPPRESS PRINTING |
| Obsolete | ENTER (accepted, no effect) |

Conditions: relation conditions (with arithmetic expressions), class
conditions (NUMERIC, ALPHABETIC, ALPHABETIC-LOWER, ALPHABETIC-UPPER, a
CLASS name), sign conditions, condition-names, switch-status conditions,
NOT, AND and OR, abbreviated combined relations (`A = 1 OR 2`), and
parenthesized conditions.

References: qualification (`OF` / `IN`), subscripts (literals, data items,
index-names, `item + n`), reference modification (`ITEM (start:length)`).

## Source text manipulation

COPY text-name [OF / IN library] [SUPPRESS] [REPLACING ...], with
replacements by pseudo-text (`==...==`), identifiers, literals or words, and
REPLACE ... / REPLACE OFF. COPY may be nested. Chapter 1 says where library
texts are found.

## Intrinsic functions

All 42 functions of the 1989 amendment: ACOS, ANNUITY, ASIN, ATAN, CHAR, COS,
CURRENT-DATE, DATE-OF-INTEGER, DAY-OF-INTEGER, FACTORIAL, INTEGER,
INTEGER-OF-DATE, INTEGER-OF-DAY, INTEGER-PART, LENGTH, LOG, LOG10,
LOWER-CASE, MAX, MEAN, MEDIAN, MIDRANGE, MIN, MOD, NUMVAL, NUMVAL-C, ORD,
ORD-MAX, ORD-MIN, PRESENT-VALUE, RANDOM, RANGE, REM, REVERSE, SIN, SQRT,
STANDARD-DEVIATION, SUM, TAN, UPPER-CASE, VARIANCE, WHEN-COMPILED. A table
argument may be given with ALL subscripts (`FUNCTION MAX (TABLE (ALL))`).

The transcendental functions use the x87 floating point unit (64-bit
mantissa); their results have 12 decimal places, SQRT and the statistics 9.
CURRENT-DATE gives the time in UTC, with 00000 as the offset.

## Segmentation

Sections with segment numbers: 0 to 49 are fixed segments, 50 to 99
independent segments, which are in their initial state (ALTER undone) each
time control enters them from another segment. SEGMENT-LIMIT is accepted.
All segments are in memory together.

## Debug module

WITH DEBUGGING MODE compiles the debugging lines and the USE FOR DEBUGGING
procedures, on procedures, ALL PROCEDURES, files and data items (ALL
REFERENCES OF), with the DEBUG-ITEM special register. The procedures run
only when the program runs with the object-time switch on: the environment
variable `COB_SET_DEBUG` set to `Y`, `y`, `1` or `ON` (chapter 7).

## Report Writer

The REPORT SECTION with RD entries (CONTROL [FINAL], PAGE LIMIT with HEADING,
FIRST DETAIL, LAST DETAIL and FOOTING, CODE) and report groups of every type
(REPORT HEADING, PAGE HEADING, CONTROL HEADING, DETAIL, CONTROL FOOTING,
PAGE FOOTING, REPORT FOOTING) with LINE (absolute, PLUS, NEXT PAGE), NEXT
GROUP, COLUMN, SOURCE, VALUE, SUM (UPON, RESET), GROUP INDICATE, JUSTIFIED
and BLANK WHEN ZERO; PAGE-COUNTER and LINE-COUNTER; INITIATE, GENERATE (a
detail group, or the report for summary reporting), TERMINATE; USE BEFORE
REPORTING with SUPPRESS PRINTING.

A report is written to the file whose FD has the REPORT clause, as lines of
text (chapter 6), with a form feed between pages. Control breaks compare the
controls as their stored bytes. The data-name of a SUM entry names its sum
counter, a signed numeric item with the digits of the printed field, which
procedures can read and change.

## Special registers

DEBUG-ITEM (Debug module), LINAGE-COUNTER (a file with LINAGE),
PAGE-COUNTER and LINE-COUNTER (Report Writer), and RETURN-CODE (an
extension, chapter 7).

## Extensions

Cobalt accepts these elements beyond COBOL-85; `-X` decides whether they
are accepted silently, with a warning or rejected (chapter 4):

- USAGE COMP-3, COMP-4, COMP-5 (and COMPUTATIONAL-3, -4, -5);
- nonnumeric literals in apostrophes (`'text'`);
- ACCEPT ... FROM DATE YYYYMMDD and FROM DAY YYYYDDD;
- the underscore in user-defined words; words longer than 30 characters;
  PICTURE strings longer than 30 characters; nonnumeric literals longer
  than 160 characters;
- the RETURN-CODE special register;
- free-form reference format (`-F`);
- ORGANIZATION LINE SEQUENTIAL;
- from COBOL 2002 (IBM Enterprise COBOL has them too): floating comments
  (`*>`); GOBACK, which returns from a called program and ends the run in
  the main program; EXIT PERFORM, which leaves the inline PERFORM it is in,
  and EXIT PERFORM CYCLE, which ends its current iteration; EXIT PARAGRAPH
  and EXIT SECTION, which go to the end of the paragraph or section.

## Not supported

- The Communication module: COMMUNICATION SECTION, CD entries, SEND,
  RECEIVE, ENABLE, DISABLE, PURGE, ACCEPT MESSAGE COUNT (each rejected with
  an error that says so).
- ASSIGN to a data item (`ASSIGN TO identifier` names the file by the word
  itself, chapter 6).
- USAGE COMP-1 and COMP-2 (floating point), EBCDIC data.
- The other elements of later standards and vendor dialects. Cobalt
  recognizes the common ones and says what they are, instead of a syntax
  error: LOCAL-STORAGE and SCREEN SECTION, level 78, USAGE POINTER,
  NATIONAL, BINARY-LONG and the other COBOL 2002 usages, the intrinsic
  functions of COBOL 2002 and 2014 (TRIM, CONCATENATE, ...), CALL ...
  RETURNING, DISPLAY ... AT, ACCEPT ... FROM ENVIRONMENT or COMMAND-LINE,
  ENTRY, the object-oriented statements, EXEC SQL / CICS blocks (which need a
  precompiler), and compiler-directing lines (`>>SOURCE`, `$SET`, IBM
  `PROCESS` / `CBL`).
