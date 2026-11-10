       IDENTIFICATION DIVISION.
       PROGRAM-ID. COPY-REPLACE.
       AUTHOR. SOMEONE WHO WRITES
           A COMMENT ENTRY OVER SEVERAL LINES, WITH "QUOTES AND
           PERIODS. AND THE WORD COPY.
       INSTALLATION.
       DATE-WRITTEN. TODAY.
      * COPY and REPLACE: plain, REPLACING with words, identifiers
      * and pseudo-text, a library name, REPLACE and REPLACE OFF
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY copy_rec.
       COPY copy_rec REPLACING REC-A BY REC-B
                               FLD-X BY ==FLD-Y==
                ==PIC 9(3) VALUE 42== BY ==PIC 9(4) VALUE 1234==.
       77  OUT-N  PIC 9(4).
       REPLACE ==SHOW== BY ==DISPLAY==
               ==LINE-1== BY =="REPLACED " FLD-X OF REC-A==.
       PROCEDURE DIVISION.
           SHOW LINE-1.
       REPLACE OFF.
           DISPLAY "SHOW".
           COPY copy_stmt.
           COPY copy_stmt REPLACING REC-A BY REC-B.
           DISPLAY FLD-Y " " FLD-N OF REC-B.
           COPY COPYLIB OF MYLIB.
           STOP RUN.
