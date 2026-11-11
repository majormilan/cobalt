       IDENTIFICATION DIVISION.
       PROGRAM-ID. AREA-CHECK.
      * Entries that belong in Area A (columns 8 to 11) written in Area B
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT PRINT-FILE ASSIGN TO "out.txt".
           DATA DIVISION.
       FILE SECTION.
           FD  PRINT-FILE.
       01  PRINT-REC         PIC X(20).
           WORKING-STORAGE SECTION.
           01  COUNTER       PIC 9(4) VALUE ZERO.
       77  TOTAL             PIC 9(4) VALUE ZERO.
            77  OTHER-TOTAL  PIC 9(4) VALUE ZERO.
       PROCEDURE DIVISION.
       MAIN-PARA.
           ADD 1 TO COUNTER TOTAL OTHER-TOTAL.
           STOP RUN.
