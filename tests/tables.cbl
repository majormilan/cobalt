       IDENTIFICATION DIVISION.
       PROGRAM-ID. TABLES.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 MONTHS-DATA.
          05 FILLER PIC X(9) VALUE "JANFEBMAR".
       01 MONTHS REDEFINES MONTHS-DATA.
          05 MONTH-NAME PIC X(3) OCCURS 3 TIMES.
       01 GRID.
          05 ROW OCCURS 2 TIMES.
             10 CELL PIC 99 OCCURS 3 TIMES.
       01 I     PIC 9 VALUE 0.
       01 J     PIC 9 VALUE 0.
       01 TOTAL PIC 999 VALUE 0.
       01 TXT   PIC X(10) VALUE "HELLOWORLD".
       01 POS   PIC 99 VALUE 6.
       01 LEN   PIC 99 VALUE 5.
       01 SCORES.
          05 SCORE PIC 9(3) OCCURS 5 TIMES
                   ASCENDING KEY IS SCORE INDEXED BY SX.
       01 FOUND-AT PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           DISPLAY MONTH-NAME(2).
           MOVE 3 TO I.
           DISPLAY MONTH-NAME(I).
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 2
                   AFTER J FROM 1 BY 1 UNTIL J > 3
               COMPUTE CELL(I, J) = I * 10 + J
           END-PERFORM.
           DISPLAY "CELL(2,3)=" CELL(2, 3).
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 2
                   AFTER J FROM 1 BY 1 UNTIL J > 3
               ADD CELL(I J) TO TOTAL
           END-PERFORM.
           DISPLAY "TOTAL=" TOTAL.
           DISPLAY "ROW2=" ROW(2).
           DISPLAY TXT(1:5) "-" TXT(POS:LEN) "-" TXT(POS:) "-" TXT(I + 1:2).
           MOVE "J" TO TXT(1:1).
           DISPLAY TXT.
           MOVE 1 TO I.
           MOVE "ABC" TO MONTH-NAME(I + 1).
           DISPLAY MONTHS-DATA.
           PERFORM VARYING SX FROM 1 BY 1 UNTIL SX > 5
               COMPUTE SCORE(SX) = SX * SX
           END-PERFORM.
           SET SX TO 1.
           SEARCH SCORE
               AT END DISPLAY "NOT FOUND"
               WHEN SCORE(SX) = 16 SET FOUND-AT TO SX
           END-SEARCH.
           DISPLAY "FOUND AT " FOUND-AT.
           SET SX TO 1.
           SEARCH SCORE
               AT END DISPLAY "NOT FOUND 99"
               WHEN SCORE(SX) = 99 DISPLAY "?"
           END-SEARCH.
           SEARCH ALL SCORE
               WHEN SCORE(SX) = 25 DISPLAY "ALL FOUND " SX
           END-SEARCH.
           SET SX TO 2.
           SET SX UP BY 2.
           DISPLAY "SX=" SX.
           INITIALIZE GRID.
           DISPLAY "GRID=" GRID.
           STOP RUN.
