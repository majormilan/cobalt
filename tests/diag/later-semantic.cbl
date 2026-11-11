      * Later-standard elements found after parsing, and misplaced EXITs
       IDENTIFICATION DIVISION.
       PROGRAM-ID. LATER-SEMANTIC.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A PIC X(10).
       78  K VALUE 5.
       PROCEDURE DIVISION.
       MAIN SECTION.
           DISPLAY FUNCTION TRIM(A).
           EXIT PERFORM.
           EXIT PARAGRAPH.
           STOP RUN.
