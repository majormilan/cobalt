      * Payroll: reads the employees (a text file), sorts them by
      * department and name, has TAXCALC (taxcalc.cbl) work out each
      * one's tax, and writes the pay slips with department totals.
      *
      *   cobalt payroll.cbl taxcalc.cbl
      *   ./payroll                    (reads employees.txt,
      *                                 writes payslips.txt)
       IDENTIFICATION DIVISION.
       PROGRAM-ID. PAYROLL.
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT EMPLOYEES ASSIGN TO "employees.txt"
               ORGANIZATION IS LINE SEQUENTIAL
               FILE STATUS IS EMP-STATUS.
           SELECT SORT-WORK ASSIGN TO "sortwork".
           SELECT PAYSLIPS ASSIGN TO "payslips.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
       DATA DIVISION.
       FILE SECTION.
       FD  EMPLOYEES.
           COPY EMPREC.
       SD  SORT-WORK.
       01  SORT-RECORD.
           05  S-DEPT          PIC X(4).
           05  S-NAME          PIC X(20).
           05  S-ID            PIC 9(5).
           05  S-GROSS         PIC 9(6)V99.
       FD  PAYSLIPS.
       01  SLIP-LINE           PIC X(60).
       WORKING-STORAGE SECTION.
       01  EMP-STATUS          PIC XX.
       01  END-OF-INPUT        PIC X VALUE "N".
           88  NO-MORE-INPUT         VALUE "Y".
       01  END-OF-SORT         PIC X VALUE "N".
           88  NO-MORE-SORTED        VALUE "Y".
       01  GROSS               PIC 9(6)V99.
       01  TAX                 PIC 9(6)V99.
       01  NET                 PIC 9(6)V99.
       01  CURRENT-DEPT        PIC X(4) VALUE SPACES.
       01  DEPT-NET            PIC 9(7)V99 VALUE 0.
       01  TOTAL-NET           PIC 9(8)V99 VALUE 0.
       01  DETAIL-LINE.
           05  D-ID            PIC 9(5).
           05  FILLER          PIC X VALUE SPACE.
           05  D-NAME          PIC X(20).
           05  D-GROSS         PIC ZZZ,ZZ9.99.
           05  D-TAX           PIC ZZZ,ZZ9.99.
           05  D-NET           PIC ZZZ,ZZ9.99.
       01  TOTAL-LINE.
           05  FILLER          PIC X(6) VALUE SPACES.
           05  T-LABEL         PIC X(20).
           05  FILLER          PIC X(20) VALUE SPACES.
           05  T-NET           PIC Z,ZZZ,ZZ9.99.
       PROCEDURE DIVISION.
       MAIN-PROGRAM.
           SORT SORT-WORK ON ASCENDING KEY S-DEPT S-NAME
               INPUT PROCEDURE IS READ-EMPLOYEES
               OUTPUT PROCEDURE IS WRITE-PAYSLIPS.
           DISPLAY "Pay slips written to payslips.txt".
           STOP RUN.

       READ-EMPLOYEES.
           OPEN INPUT EMPLOYEES.
           IF EMP-STATUS NOT = "00"
               DISPLAY "Cannot open employees.txt: status " EMP-STATUS
               STOP RUN
           END-IF.
           PERFORM UNTIL NO-MORE-INPUT
               READ EMPLOYEES
                   AT END SET NO-MORE-INPUT TO TRUE
                   NOT AT END PERFORM RELEASE-EMPLOYEE
               END-READ
           END-PERFORM.
           CLOSE EMPLOYEES.

       RELEASE-EMPLOYEE.
           MOVE EMP-DEPT TO S-DEPT.
           MOVE EMP-NAME TO S-NAME.
           MOVE EMP-ID TO S-ID.
           COMPUTE S-GROSS ROUNDED = EMP-HOURS * EMP-RATE.
           RELEASE SORT-RECORD.

       WRITE-PAYSLIPS.
           OPEN OUTPUT PAYSLIPS.
           PERFORM UNTIL NO-MORE-SORTED
               RETURN SORT-WORK
                   AT END SET NO-MORE-SORTED TO TRUE
                   NOT AT END PERFORM WRITE-ONE-SLIP
               END-RETURN
           END-PERFORM.
           PERFORM DEPARTMENT-TOTAL.
           MOVE "All departments" TO T-LABEL.
           MOVE TOTAL-NET TO T-NET.
           WRITE SLIP-LINE FROM TOTAL-LINE.
           CLOSE PAYSLIPS.

       WRITE-ONE-SLIP.
           IF S-DEPT NOT = CURRENT-DEPT
               IF CURRENT-DEPT NOT = SPACES
                   PERFORM DEPARTMENT-TOTAL
               END-IF
               MOVE S-DEPT TO CURRENT-DEPT
               MOVE SPACES TO SLIP-LINE
               STRING "Department " DELIMITED BY SIZE
                      S-DEPT DELIMITED BY SPACE
                   INTO SLIP-LINE
               WRITE SLIP-LINE
           END-IF.
           MOVE S-GROSS TO GROSS.
           CALL "TAXCALC" USING GROSS TAX.
           SUBTRACT TAX FROM GROSS GIVING NET.
           ADD NET TO DEPT-NET TOTAL-NET.
           MOVE S-ID TO D-ID.
           MOVE S-NAME TO D-NAME.
           MOVE GROSS TO D-GROSS.
           MOVE TAX TO D-TAX.
           MOVE NET TO D-NET.
           WRITE SLIP-LINE FROM DETAIL-LINE.

       DEPARTMENT-TOTAL.
           MOVE SPACES TO T-LABEL.
           STRING "Total " DELIMITED BY SIZE
                  CURRENT-DEPT DELIMITED BY SPACE
               INTO T-LABEL.
           MOVE DEPT-NET TO T-NET.
           WRITE SLIP-LINE FROM TOTAL-LINE.
           MOVE 0 TO DEPT-NET.
