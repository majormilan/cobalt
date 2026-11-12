       IDENTIFICATION DIVISION.
       PROGRAM-ID. SIZE-ERROR-18-DIGITS.
      * Results that fit the receiver raise no size error, also when an
      * 18-digit quotient or product leaves no room for extra decimals
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 A   PIC S9(18) VALUE 999999999999999999.
       01 A2  PIC S9(18) VALUE 300000000000000000.
       01 D   PIC S9 VALUE 7.
       01 Q   PIC S9(18).
       01 A17 PIC S9(17) VALUE 99999999999999998.
       01 Q17 PIC S9(17).
       01 RV  PIC S9V9.
       01 F   PIC S9(15)V99 VALUE 999999999999999.99.
       01 X   PIC S9(15)V99.
       01 H   PIC S9V9 VALUE 0.5.
       01 B2  PIC S9(18).
       01 E   PIC 99 VALUE 37.
       01 F18 PIC SV9(18).
       01 N   PIC S9 VALUE 2.
       01 D3  PIC S9 VALUE 3.
       PROCEDURE DIVISION.
           DIVIDE A BY 7 GIVING Q ON SIZE ERROR DISPLAY "SE1".
           DISPLAY Q.
           DIVIDE A BY D GIVING Q ON SIZE ERROR DISPLAY "SE2".
           DISPLAY Q.
           DIVIDE A2 BY 1 GIVING Q ON SIZE ERROR DISPLAY "SE3".
           DISPLAY Q.
           DIVIDE A17 BY 3 GIVING Q17 REMAINDER RV
               ON SIZE ERROR DISPLAY "SE4".
           DISPLAY Q17 " " RV.
           COMPUTE X = F / 3 * 3.
           DISPLAY X.
           COMPUTE X = F / 2 * 2 ON SIZE ERROR DISPLAY "SE5".
           DISPLAY X.
           COMPUTE B2 = A - H ON SIZE ERROR DISPLAY "SE6".
           DISPLAY B2.
           ADD H TO A GIVING B2 ON SIZE ERROR DISPLAY "SE7".
           DISPLAY B2.
           COMPUTE B2 = 3 ** E ON SIZE ERROR DISPLAY "SE8".
           DISPLAY B2.
           DIVIDE N BY D3 GIVING F18 ROUNDED.
           DISPLAY F18.
           STOP RUN.
