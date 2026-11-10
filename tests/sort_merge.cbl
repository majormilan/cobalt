       IDENTIFICATION DIVISION.
       PROGRAM-ID. SORT-MERGE.
      * SORT with INPUT/OUTPUT PROCEDURE, USING and GIVING, mixed key
      * kinds and directions, equal keys in input order, MERGE (equal
      * keys in the order of the USING files)
       ENVIRONMENT DIVISION.
       CONFIGURATION SECTION.
       SPECIAL-NAMES.
           ALPHABET REVERSED-ABC IS "C" "B" "A".
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT SF ASSIGN TO "sm-work".
           SELECT IN1 ASSIGN TO "sm-in1.dat".
           SELECT IN2 ASSIGN TO "sm-in2.dat".
           SELECT OUTF ASSIGN TO "sm-out.dat".
       DATA DIVISION.
       FILE SECTION.
       SD  SF.
       01  SR.
           05 K1 PIC X(3).
           05 K2 PIC S9(3).
           05 K3 PIC S9(5) PACKED-DECIMAL.
           05 DD PIC X(4).
       FD  IN1.
       01  R1 PIC X(13).
       FD  IN2.
       01  R2 PIC X(13).
       FD  OUTF.
       01  RO.
           05 FILLER PIC X(6).
           05 OK3 PIC S9(5) PACKED-DECIMAL.
           05 OD PIC X(4).
       WORKING-STORAGE SECTION.
       77  I PIC 99.
       77  EOF PIC 9 VALUE 0.
       77  SHOWN-K3 PIC -9(5).
       PROCEDURE DIVISION.
       MAIN SECTION.
       M1.
           SORT SF ON ASCENDING KEY K1 DESCENDING KEY K2
               INPUT PROCEDURE IS GEN
               OUTPUT PROCEDURE IS SHOW.
           DISPLAY "--".
           SORT SF ON ASCENDING KEY K3
               INPUT PROCEDURE IS GEN
               GIVING IN1.
           PERFORM SHOW-FILE.
           DISPLAY "--".
           SORT SF DESCENDING KEY K1 COLLATING SEQUENCE IS REVERSED-ABC
               USING IN1 GIVING IN2.
           MOVE 0 TO EOF.
           OPEN INPUT IN2.
           PERFORM UNTIL EOF = 1
               READ IN2 AT END MOVE 1 TO EOF
               NOT AT END DISPLAY R2(1:3) " " R2(10:4)
               END-READ
           END-PERFORM.
           CLOSE IN2.
           DISPLAY "--".
           SORT SF ON ASCENDING KEY K3 INPUT PROCEDURE IS GEN GIVING IN2.
           MERGE SF ON ASCENDING KEY K3 USING IN2 IN1 GIVING OUTF.
           MOVE 0 TO EOF.
           OPEN INPUT OUTF.
           PERFORM UNTIL EOF = 1
               READ OUTF AT END MOVE 1 TO EOF
               NOT AT END MOVE OK3 TO SHOWN-K3 DISPLAY SHOWN-K3 " " OD
               END-READ
           END-PERFORM.
           CLOSE OUTF.
           STOP RUN.
       SHOW-FILE.
           MOVE 0 TO EOF.
           OPEN INPUT IN1.
           PERFORM UNTIL EOF = 1
               READ IN1 AT END MOVE 1 TO EOF
               NOT AT END DISPLAY R1(1:6) " " R1(10:4)
               END-READ
           END-PERFORM.
           CLOSE IN1.
       GEN SECTION.
       G1.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 9
               EVALUATE TRUE
                   WHEN I > 6 MOVE "CCC" TO K1
                   WHEN I > 3 MOVE "BBB" TO K1
                   WHEN OTHER MOVE "AAA" TO K1
               END-EVALUATE
               COMPUTE K2 = I * 7 - 30
               COMPUTE K3 = 50 - I * I
               IF I = 4 OR I = 8 MOVE 0 TO K3 END-IF
               MOVE I TO DD
               RELEASE SR
           END-PERFORM.
       SHOW SECTION.
       S1.
           PERFORM UNTIL EOF = 1
               RETURN SF AT END MOVE 1 TO EOF
               NOT AT END
                   MOVE K3 TO SHOWN-K3
                   DISPLAY K1 " " K2 " " SHOWN-K3 " " DD
               END-RETURN
           END-PERFORM.
