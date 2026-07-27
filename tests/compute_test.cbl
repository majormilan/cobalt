       IDENTIFICATION DIVISION.
       PROGRAM-ID. COMPUTE-TEST.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A     PIC 999 VALUE 10.
       01  WS-B     PIC 999 VALUE 3.
       01  WS-C     PIC 999 VALUE 0.
       01  WS-D     PIC 9(6) VALUE 0.

       PROCEDURE DIVISION.
       MAIN.
           COMPUTE WS-C = WS-A + WS-B.
           DISPLAY WS-C.
           COMPUTE WS-C = WS-A - WS-B.
           DISPLAY WS-C.
           COMPUTE WS-C = WS-A * WS-B.
           DISPLAY WS-C.
           COMPUTE WS-C = WS-A + 5.
           DISPLAY WS-C.
           COMPUTE WS-D = WS-A * WS-B + WS-A.
           DISPLAY WS-D.
           COMPUTE WS-D = WS-A * (WS-B + 2).
           DISPLAY WS-D.
           STOP RUN.
