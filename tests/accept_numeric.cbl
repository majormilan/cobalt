       IDENTIFICATION DIVISION.
       PROGRAM-ID. ACCEPTNUM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 N1 PIC S9(3)V99.
       01 N2 PIC 9(4).
       01 N3 PIC S9(5) COMP-3.
       PROCEDURE DIVISION.
       MAIN-PARA.
           ACCEPT N1.
           DISPLAY "N1=" N1.
           ACCEPT N2.
           DISPLAY "N2=" N2.
           ACCEPT N3.
           DISPLAY "N3=" N3.
           ACCEPT N1.
           DISPLAY "N1=" N1.
           STOP RUN.
