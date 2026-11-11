      * Inventory: an indexed file of parts. Loads it from a text file,
      * applies a day's movements (random READ and REWRITE by part
      * number), then lists the parts that need ordering in part order
      * (START and READ NEXT).
      *
      *   cobalt inventory.cbl
      *   ./inventory                  (reads parts.txt and moves.txt,
      *                                 makes parts.idx)
       IDENTIFICATION DIVISION.
       PROGRAM-ID. INVENTORY.
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT PARTS-IN ASSIGN TO "parts.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
           SELECT MOVES ASSIGN TO "moves.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
           SELECT PARTS ASSIGN TO "parts.idx"
               ORGANIZATION IS INDEXED
               ACCESS MODE IS DYNAMIC
               RECORD KEY IS PART-NO
               FILE STATUS IS PARTS-STATUS.
       DATA DIVISION.
       FILE SECTION.
       FD  PARTS-IN.
       01  PART-IN.
           05  IN-NO           PIC X(6).
           05  IN-NAME         PIC X(20).
           05  IN-STOCK        PIC 9(5).
           05  IN-REORDER      PIC 9(5).
       FD  MOVES.
       01  MOVE-RECORD.
           05  MV-NO           PIC X(6).
           05  MV-KIND         PIC X.
               88  RECEIVED          VALUE "R".
               88  ISSUED            VALUE "I".
           05  MV-QUANTITY     PIC 9(5).
       FD  PARTS.
       01  PART.
           05  PART-NO         PIC X(6).
           05  PART-NAME       PIC X(20).
           05  PART-STOCK      PIC S9(5) COMP-3.
           05  PART-REORDER    PIC 9(5) COMP-3.
       WORKING-STORAGE SECTION.
       01  PARTS-STATUS        PIC XX.
       01  AT-END              PIC X.
           88  NO-MORE               VALUE "Y".
       01  REPORT-LINE.
           05  R-NO            PIC X(6).
           05  FILLER          PIC X(2) VALUE SPACES.
           05  R-NAME          PIC X(20).
           05  R-STOCK         PIC -ZZ,ZZ9.
           05  FILLER          PIC X(10) VALUE "  reorder ".
           05  R-REORDER       PIC ZZ,ZZ9.
       PROCEDURE DIVISION.
       MAIN-PROGRAM.
           PERFORM LOAD-PARTS.
           PERFORM APPLY-MOVES.
           PERFORM LIST-SHORTAGES.
           STOP RUN.

       LOAD-PARTS.
           OPEN INPUT PARTS-IN OUTPUT PARTS.
           MOVE "N" TO AT-END.
           PERFORM UNTIL NO-MORE
               READ PARTS-IN
                   AT END SET NO-MORE TO TRUE
                   NOT AT END
                       MOVE IN-NO TO PART-NO
                       MOVE IN-NAME TO PART-NAME
                       MOVE IN-STOCK TO PART-STOCK
                       MOVE IN-REORDER TO PART-REORDER
                       WRITE PART
                           INVALID KEY
                               DISPLAY "Duplicate part " IN-NO
                       END-WRITE
               END-READ
           END-PERFORM.
           CLOSE PARTS-IN PARTS.

       APPLY-MOVES.
           OPEN INPUT MOVES I-O PARTS.
           MOVE "N" TO AT-END.
           PERFORM UNTIL NO-MORE
               READ MOVES
                   AT END SET NO-MORE TO TRUE
                   NOT AT END PERFORM APPLY-ONE-MOVE
               END-READ
           END-PERFORM.
           CLOSE MOVES PARTS.

       APPLY-ONE-MOVE.
           MOVE MV-NO TO PART-NO.
           READ PARTS
               INVALID KEY
                   DISPLAY "Unknown part " MV-NO
               NOT INVALID KEY
                   IF RECEIVED
                       ADD MV-QUANTITY TO PART-STOCK
                   ELSE
                       SUBTRACT MV-QUANTITY FROM PART-STOCK
                   END-IF
                   REWRITE PART
           END-READ.

       LIST-SHORTAGES.
           DISPLAY "Parts to order:".
           OPEN INPUT PARTS.
           MOVE LOW-VALUES TO PART-NO.
           START PARTS KEY IS NOT LESS THAN PART-NO
               INVALID KEY SET NO-MORE TO TRUE
               NOT INVALID KEY MOVE "N" TO AT-END
           END-START.
           PERFORM UNTIL NO-MORE
               READ PARTS NEXT RECORD
                   AT END SET NO-MORE TO TRUE
                   NOT AT END
                       IF PART-STOCK < PART-REORDER
                           MOVE PART-NO TO R-NO
                           MOVE PART-NAME TO R-NAME
                           MOVE PART-STOCK TO R-STOCK
                           MOVE PART-REORDER TO R-REORDER
                           DISPLAY REPORT-LINE
                       END-IF
               END-READ
           END-PERFORM.
           CLOSE PARTS.
