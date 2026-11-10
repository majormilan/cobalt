       IDENTIFICATION DIVISION.
       PROGRAM-ID. DEEP-EXPR.
      * Expressions deep enough that intermediate values leave the
      * registers, mixed usages, constants folded at compile time,
      * 64-bit overflow as a size error, several receivers.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  A  PIC S9(5) COMP VALUE 12.
       77  B  PIC S9(5) COMP-3 VALUE -7.
       77  C  PIC S9(5) VALUE 30.
       77  D  PIC S9(3)V99 COMP-3 VALUE 1.25.
       77  E  PIC S9(9) COMP VALUE 1000.
       77  R1 PIC S9(15) VALUE 0.
       77  R2 PIC S9(9)V99 COMP-3 VALUE 0.
       77  R3 PIC S9(4) COMP VALUE 0.
       77  BIG PIC S9(18) COMP VALUE 999999999999999999.
       77  R4 PIC S9(18) COMP VALUE 0.
       PROCEDURE DIVISION.
           COMPUTE R1 = ((A + B) * (C - A)) + ((B - C) * (A + C))
                      - ((A * B) - (C * (A - (B + (C - (A * 2))))))
                      + (E - (A - (B - (C - (E - 3))))).
           DISPLAY R1.
           COMPUTE R2 ROUNDED = ((A + D) * (B - D))
                              / (C + (D * (A - B))).
           DISPLAY R2.
           COMPUTE R1 = 2 * 3 + 4 * (5 - 6) - (7 + 8) * 9.
           DISPLAY R1.
           COMPUTE R1 = - (A - B) + - C.
           DISPLAY R1.
           COMPUTE R3 R1 = A * C + B.
           DISPLAY R3 " " R1.
           COMPUTE R3 = A * C * E ON SIZE ERROR DISPLAY "R3 SIZE ERROR".
           DISPLAY R3.
           COMPUTE R4 = BIG * 10 ON SIZE ERROR DISPLAY "R4 SIZE ERROR".
           DISPLAY R4.
           ADD BIG BIG BIG BIG BIG BIG BIG BIG BIG BIG GIVING R4
               ON SIZE ERROR DISPLAY "R4 ADD SIZE ERROR".
           DISPLAY R4.
           SUBTRACT A B C FROM E GIVING R1.
           DISPLAY R1.
           STOP RUN.
