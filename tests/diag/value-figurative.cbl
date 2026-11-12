       IDENTIFICATION DIVISION.
       PROGRAM-ID. VALUE-FIGURATIVE.
      * A numeric item's VALUE may be ZERO, not another figurative constant
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  N-ZERO   PIC 9(3) VALUE ZERO.
       01  N-ALL    PIC 9(3) VALUE ALL "1".
       01  N-HIGH   PIC 9(3) VALUE HIGH-VALUES.
       01  N-QUOTE  PIC 9(3) VALUE QUOTE.
       PROCEDURE DIVISION.
           STOP RUN.
