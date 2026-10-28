       IDENTIFICATION DIVISION.
       PROGRAM-ID. INITJUST.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 REC.
          05 R-NAME PIC X(5) VALUE "ABCDE".
          05 R-NUM  PIC S9(3) COMP-3 VALUE -5.
          05 R-TAB  OCCURS 3.
             10 R-CODE PIC X(2).
             10 R-VAL  PIC 99.
       01 J-FIELD PIC X(6) JUSTIFIED RIGHT.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE "ZZ" TO R-CODE(2).
           MOVE 7 TO R-VAL(3).
           INITIALIZE REC.
           DISPLAY "[" R-NAME "] " R-NUM " [" R-CODE(2) "] " R-VAL(3).
           MOVE "ABC" TO J-FIELD.
           DISPLAY "[" J-FIELD "]".
           MOVE "ABCDEFGHI" TO J-FIELD.
           DISPLAY "[" J-FIELD "]".
           MOVE "Q" TO R-CODE(1).
           INITIALIZE R-TAB(1).
           DISPLAY "[" R-CODE(1) "]".
           STOP RUN.
