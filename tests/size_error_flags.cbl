      * The size error bytes across statements: an overflow in a condition
      * followed by stores, INITIALIZE after a label, ON SIZE ERROR in a
      * loop, statements that cannot overflow next to ones that can
       IDENTIFICATION DIVISION.
       PROGRAM-ID. SIZE-ERROR-FLAGS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  BIG     PIC S9(18) VALUE 999999999999999999.
       01  A       PIC S9(4) VALUE 9000.
       01  B       PIC S9(4) VALUE 2000.
       01  C       PIC S9(4) VALUE 0.
       01  P       PIC S9(5) COMP-3 VALUE 0.
       01  N       PIC 99 VALUE 0.
       01  I       PIC 99.
       01  E       PIC 9 VALUE 0.
       01  G.
           05  G1  PIC 9(3) VALUE 7.
           05  G2  PIC X(3) VALUE "ABC".
       PROCEDURE DIVISION.
       START-HERE.
           IF BIG + BIG > 0 DISPLAY "big" END-IF.
           MOVE 12 TO C.
           ADD 1 TO C.
           DISPLAY "C=" C.
           COMPUTE C = A + B
               ON SIZE ERROR DISPLAY "SE1 " C
               NOT ON SIZE ERROR DISPLAY "NO1 " C
           END-COMPUTE.
           MOVE 5 TO C.
           DISPLAY "C=" C.
       NEXT-PARA.
           INITIALIZE G.
           DISPLAY "G=" G.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 5
               ADD 30 TO N
                   ON SIZE ERROR ADD 1 TO E
               END-ADD
               COMPUTE P = P * 10 + I
           END-PERFORM.
           DISPLAY "N=" N " E=" E " P=" P.
           COMPUTE P = 99999 + 1
               ON SIZE ERROR DISPLAY "SE2 " P
           END-COMPUTE.
           COMPUTE P = 12345.
           DISPLAY "P=" P.
           DIVIDE A BY C GIVING B ROUNDED
               ON SIZE ERROR DISPLAY "SE3 " B
               NOT ON SIZE ERROR DISPLAY "NO3 " B
           END-DIVIDE.
           MOVE 0 TO C.
           DIVIDE A BY C GIVING B
               ON SIZE ERROR DISPLAY "SE4 " B
           END-DIVIDE.
           ADD 1 TO B.
           DISPLAY "B=" B.
           STOP RUN.
