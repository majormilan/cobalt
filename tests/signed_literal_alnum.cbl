       IDENTIFICATION DIVISION.
       PROGRAM-ID. SIGNED-LITERAL-ALNUM.
      * A signed numeric literal moved to, or compared with, an
      * alphanumeric item is treated as its unsigned digits
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 A5 PIC X(5).
       01 X2 PIC XX VALUE "12".
       01 X5 PIC X(5).
       PROCEDURE DIVISION.
           MOVE -12 TO A5.
           DISPLAY "[" A5 "]".
           MOVE +7 TO A5.
           DISPLAY "[" A5 "]".
           IF X2 = -12 DISPLAY "EQUAL" ELSE DISPLAY "NOT EQUAL".
           MOVE "-12" TO X5.
           IF X5 = -12 DISPLAY "EQUAL" ELSE DISPLAY "NOT EQUAL".
           STOP RUN.
