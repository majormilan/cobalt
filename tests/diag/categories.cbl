      * Warnings about the categories of operands: comparisons of numeric
      * and nonnumeric operands, groups moved to numeric items, decimals and
      * signs lost, an item compared with itself, a recursive PERFORM
       IDENTIFICATION DIVISION.
       PROGRAM-ID. CATEGORIES.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A PIC 9(3) VALUE 1.
       01  B PIC X(5) VALUE "5".
       01  N PIC S9(3)V99 VALUE -1.5.
       01  M PIC S9(3) VALUE -1.
       01  G.
           05  G1 PIC X(3) VALUE "123".
       PROCEDURE DIVISION.
       P1.
           IF A = "ABC" DISPLAY "X".
           IF B = 5 DISPLAY "Y".
           IF A = "001" DISPLAY "Z".
           MOVE N TO A.
           MOVE M TO A.
           MOVE G TO A.
           IF A = A DISPLAY "W".
           IF A > 2 PERFORM P1.
           STOP RUN.
