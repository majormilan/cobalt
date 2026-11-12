      * Extension sets: -fext=none under -X normal drops the default
      * set, so every extension is an error
       IDENTIFICATION DIVISION.
       PROGRAM-ID. EXT_SETS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  PACKED-AMOUNT PIC S9(5) COMP-3 VALUE 0.
       01  THIS-IS-A-VERY-LONG-DATA-NAME-OF-MANY-CHARACTERS PIC X.
       PROCEDURE DIVISION.
           MOVE 'x' TO THIS-IS-A-VERY-LONG-DATA-NAME-OF-MANY-CHARACTERS.
           GOBACK.
