       IDENTIFICATION DIVISION.
       PROGRAM-ID. POWER-FRACTION.
      * An exponent with decimals; a negative base with one is a size error
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 E  PIC 9V9 VALUE 0.5.
       01 E2 PIC S9V99 VALUE -1.5.
       01 B  PIC S9(3) VALUE -8.
       01 R  PIC S9(5)V9(6).
       PROCEDURE DIVISION.
           COMPUTE R = 9 ** E ON SIZE ERROR DISPLAY "SE1".
           DISPLAY R.
           COMPUTE R = 2 ** E ON SIZE ERROR DISPLAY "SE2".
           DISPLAY R.
           COMPUTE R = 4 ** E2 ON SIZE ERROR DISPLAY "SE3".
           DISPLAY R.
           COMPUTE R ROUNDED = 10 ** 0.5 ON SIZE ERROR DISPLAY "SE4".
           DISPLAY R.
           MOVE 1 TO R.
           COMPUTE R = B ** E ON SIZE ERROR DISPLAY "SE5".
           DISPLAY R.
           STOP RUN.
