       IDENTIFICATION DIVISION.
       PROGRAM-ID. INITIALIZE-INDEX.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 T2.
          05 E2 OCCURS 5 INDEXED BY X2 PIC X(7).
       01 W PIC 99.
       01 G.
          05 GA PIC X(2) VALUE "QQ".
          05 GI USAGE INDEX.
       PROCEDURE DIVISION.
           SET X2 TO 4.
           SET GI TO X2.
           SET X2 TO 1.
           SET X2 TO GI. SET W TO X2. DISPLAY "BEFORE=" W.
           INITIALIZE G.
           SET X2 TO GI. SET W TO X2. DISPLAY "AFTER=" W.
           STOP RUN.
