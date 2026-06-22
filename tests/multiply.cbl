       IDENTIFICATION DIVISION.
       PROGRAM-ID. MultiplyTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A   PIC 999 VALUE 6.
       01  B   PIC 999 VALUE 7.
       01  C   PIC 999 VALUE 0.
       01  D   PIC 999 VALUE 0.

       PROCEDURE DIVISION.
       Main.
           MULTIPLY A BY B.
           DISPLAY "Test 1 - MULTIPLY A BY B (6*7): " B.

           MULTIPLY 3 BY B.
           DISPLAY "Test 2 - MULTIPLY 3 BY B (42*3): " B.

           MULTIPLY A BY B GIVING C.
           DISPLAY "Test 3 - MULTIPLY A BY B GIVING C (6*126): " C.

           MOVE 5 TO B.
           MOVE 4 TO C.
           MULTIPLY B BY C GIVING D.
           DISPLAY "Test 4 - MULTIPLY B BY C GIVING D (5*4): " D.

           STOP RUN.
