       IDENTIFICATION DIVISION.
       PROGRAM-ID. ODO-VALUE.
      * The DEPENDING ON item beyond the OCCURS maximum
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  N          PIC 99 VALUE 9.
       01  T.
           05  E      PIC X OCCURS 1 TO 5 TIMES DEPENDING ON N.
       PROCEDURE DIVISION.
           DISPLAY T.
           STOP RUN.
