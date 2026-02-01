       IDENTIFICATION DIVISION.
       PROGRAM-ID. ExtendedTest.
       AUTHOR. Your Name.
       DATE-WRITTEN. "2026-01-30".

       DATA DIVISION.
        WORKING-STORAGE SECTION.
        01  NAME        PIC X(20) VALUE "Milán".
        01  AGE         PIC 99    VALUE 30.
        01  PRICE       PIC 9(5)V99 VALUE 1234.56.
        01  TOTAL       PIC 9(6)V99 VALUE ZERO.
        01  FLAG        PIC X     VALUE SPACE.

       PROCEDURE DIVISION.
        MAIN-PARAGRAPH.
        DISPLAY "Name = " NAME.
        DISPLAY "Age = " AGE.
        DISPLAY "Price = " PRICE.
        ADD AGE TO PRICE GIVING TOTAL.
        DISPLAY "Total after add = " TOTAL.
        IF AGE > 18
        MOVE "Adult" TO FLAG
        DISPLAY "Status: " FLAG
        ELSE
        DISPLAY "Status: Minor"
        END-IF.
        STOP RUN.
