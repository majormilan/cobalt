       IDENTIFICATION DIVISION.
       PROGRAM-ID. PEDANTIC-MOVES.
      * -X pedantic: SPACE to a numeric edited item; a REDEFINES that does
      * not follow the item it redefines
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  G.
           05  GN PIC 99.
           05  GX PIC XX.
           05  GR REDEFINES GN PIC XX.
       01  E  PIC ZZ9.99.
       PROCEDURE DIVISION.
           MOVE SPACES TO E.
           STOP RUN.
