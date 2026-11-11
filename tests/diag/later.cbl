      * Elements of later standards and vendors: named, not syntax errors
       IDENTIFICATION DIVISION.
       PROGRAM-ID. LATER.
      >>SOURCE FORMAT FIXED
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  A PIC X(10).
       01  F COMP-1.
       01  P USAGE POINTER.
       78  K VALUE 5.
       LOCAL-STORAGE SECTION.
       01  L PIC X.
       PROCEDURE DIVISION.
           DISPLAY FUNCTION TRIM(A).
           CALL "X" RETURNING A.
           DISPLAY A AT 0101.
           ACCEPT A FROM COMMAND-LINE.
           EXEC SQL
               DELETE FROM T
           END-EXEC.
           MOVE 1 TO A
           EXEC SQL COMMIT END-EXEC
           EXIT PERFORM.
           GOBACK.
