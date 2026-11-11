      * COBOL 2002 statements cobalt accepts as extensions: EXIT PERFORM
      * [CYCLE], EXIT PARAGRAPH, EXIT SECTION, GOBACK, and *> comments
       IDENTIFICATION DIVISION.
       PROGRAM-ID. EXIT-STATEMENTS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  I PIC 99.
       01  J PIC 99.
       01  T PIC 99 VALUE 0.
       PROCEDURE DIVISION.
       MAIN SECTION.
       M1.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 10
               IF I = 3 EXIT PERFORM CYCLE END-IF
               IF I = 6 EXIT PERFORM END-IF
               ADD I TO T
           END-PERFORM
           DISPLAY "VARYING " T " I=" I.
           MOVE 0 TO T J.
           PERFORM 5 TIMES
               ADD 1 TO J
               IF J = 2 EXIT PERFORM CYCLE END-IF
               IF J = 4 EXIT PERFORM END-IF
               ADD 10 TO T
           END-PERFORM
           DISPLAY "TIMES " T " J=" J.
           MOVE 0 TO I.
           PERFORM UNTIL I > 50
               ADD 1 TO I
               PERFORM 3 TIMES
                   IF I = 7 EXIT PERFORM END-IF
                   ADD 1 TO T
               END-PERFORM
               IF I = 7 EXIT PERFORM END-IF
           END-PERFORM
           DISPLAY "NESTED " T " I=" I.
           PERFORM P2.
           PERFORM S2.
           DISPLAY "DONE". *> a floating comment
           CALL "SUB".
           GOBACK.
       M2.
           DISPLAY "not reached".
       S2 SECTION.
       S2A.
           DISPLAY "S2A".
           EXIT SECTION.
       S2B.
           DISPLAY "S2B not reached".
       P SECTION.
       P2.
           DISPLAY "P2 start".
           IF T > 0 EXIT PARAGRAPH END-IF
           DISPLAY "P2 not reached".
       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUB.
       PROCEDURE DIVISION.
           DISPLAY "SUB". *> GOBACK returns here
           GOBACK.
       END PROGRAM SUB.
       END PROGRAM EXIT-STATEMENTS.
