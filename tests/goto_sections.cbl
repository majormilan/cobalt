       IDENTIFICATION DIVISION.
       PROGRAM-ID. GOTOSECTIONS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 I PIC 9 VALUE 0.
       01 K PIC 9 VALUE 2.
       PROCEDURE DIVISION.
       MAIN SECTION.
       START-PARA.
           DISPLAY "START".
           GO TO LOOP-PARA.
       SKIPPED-PARA.
           DISPLAY "NEVER".
       LOOP-PARA.
           ADD 1 TO I.
           IF I < 3 GO TO LOOP-PARA.
           DISPLAY "I=" I.
           GO TO P1 P2 P3 DEPENDING ON K.
           DISPLAY "OUT OF RANGE".
       P1.
           DISPLAY "P1".
       P2.
           DISPLAY "P2".
       P3.
           DISPLAY "P3".
           PERFORM WORK.
           PERFORM PART-A THRU PART-B.
           DISPLAY "DONE".
           STOP RUN.
       WORK SECTION.
       W1.
           DISPLAY "W1".
       W2.
           DISPLAY "W2".
       OTHER-SECTION SECTION.
       PART-A.
           DISPLAY "PART-A".
       PART-MID.
           DISPLAY "PART-MID".
       PART-B.
           DISPLAY "PART-B".
       PART-C.
           DISPLAY "PART-C".
