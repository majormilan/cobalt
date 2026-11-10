       IDENTIFICATION DIVISION.
       PROGRAM-ID. FILE-ORGS.
      * RELATIVE and INDEXED files (random, dynamic and sequential
      * access, START, DELETE, REWRITE, duplicate alternate keys),
      * variable-length records, a USE procedure, LINAGE-COUNTER
       ENVIRONMENT DIVISION.
       INPUT-OUTPUT SECTION.
       FILE-CONTROL.
           SELECT REL ASSIGN TO "fo-rel.dat"
               ORGANIZATION RELATIVE ACCESS DYNAMIC
               RELATIVE KEY RK FILE STATUS RS.
           SELECT IDX ASSIGN TO "fo-idx.dat"
               ORGANIZATION INDEXED ACCESS DYNAMIC
               RECORD KEY IK
               ALTERNATE RECORD KEY IA WITH DUPLICATES
               FILE STATUS XS.
           SELECT VAR ASSIGN TO "fo-var.dat"
               FILE STATUS VS.
           SELECT PRT ASSIGN TO "fo-prt.txt".
           SELECT MISSING ASSIGN TO "fo-none.dat"
               FILE STATUS MS.
       DATA DIVISION.
       FILE SECTION.
       FD  REL.
       01  RREC PIC X(10).
       FD  IDX.
       01  IREC.
           05 IK PIC 9(4).
           05 IA PIC X(2).
           05 ID PIC X(6).
       FD  VAR RECORD VARYING FROM 2 TO 20 DEPENDING ON VLEN.
       01  VREC PIC X(20).
       FD  PRT LINAGE 10 FOOTING 8 TOP 1 BOTTOM 1.
       01  PREC PIC X(20).
       FD  MISSING.
       01  MREC PIC X(4).
       WORKING-STORAGE SECTION.
       77  RK PIC 9(4).
       77  RS PIC XX.
       77  XS PIC XX.
       77  VS PIC XX.
       77  MS PIC XX.
       77  VLEN PIC 99.
       77  I PIC 99.
       77  EOP-COUNT PIC 99 VALUE 0.
       PROCEDURE DIVISION.
       DECLARATIVES.
       MISSING-ERR SECTION.
           USE AFTER STANDARD ERROR PROCEDURE ON MISSING.
       MISSING-1.
           DISPLAY "USE PROCEDURE: STATUS " MS.
       END DECLARATIVES.
       MAIN SECTION.
       M1.
           OPEN INPUT MISSING.
           DISPLAY "AFTER OPEN " MS.
      * relative: random writes, sequential reading, START, DELETE
           OPEN OUTPUT REL.
           PERFORM VARYING I FROM 1 BY 3 UNTIL I > 10
               MOVE I TO RK
               MOVE "REC" TO RREC
               MOVE I TO RREC(5:2)
               WRITE RREC INVALID KEY DISPLAY "INV"
           END-PERFORM.
           MOVE 4 TO RK.
           WRITE RREC INVALID KEY DISPLAY "DUP WRITE " RS.
           CLOSE REL.
           OPEN I-O REL.
           MOVE 5 TO RK.
           READ REL INVALID KEY DISPLAY "NO RECORD 5 " RS.
           MOVE 3 TO RK.
           START REL KEY > RK.
           PERFORM 3 TIMES
               READ REL NEXT AT END DISPLAY "END"
               END-READ
               DISPLAY "REL " RK " " RREC
           END-PERFORM.
           MOVE 7 TO RK.
           DELETE REL.
           MOVE 1 TO RK.
           START REL KEY NOT LESS THAN RK.
           MOVE SPACES TO RS.
           PERFORM UNTIL RS NOT = "00" AND RS NOT = SPACES
               READ REL NEXT AT END MOVE "10" TO RS
               NOT AT END DISPLAY "AFTER DELETE " RK
               END-READ
           END-PERFORM.
           CLOSE REL.
      * indexed: duplicates in writing order, REWRITE moves a record
      * after the duplicates of its new key
           OPEN OUTPUT IDX.
           PERFORM VARYING I FROM 9 BY -1 UNTIL I = 0
               MOVE I TO IK
               IF I > 4 MOVE "BB" TO IA ELSE MOVE "AA" TO IA END-IF
               MOVE "DATA" TO ID
               WRITE IREC INVALID KEY DISPLAY "INV " XS
               END-WRITE
           END-PERFORM.
           MOVE 3 TO IK.
           WRITE IREC INVALID KEY DISPLAY "DUP PRIME " XS.
           CLOSE IDX.
           OPEN I-O IDX.
           MOVE 2 TO IK.
           READ IDX.
           MOVE "BB" TO IA.
           REWRITE IREC.
           MOVE "BB" TO IA.
           START IDX KEY = IA.
           PERFORM 6 TIMES
               READ IDX NEXT
               DISPLAY "BB " IK " " XS
           END-PERFORM.
           MOVE 6 TO IK.
           DELETE IDX.
           MOVE 6 TO IK.
           READ IDX KEY IS IK INVALID KEY DISPLAY "DELETED " XS.
           MOVE "A" TO IA.
           START IDX KEY > IA.
           READ IDX NEXT.
           DISPLAY "FIRST AFTER A: " IREC.
           CLOSE IDX.
      * variable-length records
           OPEN OUTPUT VAR.
           PERFORM VARYING I FROM 2 BY 6 UNTIL I > 20
               MOVE ALL "X" TO VREC
               MOVE I TO VLEN
               WRITE VREC
           END-PERFORM.
      * longer than the maximum: 44 (GnuCOBOL writes it and returns 00)
           MOVE 21 TO VLEN.
           WRITE VREC.
           DISPLAY "TOO LONG " VS.
           CLOSE VAR.
           OPEN INPUT VAR.
           PERFORM 4 TIMES
               MOVE SPACES TO VREC
               READ VAR
               DISPLAY "VAR " VLEN " " VREC(1:VLEN) "|"
           END-PERFORM.
           CLOSE VAR.
      * LINAGE: the counter and the end-of-page condition
           OPEN OUTPUT PRT.
           PERFORM 12 TIMES
               WRITE PREC FROM "LINE"
                   AT END-OF-PAGE ADD 1 TO EOP-COUNT
                   DISPLAY "EOP AT " LINAGE-COUNTER
               END-WRITE
           END-PERFORM.
           DISPLAY "LINAGE-COUNTER " LINAGE-COUNTER " EOP " EOP-COUNT.
           CLOSE PRT.
           STOP RUN.
