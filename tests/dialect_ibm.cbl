       IDENTIFICATION DIVISION.
       PROGRAM-ID. DIALECT-IBM.
      * DISPLAY format, sign encoding and binary truncation under
      * one dialect (see dialect_ibm.flags).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  U PIC 999 VALUE 12.
       77  S PIC S999 VALUE -45.
       77  SP PIC S999 VALUE 45.
       77  V PIC 99V99 VALUE 1.5.
       77  C PIC S9(4) COMP VALUE -7.
       77  P PIC 99PP VALUE 1200.
       77  B PIC 99 COMP VALUE 90.
       77  SX PIC X(3).
       PROCEDURE DIVISION.
           DISPLAY "[" U "][" S "][" SP "][" V "][" C "][" P "]".
           MOVE S TO SX. DISPLAY "STORED " SX.
           MOVE -7 TO S. MOVE S TO SX. DISPLAY "STORED " SX.
           ADD 60 TO B ON SIZE ERROR DISPLAY "SIZE ERROR".
           DISPLAY B.
           STOP RUN.
