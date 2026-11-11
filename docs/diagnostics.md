# Diagnostics

cobalt reports errors, warnings and the notes that explain them in one
format:

```
LEDGER.cbl:16:37: error: 'TOTAL-AMONT' is not defined
    16 |            MOVE CUSTOMER-BALANCE TO TOTAL-AMONT.
       |                                     ^~~~~~~~~~~
LEDGER.cbl:11:12: note: did you mean 'TOTAL-AMOUNT'?
    11 |        01  TOTAL-AMOUNT          PIC S9(9)V99.
       |            ^~~~~~~~~~~~
1 error generated.
```

- The location is the file, line and column; the source line follows, with
  the word or literal at the column underlined.
- Notes point at what explains the message: where an item is declared, the
  candidates of an ambiguous name, the closest name to a misspelled one, the
  OCCURS of a table, the parenthesis a `)` should match.
- Text brought in by `COPY` is reported where it is, in the library text,
  after the chain of `COPY` statements that brought it in:

  ```
  In text copied at BADREC.cpy:5:12,
                   from LEDGER.cbl:7:8:
  INNER.cpy:1:46: error: VALUE 1234 does not fit PICTURE '9(3)' of 'INNER-AMOUNT'
  ```

- Every stage reports what it finds: a name that is not defined does not
  stop the checks that follow, and an error found again by a later stage is
  shown once.
- Colors are used on a terminal (`-fdiagnostics-color=always|never|auto`;
  `NO_COLOR` turns them off). `-fmax-errors=N` stops after N errors.

## Strictness (`-X`)

COBOL-85 (ANSI X3.23-1985 with its 1989 amendment of intrinsic functions)
is the language cobalt compiles. What it does with an element outside it
depends on the strictness level:

| Level | Elements outside COBOL-85 | Also |
|---|---|---|
| `relaxed` | accepted | the reference format (areas A and B, column 72) is not checked |
| `normal` (default) | accepted, each with a warning (`-Wextension`) | |
| `pedantic` | errors | obsolete elements are flagged (`-Wobsolete`) |
| `ibm-strict` | errors, except IBM Enterprise COBOL's | as `pedantic` |

The extensions cobalt knows:

| Element | IBM has it |
|---|---|
| USAGE COMP-3, COMP-4, COMP-5 (and COMPUTATIONAL-n) | yes |
| Nonnumeric literals in apostrophes | yes |
| `ACCEPT ... FROM DATE YYYYMMDD`, `FROM DAY YYYYDDD` | yes |
| `_` in a user-defined word | no |
| Words longer than 30 characters | no |
| PICTURE character-strings longer than 30 characters | no |
| Nonnumeric literals longer than 160 characters | no |
| Free-form reference format (`-F`) | no |

The vendor dialect (`-D`) is separate: it chooses how things the standard
leaves to the implementor behave (numeric DISPLAY format, sign encoding,
binary truncation), not what is accepted.

## FIPS subsets (`--subset`)

FIPS PUB 21-4 defines three subsets of COBOL-85. `--subset=minimum` or
`--subset=intermediate` reports each element the subset leaves out with a
warning `NON-CONFORMING STANDARD: ... [-Wsubset]` (`-Werror=subset` makes
them errors); `--subset=high`, the default, reports nothing.

| Subset | Modules (level) |
|---|---|
| Minimum | Nucleus 1, Sequential I-O 1, Inter-Program Communication 1 |
| Intermediate | Nucleus, Sequential, Relative and Indexed I-O, Sort-Merge, Source Text Manipulation, Inter-Program Communication, Segmentation, Debug: level 1 |
| High | all of them, level 2, with Report Writer and Intrinsic Functions |

The level 2 elements flagged include: COMPUTE, EVALUATE, INITIALIZE,
SEARCH, STRING, UNSTRING, SET ... TO TRUE, INSPECT CONVERTING, nested IF,
PERFORM VARYING and WITH TEST, CORRESPONDING, DIVIDE REMAINDER, reference
modification, qualification, more than three subscripts, arithmetic
expressions in conditions, sign and combined conditions, condition-names,
RENAMES, OCCURS DEPENDING ON and KEY, VALUE ALL, nested REDEFINES, the
continuation of words and numeric literals, SYMBOLIC CHARACTERS, ALPHABET
literals, DATE-COMPILED; OPTIONAL, RESERVE, ACCESS DYNAMIC, ALTERNATE
RECORD KEY, RECORD VARYING, LINAGE, OPEN EXTEND, CLOSE WITH LOCK, READ NEXT,
START; GLOBAL, EXTERNAL, nested programs, COMMON, INITIAL, END PROGRAM,
CANCEL, BY REFERENCE / CONTENT; COPY REPLACING, REPLACE; SEGMENT-LIMIT.
The NIST CCVS85 flagging programs (the 30xM ones with `--subset=minimum`,
the 40xM ones with `--subset=intermediate`) find every message they expect.

## Warnings

`-Wname` turns a warning on, `-Wno-name` off; `-Wall` turns on the default
set, `-Wextra` all of them; `-Werror` makes warnings errors (`-Werror=name`
one of them); `-w` turns them all off. Each warning ends with the option
that controls it, e.g. `[-Wtruncate]`.

| Warning | Default | Reports |
|---|---|---|
| `truncate` | on | a literal that does not fit the item it is moved to (characters, high-order digits, decimal places, sign) |
| `unreachable` | on | a statement after GO TO, STOP RUN (or EXIT PROGRAM in a called program) in the same procedure |
| `constant-condition` | on | a relation always true or always false by the item's PICTURE (`PIC 9(3)` compared with 1000) |
| `divide-by-zero` | on | division by the literal zero |
| `perform-range` | on | PERFORM A THRU B where B comes before A |
| `unused-file` | on | a file no statement opens or names |
| `column-overflow` | on | a word that runs past column 72 (columns 73-80 are ignored) |
| `area` | on | in area B, what belongs in area A (division and section headers, paragraph names, FD / SD / RD, level 01 and 77, DECLARATIVES); a statement in area A |
| `extension` | on | an element outside COBOL-85 (see `-X`) |
| `subset` | with `--subset` | an element outside the chosen FIPS subset |
| `unused` | -Wextra | a data item never referenced (not those of copied text) |
| `unused-procedure` | -Wextra | a paragraph or section neither performed nor reached by falling into it |
| `narrowing` | -Wextra | MOVE of an item to a smaller one |
| `uninitialized` | -Wextra | an item statements read but none gives a value, without a VALUE clause |
| `unchecked-io` | -Wextra | an I-O statement without AT END / INVALID KEY whose exception no FILE STATUS item or USE procedure handles |
| `obsolete` | -X pedantic | an obsolete element of COBOL-85 (FIPS flagging; also `--flag-obsolete`) |

A program that fills columns 73-80 (the identification area) on purpose,
writing the same text there on most lines, gets no `column-overflow`
warnings.

## Tests

`tests/diag/NAME.cbl` is compiled with the options in `NAME.flags`, and
the compiler's messages are compared with `NAME.err` (`NAME.src` names
another source, to check one program with other options). `make test` runs
them; `UPDATE=1 tools/diag_test.sh NAME` writes `NAME.err` from the output
(read it before committing).
