       IDENTIFICATION DIVISION.
       PROGRAM-ID. INVALID-ENTRIES.
      * Invalid code that used to compile without a diagnostic
       ENVIRONMENT DIVISION.
       CONFIGURATION SECTION.
       SPECIAL-NAMES.
           ALPHABET ODD IS "A" ALSO "a" THRU "c".
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  PIC 9.
       01  T.
           05  E OCCURS 3 ASCENDING KEY IS K INDEXED BY X.
               10  K PIC 9.
               10  V PIC X VALUE "Y".
       PROCEDURE DIVISION.
           REPLACE ==== BY ==Y==.
      X    DISPLAY "X".
           SEARCH ALL E
               WHEN V (X) = "A" DISPLAY "FOUND"
           END-SEARCH.
           STOP RUN.
