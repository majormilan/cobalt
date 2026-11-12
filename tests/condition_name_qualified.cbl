       IDENTIFICATION DIVISION.
       PROGRAM-ID. CONDITION-NAME-QUALIFIED.
      * A name that is a condition-name in one record and a data item in
      * another: its qualification decides
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 REC-A.
          05 FLG PIC X VALUE "Y".
             88 ACTIVE VALUE "Y".
       01 REC-B.
          05 ACTIVE PIC X VALUE "N".
       PROCEDURE DIVISION.
           IF ACTIVE OF REC-A DISPLAY "A ACTIVE" ELSE DISPLAY "A NOT".
           IF ACTIVE OF REC-B = "N" DISPLAY "B IS N".
           EVALUATE ACTIVE OF REC-B
               WHEN "Y" DISPLAY "B Y"
               WHEN "N" DISPLAY "B N"
           END-EVALUATE.
           EVALUATE TRUE
               WHEN ACTIVE OF REC-A DISPLAY "A TRUE"
           END-EVALUATE.
           STOP RUN.
