       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRING-UNSTRING.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  T PIC X(20).
       77  P PIC 99.
       77  SRC PIC X(30) VALUE "ALPHA, BETA,,GAMMA   DELTA".
       77  A1 PIC X(6).
       77  A2 PIC X(6).
       77  A3 PIC X(6).
       77  A4 PIC X(6) JUSTIFIED RIGHT.
       77  N1 PIC 999.
       77  D1 PIC XX.
       77  C1 PIC 99.
       77  TL PIC 99 VALUE 0.
       77  NUMS PIC X(12) VALUE "12/345/6".
       PROCEDURE DIVISION.
           MOVE SPACES TO T.
           STRING "AB" "CDEF" DELIMITED BY SIZE
                  "XY*Z" DELIMITED BY "*"
                  SPACE DELIMITED BY SIZE
                  SRC DELIMITED BY ","
                  INTO T.
           DISPLAY "[" T "]".
           MOVE ALL "-" TO T. MOVE 3 TO P.
           STRING "123456" DELIMITED BY SIZE INTO T WITH POINTER P
               ON OVERFLOW DISPLAY "OVF1"
               NOT ON OVERFLOW DISPLAY "NO OVF1".
           DISPLAY "[" T "] " P.
           MOVE 18 TO P.
           STRING "123456" DELIMITED BY SIZE INTO T WITH POINTER P
               ON OVERFLOW DISPLAY "OVF2".
           DISPLAY "[" T "] " P.
           MOVE 1 TO P.
           UNSTRING SRC DELIMITED BY "," OR ALL SPACE
               INTO A1 DELIMITER IN D1 COUNT IN C1
                    A2 A3 A4
               WITH POINTER P TALLYING IN TL
               ON OVERFLOW DISPLAY "UOVF".
           DISPLAY "[" A1 "][" D1 "]" C1 "[" A2 "][" A3 "][" A4 "]".
           DISPLAY P " " TL.
           UNSTRING NUMS DELIMITED BY "/" INTO N1 C1.
           MOVE LOW-VALUE TO T.
           STRING "A" LOW-VALUE "B" DELIMITED BY SIZE INTO T.
           INSPECT T REPLACING ALL LOW-VALUE BY "-".
           DISPLAY T(1:4).
           DISPLAY N1 " " C1.
           MOVE "ABCDEFGHIJ" TO T.
           UNSTRING T INTO A1 D1
               ON OVERFLOW DISPLAY "UOVF2" END-UNSTRING.
           DISPLAY A1 " " D1.
           STOP RUN.
