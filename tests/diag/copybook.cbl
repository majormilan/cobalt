       IDENTIFICATION DIVISION.
       PROGRAM-ID. COPYBOOK.
      * Errors in copied text are shown where they are, in the library
      * text, with the COPY statements that brought the text in
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY BADREC.
       01  AFTER-COPY     PIC X(4).
       PROCEDURE DIVISION.
           DISPLAY BAD-CODE BAD-COUNT INNER-AMOUNT AFTER-COPY.
           MOVE 1 TO AFTER-COPY-X.
           STOP RUN.
