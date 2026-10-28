       IDENTIFICATION DIVISION.
       PROGRAM-ID. RESERVEDNAMES.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 DS  PIC 99 VALUE 1.
       01 AX  PIC 99 VALUE 2.
       01 RAX PIC 99 VALUE 3.
       01 P   PIC X(3) VALUE "ABC".
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM add-them.
           DISPLAY "DS=" DS " P=" P.
           STOP RUN.
       ADD-THEM.
           ADD AX RAX TO DS.
