       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORM-ABANDONED.
      * PERFORMs left with GO TO, millions of times, and PERFORM ranges
      * that end out of order: nothing builds up, each returns correctly
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 N PIC 9(8) VALUE 0.
       01 M PIC 9(8) VALUE 0.
       01 K PIC 9(8) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM LOOP-PARA THRU LOOP-EXIT.
           DISPLAY "PERFORMED " N.
           PERFORM TIMES-PARA THRU TIMES-EXIT.
           DISPLAY "TIMES " M.
           PERFORM A THRU B.
           DISPLAY "OVERLAP DONE " K.
           STOP RUN.
       LOOP-PARA.
           ADD 1 TO N.
           IF N > 2000000 GO TO LOOP-EXIT.
           PERFORM P.
       P.
           GO TO LOOP-PARA.
       LOOP-EXIT.
           EXIT.
       TIMES-PARA.
           ADD 1 TO M.
           IF M > 1000000 GO TO TIMES-EXIT.
           PERFORM 3 TIMES
               GO TO TIMES-PARA
           END-PERFORM.
       TIMES-EXIT.
           EXIT.
       A.
           ADD 1 TO K.
           IF K = 1 PERFORM B THRU C.
       B.
           ADD 10 TO K.
       C.
           ADD 100 TO K.
