       IDENTIFICATION DIVISION.
       PROGRAM-ID. SubtractTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A   PIC 999 VALUE 100.
       01  B   PIC 999 VALUE 30.
       01  C   PIC 999 VALUE 15.
       01  R   PIC 999 VALUE 0.

       PROCEDURE DIVISION.
       Main.
           SUBTRACT B FROM A.
           DISPLAY "Test 1 - SUBTRACT B FROM A (100-30): " A.

           SUBTRACT 5 FROM A.
           DISPLAY "Test 2 - SUBTRACT 5 FROM A (70-5): " A.

           SUBTRACT B C FROM A.
           DISPLAY "Test 3 - SUBTRACT B C FROM A (65-30-15): " A.

           SUBTRACT B FROM A GIVING R.
           DISPLAY "Test 4 - SUBTRACT B FROM A GIVING R (20-30 -> 10, un
      -    "signed R): " R.

           MOVE 80 TO A.
           SUBTRACT 10 20 FROM A GIVING R.
           DISPLAY
             "Test 5 - SUBTRACT 10 20 FROM A GIVING R (80-10-20): " R.

           STOP RUN.
