      * ASSIGN names mapped at run time: DD_name, dd_name, name
       IDENTIFICATION DIVISION.
       PROGRAM-ID. FILE-MAPPING.
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT OUT-1 ASSIGN TO MAPONE.
           SELECT OUT-2 ASSIGN TO MAPTWO.
           SELECT OUT-3 ASSIGN TO MAPTHREE.
           SELECT IN-1 ASSIGN TO "mapped-1.dat".
           SELECT IN-2 ASSIGN TO "mapped-2.dat".
           SELECT IN-3 ASSIGN TO "mapped-3.dat".
       DATA DIVISION.
       FILE SECTION.
       FD  OUT-1.
       01  O1 PIC X(5).
       FD  OUT-2.
       01  O2 PIC X(5).
       FD  OUT-3.
       01  O3 PIC X(5).
       FD  IN-1.
       01  I1 PIC X(5).
       FD  IN-2.
       01  I2 PIC X(5).
       FD  IN-3.
       01  I3 PIC X(5).
       PROCEDURE DIVISION.
           OPEN OUTPUT OUT-1 OUT-2 OUT-3.
           MOVE "ONE" TO O1. WRITE O1.
           MOVE "TWO" TO O2. WRITE O2.
           MOVE "THREE" TO O3. WRITE O3.
           CLOSE OUT-1 OUT-2 OUT-3.
           OPEN INPUT IN-1 IN-2 IN-3.
           READ IN-1. READ IN-2. READ IN-3.
           DISPLAY I1 "|" I2 "|" I3.
           CLOSE IN-1 IN-2 IN-3.
           STOP RUN.
