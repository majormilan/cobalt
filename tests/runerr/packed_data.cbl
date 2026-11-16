       IDENTIFICATION DIVISION.
       PROGRAM-ID. PACKED-DATA.
      * -fcheck: a packed-decimal item whose bytes are not packed digits
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 X PIC XX VALUE X"1A2C".
       01 P REDEFINES X PIC S9(3) COMP-3.
       01 R PIC 9(3).
       PROCEDURE DIVISION.
           DISPLAY "SUB CHECKED".
           COMPUTE R = P + 1.
           STOP RUN.
