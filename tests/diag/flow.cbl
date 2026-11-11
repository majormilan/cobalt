       IDENTIFICATION DIVISION.
       PROGRAM-ID. FLOW.
      * Control flow: statements after GO TO or STOP RUN, paragraphs
      * nothing performs or falls into
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  COUNTER        PIC 9(4) VALUE ZERO.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM COUNT-PARA THRU COUNT-EXIT.
           GO TO FINISH.
           DISPLAY "NEVER SHOWN".
       COUNT-PARA.
           ADD 1 TO COUNTER.
       COUNT-EXIT.
           EXIT.
       FINISH.
           DISPLAY COUNTER.
           STOP RUN.
       OLD-REPORT.
           DISPLAY "NOBODY PERFORMS THIS".
       OLD-REPORT-EXIT.
           EXIT.
