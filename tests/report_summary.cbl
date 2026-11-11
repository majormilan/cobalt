       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUMMARY-REPORT.
      * Report Writer: summary reporting (GENERATE report-name), SUM
      * UPON, RESET ON FINAL, a report without a PAGE clause, two
      * reports written to one file
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT PRINT-FILE ASSIGN TO "summary.txt".
           SELECT PRINT-IN ASSIGN TO "summary.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
       DATA DIVISION.
       FILE SECTION.
       FD  PRINT-FILE
           REPORTS ARE BY-DEPT RUNNING.
       FD  PRINT-IN.
       01  PRINT-IN-LINE  PIC X(40).
       WORKING-STORAGE SECTION.
       01  DEPT           PIC X(4).
       01  HOURS          PIC 99.
       01  K              PIC 9.
       01  EOF-FLAG       PIC X VALUE "N".
       REPORT SECTION.
       RD  BY-DEPT
           CONTROLS ARE DEPT.
       01  HOURS-LINE TYPE DETAIL LINE PLUS 1.
           05  COLUMN 1  PIC X(4)  SOURCE DEPT.
           05  COLUMN 8  PIC Z9    SOURCE HOURS.
       01  TYPE CONTROL FOOTING DEPT LINE PLUS 1.
           05  COLUMN 1  PIC X(4)  SOURCE DEPT.
           05  COLUMN 6  PIC ZZZ9  SUM HOURS.
           05  COLUMN 12 PIC ZZZ9  SUM HOURS RESET ON FINAL.
       RD  RUNNING.
       01  TYPE DETAIL LINE PLUS 1.
           05  COLUMN 1  PIC X(8) VALUE "RUNNING".
           05  COLUMN 10 PIC Z9   SOURCE K.
       01  TYPE REPORT FOOTING LINE PLUS 2.
           05  COLUMN 1  PIC X(6) VALUE "COUNT".
           05  COLUMN 9  PIC ZZ9  SUM K.
       PROCEDURE DIVISION.
       MAIN-PARA.
           OPEN OUTPUT PRINT-FILE.
           INITIATE BY-DEPT RUNNING.
           MOVE "ACCT" TO DEPT.
           MOVE 8 TO HOURS. GENERATE BY-DEPT.
           MOVE 7 TO HOURS. GENERATE BY-DEPT.
           MOVE "SHOP" TO DEPT.
           MOVE 9 TO HOURS. GENERATE BY-DEPT.
           MOVE 6 TO HOURS. GENERATE HOURS-LINE.
           PERFORM VARYING K FROM 1 BY 1 UNTIL K > 3
               GENERATE RUNNING
           END-PERFORM.
           TERMINATE BY-DEPT RUNNING.
           CLOSE PRINT-FILE.
           OPEN INPUT PRINT-IN.
           PERFORM UNTIL EOF-FLAG = "Y"
               READ PRINT-IN
                   AT END MOVE "Y" TO EOF-FLAG
                   NOT AT END DISPLAY "|" PRINT-IN-LINE "|"
               END-READ
           END-PERFORM.
           CLOSE PRINT-IN.
           STOP RUN.
