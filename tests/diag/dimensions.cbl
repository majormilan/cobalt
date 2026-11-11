      * More than 16 dimensions: an error at the declaration
       IDENTIFICATION DIVISION.
       PROGRAM-ID. DIMS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  T.
           02 L0 OCCURS 2.
           03 L1 OCCURS 2.
           04 L2 OCCURS 2.
           05 L3 OCCURS 2.
           06 L4 OCCURS 2.
           07 L5 OCCURS 2.
           08 L6 OCCURS 2.
           09 L7 OCCURS 2.
           10 L8 OCCURS 2.
           11 L9 OCCURS 2.
           12 L10 OCCURS 2.
           13 L11 OCCURS 2.
           14 L12 OCCURS 2.
           15 L13 OCCURS 2.
           16 L14 OCCURS 2.
           17 L15 OCCURS 2.
           18 L16 OCCURS 2 PIC X.
       PROCEDURE DIVISION.
           DISPLAY T(1:1).
           STOP RUN.
