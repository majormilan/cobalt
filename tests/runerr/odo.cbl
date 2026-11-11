       IDENTIFICATION DIVISION.
       PROGRAM-ID. ODO.
      * A subscript beyond the current number of occurrences
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  N          PIC 99 VALUE 3.
       01  I          PIC 99 VALUE 4.
       01  T.
           05  E      PIC X OCCURS 1 TO 5 TIMES DEPENDING ON N.
       PROCEDURE DIVISION.
           MOVE "A" TO E (2).
           MOVE "B" TO E (I).
           STOP RUN.
