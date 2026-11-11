       IDENTIFICATION DIVISION.
       PROGRAM-ID. COMMUNICATION-USE.
      * The Communication module is not supported: a clear error for
      * each of its elements
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  MSG-COUNT      PIC 9(6).
       COMMUNICATION SECTION.
       CD  INPUT-QUEUE FOR INPUT.
       01  QUEUE-NAME     PIC X(12).
       PROCEDURE DIVISION.
           ENABLE INPUT INPUT-QUEUE WITH KEY "SECRET".
           ACCEPT INPUT-QUEUE MESSAGE COUNT.
           RECEIVE INPUT-QUEUE MESSAGE INTO QUEUE-NAME.
           SEND INPUT-QUEUE FROM QUEUE-NAME.
           DISPLAY MSG-COUNT.
           STOP RUN.
