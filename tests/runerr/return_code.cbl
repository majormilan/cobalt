       IDENTIFICATION DIVISION.
       PROGRAM-ID. RC-MAIN.
      * RETURN-CODE is shared by the programs of the run unit; STOP RUN
      * ends the run with its value
       PROCEDURE DIVISION.
           CALL "RC-SUB".
           DISPLAY "SUB SET " RETURN-CODE.
           ADD 1 TO RETURN-CODE.
           STOP RUN.
       END PROGRAM RC-MAIN.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. RC-SUB.
       PROCEDURE DIVISION.
           MOVE 41 TO RETURN-CODE.
           EXIT PROGRAM.
       END PROGRAM RC-SUB.
