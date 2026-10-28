       IDENTIFICATION DIVISION.
       PROGRAM-ID. VarTest.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  NAME        PIC X(20) VALUE "Milán".
       01  AGE         PIC 99    VALUE 30.
       01  PRICE       PIC 9(5)V99.

       PROCEDURE DIVISION.
           DISPLAY "Name = " NAME.
           DISPLAY "Age is " AGE " years old".
           STOP RUN.
