       IDENTIFICATION DIVISION.
       PROGRAM-ID. SYNTAX.
      * Syntax errors: each is reported, and parsing goes on
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A          PIC 9(4).
       01  B          PIC 9(4).
       PROCEDURE DIVISION.
       MAIN-PARA.
           COMPUTE A = A + .
           COMPUTE B = (A * 2.
           MOVE 1 TO A.
           ELSE MOVE 2 TO B.
           IF A = 1 DISPLAY "ONE" END-IF END-IF.
           ADD 1 A.
           IF A DISPLAY "A" END-IF.
           MOVE A TO B
           DISPLAY "X" "Y
           STOP RUN.
