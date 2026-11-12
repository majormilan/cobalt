       IDENTIFICATION DIVISION.
       PROGRAM-ID. CONSTANT-LOOP.
      * A loop condition the PICTURE makes constant: the warning describes
      * the condition as written
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 I PIC 9.
       PROCEDURE DIVISION.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > 9
               CONTINUE
           END-PERFORM.
           PERFORM UNTIL NOT I <= 9
               CONTINUE
           END-PERFORM.
           STOP RUN.
