       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORMTHRUEXIT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 X PIC 9 VALUE 1.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM PROC-A THRU PROC-A-EXIT.
           MOVE 2 TO X.
           PERFORM PROC-A THRU PROC-A-EXIT.
           DISPLAY "END".
           STOP RUN.
       PROC-A.
           DISPLAY "IN PROC-A X=" X.
           IF X = 1 GO TO PROC-A-EXIT.
           DISPLAY "X IS NOT 1".
       PROC-A-EXIT.
           EXIT.
