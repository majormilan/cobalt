       IDENTIFICATION DIVISION.
       PROGRAM-ID. SEGMENTATION.
      * Sections with segment numbers: an independent segment (50-99)
      * is in its initial state when entered from another segment, a
      * fixed one keeps its last state
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  N PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-SECT SECTION.
       M1.
           PERFORM IND-SECT.
           ALTER IND-GO TO PROCEED TO IND-ALTERED.
           PERFORM IND-SECT.
           PERFORM FIX-SECT.
           ALTER FIX-GO TO PROCEED TO FIX-ALTERED.
           PERFORM FIX-SECT.
           STOP RUN.
       FIX-SECT SECTION 10.
       FIX-GO.
           GO TO FIX-FIRST.
       FIX-FIRST.
           DISPLAY "FIXED: FIRST TARGET".
           GO TO FIX-END.
       FIX-ALTERED.
           DISPLAY "FIXED: ALTERED TARGET".
       FIX-END.
           EXIT.
       IND-SECT SECTION 60.
       IND-GO.
           GO TO IND-FIRST.
       IND-FIRST.
           DISPLAY "INDEPENDENT: FIRST TARGET".
           GO TO IND-END.
       IND-ALTERED.
           DISPLAY "INDEPENDENT: ALTERED TARGET".
       IND-END.
           EXIT.
