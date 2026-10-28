       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORMFORMS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 I PIC 99 VALUE 0.
       01 J PIC 99 VALUE 0.
       01 N PIC 9 VALUE 2.
       01 T PIC 999 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM 3 TIMES DISPLAY "INLINE TIMES" END-PERFORM.
           PERFORM N TIMES ADD 1 TO T END-PERFORM.
           DISPLAY "T=" T.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 3
               DISPLAY "I=" I
           END-PERFORM.
           MOVE 0 TO T.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 2
                   AFTER J FROM 1 BY 1 UNTIL J > 3
               ADD 1 TO T
           END-PERFORM.
           DISPLAY "T=" T.
           MOVE 5 TO I.
           PERFORM WITH TEST AFTER UNTIL I > 3
               DISPLAY "TEST AFTER I=" I
           END-PERFORM.
           PERFORM UNTIL I > 3 DISPLAY "NOT SHOWN" END-PERFORM.
           PERFORM SHOW-I VARYING I FROM 10 BY -3 UNTIL I < 5.
           PERFORM SHOW-I 2 TIMES.
           MOVE 0 TO J.
           PERFORM OUTER-P 2 TIMES.
           DISPLAY "J=" J.
           STOP RUN.
       SHOW-I.
           DISPLAY "SHOW " I.
       OUTER-P.
           PERFORM INNER-P 2 TIMES.
       INNER-P.
           ADD 1 TO J.
