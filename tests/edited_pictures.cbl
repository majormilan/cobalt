       IDENTIFICATION DIVISION.
       PROGRAM-ID. EDITEDPICS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 E1  PIC ZZZ9.
       01 E2  PIC ZZZZ.
       01 E3  PIC ZZ,ZZ9.99.
       01 E4  PIC ***,**9.99.
       01 E5  PIC $$$,$$9.99.
       01 E6  PIC -ZZZ9.
       01 E7  PIC ++++9.
       01 E8  PIC 9999CR.
       01 E9  PIC 999.99DB.
       01 E10 PIC 99/99/99.
       01 E11 PIC 999B999.
       01 E12 PIC 99900.
       01 E13 PIC XXBXX.
       01 E14 PIC ZZ9 BLANK WHEN ZERO.
       01 E15 PIC $$$9.99.
       01 P1  PIC 99PPP VALUE 12000.
       01 P2  PIC PP99 VALUE .0012.
       01 N1  PIC 9(6).
       01 N2  PIC 9(5)V99.
       01 A   PIC 99V9 VALUE 10.5.
       01 B   PIC 9V99 VALUE 2.25.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 5 TO E1.        DISPLAY "[" E1 "]".
           MOVE 0 TO E1.        DISPLAY "[" E1 "]".
           MOVE 0 TO E2.        DISPLAY "[" E2 "]".
           MOVE 1234.5 TO E3.   DISPLAY "[" E3 "]".
           MOVE 12.3 TO E3.     DISPLAY "[" E3 "]".
           MOVE 1234.5 TO E4.   DISPLAY "[" E4 "]".
           MOVE 1234.5 TO E5.   DISPLAY "[" E5 "]".
           MOVE 5 TO E5.        DISPLAY "[" E5 "]".
           MOVE -42 TO E6.      DISPLAY "[" E6 "]".
           MOVE 42 TO E6.       DISPLAY "[" E6 "]".
           MOVE -42 TO E7.      DISPLAY "[" E7 "]".
           MOVE 42 TO E7.       DISPLAY "[" E7 "]".
           MOVE -12 TO E8.      DISPLAY "[" E8 "]".
           MOVE 12 TO E8.       DISPLAY "[" E8 "]".
           MOVE -1.5 TO E9.     DISPLAY "[" E9 "]".
           MOVE 102826 TO E10.  DISPLAY "[" E10 "]".
           MOVE 123456 TO E11.  DISPLAY "[" E11 "]".
           MOVE 123 TO E12.     DISPLAY "[" E12 "]".
           MOVE "ABCD" TO E13.  DISPLAY "[" E13 "]".
           MOVE 0 TO E14.       DISPLAY "[" E14 "]".
           MOVE 5 TO E14.       DISPLAY "[" E14 "]".
           DISPLAY "P1=" P1 " P2=" P2.
           COMPUTE N1 = P1 * 2.
           DISPLAY "N1=" N1.
           MOVE 1234.5 TO E3.
           MOVE E3 TO N2.
           DISPLAY "N2=" N2.
           ADD A B GIVING E15.
           DISPLAY "[" E15 "]".
           COMPUTE E1 = 123456 ON SIZE ERROR DISPLAY "EDIT SIZE ERROR".
           DISPLAY "[" E1 "]".
           STOP RUN.
