       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUBSET-CHECK.
      * FIPS subsets: --subset=intermediate flags the level 2 elements
      * (High subset), --subset=minimum also the modules Minimum leaves
      * out (relative and indexed I-O, Sort-Merge, ...)
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT OPTIONAL MASTER ASSIGN TO "master.dat"
               ORGANIZATION IS RELATIVE
               ACCESS MODE IS RANDOM
               RELATIVE KEY IS REC-NO.
       DATA DIVISION.
       FILE SECTION.
       FD  MASTER.
       01  MASTER-REC        PIC X(20).
       WORKING-STORAGE SECTION.
       01  REC-NO            PIC 9(4).
       01  AMOUNT            PIC 9(4) VALUE 10.
           88  BIG-AMOUNT    VALUE 1000 THRU 9999.
       01  NAME-FIELD        PIC X(20) VALUE "SMITH".
       PROCEDURE DIVISION.
       MAIN-PARA.
           COMPUTE AMOUNT = AMOUNT * 2.
           IF AMOUNT > 5
               IF BIG-AMOUNT DISPLAY "BIG"
               ELSE DISPLAY NAME-FIELD (1:3).
           DISPLAY FUNCTION UPPER-CASE (NAME-FIELD).
           OPEN I-O MASTER.
           MOVE 1 TO REC-NO.
           READ MASTER INVALID KEY DISPLAY "NONE".
           CLOSE MASTER.
           STOP RUN.
