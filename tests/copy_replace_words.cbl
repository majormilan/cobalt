       IDENTIFICATION DIVISION.
       PROGRAM-ID. COPY-REPLACE-WORDS.
      * A replacement inside a PICTURE string and in part of a word,
      * and pseudo-text that ends in a period
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY copy_rec_len REPLACING ==:P:== BY ==WS== LEN BY 4.
       PROCEDURE DIVISION.
           REPLACE ==STOP RUN.== BY ==DISPLAY "END" STOP RUN.==.
           MOVE "ABCD" TO WS-NAME.
           DISPLAY "[" WS-NAME "] " WS-CODE.
           STOP RUN.
