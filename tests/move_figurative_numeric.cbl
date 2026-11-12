       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-FIGURATIVE-NUMERIC.
      * Figurative constants other than SPACE move to numeric items as
      * alphanumeric data of the receiver's digits
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 N4 PIC 9(4).
       01 X4 REDEFINES N4 PIC X(4).
       01 C3 PIC 9(3) COMP.
       PROCEDURE DIVISION.
           MOVE HIGH-VALUES TO N4.
           IF X4 = HIGH-VALUES DISPLAY "HV" ELSE DISPLAY "NOT".
           MOVE ALL "5" TO N4. DISPLAY N4.
           MOVE ALL "12" TO N4. DISPLAY N4.
           MOVE QUOTE TO N4. DISPLAY X4.
           MOVE ALL "7" TO C3. DISPLAY C3.
           STOP RUN.
