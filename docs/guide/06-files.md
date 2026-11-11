# 6. Files

## Naming a file

The ASSIGN clause of a SELECT entry gives the name of the file on disk:

```cobol
           SELECT CUSTOMERS ASSIGN TO "data/customers.dat".
           SELECT REPORT-FILE ASSIGN TO REPORTOUT.
```

- A literal is the file name as written: a path, relative to the directory
  the program runs in, or absolute.
- A word is the file name itself, in the case it is written: `ASSIGN TO
  REPORTOUT` reads and writes the file `REPORTOUT` in the current
  directory. The word is taken as a name even when a data item has the
  same name; ASSIGN to a data item (a file name chosen at run time) is not
  supported in this release.
- Further names after the first (`ASSIGN TO DISK "x.dat"`) are accepted and
  ignored.

On Windows both `/` and `\` separate directories.

## Organizations

| Organization | Access | Statements |
|---|---|---|
| SEQUENTIAL (the default) | SEQUENTIAL | OPEN INPUT, OUTPUT, I-O, EXTEND; READ, WRITE, REWRITE, CLOSE |
| LINE SEQUENTIAL (an extension) | SEQUENTIAL | as SEQUENTIAL, except REWRITE; text files |
| RELATIVE | SEQUENTIAL, RANDOM, DYNAMIC | adds READ by RELATIVE KEY, READ NEXT, START, DELETE |
| INDEXED | SEQUENTIAL, RANDOM, DYNAMIC | adds RECORD KEY, ALTERNATE RECORD KEY [WITH DUPLICATES], READ KEY IS, START, DELETE |

OPTIONAL files (`SELECT OPTIONAL`) may be missing: OPEN INPUT of a missing
one succeeds with status 05 and the first READ is at end; OPEN I-O or
EXTEND creates it (status 05 as well).

Accepted with no effect: RESERVE AREAS, BLOCK CONTAINS, RECORDING MODE,
CODE-SET, LABEL RECORDS, VALUE OF, PADDING, the REEL / UNIT and NO REWIND
phrases (status 07), MULTIPLE FILE TAPE.

## FILE STATUS

After every I-O statement the two characters of the FILE STATUS item, if
the file has one, tell what happened:

| Status | Meaning |
|---|---|
| 00 | success |
| 02 | indexed: the record has an alternate key value that is already present, and the key allows duplicates |
| 04 | READ: the record length is outside the file's limits |
| 05 | OPEN of an OPTIONAL file that does not exist |
| 07 | CLOSE REEL / UNIT, or NO REWIND, on a disk file |
| 10 | at end |
| 14 | relative READ NEXT: the record number does not fit the RELATIVE KEY item |
| 21 | indexed: prime key out of sequence (sequential access), or changed before REWRITE |
| 22 | duplicate key |
| 23 | no record with that key (READ, REWRITE, DELETE, START) |
| 24 | relative: WRITE with record number 0 |
| 30 | other I-O error (from the system) |
| 35 | OPEN INPUT, I-O or EXTEND of a file that does not exist |
| 37 | OPEN not permitted (the file's permissions) |
| 38 | OPEN of a file closed WITH LOCK in this run |
| 41 | OPEN of an open file |
| 42 | CLOSE of a file that is not open |
| 43 | REWRITE or DELETE not after a successful READ (sequential access) |
| 44 | record length outside the limits, or REWRITE with another length (sequential) |
| 46 | READ NEXT with no next record (after at end or a failed START) |
| 47 | READ or START of a file not open INPUT or I-O |
| 48 | WRITE to a file not open OUTPUT or EXTEND (or I-O, relative and indexed) |
| 49 | REWRITE or DELETE of a file not open I-O |

What happens after an unsuccessful statement (status 10 or more):

1. the AT END or INVALID KEY phrase of the statement, if it has one;
2. otherwise the USE procedure for the file, or the one for its open mode
   (`USE AFTER ERROR PROCEDURE ON INPUT`), if there is one;
3. otherwise, for a status of 30 or more on a file without a FILE STATUS
   item, the run ends with an error (chapter 7):

   ```
   ledger.cbl:31: run-time error in program LEDGER: I-O error on file 'CUSTOMERS': status 35 (the file does not exist)
   ```

`-Wunchecked-io` (chapter 5) reports the statements none of these handles.

## Formats on disk

The files are ordinary files that other programs can read and write.

**Record sequential, one record length:** the records one after the other,
with no separators. A short last record is read padded with spaces (status
04).

**Record sequential, several record lengths** (RECORD VARYING, or records of
different sizes): each record has a 4-byte header with its length,
big-endian, in the first two bytes (the format of IBM variable-length
records).

**Text** (LINE SEQUENTIAL, and any file written with ADVANCING): each record
is a line, written without its trailing spaces and ended by a newline (LF,
on Windows too). READ takes one line, without a carriage return before the
newline, and pads the record with spaces; a longer line is cut at the record
length.

**RELATIVE:** an array of slots, record number k at slot k: an 8-byte length
(0: no record there) and the record area.

**INDEXED:** an array of slots too: an 8-byte length (0: a free slot), an
8-byte sequence number per key, and the record area. The indexes are not
stored: OPEN builds them in memory from the records, and every WRITE,
REWRITE and DELETE keeps them up to date. Duplicate alternate keys come
back in the order the records got their key value.

Numbers in records are stored as their USAGE says (chapter 7), sign
overpunches included, so files from IBM systems that were transferred
without conversion of the numeric fields need to be in ASCII.

## Printing: ADVANCING and LINAGE

WRITE ... ADVANCING follows a printer, on a text file:

| Phrase | Effect |
|---|---|
| `AFTER n` (or `AFTER n LINES`) | moves down n lines, then prints the record |
| `BEFORE n` | prints the record, then moves down n lines |
| `AFTER PAGE`, `BEFORE PAGE` | a form feed before, or after, the record |
| (none) | as `BEFORE 1` |
| `AFTER mnemonic-name` | a mnemonic-name of SPECIAL-NAMES for a channel (C01, TOP): a new page |

A printer would overprint when BEFORE follows AFTER on the same line; in a
file the record starts a new line instead.

LINAGE gives the file pages:

```cobol
       FD  PRINT-FILE
           LINAGE IS 60 LINES WITH FOOTING AT 55
               LINES AT TOP 3 LINES AT BOTTOM 3.
