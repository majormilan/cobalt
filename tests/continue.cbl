       IDENTIFICATION DIVISION.
       PROGRAM-ID. ContinueTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC 99 VALUE 10.

       PROCEDURE DIVISION.

       Main.
           CONTINUE.
           DISPLAY "Before: " WS-A.
           CONTINUE.
           MOVE 42 TO WS-A.
           CONTINUE.
           DISPLAY "After: " WS-A.
           STOP RUN.
