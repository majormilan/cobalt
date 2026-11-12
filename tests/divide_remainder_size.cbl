       IDENTIFICATION DIVISION.
       PROGRAM-ID. DIVIDE-REMAINDER-SIZE.
      * A size error on the quotient leaves the remainder unchanged
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 Q PIC 9.
       01 R PIC 999.
       PROCEDURE DIVISION.
           MOVE 7 TO Q. MOVE 9 TO R.
           DIVIDE 250 BY 10 GIVING Q REMAINDER R
               ON SIZE ERROR DISPLAY "SIZE ERROR"
               NOT ON SIZE ERROR DISPLAY "NO SIZE ERROR".
           DISPLAY Q " " R.
           DIVIDE 25 BY 10 GIVING Q REMAINDER R
               ON SIZE ERROR DISPLAY "SIZE ERROR"
               NOT ON SIZE ERROR DISPLAY "NO SIZE ERROR".
           DISPLAY Q " " R.
           STOP RUN.
