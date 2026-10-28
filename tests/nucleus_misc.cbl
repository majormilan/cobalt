       identification division.
       program-id. nucleus-misc.
      * Lower-case words, word continuation, ALTER, SYMBOLIC
      * CHARACTERS, SET switch, INITIALIZE REPLACING, VALUE in
      * tables, run-time exponents, group and literal moves.
       ENVIRONMENT DIVISION.
       CONFIGURATION SECTION.
       SPECIAL-NAMES.
           SWITCH-2 IS SW-2 ON STATUS IS S2-ON OFF STATUS IS S2-OFF
           ALPHABET REV IS "ZYX"
           SYMBOLIC CHARACTERS STAR IS 43 PLUS-SIGN DOLLAR ARE 44 37
           SYMBOLIC CHARACTERS LETTER-Y IS 2 IN REV.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  value-table.
           03  VT-1 OCCURS 3.
               05  VT-2 OCCURS 2 PIC XX VALUE "AZ".
               05  VT-N PIC 99 VALUE 7.
       01  move1 PIC 9(5).
       01  G.
           03  GA PIC AAA VALUE "XYZ".
           03  GX PIC XXX VALUE "XYZ".
           03  GN PIC 999 VALUE 999.
           03  GE PIC ZZ9 VALUE "999".
           03  GD PIC X/X VALUE "Q/Q".
           03  GT PIC 99 OCCURS 2 VALUE 11.
       77  NUM PIC 9(4) VALUE 4321.
       77  THREE PIC 9 VALUE 3.
       77  MTWO PIC S9 VALUE -2.
       77  HALF PIC 9V9 VALUE 0.5.
       77  R PIC S9(6)V9(4).
       77  F PIC X(3).
       77  J PIC X(4) JUSTIFIED RIGHT VALUE "XY".
       77  ED PIC 9B(3)99.
       77  AE PIC XX0XX.
       01  GRP.
           03  FILLER PIC X(4) VALUE "12AB".
       77  N2 PIC 99.
       PROCEDURE DIVISION.
       START-UP.
           display value-table.
           MOVE "12345" TO MOVE1. DISPLAY MOVE1.
           MOVE "1234567" TO MOVE1. DISPLAY MOVE1.
           IF ((((MOVE1 - 1 = 34566 O
      -    R 3)))) DISPLAY "PAREN OK".
           INITIALIZE G REPLACING ALPHABETIC DATA BY "AA"
                                  NUMERIC DATA BY 7.
           DISPLAY G.
           INITIALIZE G REPLACING ALPHANUMERIC BY "**"
               ALPHANUMERIC-EDITED BY "DD" NUMERIC-EDITED BY NUM.
           DISPLAY G.
           INITIALIZE G.
           DISPLAY "[" G "]".
           IF 1000 LESS THAN 10 ** THREE + 1 DISPLAY "LT OK".
           COMPUTE R = 2 ** MTWO. DISPLAY R.
           COMPUTE R = 1.5 ** THREE. DISPLAY R.
           COMPUTE R = 4 ** HALF ON SIZE ERROR DISPLAY "FRAC ERR".
           COMPUTE R = 10 ** (THREE * 3) ON SIZE ERROR DISPLAY "BIG".
           MOVE STAR TO F. DISPLAY F.
           DISPLAY PLUS-SIGN DOLLAR LETTER-Y.
           IF F = STAR DISPLAY "EQUAL".
           IF S2-OFF DISPLAY "2 OFF".
           SET SW-2 TO ON.
           IF S2-ON DISPLAY "2 ON".
           DISPLAY "[" J "]".
           MOVE "926" TO ED. DISPLAY "[" ED "]".
           MOVE LOW-VALUE TO AE.
           IF AE = LOW-VALUE DISPLAY "AE LOW" ELSE DISPLAY "AE EDITED".
           MOVE GRP TO N2. DISPLAY N2.
           PERFORM SWITCHER THRU P-END 2 TIMES.
           ALTER SWITCHER TO PROCEED TO P-B.
           PERFORM SWITCHER THRU P-END.
           ALTER LATER TO DONE.
           GO TO LATER.
       SWITCHER.
           GO TO P-A.
       P-A.
           DISPLAY "A".
           GO TO P-END.
       P-B.
           DISPLAY "B".
       P-END.
           EXIT.
       LATER.
           GO TO.
       DONE.
           STOP "STOPPED".
           DISPLAY "DONE".
           STOP RUN.
