       IDENTIFICATION DIVISION.
       PROGRAM-ID. FRONTENDWORDS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 3-DIM-TBL.
          05 2ND-ENTRY PIC X(4) OCCURS 2.
       01 25ANS   PIC 99 VALUE 25.
       01 STARS   PIC X(5) VALUE ALL "*".
       01 PAT     PIC X(7).
       01 IX-ITEM USAGE INDEX.
       PROCEDURE DIVISION.
       0001.
           MOVE ALL "AB" TO PAT.
           DISPLAY STARS " " PAT.
           IF PAT = ALL "AB" DISPLAY "PATTERN EQUAL".
           MOVE "WXYZ" TO 2ND-ENTRY (2).
           DISPLAY 2ND-ENTRY (2) " " 25ANS.
           SET IX-ITEM TO 3.
           DISPLAY "IX=" IX-ITEM.
           PERFORM 0002 THRU 0003.
           GO TO 0004.
       0002.
           DISPLAY "IN 0002".
       0003.
           DISPLAY "IN 0003".
       0004.
           DISPLAY "IN 0004".
           EXIT PROGRAM.
           DISPLAY "AFTER EXIT PROGRAM".
           STOP RUN.
