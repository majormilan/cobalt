       IDENTIFICATION DIVISION.
       PROGRAM-ID. POWER-ZERO.
      * x ** 0 is 1; 0 ** 0 is a size error, whatever the exponent's form
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 X PIC 9 VALUE 0.
       01 Y PIC 9 VALUE 5.
       01 E PIC 9 VALUE 0.
       01 Q PIC 9 VALUE 7.
       PROCEDURE DIVISION.
           COMPUTE Q = X ** 0 ON SIZE ERROR DISPLAY "SIZE ERROR 1".
           DISPLAY Q.
           COMPUTE Q = X ** E ON SIZE ERROR DISPLAY "SIZE ERROR 2".
           DISPLAY Q.
           COMPUTE Q = 0 ** 0 ON SIZE ERROR DISPLAY "SIZE ERROR 3".
           DISPLAY Q.
           COMPUTE Q = Y ** 0 ON SIZE ERROR DISPLAY "SIZE ERROR 4".
           DISPLAY Q.
           COMPUTE Q = 3 ** 0 ON SIZE ERROR DISPLAY "SIZE ERROR 5".
           DISPLAY Q.
           STOP RUN.
