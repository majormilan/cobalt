      * Sales report: the Report Writer prints a sales file sorted by
      * region with a page heading, a line per sale, a total for each
      * region (a control break) and a grand total. The program only
      * INITIATEs, GENERATEs and TERMINATEs; the RD says the rest.
      *
      *   cobalt sales.cbl
      *   ./sales                      (reads sales.txt,
      *                                 writes report.txt)
       IDENTIFICATION DIVISION.
       PROGRAM-ID. SALES.
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT SALES-FILE ASSIGN TO "sales.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
           SELECT REPORT-FILE ASSIGN TO "report.txt".
       DATA DIVISION.
       FILE SECTION.
       FD  SALES-FILE.
       01  SALE.
           05  SALE-REGION     PIC X(8).
           05  SALE-REP        PIC X(12).
           05  SALE-AMOUNT     PIC 9(5)V99.
       FD  REPORT-FILE
           REPORT IS SALES-REPORT.
       WORKING-STORAGE SECTION.
       01  END-OF-SALES        PIC X VALUE "N".
           88  NO-MORE-SALES         VALUE "Y".
       REPORT SECTION.
       RD  SALES-REPORT
           CONTROLS ARE FINAL SALE-REGION
           PAGE LIMIT IS 30 LINES
           HEADING 1
           FIRST DETAIL 4
           LAST DETAIL 26.
       01  TYPE IS PAGE HEADING.
           05  LINE 1.
               10  COLUMN 1    PIC X(20) VALUE "Sales by region".
               10  COLUMN 40   PIC X(5) VALUE "Page".
               10  COLUMN 46   PIC Z9 SOURCE PAGE-COUNTER.
           05  LINE 2.
               10  COLUMN 1    PIC X(6) VALUE "Region".
               10  COLUMN 11   PIC X(14) VALUE "Representative".
               10  COLUMN 36   PIC X(6) VALUE "Amount".
       01  SALE-LINE TYPE IS DETAIL.
           05  LINE PLUS 1.
               10  COLUMN 1    PIC X(8) SOURCE SALE-REGION
                               GROUP INDICATE.
               10  COLUMN 11   PIC X(12) SOURCE SALE-REP.
               10  COLUMN 31   PIC ZZ,ZZ9.99 SOURCE SALE-AMOUNT.
       01  TYPE IS CONTROL FOOTING SALE-REGION.
           05  LINE PLUS 1.
               10  COLUMN 11   PIC X(12) VALUE "Region total".
               10  REGION-TOTAL COLUMN 30
                               PIC ZZZ,ZZ9.99 SUM SALE-AMOUNT.
           05  LINE PLUS 1.
               10  COLUMN 1    PIC X VALUE SPACE.
       01  TYPE IS CONTROL FOOTING FINAL.
           05  LINE PLUS 1.
               10  COLUMN 11   PIC X(11) VALUE "Grand total".
               10  COLUMN 28   PIC Z,ZZZ,ZZ9.99 SUM REGION-TOTAL.
       PROCEDURE DIVISION.
       MAIN-PROGRAM.
           OPEN INPUT SALES-FILE OUTPUT REPORT-FILE.
           INITIATE SALES-REPORT.
           PERFORM UNTIL NO-MORE-SALES
               READ SALES-FILE
                   AT END SET NO-MORE-SALES TO TRUE
                   NOT AT END GENERATE SALE-LINE
               END-READ
           END-PERFORM.
           TERMINATE SALES-REPORT.
           CLOSE SALES-FILE REPORT-FILE.
           DISPLAY "Report written to report.txt".
           STOP RUN.
