       IDENTIFICATION DIVISION.
       PROGRAM-ID. EXTENSIONS.
      * Elements outside COBOL-85: -X relaxed accepts them, normal warns,
      * pedantic rejects them, ibm-strict rejects those IBM does not have
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  PACKED-AMOUNT    PIC S9(7)V99 COMP-3.
       01  NATIVE-COUNT     PIC 9(9)     COMP-5.
       01  STANDARD-AMOUNT  PIC S9(7)V99 PACKED-DECIMAL.
       01  TODAY            PIC 9(8).
       01  CUSTOMER_NAME    PIC X(10)    VALUE 'SMITH'.
       01  A-VERY-LONG-DATA-NAME-OF-THIRTY-FIVE PIC X.
       PROCEDURE DIVISION.
           MOVE 1 TO PACKED-AMOUNT NATIVE-COUNT STANDARD-AMOUNT.
           ACCEPT TODAY FROM DATE YYYYMMDD.
           DISPLAY CUSTOMER_NAME A-VERY-LONG-DATA-NAME-OF-THIRTY-FIVE.
           STOP RUN.
