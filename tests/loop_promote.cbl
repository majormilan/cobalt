       IDENTIFICATION DIVISION.
       PROGRAM-ID. LOOP-PROMOTE.
      * Loops whose numeric fields the compiler keeps in registers:
      * size errors and truncation inside the loop, several exits,
      * conditional stores, invalid data that must survive untouched,
      * aliasing through REDEFINES and subscripts, nested loops.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  I    PIC S9(4) COMP-5.
       77  J    PIC S9(4) COMP-5.
       77  K    PIC 9(4) COMP.
       77  N    PIC S9(9) COMP-5 VALUE 0.
       77  P3   PIC S9(3) COMP-3 VALUE 0.
       77  P3U  PIC 9(3) COMP-3 VALUE 5.
       77  P17  PIC S9(15)V99 COMP-3 VALUE 0.
       77  D5   PIC S9(5) VALUE 0.
       77  D2   PIC 99 VALUE 0.
       77  E    PIC 9(4) VALUE 0.
       77  C2   PIC S9(4) COMP VALUE 0.
       01  R1.
           05 X  PIC S9(9) COMP-5 VALUE 0.
       01  R1X REDEFINES R1.
           05 XB PIC X(4).
       01  T.
           05 TV PIC S9(5) COMP-3 OCCURS 10.
       77  S    PIC S9(7) COMP-3 VALUE 0.
       01  SP.
           05 SPD PIC 9(3).
       01  SPX REDEFINES SP PIC X(3).
       77  U    PIC 9(3) VALUE 0.
       77  F1   PIC S9(5)V99 VALUE 1.5.
       77  F2   PIC S9(7) COMP-3 VALUE 0.
       77  F3   PIC S9(4) COMP VALUE 0.
       77  F4   PIC S9(3)V9 VALUE 0.
       77  F5   PIC 9(3) COMP-3 VALUE 0.
       77  F6   PIC S9(9) COMP-5 VALUE 0.
       77  F7   PIC S9(2) VALUE 0.
       77  SE   PIC 9(5) VALUE 0.
       77  LIM  PIC S9(4) COMP-3 VALUE 300.
       PROCEDURE DIVISION.
      * accumulation and truncation without ON SIZE ERROR: 999 wraps
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 1500
               ADD 1 TO P3
           END-PERFORM.
           DISPLAY "P3 " P3 " I " I.
      * size error kept: the receiver stays at its limit
           MOVE 0 TO E.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 200
               ADD 7 TO D2 ON SIZE ERROR ADD 1 TO E
           END-PERFORM.
           DISPLAY "D2 " D2 " E " E.
      * unsigned receivers get the magnitude
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 3
               SUBTRACT 4 FROM P3U
           END-PERFORM.
           DISPLAY "P3U " P3U.
      * decimals, several statements, subtraction
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 1000
               ADD 1.25 TO P17
               SUBTRACT 0.5 FROM P17
               ADD 3 TO D5
           END-PERFORM.
           DISPLAY "P17 " P17 " D5 " D5.
      * exit through an OR condition on two fields
           MOVE 0 TO J.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 50 OR J > 20
               ADD 3 TO J
           END-PERFORM.
           DISPLAY "I " I " J " J.
      * a store that never happens leaves invalid data untouched
           MOVE "A C" TO SPX.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 5
               IF I > 10
                   ADD 1 TO U
                   MOVE U TO SPD
               END-IF
           END-PERFORM.
           DISPLAY "SP " SP.
      * the field and its REDEFINES: the alias sees every change
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 3
               ADD 1 TO X
           END-PERFORM.
           DISPLAY "X " X.
           MOVE 0 TO X.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 3
               ADD 1 TO X
               IF XB = SPACES
                   DISPLAY "never"
               END-IF
           END-PERFORM.
           DISPLAY "X " X.
      * a table through subscripts and a promoted sum
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 10
               COMPUTE TV(I) = I * I
           END-PERFORM.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 10
               ADD TV(I) TO S
           END-PERFORM.
           DISPLAY "S " S.
      * an inner loop inside PERFORM TIMES (which uses rbx)
           MOVE 0 TO N.
           PERFORM 3 TIMES
               PERFORM VARYING J FROM 1 BY 1 UNTIL J > 4
                   ADD J TO N
                   ADD 1 TO C2
               END-PERFORM
           END-PERFORM.
           DISPLAY "N " N " C2 " C2.
      * nested VARYING loops
           MOVE 0 TO N.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 10
               PERFORM VARYING J FROM 1 BY 1 UNTIL J > I
                   ADD 1 TO N
               END-PERFORM
           END-PERFORM.
           DISPLAY "N " N " I " I " J " J.
      * a PIC-limited COMP counter running past its digits
           MOVE 9990 TO K.
           PERFORM UNTIL K < 5
               ADD 1 TO K
           END-PERFORM.
           DISPLAY "K " K.
      * more fields than registers: the rest in binary slots
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > LIM
               ADD F1 TO F2
               ADD 1 TO F3
               ADD 4.5 TO F4 ON SIZE ERROR MOVE I TO SE
               END-ADD
               SUBTRACT 1 FROM F5
               ADD F3 TO F6
               ADD 1 TO F7
               IF F7 > 40
                   MOVE 0 TO F7
               END-IF
           END-PERFORM.
           DISPLAY "F " F1 " " F2 " " F3 " " F4 " " F5 " " F6 " " F7.
           DISPLAY "SE " SE " I " I.
      * a slot field in the loop test
           MOVE 0 TO F5 F6.
           PERFORM UNTIL F5 > 50 OR F6 > 1000
               ADD 1 TO F5 F1 F4 F7
               ADD 7 TO F6 F2 F3
           END-PERFORM.
           DISPLAY "F " F1 " " F2 " " F3 " " F4 " " F5 " " F6 " " F7.
           STOP RUN.
