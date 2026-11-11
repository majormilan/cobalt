; runtime/x86_64/common/file.inc
; File control block layout and file constants (runtime/ABI.md section 7),
; shared by file.asm, relative.asm, indexed.asm and sort.asm.

%define FCB_FD          0               ; OS handle while open
%define FCB_MODE        8               ; 0 closed, OPEN_* while open
%define FCB_SIZE        16              ; record area size (the longest record)
%define FCB_FLAGS       24              ; FILE_*, ORG_* and ACC_* bits
%define FCB_NAME        32              ; NUL-terminated file name
%define FCB_STATUS      40              ; FILE STATUS item (2 bytes), or 0
%define FCB_STATE       48              ; ST_* bits
%define FCB_LAST_MODE   56              ; the mode of the last OPEN (for USE procedures)
%define FCB_MIN         64              ; shortest record (variable-length records)
%define FCB_RECLEN      72              ; length of the record read / to write
%define FCB_KEY         80              ; relative record number (RELATIVE KEY), in and out
%define FCB_KEYS        88              ; indexed: key table (see indexed.asm)
%define FCB_BUF         96              ; I-O buffer (FILE_BUFSZ bytes)
%define FCB_BUFLEN      104             ; bytes in the buffer
%define FCB_BUFPOS      112             ; file offset of the buffer (input)
%define FCB_POS         120             ; file offset of the next record (sequential)
%define FCB_RECPOS      128             ; file offset of the last record read
%define FCB_LASTLEN     136             ; length of the last record read
%define FCB_LOCK        144             ; closed WITH LOCK
%define FCB_LINAGE      152             ; LINAGE block (see LIN_*), or 0
%define FCB_PRIV        160             ; organization state (relative, indexed)
%define FCB_KEYREF      168             ; indexed: key of reference (key number)
%define FCB_NEXT        176             ; next open file
%define FCB_SORT        184             ; sort files: sort state
%define FCB_ORG         184             ; relative and indexed files: their operations
                                        ; (cobrt_rel_ops, cobrt_idx_ops, from the compiler)
; The operations of an organization, in its table (so a program links the
; code of the organizations it uses only)
%define ORG_OPEN        0
%define ORG_CLOSE       8
%define ORG_READ        16
%define ORG_WRITE       24
%define ORG_REWRITE     32
%define ORG_DELETE      40
%define ORG_START       48
%define FCB_BYTES       192

%define FILE_LINE       1               ; line sequential: text lines
%define FILE_OPTIONAL   2               ; SELECT OPTIONAL
%define FILE_USE        4               ; a USE procedure handles errors
%define FILE_VARIABLE   8               ; records of different lengths
%define FILE_LINAGE     16              ; FD LINAGE
%define FILE_MAPPED     0x10000         ; runtime: the name was mapped (map_file_name)
%define ORG_RELATIVE    0x100
%define ORG_INDEXED     0x200
%define ACC_RANDOM      0x1000
%define ACC_DYNAMIC     0x2000

%define ST_AT_END       1               ; an AT END condition was reported
%define ST_READ_OK      2               ; the last READ succeeded (for REWRITE)
%define ST_LINE_OPEN    4               ; text output: the current line has text
%define ST_ABSENT       8               ; OPTIONAL file that does not exist
%define ST_REVERSED     16              ; OPEN ... REVERSED
%define ST_EOP          32              ; end-of-page condition of the last WRITE
%define ST_POS_VALID    64              ; relative, indexed: the position is set

%define OPEN_INPUT      1
%define OPEN_OUTPUT     2
%define OPEN_IO         3
%define OPEN_EXTEND     4
%define OPT_REVERSED    1               ; OPEN options (bits 8.. of the mode)
%define OPT_NO_REWIND   2

%define CLOSE_REEL      1               ; CLOSE options
%define CLOSE_NO_REWIND 2
%define CLOSE_LOCK      4

%define READ_NEXT       0               ; runtime_file_read: sequential
%define READ_KEY        1               ; by key

%define START_EQ        0               ; runtime_file_start conditions
%define START_GT        1
%define START_GE        2

; WRITE advancing modes
%define ADV_NONE        0
%define ADV_BEFORE      1
%define ADV_AFTER       2
%define ADV_BEFORE_PAGE 3
%define ADV_AFTER_PAGE  4

; LINAGE block: values the program stores (before OPEN and each WRITE),
; the values of the current page, and LINAGE-COUNTER
%define LIN_LINES       0
%define LIN_FOOTING     8
%define LIN_TOP         16
%define LIN_BOTTOM      24
%define LIN_COUNTER     32
%define LIN_LINES_A     40
%define LIN_FOOTING_A   48
%define LIN_TOP_A       56
%define LIN_BOTTOM_A    64

%define FILE_BUFSZ      65536
