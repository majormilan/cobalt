       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-ALNUM-LITERAL.
      * A literal that is not all digits into numeric DISPLAY items:
      * characters as an unsigned integer for integer items.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  R1.
           05 U3  PIC 9(3).
       01  R2.
           05 S3  PIC S9(3).
       01  R3.
           05 V2  PIC 9(3)V99.
       01  R4.
           05 SS  PIC S9(3) SIGN LEADING SEPARATE.
       01  R5.
           05 U2  PIC 99.
       01  R6.
           05 U5  PIC 9(5).
       01  X4     PIC X(4) VALUE "A1B2".
       PROCEDURE DIVISION.
           MOVE "ABC" TO U3. DISPLAY R1.
           MOVE "ABC" TO S3. DISPLAY R2.
           MOVE "AB3" TO S3. DISPLAY R2.
           MOVE "ABC" TO V2. DISPLAY R3.
           MOVE "ABC" TO SS. DISPLAY R4.
           MOVE "ABC" TO U2. DISPLAY R5.
           MOVE "ABC" TO U5. DISPLAY R6.
           MOVE X4 TO U3. DISPLAY R1.
           MOVE X4 TO U5. DISPLAY R6.
           MOVE "12" TO S3. DISPLAY R2.
           STOP RUN.
