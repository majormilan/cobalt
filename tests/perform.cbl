       IDENTIFICATION DIVISION.
       PROGRAM-ID. PerformTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-COUNT PIC 9 VALUE 3.
       01  WS-NUM   PIC 99 VALUE 0.

       PROCEDURE DIVISION.

       Main.
           DISPLAY "Test 1 - Basic PERFORM:".
           PERFORM ShowHello.

           DISPLAY "Test 2 - PERFORM literal TIMES:".
           PERFORM ShowHello 3 TIMES.

           DISPLAY "Test 3 - PERFORM var TIMES:".
           PERFORM ShowHello WS-COUNT TIMES.

           DISPLAY "Test 4 - PERFORM with side effect:".
           PERFORM AddTen 5 TIMES.
           DISPLAY WS-NUM.

           STOP RUN.

       ShowHello.
           DISPLAY "Hello!".

       AddTen.
           ADD 10 TO WS-NUM.
