       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-REFMOD-NUMERIC.
      * A reference-modified item of variable length moved to numeric items
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 X8 PIC X(8) VALUE "12345678".
       01 N2 PIC 99.
       01 N6 PIC 9(6).
       01 P PIC 9 VALUE 3.
       01 L PIC 9 VALUE 4.
       PROCEDURE DIVISION.
           MOVE X8(P:L) TO N2. DISPLAY N2.
           MOVE X8(P:L) TO N6. DISPLAY N6.
           MOVE X8(P:) TO N6. DISPLAY N6.
           STOP RUN.
