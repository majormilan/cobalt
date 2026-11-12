       IDENTIFICATION DIVISION.
       PROGRAM-ID. UNSTRING-NUMERIC-LONG.
      * A delimited field of more than 18 characters into a numeric
      * receiver: its low-order digits
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 SRC PIC X(44) VALUE
           "1234567890123456789012,98765432109876543210,".
       01 N5  PIC 9(5).
       01 N18 PIC 9(18).
       PROCEDURE DIVISION.
           UNSTRING SRC DELIMITED BY "," INTO N5 N18.
           DISPLAY N5 " " N18.
           STOP RUN.
