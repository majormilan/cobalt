# 9. Limits

## Sizes

| What | Limit | COBOL-85 requires |
|---|---|---|
| Digits of a numeric item, a literal, an intermediate result | 18 | 18 |
| Dimensions of a table (OCCURS within OCCURS) | 16 | 7 |
| Data items of one program (with those of its files and copybooks) | 4096 | |
| Files of one program | 64 | |
| Paragraphs and sections of one program | 65536 | |
| Programs of one run unit (nested ones included) | 256 | |
| Sources of one compilation | 64 | |
| COPY statements within copied text, nested | 50 | |
| Reports (RD) of one program | 16 | |
| Size of a program's code and data | 2 GiB (absolute 32-bit addresses) | |
| Characters of a user-defined word | 30 (more as an extension) | 30 |
| Characters of a nonnumeric literal | 160 (more as an extension) | 160 |
| Characters of a PICTURE character-string | 30 (more as an extension) | 30 |

Going past a limit is an error when compiling, with the limit in the
message:

```
ledger.cbl:23:15: error: 'L16' is a table of 17 dimensions (16 limit)
```

## Known limitations

What this release does not do, and what it does differently from other
compilers:

**Language**

- The Communication module (CD, RECEIVE, SEND, ACCEPT and DISABLE / ENABLE
  MESSAGE): an error says it is not supported. It is obsolete in COBOL-85 and
  was removed from the 2002 standard.
- ASSIGN to a data item (the name of a file computed by the program); the
  environment can map the names of ASSIGN when the program runs (chapter 6).
- USAGE COMP-1 and COMP-2 (floating point), and the other extensions of
  later standards and of vendors that chapter 4 does not list.
- An exponent with decimals (`2 ** 0.5`) is a size error.
- In the Report Writer, the data-name of a SUM entry names its printed
  field, not the sum counter (chapter 3).
- Accepted with no effect: CODE-SET (data are ASCII), SYNCHRONIZED, RERUN,
  MEMORY SIZE, BLOCK CONTAINS, RESERVE.
- DISPLAY UPON SYSERR writes to standard output, as all DISPLAY does.

**Files**

- Indexes are not stored: OPEN of an INDEXED file reads every record and
  builds the indexes in memory, so opening takes time in proportion to the
  file, and the file must fit in memory.
- SORT and MERGE work in memory, with no work files.
- No record locking or sharing between programs that run at the same time.
- Files are ASCII; there is no EBCDIC conversion.

**Programs**

- Programs in other languages cannot be called, and COBOL programs cannot
  be called from them.
- Objects made with `-c` are cobalt's own (in ELF format, for both targets):
  cobalt links them for either system, but other linkers cannot use them
  on Windows, and nothing links them with code in other languages.
- No source-level debugging information.

**Platforms**

- x86-64 only, on Linux and Windows. cobalt.exe is cross-built on Linux;
  building it on Windows is not tested yet.
