      * A numeric item loaded, changed by statements that are not numeric
      * (MOVE through REDEFINES, INSPECT, group MOVE, CALL BY REFERENCE),
      * then loaded again: the new value is used
       IDENTIFICATION DIVISION.
       PROGRAM-ID. RELOAD-AFTER-CHANGE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  G.
           05  X   PIC 9(4) VALUE 10.
       01  XR REDEFINES G PIC X(4).
       01  T   PIC 9(6) VALUE 0.
       PROCEDURE DIVISION.
           IF X > 5
               MOVE "0020" TO XR
               ADD X TO T
           END-IF.
           DISPLAY "T=" T.
           IF X > 5
               INSPECT XR REPLACING ALL "2" BY "3"
               ADD X TO T
           END-IF.
           DISPLAY "T=" T.
           IF X > 5
               MOVE "0100" TO G
               ADD X TO T
           END-IF.
           DISPLAY "T=" T.
           IF X > 5
               CALL "SETX" USING X
               ADD X TO T
           END-IF.
           DISPLAY "T=" T.
           STOP RUN.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. SETX.
       DATA DIVISION.
       LINKAGE SECTION.
       01  P   PIC 9(4).
       PROCEDURE DIVISION USING P.
           MOVE 7000 TO P.
           EXIT PROGRAM.
       END PROGRAM SETX.
       END PROGRAM RELOAD-AFTER-CHANGE.
