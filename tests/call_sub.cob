       IDENTIFICATION DIVISION.
       PROGRAM-ID. CALLSUB.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  CNT PIC 99 VALUE 0.
       01  EXT IS EXTERNAL.
           05 EXT-N PIC 9(3).
       LINKAGE SECTION.
       01  X PIC 9(4).
       01  Y PIC X(5).
       PROCEDURE DIVISION USING X Y.
           ADD 1 TO CNT.
           DISPLAY "SUB CALL " CNT " X=" X " Y=" Y " EXT " EXT-N.
           ADD 100 TO X.
           MOVE "WORLD" TO Y.
           ADD 1 TO EXT-N.
           PERFORM P1.
           EXIT PROGRAM.
       P1.
           DISPLAY "  P1".
       END PROGRAM CALLSUB.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. INIT-SUB IS INITIAL.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  N PIC 9 VALUE 1.
       PROCEDURE DIVISION.
           DISPLAY "INITIAL " N.
           ADD 1 TO N.
           EXIT PROGRAM.
       END PROGRAM INIT-SUB.
