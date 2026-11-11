       IDENTIFICATION DIVISION.
       PROGRAM-ID. VALUE-CHECKS.
      * Data description entries: PICTURE and VALUE
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  COUNTER        PIC 9(3)      VALUE 1000.
       01  RATE           PIC 9V99      VALUE 1.234.
       01  BALANCE        PIC 9(5)V99   VALUE -12.50.
       01  CODE-X         PIC X(3)      VALUE "ABCD".
       01  AMOUNT         PIC 9(5)      VALUE "12".
       01  NAME-X         PIC X(10)     VALUE 42.
       01  HUGE           PIC 9(20).
       01  BAD-PIC        PIC 9(3)Q.
       01  PACKED-NAME    PIC X(4)      USAGE PACKED-DECIMAL.
       01  NUM-SPACES     PIC 9(4)      VALUE SPACES.
       PROCEDURE DIVISION.
       MAIN-PARA.
           DISPLAY COUNTER RATE BALANCE CODE-X AMOUNT NAME-X HUGE
                   BAD-PIC PACKED-NAME NUM-SPACES.
           STOP RUN.
