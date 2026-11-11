       IDENTIFICATION DIVISION.
       PROGRAM-ID. REFMOD.
      * Reference modification past the end of the item
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  S          PIC X(10) VALUE "ABCDEFGHIJ".
       01  P          PIC 99 VALUE 3.
       01  L          PIC 99 VALUE 9.
       PROCEDURE DIVISION.
           DISPLAY S (P:4).
           DISPLAY S (P:L).
           STOP RUN.
