       IDENTIFICATION DIVISION.
       PROGRAM-ID. CORRESP.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 SRC.
          05 A      PIC 99 VALUE 11.
          05 B      PIC X(3) VALUE "BBB".
          05 SUB-G.
             10 C   PIC 9 VALUE 3.
             10 D   PIC 9 VALUE 4.
          05 E      PIC 99 VALUE 55.
          05 FILLER PIC X VALUE "F".
       01 DST.
          05 B      PIC X(5) VALUE ALL "-".
          05 X      PIC 99 VALUE 0.
          05 SUB-G.
             10 D   PIC 99 VALUE 0.
          05 A      PIC 999V9 VALUE 0.
          05 E      PIC X(2) VALUE "ZZ".
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE CORRESPONDING SRC TO DST.
           DISPLAY "[" DST "]".
           ADD CORR SRC TO DST.
           DISPLAY A OF DST " " D OF DST.
           SUBTRACT CORR SRC FROM DST ROUNDED.
           DISPLAY A OF DST " " D OF DST.
           STOP RUN.
