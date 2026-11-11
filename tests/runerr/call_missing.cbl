       IDENTIFICATION DIVISION.
       PROGRAM-ID. CALLER.
      * CALL of a program that is not in the run unit
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  NAME-OF-SUB  PIC X(8) VALUE "NOSUCH".
       PROCEDURE DIVISION.
           CALL NAME-OF-SUB.
           STOP RUN.
