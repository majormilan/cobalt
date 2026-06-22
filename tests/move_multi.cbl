       IDENTIFICATION DIVISION.
       PROGRAM-ID. MoveMultiTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A   PIC 999 VALUE 0.
       01  WS-B   PIC 999 VALUE 0.
       01  WS-C   PIC 999 VALUE 0.
       01  WS-SRC PIC 999 VALUE 77.

       PROCEDURE DIVISION.

       Main.
           MOVE 42 TO WS-A WS-B WS-C.
           DISPLAY "Test 1 - MOVE literal to 3 targets:".
           DISPLAY "A=" WS-A.
           DISPLAY "B=" WS-B.
           DISPLAY "C=" WS-C.

           MOVE ZERO TO WS-A WS-B WS-C.
           DISPLAY "Test 2 - MOVE ZERO to 3 targets:".
           DISPLAY "A=" WS-A.
           DISPLAY "B=" WS-B.
           DISPLAY "C=" WS-C.

           MOVE WS-SRC TO WS-A WS-B.
           DISPLAY "Test 3 - MOVE var to 2 targets:".
           DISPLAY "A=" WS-A.
           DISPLAY "B=" WS-B.

           STOP RUN.
