       IDENTIFICATION DIVISION.
       PROGRAM-ID. ACCEPT-TEST.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 WS-NAME   PIC X(10).
       01 WS-AGE    PIC 9(2).
       01 WS-CODE   PIC X(5) VALUE "INIT".

       PROCEDURE DIVISION.
           DISPLAY "Enter your name (10 chars): ".
           ACCEPT WS-NAME.
           DISPLAY "Enter your age (2 digits): ".
           ACCEPT WS-AGE.
           DISPLAY "Hello, " WS-NAME "! You are " WS-AGE " years old.".
           DISPLAY "Code: " WS-CODE.
           STOP RUN.