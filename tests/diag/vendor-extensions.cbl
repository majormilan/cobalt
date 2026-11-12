       IDENTIFICATION DIVISION.
       PROGRAM-ID. VENDOR-EXTENSIONS.
      * Extensions Cobalt does not implement are named, without cascades
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT F ASSIGN TO DYNAMIC F-NAME.
       DATA DIVISION.
       FILE SECTION.
       FD  F.
       01  F-REC PIC X(10).
       WORKING-STORAGE SECTION.
       01  F-NAME PIC X(20).
       01  PATH   PIC X(20).
       01  N      PIC 9(4) COMP-5.
       01  R      PIC 9(4) COMP-5.
       01  S      PIC X(10).
       PROCEDURE DIVISION.
           ACCEPT PATH FROM ENVIRONMENT "HOME"
               ON EXCEPTION
                   MOVE SPACES TO PATH
           END-ACCEPT
           CALL "socket" USING BY VALUE N
               BY VALUE 1
               RETURNING R
           END-CALL
           INSPECT S REPLACING TRAILING SPACES BY "*"
           MOVE X"410042" TO S
           STOP RUN.
