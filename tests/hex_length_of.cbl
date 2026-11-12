       IDENTIFICATION DIVISION.
       PROGRAM-ID. HEX-LENGTH-OF.
      * Hexadecimal literals and LENGTH OF (extensions of the common set)
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 CRLF PIC XX VALUE X"0D0A".
       01 AB   PIC X(3) VALUE X'414243'.
       01 Q    PIC X.
       PROCEDURE DIVISION.
           MOVE X"22" TO Q.
           DISPLAY AB " " Q.
           IF CRLF = X"0D0A" DISPLAY "CRLF".
           DISPLAY X"4F4B".
           DISPLAY LENGTH OF AB " " LENGTH OF CRLF.
           STOP RUN.
