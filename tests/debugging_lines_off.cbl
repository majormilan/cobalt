       IDENTIFICATION DIVISION.
       PROGRAM-ID. DEBUGGING-LINES-OFF.
      * Without WITH DEBUGGING MODE, debugging lines are comments, even
      * when the words appear elsewhere (a literal, a comment line)
      * WITH DEBUGGING MODE
       ENVIRONMENT DIVISION.
       CONFIGURATION SECTION.
       SOURCE-COMPUTER. GENERIC.
       OBJECT-COMPUTER. GENERIC.
       PROCEDURE DIVISION.
           DISPLAY "NOT IN DEBUGGING MODE".
      D    DISPLAY "DEBUG LINE COMPILED".
           STOP RUN.
