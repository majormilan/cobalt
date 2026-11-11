      * MOVE of literals into small items (stored as immediates): every
      * size from 1 to 8, JUSTIFIED RIGHT, truncation, bytes above 127
       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-LITERAL-SMALL.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  R.
           05  X1  PIC X.
           05  X2  PIC X(2).
           05  X3  PIC X(3).
           05  X4  PIC X(4).
           05  X5  PIC X(5).
           05  X6  PIC X(6).
           05  X7  PIC X(7).
           05  X8  PIC X(8).
           05  J3  PIC X(3) JUSTIFIED RIGHT.
           05  J8  PIC X(8) JUSTIFIED RIGHT.
           05  H4  PIC X(4).
       PROCEDURE DIVISION.
           MOVE ALL "*" TO R.
           MOVE "ABC" TO X1.
           MOVE "A" TO X2.
           MOVE "ABCD" TO X3.
           MOVE "WXYZ" TO X4.
           MOVE "12" TO X5.
           MOVE "SIX" TO X6.
           MOVE "SEVEN77" TO X7.
           MOVE "EIGHT888" TO X8.
           MOVE "ABCDE" TO J3.
           MOVE "AB" TO J8.
           MOVE "ÿÿ" TO H4.
           DISPLAY "[" R "]".
           MOVE SPACES TO X8.
           MOVE "ÿÿÿÿ" TO X8.
           DISPLAY "[" X8 "]".
           STOP RUN.
