       IDENTIFICATION DIVISION.
       PROGRAM-ID. INTRINSIC-FUNCTIONS.
      * Intrinsic functions: numeric (FPU and integer ones), ALL
      * subscripts, nested references, alphanumeric results with
      * reference modification, arguments with 17-digit scales
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  X PIC S9(5)V9(6).
       01  N PIC S9(9).
       01  S PIC X(10) VALUE "Hello Wor".
       01  T.
           05 TV PIC 9(3) OCCURS 5.
       01  D PIC 9(8).
       01  A PIC X(10).
       01  PI PIC S9V9(17) VALUE 3.14159265358979323.
       01  SQRT2 PIC S9V9(17) VALUE 1.41421356237309504.
       PROCEDURE DIVISION.
           COMPUTE X = FUNCTION SQRT(2).
           DISPLAY "SQRT 2 " X.
           COMPUTE X = FUNCTION SIN(1) + FUNCTION COS(0).
           DISPLAY "SIN 1 + COS 0 " X.
           COMPUTE X = FUNCTION LOG(10) + FUNCTION LOG10(1000).
           DISPLAY "LOG " X.
           COMPUTE X = FUNCTION ACOS(-1).
           DISPLAY "ACOS -1 " X.
           COMPUTE X = FUNCTION ATAN(1) * 4.
           DISPLAY "ATAN 1 * 4 " X.
           COMPUTE X = FUNCTION ACOS(1 / SQRT2).
           DISPLAY "ACOS 1/SQRT2 " X.
           COMPUTE X = FUNCTION COS((PI * 272) / 180).
           DISPLAY "COS 272 " X.
           COMPUTE X = FUNCTION TAN(0.5).
           DISPLAY "TAN 0.5 " X.
           MOVE 3 TO TV(1) MOVE 9 TO TV(2) MOVE 1 TO TV(3)
           MOVE 7 TO TV(4) MOVE 5 TO TV(5).
           COMPUTE N = FUNCTION MAX(TV(ALL)).
           DISPLAY "MAX " N.
           COMPUTE N = FUNCTION ORD-MIN(TV(ALL)).
           DISPLAY "ORD-MIN " N.
           COMPUTE X = FUNCTION MEAN(TV(ALL)).
           DISPLAY "MEAN " X.
           COMPUTE X = FUNCTION MEDIAN(TV(ALL)).
           DISPLAY "MEDIAN " X.
           COMPUTE X = FUNCTION VARIANCE(TV(ALL)).
           DISPLAY "VARIANCE " X.
           COMPUTE X = FUNCTION RANGE(TV(ALL))
               + FUNCTION SUM(TV(ALL)).
           DISPLAY "RANGE + SUM " X.
           COMPUTE N = FUNCTION MOD(-11, 5).
           DISPLAY "MOD " N.
           COMPUTE X = FUNCTION REM(-11, 5).
           DISPLAY "REM " X.
           COMPUTE N = FUNCTION INTEGER(-2.5)
               + FUNCTION INTEGER-PART(-2.5).
           DISPLAY "INTEGER " N.
           COMPUTE N = FUNCTION INTEGER-OF-DATE(20240229).
           DISPLAY "INTEGER-OF-DATE " N.
           COMPUTE D = FUNCTION DATE-OF-INTEGER(N + 1).
           DISPLAY "DATE-OF-INTEGER " D.
           COMPUTE D = FUNCTION DAY-OF-INTEGER(N).
           DISPLAY "DAY-OF-INTEGER " D.
           COMPUTE N = FUNCTION FACTORIAL(10).
           DISPLAY "FACTORIAL " N.
           MOVE FUNCTION UPPER-CASE(S) TO A.
           DISPLAY A.
           MOVE FUNCTION REVERSE(FUNCTION LOWER-CASE(S)) TO A.
           DISPLAY A.
           MOVE FUNCTION UPPER-CASE(S)(2:3) TO A.
           DISPLAY A.
           COMPUTE N = FUNCTION LENGTH(S) + FUNCTION ORD("A").
           DISPLAY "LENGTH + ORD " N.
           MOVE FUNCTION CHAR(66) TO A.
           DISPLAY A.
           MOVE FUNCTION MAX("PEAR", "APPLE", "PLUM") TO A.
           DISPLAY A.
           COMPUTE X = FUNCTION NUMVAL(" -12.5 ")
               + FUNCTION NUMVAL("3").
           DISPLAY "NUMVAL " X.
           COMPUTE X = FUNCTION NUMVAL-C("$1,234.56CR").
           DISPLAY "NUMVAL-C " X.
           COMPUTE X = FUNCTION ANNUITY(0.05, 10).
           DISPLAY "ANNUITY " X.
           COMPUTE X = FUNCTION PRESENT-VALUE(0.1, 100, 100).
           DISPLAY "PRESENT-VALUE " X.
           COMPUTE X = FUNCTION MAX(2 * 3, (4 + 1) / 2, 3 + 4).
           DISPLAY "MAX OF EXPRESSIONS " X.
           STOP RUN.
