       IDENTIFICATION DIVISION.
       PROGRAM-ID. COMPUTEEXPR.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 A PIC S9(4)V99 VALUE 0.
       01 B PIC S9(4) VALUE 7.
       01 C PIC S9(4) VALUE 2.
       01 E PIC S9(6) VALUE 0.
       01 F PIC S9(6) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           COMPUTE A = B / C.
           DISPLAY "A=" A.
           COMPUTE A = -B + C * 3.
           DISPLAY "A=" A.
           COMPUTE E F = 2 ** 10.
           DISPLAY "E=" E " F=" F.
           COMPUTE A = (B + C) * (B - C) / 4.
           DISPLAY "A=" A.
           COMPUTE A ROUNDED = 10 / 3.
           DISPLAY "A=" A.
           COMPUTE E = B ** 2 - C ** 3.
           DISPLAY "E=" E.
           COMPUTE A = - (B * C).
           DISPLAY "A=" A.
           COMPUTE A = 1 / 3 * 3.
           DISPLAY "A=" A.
           STOP RUN.
