      * After syntax errors in statements the later checks still run
       IDENTIFICATION DIVISION.
       PROGRAM-ID. RECOVERY.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A PIC 9(3).
       01  B PIC X(5).
       PROCEDURE DIVISION.
           MOVE 1 TO A
           DISPLY A.
           ADD B TO A.
           COMPUTE A = A +.
           PERFORM NOPARA.
           STOP RUN.
