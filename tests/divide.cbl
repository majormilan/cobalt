       IDENTIFICATION DIVISION.
       PROGRAM-ID. DivideTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A   PIC 999 VALUE 100.
       01  B   PIC 999 VALUE 4.
       01  Q   PIC 999 VALUE 0.
       01  R   PIC 999 VALUE 0.

       PROCEDURE DIVISION.
       Main.
           DIVIDE B INTO A.
           DISPLAY "Test 1 - DIVIDE B INTO A (100/4): " A.

           MOVE 100 TO A.
           DIVIDE 5 INTO A.
           DISPLAY "Test 2 - DIVIDE 5 INTO A (100/5): " A.

           MOVE 100 TO A.
           DIVIDE B INTO A GIVING Q.
           DISPLAY "Test 3 - DIVIDE B INTO A GIVING Q (100/4): " Q.

           MOVE 17 TO A.
           DIVIDE 5 INTO A GIVING Q REMAINDER R.
           DISPLAY "Test 4 - DIVIDE 5 INTO 17 GIVING Q (17/5): " Q.
           DISPLAY "Test 4 - REMAINDER R: " R.

           DIVIDE A BY B GIVING Q.
           DISPLAY "Test 5 - DIVIDE A BY B GIVING Q (17/4): " Q.

           DIVIDE 22 BY 7 GIVING Q REMAINDER R.
           DISPLAY "Test 6 - DIVIDE 22 BY 7 GIVING Q: " Q.
           DISPLAY "Test 6 - REMAINDER R: " R.

           STOP RUN.
