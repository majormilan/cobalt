      * -fcheck: a numeric DISPLAY item that holds no number (spaces
      * moved through its group) ends the run where it is used
       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUMDATA.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  G.
           05  N PIC 9(4).
       01  T PIC 9(5) VALUE 0.
       PROCEDURE DIVISION.
           MOVE 12 TO N.
           ADD N TO T.
           MOVE SPACES TO G.
           DISPLAY "before".
           ADD N TO T.
           DISPLAY T.
           STOP RUN.
