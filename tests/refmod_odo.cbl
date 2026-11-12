       IDENTIFICATION DIVISION.
       PROGRAM-ID. REFMOD-ODO.
      * A reference modifier without a length runs to the current end of a
      * group whose table has OCCURS DEPENDING ON
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 CNT PIC 9 VALUE 2.
       01 G.
          05 H PIC XX VALUE "HH".
          05 E OCCURS 1 TO 5 DEPENDING ON CNT PIC X.
       01 X10 PIC X(10).
       01 P PIC 9 VALUE 2.
       PROCEDURE DIVISION.
           MOVE 5 TO CNT.
           MOVE "abcdefg" TO G.
           MOVE 2 TO CNT.
           MOVE G(2:) TO X10.
           DISPLAY "[" X10 "]".
           MOVE G(P:) TO X10.
           DISPLAY "[" X10 "]".
           MOVE 4 TO CNT.
           MOVE G(3:) TO X10.
           DISPLAY "[" X10 "]".
           MOVE G(2:2) TO X10.
           DISPLAY "[" X10 "]".
           STOP RUN.
