       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUMDECIMAL.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 P  PIC 9(5)V99 VALUE 1234.56.
       01 Q  PIC 9V9 VALUE 2.5.
       01 R  PIC 9(7)V99 VALUE 0.
       01 T  PIC 999 VALUE 0.
       01 D  PIC 9V9(4) VALUE 0.
       01 SDV PIC S99V99 VALUE -1.05.
       PROCEDURE DIVISION.
       MAIN-PARA.
           DISPLAY "P=" P.
           ADD Q TO P.
           DISPLAY "P=" P.
           MULTIPLY Q BY P GIVING R.
           DISPLAY "R=" R.
           MOVE P TO T.
           DISPLAY "T=" T.
           DIVIDE 3 INTO 1 GIVING D.
           DISPLAY "D=" D.
           MOVE 12345.678 TO P.
           DISPLAY "P=" P.
           SUBTRACT SDV FROM Q.
           DISPLAY "Q=" Q.
           MOVE 0.05 TO SDV.
           DISPLAY "SDV=" SDV.
           MOVE SDV TO Q.
           DISPLAY "Q=" Q.
           STOP RUN.
