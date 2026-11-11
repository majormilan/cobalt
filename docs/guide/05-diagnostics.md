# 5. Diagnostics

## The messages

cobalt reports errors, warnings and the notes that explain them in one
format:

```
ledger.cbl:16:37: error: 'TOTAL-AMONT' is not defined
    16 |            MOVE CUSTOMER-BALANCE TO TOTAL-AMONT.
       |                                     ^~~~~~~~~~~
ledger.cbl:11:12: note: did you mean 'TOTAL-AMOUNT'?
    11 |        01  TOTAL-AMOUNT          PIC S9(9)V99.
       |            ^~~~~~~~~~~~
1 error generated.
```

- The location is the file, line and column. The source line follows, with
  the word or literal at the column underlined.
- Notes point at what explains the message: where an item is declared, the
  candidates of an ambiguous name, the closest name to a misspelled one, the
  OCCURS of a table, the parenthesis a `)` should match, the statement that
  makes another unreachable.
- Text brought in by COPY is reported where it is, in the library text,
  after the chain of COPY statements that brought it in:

  ```
  In text copied at BADREC.cpy:5:12,
                   from ledger.cbl:7:8:
  INNER.cpy:1:46: error: VALUE 1234 does not fit PICTURE '9(3)' of 'INNER-AMOUNT'
  ```

- Every stage reports what it finds. After a syntax error in a statement,
  the statement is left out and the compilation goes on to the next one;
  when the data and the rest of the program are sound, the later checks
  run too, so one compilation reports the errors of the whole program. (The
  warnings that need every statement, such as `-Wuninitialized` and
  `-Wunused`, are then left out.) A second error at the place of an error,
  and an error found again by a later stage, are not shown.
- A misspelled statement gets the closest verb (`did you mean 'DISPLAY'?`),
  also when the statement before has no period and the word reads as one of
  its operands.
- Elements of later standards and vendor dialects that cobalt does not
  support are named as such: `FUNCTION TRIM is COBOL 2002, not COBOL-85;
  cobalt does not support it`.
- The last line counts the errors and warnings (`2 errors and 1 warning
  generated.`).

An error means no output is written and the exit status is 1. Warnings do
not stop the compilation.

| Option | Effect |
|---|---|
| `-fdiagnostics-color=always\|never\|auto` | Colors. `auto`, the default, uses them on a terminal unless the `NO_COLOR` environment variable is set. |
| `-fmax-errors=N` | Stops after N errors (`fatal error: too many errors emitted`). |

## Warnings

Each warning ends with the option that controls it:

```
ledger.cbl:10:19: warning: The relation is always false [-Wconstant-condition]
    10 |            IF A > 1000 DISPLAY "X".
       |                   ^~~~
ledger.cbl:5:12: note: 'A' is at most 999 (PICTURE 9(3))
     5 |        01  A PIC 9(3).
       |            ^
```

| Option | Effect |
|---|---|
| `-Wname` | Turns the warning `name` on. |
| `-Wno-name` | Turns it off. |
| `-Wall` | The default set. |
| `-Wextra` | Every warning. |
| `-Werror` | Every warning is an error. |
| `-Werror=name` | This warning is an error (and on). The message then ends with `[-Werror=name]`. |
| `-Wno-error`, `-Wno-error=name` | Undo `-Werror`. |
| `-w` | No warnings. |

The options apply in order: `-Wextra -Wno-unused` is every warning but
`unused`.

| Warning | On | Reports |
|---|---|---|
| `truncate` | by default | a literal that does not fit the item it is moved to (characters, high-order digits, decimal places, the sign) |
| `unreachable` | by default | a statement after GO TO, STOP RUN (or EXIT PROGRAM in a called program) in the same procedure |
| `constant-condition` | by default | a relation always true or always false: by the item's PICTURE (`PIC 9(3)` compared with 1000), an item compared with itself, a numeric item compared for equality with a literal that is not all digits |
| `divide-by-zero` | by default | division by the literal zero |
| `perform-range` | by default | PERFORM A THRU B where B comes before A |
| `recursive-perform` | by default | PERFORM of the paragraph, or the section, the PERFORM is in (undefined in COBOL-85) |
| `unused-file` | by default | a file no statement opens or names |
| `column-overflow` | by default | a word that runs past column 72 (columns 73 to 80 are ignored) |
| `area` | by default | in area B, what belongs in area A (division and section headers, paragraph names, FD, SD, RD, level 01 and 77, DECLARATIVES); a statement in area A |
| `extension` | by default | an element outside COBOL-85, under `-X normal` (chapter 4) |
| `subset` | with `--subset` | an element outside the chosen FIPS subset (chapter 4) |
| `obsolete` | `-Wextra`, `-X pedantic`, `--flag-obsolete` | an obsolete element of COBOL-85 (chapter 4) |
| `unused` | `-Wextra` | a data item never referenced (not those of copied text) |
| `unused-procedure` | `-Wextra` | a paragraph or section neither performed nor reached by falling into it |
| `narrowing` | `-Wextra` | MOVE of an item to a smaller one: characters or integer digits, decimal places, or the sign (signed to unsigned) lost |
| `group-move` | `-Wextra` | MOVE of a group to an elementary numeric item (its characters are moved unconverted) |
| `compare` | `-Wextra` | a numeric operand compared with a nonnumeric one: they are compared as characters |
| `uninitialized` | `-Wextra` | an item statements read but none gives a value, without a VALUE clause |
| `unchecked-io` | `-Wextra` | an I-O statement without AT END or INVALID KEY whose exception no FILE STATUS item or USE procedure handles |

`-X relaxed` turns off `area` and `column-overflow`, as well as
`extension`. A program that fills columns 73 to 80 (the identification
area) on purpose, writing the same text there on most lines, gets no
`column-overflow` warnings.

## Errors

Errors are not named and cannot be turned off. They are reported for:

- the reference format and the words: an unterminated literal, a
  continuation line without a line to continue, a word that is not
  COBOL;
- the syntax: what was expected and what was found, with the statement or
  clause being read;
- names: not defined (with the closest name), ambiguous (with the
  candidates and how to qualify), of the wrong kind (a file where a data item
  is needed);
- the data: a PICTURE that is not valid, a VALUE that does not fit or is of
  the wrong category, OCCURS deeper than allowed, a REDEFINES of an item that
  does not precede it, a level number out of place;
- the statements: operands of the wrong category (an alphanumeric item in
  ADD), a subscript count that does not match the OCCURS, a reference
  modification out of the item by its literals, a file statement that does
  not fit the file's organization or access;
- the elements outside COBOL-85, under `-X pedantic` and `-X ibm-strict`;
- what cobalt does not support (chapter 3), each with a message saying
  so rather than a syntax error.

Errors in what cobalt was given on the command line (an unknown option, a
file it cannot read) are reported as `cobalt: error: ...`.

The errors and warnings of the program when it runs are in chapter 7.
