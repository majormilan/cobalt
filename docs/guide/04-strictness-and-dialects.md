# 4. Standards, extensions, strictness and dialects

Four settings shape what Cobalt accepts and how programs behave:

| Setting | Decides | Options |
|---|---|---|
| Standard | the language the program is written in | `-std=cobol85` (the only one in this release) |
| Extension sets | which elements beyond the standard are accepted | `-fext=common \| ibm \| gnucobol \| cobol2002 \| none` |
| Strictness | how accepted extensions are reported, and which sets apply | `-X relaxed \| normal \| pedantic \| ibm-strict` |
| Implementation profile | how what the standard leaves to the implementor behaves | `-D standard \| ibm \| gnucobol \| microfocus`, `-f KEY=VALUE` |
| FIPS flagging | which standard elements are reported | `--flag-obsolete`, `--subset=minimum \| intermediate` |

A compilation uses exactly one standard. Elements of other standards and of
vendor dialects are never mixed in silently: they are accepted only through
an extension set.

## Extension sets (`-fext`)

An extension set groups the elements that a vendor compiler or a later
standard adds to COBOL-85. `-fext=SET[,SET...]` enables sets; the option may
be repeated.

| Set | Contents |
|---|---|
| `common` | the extensions in wide use, listed below; enabled by default |
| `ibm` | the extensions of IBM Enterprise COBOL |
| `gnucobol` | the extensions of GnuCOBOL |
| `cobol2002` | the elements of COBOL 2002 that Cobalt accepts in a COBOL-85 program |
| `none` | not a set: drops the default `common` set |

The extensions Cobalt implements, and the sets that contain them:

| Element | common | ibm | gnucobol | cobol2002 |
|---|---|---|---|---|
| USAGE COMP-3, COMP-4, COMP-5 (and COMPUTATIONAL-n) | yes | yes | yes | |
| Nonnumeric literals in apostrophes (`'text'`) | yes | yes | yes | yes |
| `ACCEPT ... FROM DATE YYYYMMDD`, `FROM DAY YYYYDDD` | yes | yes | yes | yes |
| ORGANIZATION LINE SEQUENTIAL | yes | yes | yes | |
| Hexadecimal literals (`X"0D0A"`; the byte 00 not yet) | yes | yes | yes | yes |
| `LENGTH OF identifier` | yes | yes | yes | |
| The RETURN-CODE special register | yes | yes | yes | |
| `_` in a user-defined word | yes | | yes | yes |
| Words longer than 30 characters | yes | | yes | |
| PICTURE character-strings longer than 30 characters | yes | | yes | |
| Nonnumeric literals longer than 160 characters | yes | | yes | |
| Free-form reference format (`-F`) | yes | | yes | yes |
| Floating comments (`*>`) | yes | yes | yes | yes |
| GOBACK | yes | yes | yes | yes |
| EXIT PERFORM, EXIT PERFORM CYCLE | yes | yes | yes | yes |
| EXIT PARAGRAPH, EXIT SECTION | yes | yes | yes | yes |

Elements of later standards and vendor dialects that Cobalt does not
implement yet (LOCAL-STORAGE, level 78, USAGE POINTER, the COBOL 2002
intrinsic functions and others) are errors in every case; the message names
where the element comes from and says that Cobalt does not support it yet.

## Strictness (`-X`)

| Level | Sets in effect | Accepted extensions | Also |
|---|---|---|---|
| `relaxed` | `common` and the `-fext` sets | silently | the reference format is not checked (no `-Warea`, `-Wcolumn-overflow`) |
| `normal` (default) | `common` and the `-fext` sets | each with a warning (`-Wextension`) | |
| `pedantic` | only the `-fext` sets | silently | the obsolete elements are flagged (`-Wobsolete`) |
| `ibm-strict` | `ibm` and the `-fext` sets | silently | as `pedantic`; the same as `-X pedantic -fext=ibm` |

An element outside the sets in effect is an error. Under `normal` an
extension gives one warning where it first appears (a long word: once, not
at every use):

```
ledger.cbl:7:42: warning: USAGE COMP-3 is an extension to COBOL-85 [-Wextension]
     7 |        01  PACKED-AMOUNT    PIC S9(7)V99 COMP-3.
       |                                          ^~~~~~
note: the standard name of this usage is PACKED-DECIMAL
```

Under `pedantic` the same is an error, with a note naming a set that
accepts it:

```
ledger.cbl:7:42: error: USAGE COMP-3 is not COBOL-85
note: it is an IBM, GnuCOBOL and Micro Focus extension; -fext=ibm accepts it
```

`-Wno-extension` silences the warnings of `normal`. Strict COBOL-85 with the
GnuCOBOL extensions is `-X pedantic -fext=gnucobol`.

## Implementation profiles (`-D`)

The standard leaves some behavior to the implementor. An implementation
profile chooses it the way another compiler does; it never changes what is
accepted, which is the extension sets' role.

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
