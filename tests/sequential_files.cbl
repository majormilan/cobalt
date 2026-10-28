       IDENTIFICATION DIVISION.
       PROGRAM-ID. SEQFILES.
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT OUT-FILE ASSIGN TO "seq_test.dat"
               ORGANIZATION IS SEQUENTIAL
               FILE STATUS IS FS.
           SELECT TXT-FILE ASSIGN TO "seq_test.txt"
               ORGANIZATION IS LINE SEQUENTIAL.
           SELECT OPTIONAL MISSING-FILE ASSIGN TO "no_such_file.dat"
               FILE STATUS IS FS2.
           SELECT REQUIRED-FILE ASSIGN TO "no_such_file2.dat"
               FILE STATUS IS FS3.
       DATA DIVISION.
       FILE SECTION.
       FD  OUT-FILE
           LABEL RECORDS ARE STANDARD
           RECORD CONTAINS 10 CHARACTERS.
       01  OUT-REC.
           05 OUT-ID   PIC 9(3).
           05 OUT-NAME PIC X(7).
       01  OUT-REC-ALT PIC X(10).
       FD  TXT-FILE.
       01  TXT-REC PIC X(20).
       FD  MISSING-FILE.
       01  MISSING-REC PIC X(5).
       FD  REQUIRED-FILE.
       01  REQUIRED-REC PIC X(5).
       WORKING-STORAGE SECTION.
       01 FS  PIC XX.
       01 FS2 PIC XX.
       01 FS3 PIC XX.
       01 EOF-FLAG PIC X VALUE "N".
          88 AT-EOF VALUE "Y".
       01 COUNTER PIC 99 VALUE 0.
       01 HOLD    PIC X(10).
       PROCEDURE DIVISION.
       MAIN-PARA.
           OPEN OUTPUT OUT-FILE.
           DISPLAY "OPEN OUTPUT " FS.
           MOVE 1 TO OUT-ID. MOVE "ALPHA" TO OUT-NAME. WRITE OUT-REC.
           MOVE 2 TO OUT-ID. MOVE "BETA" TO OUT-NAME. WRITE OUT-REC.
           WRITE OUT-REC-ALT FROM "003GAMMA".
           CLOSE OUT-FILE.
           OPEN INPUT OUT-FILE.
           PERFORM UNTIL AT-EOF
               READ OUT-FILE INTO HOLD
                   AT END SET AT-EOF TO TRUE
                   NOT AT END
                       ADD 1 TO COUNTER
                       DISPLAY "READ " OUT-ID " " OUT-NAME " [" HOLD "]"
               END-READ
           END-PERFORM.
           DISPLAY "RECORDS " COUNTER " STATUS " FS.
           READ OUT-FILE.
           DISPLAY "READ AFTER END " FS.
           CLOSE OUT-FILE.
           OPEN I-O OUT-FILE.
           READ OUT-FILE.
           MOVE "REWRITTEN" TO OUT-NAME.
           REWRITE OUT-REC.
           DISPLAY "REWRITE " FS.
           CLOSE OUT-FILE.
           OPEN INPUT OUT-FILE.
           READ OUT-FILE.
           DISPLAY "FIRST NOW " OUT-REC.
           CLOSE OUT-FILE.
           OPEN OUTPUT TXT-FILE.
           MOVE "LINE ONE" TO TXT-REC.
           WRITE TXT-REC.
           WRITE TXT-REC FROM "LINE TWO".
           CLOSE TXT-FILE.
           OPEN EXTEND TXT-FILE.
           WRITE TXT-REC FROM "LINE THREE".
           CLOSE TXT-FILE.
           MOVE "N" TO EOF-FLAG.
           OPEN INPUT TXT-FILE.
           PERFORM UNTIL AT-EOF
               READ TXT-FILE AT END SET AT-EOF TO TRUE
                   NOT AT END DISPLAY "[" TXT-REC "]"
               END-READ
           END-PERFORM.
           CLOSE TXT-FILE.
           OPEN INPUT MISSING-FILE.
           DISPLAY "OPTIONAL " FS2.
           READ MISSING-FILE AT END DISPLAY "MISSING AT END " FS2.
           CLOSE MISSING-FILE.
           OPEN INPUT REQUIRED-FILE.
           DISPLAY "REQUIRED " FS3.
           CLOSE OUT-FILE.
           DISPLAY "CLOSE NOT OPEN " FS.
           STOP RUN.
