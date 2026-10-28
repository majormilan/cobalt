       IDENTIFICATION DIVISION.
       PROGRAM-ID. RENAMES-ODO.
      * Level-66 RENAMES and OCCURS DEPENDING ON.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  REC.
           03  A PIC XX VALUE "AB".
           03  B.
               05  B1 PIC 99 VALUE 12.
               05  B2 PIC X VALUE "C".
           03  C PIC S9(3) VALUE -45.
       66  AB RENAMES A THRU B.
       66  B1-ALIAS RENAMES B1 OF B.
       66  C-NUM RENAMES C.
       77  N PIC 99 VALUE 3.
       01  OREC.
           03  HDR PIC XX VALUE "H:".
           03  TAB.
               05  E PIC X OCCURS 1 TO 9 DEPENDING ON N
                   INDEXED BY IX IY.
       01  SELF-REC.
           03  SN PIC 9 VALUE 3.
           03  SE PIC X OCCURS 0 TO 9 DEPENDING ON SN.
       01  SELF-REC2.
           03  SN2 PIC 9 VALUE 1.
           03  SE2 PIC X OCCURS 0 TO 9 DEPENDING ON SN2.
       77  W PIC X(12).
       PROCEDURE DIVISION.
           DISPLAY "[" AB "]".
           ADD 1 TO B1-ALIAS. DISPLAY B1.
           MOVE "XYZ" TO AB. DISPLAY "[" REC "]".
           ADD 5 TO C-NUM. DISPLAY C-NUM.
           MOVE 9 TO N. MOVE "ABCDEFGHI" TO TAB. MOVE 3 TO N.
           DISPLAY "[" OREC "]".
           MOVE 5 TO N.
           DISPLAY "[" OREC "] [" TAB "]".
           MOVE OREC TO W. DISPLAY "[" W "]".
           MOVE 2 TO N.
           MOVE "XYZ" TO TAB.
           MOVE 9 TO N.
           DISPLAY "[" OREC "]".
           MOVE 4 TO N.
           SET IX TO 1.
           SEARCH E AT END DISPLAY "NOT FOUND"
               WHEN E (IX) = "G" DISPLAY "FOUND G".
           MOVE 9 TO N.
           SET IY TO 1. SET IX TO 9.
           SEARCH E VARYING IY AT END DISPLAY "NOT FOUND"
               WHEN E (IY) = "G" DISPLAY "FOUND G AT IY".
      * A receiving group holding its own DEPENDING ON item takes its
      * maximum length; a sending group its current length
           MOVE "3PQRSTUVWX" TO SELF-REC.
           DISPLAY "[" SELF-REC "]".
           MOVE ALL "*" TO SE2 (1) SE2 (2) SE2 (3) SE2 (4).
           MOVE SELF-REC TO SELF-REC2.
           MOVE 9 TO SN2.
           DISPLAY "[" SELF-REC2 "]".
           STOP RUN.