```

LINAGE-COUNTER is the line of the page body the next record goes on,
starting at 1. Moving past the body goes to the first line of the next page:
newlines for the rest of the body and the margins, not a form feed. `AT
END-OF-PAGE` (or `EOP`) runs when a WRITE reaches the FOOTING line, or starts
a new page. The LINAGE values may be data items; they are taken at the
start of each page.

The Report Writer (chapter 3) writes its reports through this mechanism.

## SORT and MERGE

```cobol
           SORT SORT-FILE ON ASCENDING KEY S-DEPT
                          ON DESCENDING KEY S-SALARY
               USING EMPLOYEES GIVING SORTED-EMPLOYEES.
```

- Keys may be ascending and descending, alphanumeric and numeric of any
  USAGE. Alphanumeric keys compare in the program collating sequence, or
  the `COLLATING SEQUENCE` alphabet of the statement.
- Records with equal keys keep the order in which they were released
  (the sort is stable), and DUPLICATES IN ORDER is accepted.
- INPUT PROCEDURE with RELEASE, and OUTPUT PROCEDURE with RETURN, take the
  place of USING and GIVING. Several USING and GIVING files are allowed.
- MERGE takes the records of its USING files, which should be in the key's
  order; equal keys come out in the order of the files.

The records are sorted in memory, so a sort is limited by the memory of the
machine (chapter 9). No work files are written.
