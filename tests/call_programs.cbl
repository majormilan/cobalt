       IDENTIFICATION DIVISION.
       PROGRAM-ID. CALL-PROGRAMS.
      * CALL of a separately compiled program (tests/call_sub.cob) BY
      * REFERENCE and BY CONTENT, through a data item, ON EXCEPTION,
      * CANCEL, an INITIAL program, a nested program with GLOBAL data,
      * EXTERNAL data shared with the subprogram. (GnuCOBOL 3 also runs
      * the NOT ON EXCEPTION phrase of the CALL of a missing program.)
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A PIC 9(4) VALUE 12.
       01  B PIC X(5) VALUE "HELLO".
       01  PN PIC X(8) VALUE "CALLSUB".
       01  SHARED-G IS GLOBAL PIC X(6) VALUE "PARENT".
       01  EXT IS EXTERNAL.
           05 EXT-N PIC 9(3).
       PROCEDURE DIVISION.
           MOVE 7 TO EXT-N.
           CALL "CALLSUB" USING A B.
           DISPLAY "AFTER " A " " B " EXT " EXT-N.
           CALL PN USING BY CONTENT A B.
           DISPLAY "CONTENT " A " " B.
           CALL "NO-SUCH" ON EXCEPTION DISPLAY "NO SUCH PROGRAM"
               NOT ON EXCEPTION DISPLAY "UNEXPECTED" END-CALL.
           CANCEL "CALLSUB".
           CALL "CALLSUB" USING A B.
           CALL "INIT-SUB".
           CALL "INIT-SUB".
           CALL "NESTED".
           DISPLAY "GLOBAL NOW " SHARED-G.
           STOP RUN.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. NESTED.
       PROCEDURE DIVISION.
           DISPLAY "NESTED SEES " SHARED-G.
           MOVE "CHILD" TO SHARED-G.
           EXIT PROGRAM.
       END PROGRAM NESTED.
       END PROGRAM CALL-PROGRAMS.
