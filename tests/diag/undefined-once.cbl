       IDENTIFICATION DIVISION.
       PROGRAM-ID. UNDEFINED-ONCE.
      * An undefined name is reported at its first use only
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  COUNTER PIC 9 VALUE 1.
       PROCEDURE DIVISION.
           DISPLAY COUNTR.
           ADD 1 TO COUNTR.
           DISPLAY COUNTR.
           STOP RUN.
