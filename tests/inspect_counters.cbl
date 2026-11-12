       IDENTIFICATION DIVISION.
       PROGRAM-ID. INSPECT-COUNTERS.
      * TALLYING counters that are subscripted or qualified
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 S PIC X(6) VALUE "A0A0A0".
       01 T.
          05 TC PIC 99 OCCURS 2 VALUE 0.
       01 G.
          05 C2 PIC 99 VALUE 0.
       01 C1 PIC 99 VALUE 0.
       PROCEDURE DIVISION.
           INSPECT S TALLYING C1 FOR ALL "0" TC(2) FOR ALL "A".
           DISPLAY C1 " " T.
           INSPECT S TALLYING C1 FOR ALL "A" C2 OF G FOR CHARACTERS.
           DISPLAY C1 " " C2.
           STOP RUN.
