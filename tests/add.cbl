       IDENTIFICATION DIVISION.
       PROGRAM-ID. AddTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.

       01  A          PIC 99 VALUE 15.
       01  B          PIC 99 VALUE 25.
       01  C          PIC 99 VALUE 10.
       01  RESULT-TO  PIC 999 VALUE ZERO.
       01  RESULT-GIVING PIC 999 VALUE ZERO.

       PROCEDURE DIVISION.

       MainPara.

           ADD A B TO RESULT-TO.
           DISPLAY "Test 1 - ADD A B TO RESULT-TO: " RESULT-TO.

           MOVE ZERO TO RESULT-TO.
           ADD A B C TO RESULT-TO.
           DISPLAY "Test 2 - ADD A B C TO RESULT-TO: " RESULT-TO.

           ADD A B GIVING RESULT-GIVING.
           DISPLAY "Test 3 - ADD A B GIVING RESULT-GIVING: " RESULT-GIVING.

           ADD A B C A GIVING RESULT-GIVING.
           DISPLAY "Test 4 - ADD A B C A GIVING RESULT-GIVING: " RESULT-GIVING.

           MOVE 5 TO RESULT-TO.
           ADD A TO RESULT-TO.
           ADD B TO RESULT-TO.
           ADD C TO RESULT-TO.
           DISPLAY "Test 5 - Chained ADD ... TO RESULT-TO: " RESULT-TO.

           STOP RUN.
