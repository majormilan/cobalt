       IDENTIFICATION DIVISION.
       PROGRAM-ID. LOOP-CLOSED-FORM.
      * Counting loops that only accumulate: the compiler computes the
      * final values, unless an intermediate value would not fit, in
      * which case the loop runs (truncation, size errors, magnitudes).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  I    PIC S9(9) COMP-5.
       77  K    PIC 9(4) COMP.
       77  A    PIC S9(7)V99 COMP-3 VALUE 123.45.
       77  B    PIC S9(5) VALUE -17.
       77  C    PIC S9(13)V9999 COMP-5 VALUE 0.
       77  D    PIC S9(15)V99 VALUE 0.
       77  P3   PIC S9(3) COMP-3 VALUE 0.
       77  U3   PIC 9(3) VALUE 10.
       77  N18  PIC S9(18) COMP-5 VALUE 0.
       77  BIG  PIC S9(18) COMP-5 VALUE 4000000000000000.
       77  Q    PIC S9(9) COMP VALUE 0.
       77  E    PIC 9(4) VALUE 0.
       PROCEDURE DIVISION.
      * two accumulators, a scale change, a counter from 10 by 3
           PERFORM VARYING I FROM 10 BY 3 UNTIL I > 100000
               ADD A TO C
               SUBTRACT B FROM D
           END-PERFORM.
           DISPLAY "C " C " D " D " I " I.
      * an expression of invariants, strict limit
           MOVE 0 TO D.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I >= 5000
               COMPUTE D = D + A * 3 - B
           END-PERFORM.
           DISPLAY "D " D " I " I.
      * zero iterations
           PERFORM VARYING I FROM 7 BY 1 UNTIL I > 6
               ADD 1 TO D
           END-PERFORM.
           DISPLAY "D " D " I " I.
      * truncation: the loop must run
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 25
               ADD 47 TO P3
           END-PERFORM.
           DISPLAY "P3 " P3.
      * an unsigned item going below zero keeps magnitudes
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 7
               SUBTRACT 3 FROM U3
           END-PERFORM.
           DISPLAY "U3 " U3.
      * near the 64-bit limits: the loop must run (size errors)
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 3000
               ADD BIG TO N18 ON SIZE ERROR ADD 1 TO E
               END-ADD
           END-PERFORM.
           DISPLAY "N18 " N18 " E " E.
           MOVE 0 TO N18.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 200
               ADD BIG TO N18
           END-PERFORM.
           DISPLAY "N18 " N18.
      * a PIC-limited counter and accumulator
           PERFORM VARYING K FROM 1 BY 1 UNTIL K > 9998
               ADD 1000 TO Q
           END-PERFORM.
           DISPLAY "Q " Q " K " K.
           PERFORM VARYING K FROM 1 BY 1 UNTIL K > 9998
               ADD 100000 TO Q
           END-PERFORM.
           DISPLAY "Q " Q " K " K.
           STOP RUN.
