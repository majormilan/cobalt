# 4. Strictness, dialects and FIPS flagging

Three independent settings shape what cobalt accepts and how programs
behave:

| Setting | Decides | Options |
|---|---|---|
| Strictness | what is done with elements outside COBOL-85 | `-X relaxed \| normal \| pedantic \| ibm-strict` |
| Dialect | how what the standard leaves to the implementor behaves | `-D standard \| ibm \| gnucobol \| microfocus`, `-f KEY=VALUE` |
| FIPS flagging | which standard elements are reported | `--flag-obsolete`, `--subset=minimum \| intermediate` |

## Strictness (`-X`)

| Level | Elements outside COBOL-85 | Also |
|---|---|---|
| `relaxed` | accepted silently | the reference format is not checked (no `-Warea`, `-Wcolumn-overflow`) |
| `normal` (default) | accepted, each with a warning (`-Wextension`) | |
| `pedantic` | rejected: an error each | the obsolete elements are flagged (`-Wobsolete`) |
| `ibm-strict` | rejected, except IBM Enterprise COBOL's | as `pedantic` |

The extensions cobalt knows:

| Element | IBM has it |
|---|---|
| USAGE COMP-3, COMP-4, COMP-5 (and COMPUTATIONAL-n) | yes |
| Nonnumeric literals in apostrophes (`'text'`) | yes |
| `ACCEPT ... FROM DATE YYYYMMDD`, `FROM DAY YYYYDDD` | yes |
| ORGANIZATION LINE SEQUENTIAL | yes |
| The RETURN-CODE special register | yes |
| `_` in a user-defined word | no |
| Words longer than 30 characters | no |
| PICTURE character-strings longer than 30 characters | no |
| Nonnumeric literals longer than 160 characters | no |
| Free-form reference format (`-F`) | no |

Under `normal` an extension gives one warning where it first appears (a
long word: once, not at every use):

```
ledger.cbl:7:42: warning: USAGE COMP-3 is an extension to COBOL-85 [-Wextension]
     7 |        01  PACKED-AMOUNT    PIC S9(7)V99 COMP-3.
       |                                          ^~~~~~
note: the standard name of this usage is PACKED-DECIMAL
```

Under `pedantic` the same is an error (`USAGE COMP-3 is not COBOL-85`), with
a note saying which levels accept it. `-Wno-extension` silences the
warnings of `normal`.

## Dialects (`-D`)

The standard leaves some behavior to the implementor. A dialect chooses it
the way another compiler does; it never changes what is accepted.

| Setting (`-f`) | Values | Meaning |
|---|---|---|
| `display` | `stored`, `pretty` | DISPLAY of a numeric item: its stored characters (all digits, the sign overpunched, no decimal point), or edited (sign, all digits, the decimal point) |
| `sign` | `ascii`, `ebcdic` | how a sign overpunched on a digit is written: `p` to `y` for -0 to -9 (positive digits plain), or `{` `A`..`I` / `}` `J`..`R` |
| `binary-truncate` | `yes`, `no` | whether a BINARY (COMP) item holds only its PICTURE's digits, or all its bytes can hold |

| Dialect | `display` | `sign` | `binary-truncate` |
|---|---|---|---|
| `standard` (default) | `stored` | `ascii` | `yes` |
| `ibm` | `stored` | `ebcdic` | `yes` |
| `gnucobol` | `pretty` | `ascii` | `yes` |
| `microfocus` | `stored` | `ascii` | `yes` |

`-f` changes one setting after `-D`: `-D ibm -f binary-truncate=no`.

The same items under each dialect (`S PIC S999 VALUE -45`, `V PIC 99V99
VALUE 1.5`, `C PIC S9(4) COMP VALUE -7`, `E PIC -ZZ9.99` holding -1.5):

```
DISPLAY "[" S "][" V "][" C "][" E "]"
standard:    [04u][0150][000w][-  1.50]
ibm:         [04N][0150][000P][-  1.50]
gnucobol:    [-045][01.50][-0007][-  1.50]
microfocus:  [04u][0150][000w][-  1.50]
```

Reading data, the runtime accepts both sign conventions whatever the
dialect, so files from other systems read correctly.

## FIPS flagging

FIPS PUB 21-4, the federal standard for COBOL-85 compilers, asks for
messages on two kinds of elements:

**Obsolete elements** (`--flag-obsolete`, or `-Wobsolete`; on under
`pedantic`): the elements COBOL-85 marks for deletion from the next
standard, such as ALTER, GO TO without a procedure-name, ENTER, STOP literal,
LABEL RECORDS, VALUE OF, DATA RECORDS, MEMORY SIZE, RERUN, MULTIPLE FILE
TAPE, the AUTHOR (and other comment) paragraphs, OPEN REVERSED, the Debug and
Segmentation modules. Each gives `warning: OBSOLETE: ...`.

**Subsets** (`--subset=minimum` or `--subset=intermediate`): FIPS defines
three subsets of COBOL-85. An element the chosen subset leaves out gives
`warning: NON-CONFORMING STANDARD: ...` (`-Wsubset`; `-Werror=subset` makes
them errors):

| Subset | Modules (level) |
|---|---|
| Minimum | Nucleus 1, Sequential I-O 1, Inter-Program Communication 1 |
| Intermediate | level 1 of Nucleus, Sequential, Relative and Indexed I-O, Sort-Merge, Source Text Manipulation, Inter-Program Communication, Segmentation, Debug |
| High (default) | level 2 of all of them, with Report Writer and Intrinsic Functions |

```
$ cobalt --subset=intermediate ledger.cbl
ledger.cbl:24:12: warning: NON-CONFORMING STANDARD: COMPUTE is not in the Intermediate subset (Nucleus level 2) [-Wsubset]
```

The level 2 elements reported include COMPUTE, EVALUATE, INITIALIZE,
SEARCH, STRING, UNSTRING, SET TO TRUE, INSPECT CONVERTING, nested IF, PERFORM
VARYING and WITH TEST, CORRESPONDING, DIVIDE REMAINDER, reference
modification, qualification, more than three subscripts, arithmetic
expressions in conditions, sign and combined conditions, condition-names,
RENAMES, OCCURS DEPENDING ON and KEY, VALUE ALL, nested REDEFINES, continued
words and numeric literals, SYMBOLIC CHARACTERS, ALPHABET literals,
DATE-COMPILED; OPTIONAL, RESERVE, ACCESS DYNAMIC, ALTERNATE RECORD KEY,
RECORD VARYING, LINAGE, OPEN EXTEND, CLOSE WITH LOCK, READ NEXT, START;
GLOBAL, EXTERNAL, nested programs, COMMON, INITIAL, END PROGRAM, CANCEL, BY
REFERENCE / CONTENT; COPY REPLACING, REPLACE; SEGMENT-LIMIT. Under
`minimum`, the elements of Relative and Indexed I-O, Sort-Merge, COPY, the
Debug and Report Writer modules are reported as well.

The NIST flagging programs find every message they expect: the 30xM
programs compiled with `--subset=minimum`, the 40xM ones with
`--subset=intermediate`, both with `--flag-obsolete`.
