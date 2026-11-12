       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-SUBSCRIPT-ONCE.
      * The sending item's subscripts and reference modifier are evaluated
      * once, before the first receiver
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 T.
          05 TE PIC 9 OCCURS 5.
       01 I PIC 9 VALUE 1.
       01 J PIC 9.
       01 S PIC X(5) VALUE "ABCDE".
       01 P PIC 9 VALUE 2.
       01 X PIC X.
       PROCEDURE DIVISION.
           MOVE "35791" TO T.
           MOVE TE(I) TO I J.
           DISPLAY I " " J.
           MOVE 5 TO I.
           MOVE TE(I) TO J I.
           DISPLAY I " " J.
           MOVE S(P:1) TO X P.
           DISPLAY X " " P.
           STOP RUN.
