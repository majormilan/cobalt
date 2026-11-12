       IDENTIFICATION DIVISION.
       PROGRAM-ID. REPORT-SUM-COUNTER.
      * The data-name of a SUM entry names its sum counter, which the
      * procedures can read and change
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT RPT ASSIGN TO "report_sum_counter.txt".
       DATA DIVISION.
       FILE SECTION.
       FD  RPT REPORT IS TOTALS.
       WORKING-STORAGE SECTION.
       01  AMOUNT PIC 999.
       01  K      PIC 9.
       REPORT SECTION.
       RD  TOTALS.
       01  TYPE DETAIL LINE PLUS 1.
           05  COLUMN 1  PIC ZZ9  SOURCE AMOUNT.
       01  TYPE REPORT FOOTING LINE PLUS 2.
           05  COLUMN 1  PIC X(5) VALUE "TOTAL".
           05  TOTAL-AMT COLUMN 7  PIC ZZZ9  SUM AMOUNT.
       PROCEDURE DIVISION.
           OPEN OUTPUT RPT.
           INITIATE TOTALS.
           PERFORM VARYING K FROM 1 BY 1 UNTIL K > 3
               COMPUTE AMOUNT = K * 10
               GENERATE TOTALS
           END-PERFORM.
           DISPLAY "COUNTER " TOTAL-AMT.
           ADD 1 TO TOTAL-AMT.
           TERMINATE TOTALS.
           CLOSE RPT.
           STOP RUN.
