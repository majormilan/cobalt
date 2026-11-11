       IDENTIFICATION DIVISION.
       PROGRAM-ID. IO-ERROR.
      * OPEN INPUT of a file that does not exist, nothing handling it
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT MASTER ASSIGN TO "no-such-master.dat".
       DATA DIVISION.
       FILE SECTION.
       FD  MASTER.
       01  MASTER-RECORD  PIC X(80).
       PROCEDURE DIVISION.
           OPEN INPUT MASTER.
           DISPLAY "NOT REACHED".
           STOP RUN.
