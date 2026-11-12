       IDENTIFICATION DIVISION.
       PROGRAM-ID. EVALUATE-CONDITIONS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A PIC 9 VALUE 3.
       01  B PIC 9 VALUE 5.
       01  F PIC 9 VALUE 1.
           88 F-ON VALUE 1.
       PROCEDURE DIVISION.
       P1.
           EVALUATE A = 3 AND B = 5
             WHEN TRUE DISPLAY "E1"
             WHEN FALSE DISPLAY "E1F"
           END-EVALUATE
           EVALUATE NOT A = 3
             WHEN FALSE DISPLAY "E2"
           END-EVALUATE
           EVALUATE F-ON OR A = 1
             WHEN TRUE DISPLAY "E3"
           END-EVALUATE
           EVALUATE (A = 3)
             WHEN TRUE DISPLAY "E4"
           END-EVALUATE
           EVALUATE A = 1 OR 3
             WHEN TRUE DISPLAY "E5"
           END-EVALUATE
           EVALUATE A + 1 ALSO B > 4
             WHEN 4 ALSO TRUE DISPLAY "E6"
           END-EVALUATE
           STOP RUN.
