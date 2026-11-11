      * Initial values of the main program, written into its storage when
      * compiling: the bytes of every usage and sign form, as the runtime
      * stores them (the expected output comes from a called program,
      * which sets them with code)
       IDENTIFICATION DIVISION.
       PROGRAM-ID. INITIAL-VALUES.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  R.
           05  D1  PIC 9(5) VALUE 123.
           05  D2  PIC S9(5) VALUE -123.
           05  D3  PIC S9(5) VALUE 120.
           05  D4  PIC S9(3)V99 VALUE -1.5.
           05  D5  PIC S9(4) SIGN LEADING VALUE -42.
           05  D6  PIC S9(4) SIGN TRAILING SEPARATE VALUE -42.
           05  D7  PIC S9(4) SIGN LEADING SEPARATE VALUE 42.
           05  D8  PIC S9(3) VALUE -0.
           05  D9  PIC S99 VALUE ZERO.
           05  B1  PIC S9(4) COMP VALUE -2.
           05  B2  PIC 9(9) BINARY VALUE 305419896.
           05  B3  PIC S9(18) COMP VALUE -123456789012345678.
           05  F1  PIC S9(4) COMP-5 VALUE -2.
           05  F2  PIC 9(9) COMP-5 VALUE 305419896.
           05  P1  PIC S9(5) COMP-3 VALUE -12345.
           05  P2  PIC 9(4) COMP-3 VALUE 12.
           05  P3  PIC S9(3)V9 COMP-3 VALUE 5.5.
           05  A1  PIC X(6) VALUE "AB".
           05  A2  PIC X(5) VALUE ALL "XY".
           05  A3  PIC X(3) VALUE HIGH-VALUE.
           05  A4  PIC X(3) VALUE QUOTE.
           05  A5  PIC X(4) VALUE ZEROS.
           05  T1  PIC 9(2) OCCURS 3 VALUE 7.
           05  E1  PIC ZZ9.99 VALUE "  1.50".
       01  RX REDEFINES R PIC X(101).
       01  G   VALUE "GROUPVAL".
           05  G1 PIC X(4).
           05  G2 PIC X(4).
       01  K   PIC X(4) VALUE "ABC" JUSTIFIED RIGHT.
       PROCEDURE DIVISION.
           INSPECT RX REPLACING ALL LOW-VALUE BY "~".
           DISPLAY "[" RX "]".
           DISPLAY "[" G "][" K "]".
           STOP RUN.
