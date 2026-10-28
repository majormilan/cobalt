       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUMSIGNED.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 S1 PIC S999 VALUE -42.
       01 S2 PIC S999 VALUE 17.
       01 S3 PIC S9(5) VALUE 0.
       01 U1 PIC 999 VALUE 5.
       PROCEDURE DIVISION.
       MAIN-PARA.
           DISPLAY "S1=" S1.
           DISPLAY "S2=" S2.
           ADD S1 TO S2.
           DISPLAY "S2=" S2.
           SUBTRACT 100 FROM S3.
           DISPLAY "S3=" S3.
           MULTIPLY -3 BY S3.
           DISPLAY "S3=" S3.
           MOVE S1 TO U1.
           DISPLAY "U1=" U1.
           COMPUTE S3 = S1 * 2 - 16.
           DISPLAY "S3=" S3.
           COMPUTE S1 = 0 - 0.
           DISPLAY "S1=" S1.
           STOP RUN.
