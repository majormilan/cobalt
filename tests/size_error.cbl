       IDENTIFICATION DIVISION.
       PROGRAM-ID. SIZEERROR.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 X PIC 99 VALUE 95.
       01 Y PIC 99 VALUE 0.
       01 Z PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           ADD 10 TO X
               ON SIZE ERROR DISPLAY "SIZE ERROR X=" X
               NOT ON SIZE ERROR DISPLAY "OK X=" X
           END-ADD.
           ADD 4 TO X
               ON SIZE ERROR DISPLAY "SIZE ERROR X=" X
               NOT ON SIZE ERROR DISPLAY "OK X=" X
           END-ADD.
           ADD 10 TO X.
           DISPLAY "X=" X.
           DIVIDE 0 INTO X ON SIZE ERROR DISPLAY "DIV BY ZERO X=" X.
           DIVIDE X BY 0 GIVING Y.
           DISPLAY "Y=" Y.
           COMPUTE Z = 12 ON SIZE ERROR DISPLAY "Z ERROR" END-COMPUTE.
           DISPLAY "Z=" Z.
           MOVE 2 TO Z.
           MOVE 30 TO Y.
           MULTIPLY 4 BY Z Y
               ON SIZE ERROR DISPLAY "MUL ERROR Z=" Z " Y=" Y.
           SUBTRACT 1 FROM Z NOT ON SIZE ERROR DISPLAY "SUB OK Z=" Z.
           STOP RUN.
