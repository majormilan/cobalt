       IDENTIFICATION DIVISION.
       PROGRAM-ID. MISSPELLED-DIVISION.
      * A misspelled PROCEDURE DIVISION header: one error, not one per line
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A PIC 9 VALUE 1.
       PROCEDUR DIVISION.
       MAIN-PARA.
           DISPLAY A.
           ADD 1 TO A.
           DISPLAY A.
           ADD 1 TO A.
           DISPLAY A.
           STOP RUN.
