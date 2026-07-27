       IDENTIFICATION DIVISION.
       PROGRAM-ID. IF-TEST.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-AGE    PIC 99  VALUE 25.
       01  WS-SCORE  PIC 999 VALUE 85.
       01  WS-MIN    PIC 99  VALUE 18.
       01  WS-ZERO   PIC 99  VALUE 0.

       PROCEDURE DIVISION.
       MAIN.
           IF WS-AGE > 18
               DISPLAY "Test 1 PASS: Adult"
           END-IF.
           IF WS-SCORE >= 80
               DISPLAY "Test 2 PASS: High Score"
           ELSE
               DISPLAY "Test 2 FAIL: Low Score"
           END-IF.
           IF WS-AGE = 0
               DISPLAY "Test 3 FAIL: Is Zero"
           ELSE
               DISPLAY "Test 3 PASS: Not Zero"
           END-IF.
           IF WS-AGE > WS-MIN
               DISPLAY "Test 4 PASS: Age > Min"
           ELSE
               DISPLAY "Test 4 FAIL: Age <= Min"
           END-IF.
           IF WS-MIN < WS-AGE
               DISPLAY "Test 5 PASS: Min < Age"
           ELSE
               DISPLAY "Test 5 FAIL: Min >= Age"
           END-IF.
           IF WS-AGE <> WS-SCORE
               DISPLAY "Test 6 PASS: Age <> Score"
           ELSE
               DISPLAY "Test 6 FAIL: Age = Score"
           END-IF.
           IF WS-ZERO > 99
               DISPLAY "Test 7 FAIL: Should be skipped"
           END-IF.
           DISPLAY "Test 7 PASS: Continued after false IF".
           IF WS-MIN <= 18
               DISPLAY "Test 8 PASS: Min <= 18"
           ELSE
               DISPLAY "Test 8 FAIL: Min > 18"
           END-IF.
           STOP RUN.
