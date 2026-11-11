       IDENTIFICATION DIVISION.
       PROGRAM-ID. WARNINGS.
      * One of each warning (with -Wextra)
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT AUDIT-FILE ASSIGN TO "audit.dat".
           SELECT MASTER-FILE ASSIGN TO "master.dat"
               ORGANIZATION IS INDEXED
               ACCESS MODE IS RANDOM
               RECORD KEY IS MASTER-KEY.
       DATA DIVISION.
       FILE SECTION.
       FD  AUDIT-FILE.
       01  AUDIT-RECORD   PIC X(80).
       FD  MASTER-FILE.
       01  MASTER-RECORD.
           05  MASTER-KEY PIC 9(6).
           05  MASTER-NAME PIC X(20).
       WORKING-STORAGE SECTION.
       01  COUNTER        PIC 9(3).
       01  SMALL-CODE     PIC X(4)     VALUE SPACES.
       01  LONG-NAME      PIC X(30)    VALUE SPACES.
       01  AMOUNT         PIC 9(3)V99  VALUE ZERO.
       01  BIG-AMOUNT     PIC 9(7)V99  VALUE ZERO.
       01  NEVER-USED     PIC X(10).
       PROCEDURE DIVISION.
       MAIN-PARA.
           OPEN I-O MASTER-FILE.
           MOVE "ABCDEFGH" TO SMALL-CODE.
           MOVE 12345 TO AMOUNT.
           MOVE 1.255 TO AMOUNT.
           MOVE LONG-NAME TO SMALL-CODE.
           MOVE BIG-AMOUNT TO AMOUNT.
           ADD 1 TO COUNTER.
           IF AMOUNT > 999.99 DISPLAY "NEVER" END-IF.
           COMPUTE AMOUNT = BIG-AMOUNT / 0.
           READ MASTER-FILE.
           PERFORM LATER-PARA THRU MAIN-PARA.
          DISPLAY COUNTER.
           STOP RUN.
           DISPLAY "AFTER STOP RUN".
       LATER-PARA.
           DISPLAY "LATER".
           MOVE SPACES TO                                            LONG-NAME.
           STOP RUN.
         ORPHAN-PARA.
           DISPLAY "NOTHING PERFORMS THIS ONE" SMALL-CODE AMOUNT.
