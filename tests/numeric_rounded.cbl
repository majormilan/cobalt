       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUMROUNDED.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 A PIC 99V9 VALUE 0.
       01 B PIC S99 VALUE 0.
       01 C PIC 99 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           COMPUTE A ROUNDED = 2.45.
           DISPLAY "A=" A.
           COMPUTE A = 2.45.
           DISPLAY "A=" A.
           COMPUTE B ROUNDED = -7.5.
           DISPLAY "B=" B.
           COMPUTE B ROUNDED = -7.49.
           DISPLAY "B=" B.
           COMPUTE B = -7.9.
           DISPLAY "B=" B.
           DIVIDE 3 INTO 2 GIVING C ROUNDED.
           DISPLAY "C=" C.
           DIVIDE 3 INTO 2 GIVING C.
           DISPLAY "C=" C.
           ADD 0.5 TO C ROUNDED.
           DISPLAY "C=" C.
           MULTIPLY 1.5 BY C ROUNDED.
           DISPLAY "C=" C.
           STOP RUN.
