       IDENTIFICATION DIVISION.
       PROGRAM-ID. SENTENCESIF.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 A PIC 9 VALUE 1.
       01 B PIC 9 VALUE 2.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 3 TO A MOVE 4 TO B DISPLAY "A=" A " B=" B.
           IF A = 3 DISPLAY "A IS 3" ELSE DISPLAY "A NOT 3".
           IF A = 3
              IF B = 5 DISPLAY "B IS 5"
              ELSE DISPLAY "B NOT 5"
              END-IF
              DISPLAY "STILL IN OUTER"
           ELSE
              DISPLAY "OUTER ELSE"
           END-IF.
           IF A = 9 DISPLAY "X" ELSE IF B = 4 DISPLAY "ELSE-IF".
           IF A = 3 NEXT SENTENCE ELSE DISPLAY "NOT SHOWN".
           DISPLAY "AFTER NEXT SENTENCE".
           IF A = 3 THEN
              DISPLAY "THEN BRANCH"
           END-IF
           DISPLAY "SAME SENTENCE".
           STOP RUN.
