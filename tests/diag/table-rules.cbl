       IDENTIFICATION DIVISION.
       PROGRAM-ID. TABLE-RULES.
      * -X pedantic: VALUE in a table entry; SEARCH ALL on a non-key
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  T.
           05  E OCCURS 3 ASCENDING KEY IS K INDEXED BY X.
               10  K PIC 9.
               10  V PIC X VALUE "Y".
       01  U.
           05  F PIC X OCCURS 3 INDEXED BY Y.
       PROCEDURE DIVISION.
           SEARCH ALL E
               WHEN V (X) = "A" DISPLAY "FOUND"
           END-SEARCH.
           SEARCH ALL F
               WHEN F (Y) = "A" DISPLAY "FOUND"
           END-SEARCH.
           STOP RUN.
