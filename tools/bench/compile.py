#!/usr/bin/env python3
"""Generate a large COBOL-85 program for the compile-time benchmark.

Usage: python3 -I tools/bench/compile.py <output-dir> [paragraphs]

Writes LEDGER.cbl and its copybooks (CUSTREC, TXNREC, WORKAREA, CALCPARA)
into <output-dir>. The program is strict COBOL-85 (it compiles with
GnuCOBOL -std=cobol85): it generates a sequential file of transactions,
sorts it, loads an indexed customer master (alternate key with duplicates),
applies the transactions with random READ / REWRITE / WRITE, reads the
master back sequentially into a printed report (LINAGE, edited items,
STRING, INSPECT, EVALUATE), with a USE procedure for the master file, and
runs <paragraphs> generated calculation paragraphs (default 1600): half
written out, half as COPY CALCPARA REPLACING. Its DISPLAY output is the same
with both compilers.
"""
import os
import sys

out = sys.argv[1]
paragraphs = int(sys.argv[2]) if len(sys.argv) > 2 else 1600
os.makedirs(out, exist_ok=True)


def card(text):
    """A line of fixed-format source: columns 8-72"""
    assert len(text) <= 65, text
    return "       " + text


def write(name, lines):
    with open(os.path.join(out, name), "w") as f:
        f.write("\n".join(card(t) if not t.startswith("*") else "      " + t for t in lines) + "\n")


write("CUSTREC.cpy", [
    "01  CUST-RECORD.",
    "    05  CUST-KEY.",
    "        10  CUST-REGION       PIC 9(2).",
    "        10  CUST-NUMBER       PIC 9(6).",
    "    05  CUST-NAME             PIC X(24).",
    "    05  CUST-CITY             PIC X(16).",
    "    05  CUST-BALANCE          PIC S9(11)V99 PACKED-DECIMAL.",
    "    05  CUST-CREDIT-LIMIT     PIC S9(9)V99  PACKED-DECIMAL.",
    "    05  CUST-TXN-COUNT        PIC 9(7)      BINARY.",
    "    05  CUST-STATUS           PIC X.",
    "        88  CUST-ACTIVE       VALUE \"A\".",
    "        88  CUST-OVER-LIMIT   VALUE \"O\".",
    "        88  CUST-CLOSED       VALUE \"C\".",
    "    05  CUST-HISTORY OCCURS 12 TIMES.",
    "        10  CUST-MONTH-AMT    PIC S9(9)V99  PACKED-DECIMAL.",
])

write("TXNREC.cpy", [
    "01  TXN-RECORD.",
    "    05  TXN-CUST-KEY.",
    "        10  TXN-REGION        PIC 9(2).",
    "        10  TXN-NUMBER        PIC 9(6).",
    "    05  TXN-SEQ               PIC 9(8).",
    "    05  TXN-TYPE              PIC X.",
    "        88  TXN-DEBIT         VALUE \"D\".",
    "        88  TXN-CREDIT        VALUE \"C\".",
    "        88  TXN-FEE           VALUE \"F\".",
    "    05  TXN-AMOUNT            PIC S9(7)V99.",
    "    05  TXN-MONTH             PIC 99.",
    "    05  TXN-MEMO              PIC X(20).",
])

write("WORKAREA.cpy", [
    "01  WK-COUNTERS.",
    "    05  WK-TXN-WRITTEN        PIC 9(8)   VALUE ZERO.",
    "    05  WK-TXN-APPLIED        PIC 9(8)   VALUE ZERO.",
    "    05  WK-CUST-NEW           PIC 9(8)   VALUE ZERO.",
    "    05  WK-CUST-REPORTED      PIC 9(8)   VALUE ZERO.",
    "    05  WK-OVER-LIMIT         PIC 9(8)   VALUE ZERO.",
    "    05  WK-IO-ERRORS          PIC 9(4)   VALUE ZERO.",
    "    05  WK-PAGES              PIC 9(4)   VALUE ZERO.",
    "01  WK-RANDOM.",
    "    05  WK-SEED               PIC 9(9)   BINARY VALUE 12345.",
    "    05  WK-PRODUCT            PIC 9(18)  BINARY.",
    "    05  WK-DRAW               PIC 9(9)   BINARY.",
    "01  WK-TOTALS.",
    "    05  WK-TOTAL-DEBIT        PIC S9(13)V99 PACKED-DECIMAL",
    "                              VALUE ZERO.",
    "    05  WK-TOTAL-CREDIT       PIC S9(13)V99 PACKED-DECIMAL",
    "                              VALUE ZERO.",
    "    05  WK-TOTAL-BALANCE      PIC S9(13)V99 PACKED-DECIMAL",
    "                              VALUE ZERO.",
    "    05  WK-CHECKSUM           PIC S9(15)    PACKED-DECIMAL",
    "                              VALUE ZERO.",
    "01  WK-CALC.",
    "    05  WK-ACC OCCURS 10 TIMES PIC S9(9)V9(4) PACKED-DECIMAL.",
    "    05  WK-X                  PIC S9(7)V99.",
    "    05  WK-Y                  PIC S9(7)V99.",
    "    05  WK-Z                  PIC S9(9)V9(4) BINARY.",
    "    05  WK-I                  PIC 9(4) BINARY.",
    "    05  WK-J                  PIC 9(4) BINARY.",
    "01  WK-REGION-TABLE.",
    "    05  WK-REGION-ENTRY OCCURS 20 TIMES INDEXED BY RX.",
    "        10  WK-REGION-CODE    PIC 99.",
    "        10  WK-REGION-TOTAL   PIC S9(13)V99 PACKED-DECIMAL.",
    "        10  WK-REGION-COUNT   PIC 9(7) BINARY.",
])

# A calculation paragraph, written once as library text: COPY REPLACING
# names the paragraph, the accumulator and the constants
CALC_BODY = [
    "CALC-NAME.",
    "    MOVE CALC-K1 TO WK-X.",
    "    COMPUTE WK-Y ROUNDED = (WK-X * CALC-K2 + CALC-K3) / 7.",
    "    EVALUATE TRUE",
    "        WHEN WK-Y > 500",
    "            ADD WK-Y TO WK-ACC (CALC-SLOT)",
    "        WHEN WK-Y < -500",
    "            SUBTRACT WK-Y FROM WK-ACC (CALC-SLOT)",
    "        WHEN OTHER",
    "            MULTIPLY 2 BY WK-Y",
    "            ADD WK-Y TO WK-ACC (CALC-SLOT)",
    "    END-EVALUATE.",
    "    COMPUTE WK-Z = WK-ACC (CALC-SLOT) * 3 - WK-X.",
    "    IF WK-Z > 1000000",
    "        DIVIDE 3 INTO WK-ACC (CALC-SLOT) ROUNDED",
    "    END-IF.",
]
write("CALCPARA.cpy", CALC_BODY)

src = []
A = src.append
A("IDENTIFICATION DIVISION.")
A("PROGRAM-ID. LEDGER.")
A("*  A ledger run: transactions are generated, sorted, applied to an")
A("*  indexed customer master, and reported; then generated")
A("*  calculation paragraphs.")
A("ENVIRONMENT DIVISION.")
A("CONFIGURATION SECTION.")
A("SOURCE-COMPUTER. X86-64.")
A("OBJECT-COMPUTER. X86-64.")
A("INPUT-OUTPUT SECTION.")
A("FILE-CONTROL.")
A("    SELECT TXN-FILE ASSIGN TO \"ledger-txn.dat\"")
A("        ORGANIZATION IS SEQUENTIAL")
A("        FILE STATUS IS FS-TXN.")
A("    SELECT SORTED-FILE ASSIGN TO \"ledger-sorted.dat\"")
A("        FILE STATUS IS FS-SORTED.")
A("    SELECT SORT-WORK ASSIGN TO \"ledger-sort.tmp\".")
A("    SELECT CUST-FILE ASSIGN TO \"ledger-cust.dat\"")
A("        ORGANIZATION IS INDEXED")
A("        ACCESS MODE IS DYNAMIC")
A("        RECORD KEY IS CUST-KEY")
A("        ALTERNATE RECORD KEY IS CUST-CITY WITH DUPLICATES")
A("        FILE STATUS IS FS-CUST.")
A("    SELECT REPORT-FILE ASSIGN TO \"ledger-report.txt\"")
A("        FILE STATUS IS FS-REPORT.")
A("DATA DIVISION.")
A("FILE SECTION.")
A("FD  TXN-FILE.")
A("COPY TXNREC.")
A("FD  SORTED-FILE.")
A("COPY TXNREC REPLACING TXN-RECORD BY SORTED-RECORD.")
A("SD  SORT-WORK.")
A("COPY TXNREC REPLACING TXN-RECORD BY SORT-RECORD.")
A("FD  CUST-FILE.")
A("COPY CUSTREC.")
A("FD  REPORT-FILE")
A("    LINAGE IS 50 LINES WITH FOOTING AT 46")
A("    LINES AT TOP 2 LINES AT BOTTOM 2.")
A("01  REPORT-LINE                 PIC X(100).")
A("WORKING-STORAGE SECTION.")
A("01  FILE-STATUSES.")
A("    05  FS-TXN                PIC XX.")
A("    05  FS-SORTED             PIC XX.")
A("    05  FS-CUST               PIC XX.")
A("    05  FS-REPORT             PIC XX.")
A("01  SWITCHES.")
A("    05  SW-EOF                PIC X VALUE \"N\".")
A("        88  AT-EOF            VALUE \"Y\".")
A("COPY WORKAREA.")
A("01  DETAIL-LINE.")
A("    05  DL-KEY                PIC 99B999999.")
A("    05  FILLER                PIC X(2) VALUE SPACES.")
A("    05  DL-NAME               PIC X(24).")
A("    05  DL-CITY               PIC X(16).")
A("    05  DL-BALANCE            PIC Z,ZZZ,ZZZ,ZZ9.99-.")
A("    05  FILLER                PIC X VALUE SPACE.")
A("    05  DL-COUNT              PIC ZZZ,ZZ9.")
A("    05  FILLER                PIC X VALUE SPACE.")
A("    05  DL-FLAG               PIC X(10).")
A("01  HEADING-LINE.")
A("    05  FILLER            PIC X(20) VALUE \"LEDGER REPORT  PAGE\".")
A("    05  HL-PAGE               PIC ZZZ9.")
A("01  SHOW-AMOUNT               PIC -(13)9.99.")
A("01  SHOW-COUNT                PIC Z(7)9.")
A("01  SHOW-SUM                  PIC -(15)9.")
A("01  NAME-WORK                 PIC X(24).")
A("01  TALLY-COUNT               PIC 9(4).")
A("PROCEDURE DIVISION.")
A("DECLARATIVES.")
A("CUST-ERRORS SECTION.")
A("    USE AFTER STANDARD ERROR PROCEDURE ON CUST-FILE.")
A("CUST-ERROR-COUNT.")
A("    ADD 1 TO WK-IO-ERRORS.")
A("END DECLARATIVES.")
A("MAIN SECTION.")
A("MAIN-LINE.")
A("    PERFORM GENERATE-TRANSACTIONS.")
A("    SORT SORT-WORK ON ASCENDING KEY TXN-CUST-KEY OF SORT-RECORD")
A("                      ASCENDING KEY TXN-SEQ OF SORT-RECORD")
A("        USING TXN-FILE GIVING SORTED-FILE.")
A("    PERFORM LOAD-CUSTOMERS.")
A("    PERFORM APPLY-TRANSACTIONS.")
A("    PERFORM PRINT-REPORT.")
A("    PERFORM RUN-CALCULATIONS.")
A("    PERFORM SHOW-TOTALS.")
A("    STOP RUN.")
A("")
A("NEXT-RANDOM.")
A("    COMPUTE WK-PRODUCT = WK-SEED * 16807.")
A("    DIVIDE WK-PRODUCT BY 2147483647 GIVING WK-DRAW")
A("        REMAINDER WK-SEED.")
A("")
A("GENERATE-TRANSACTIONS.")
A("    OPEN OUTPUT TXN-FILE.")
A("    PERFORM VARYING WK-I FROM 1 BY 1 UNTIL WK-I > 40")
A("        PERFORM GENERATE-BATCH")
A("    END-PERFORM.")
A("    CLOSE TXN-FILE.")
A("GENERATE-BATCH.")
A("    PERFORM VARYING WK-J FROM 1 BY 1 UNTIL WK-J > 1000")
A("        PERFORM NEXT-RANDOM")
A("        DIVIDE WK-SEED BY 20 GIVING WK-DRAW")
A("            REMAINDER TXN-REGION OF TXN-RECORD")
A("        PERFORM NEXT-RANDOM")
A("        DIVIDE WK-SEED BY 600 GIVING WK-DRAW")
A("            REMAINDER TXN-NUMBER OF TXN-RECORD")
A("        ADD 1 TO WK-TXN-WRITTEN")
A("        MOVE WK-TXN-WRITTEN TO TXN-SEQ OF TXN-RECORD")
A("        PERFORM NEXT-RANDOM")
A("        DIVIDE WK-SEED BY 3 GIVING WK-DRAW REMAINDER WK-Z")
A("        EVALUATE WK-Z")
A("            WHEN 0 MOVE \"D\" TO TXN-TYPE OF TXN-RECORD")
A("            WHEN 1 MOVE \"C\" TO TXN-TYPE OF TXN-RECORD")
A("            WHEN OTHER MOVE \"F\" TO TXN-TYPE OF TXN-RECORD")
A("        END-EVALUATE")
A("        PERFORM NEXT-RANDOM")
A("        DIVIDE WK-SEED BY 250000 GIVING WK-DRAW REMAINDER WK-X")
A("        DIVIDE WK-X BY 100 GIVING TXN-AMOUNT OF TXN-RECORD")
A("        DIVIDE WK-SEED BY 12 GIVING WK-DRAW")
A("            REMAINDER TXN-MONTH OF TXN-RECORD")
A("        ADD 1 TO TXN-MONTH OF TXN-RECORD")
A("        MOVE \"GENERATED ENTRY\" TO TXN-MEMO OF TXN-RECORD")
A("        WRITE TXN-RECORD")
A("    END-PERFORM.")
A("")
A("LOAD-CUSTOMERS.")
A("    OPEN OUTPUT CUST-FILE.")
A("    PERFORM VARYING WK-I FROM 0 BY 2 UNTIL WK-I > 19")
A("        PERFORM VARYING WK-J FROM 0 BY 3 UNTIL WK-J > 599")
A("            INITIALIZE CUST-RECORD")
A("            MOVE WK-I TO CUST-REGION")
A("            MOVE WK-J TO CUST-NUMBER")
A("            STRING \"CUSTOMER \" DELIMITED BY SIZE")
A("                   CUST-NUMBER DELIMITED BY SIZE")
A("                INTO CUST-NAME")
A("            END-STRING")
A("            DIVIDE WK-J BY 7 GIVING WK-DRAW REMAINDER WK-Z")
A("            EVALUATE WK-Z")
A("                WHEN 0 MOVE \"BUDAPEST\" TO CUST-CITY")
A("                WHEN 1 MOVE \"DEBRECEN\" TO CUST-CITY")
A("                WHEN 2 MOVE \"SZEGED\" TO CUST-CITY")
A("                WHEN 3 MOVE \"PECS\" TO CUST-CITY")
A("                WHEN 4 MOVE \"GYOR\" TO CUST-CITY")
A("                WHEN OTHER MOVE \"MISKOLC\" TO CUST-CITY")
A("            END-EVALUATE")
A("            COMPUTE CUST-CREDIT-LIMIT = 5000 + WK-J * 25")
A("            SET CUST-ACTIVE TO TRUE")
A("            WRITE CUST-RECORD")
A("        END-PERFORM")
A("    END-PERFORM.")
A("    CLOSE CUST-FILE.")
A("")
A("APPLY-TRANSACTIONS.")
A("    OPEN INPUT SORTED-FILE.")
A("    OPEN I-O CUST-FILE.")
A("    MOVE \"N\" TO SW-EOF.")
A("    PERFORM UNTIL AT-EOF")
A("        READ SORTED-FILE")
A("            AT END SET AT-EOF TO TRUE")
A("            NOT AT END PERFORM APPLY-ONE")
A("        END-READ")
A("    END-PERFORM.")
A("    CLOSE SORTED-FILE CUST-FILE.")
A("APPLY-ONE.")
A("    MOVE TXN-CUST-KEY OF SORTED-RECORD TO CUST-KEY.")
A("    READ CUST-FILE")
A("        INVALID KEY PERFORM NEW-CUSTOMER")
A("    END-READ.")
A("    EVALUATE TRUE")
A("        WHEN TXN-DEBIT OF SORTED-RECORD")
A("            SUBTRACT TXN-AMOUNT OF SORTED-RECORD")
A("                FROM CUST-BALANCE")
A("            ADD TXN-AMOUNT OF SORTED-RECORD TO WK-TOTAL-DEBIT")
A("        WHEN TXN-CREDIT OF SORTED-RECORD")
A("            ADD TXN-AMOUNT OF SORTED-RECORD TO CUST-BALANCE")
A("            ADD TXN-AMOUNT OF SORTED-RECORD TO WK-TOTAL-CREDIT")
A("        WHEN OTHER")
A("            COMPUTE CUST-BALANCE ROUNDED = CUST-BALANCE")
A("                - TXN-AMOUNT OF SORTED-RECORD / 10")
A("    END-EVALUATE.")
A("    ADD TXN-AMOUNT OF SORTED-RECORD")
A("        TO CUST-MONTH-AMT (TXN-MONTH OF SORTED-RECORD).")
A("    ADD 1 TO CUST-TXN-COUNT.")
A("    IF CUST-BALANCE < 0 - CUST-CREDIT-LIMIT")
A("        SET CUST-OVER-LIMIT TO TRUE")
A("    END-IF.")
A("    REWRITE CUST-RECORD")
A("        INVALID KEY ADD 1 TO WK-IO-ERRORS")
A("    END-REWRITE.")
A("    ADD 1 TO WK-TXN-APPLIED.")
A("NEW-CUSTOMER.")
A("    INITIALIZE CUST-RECORD.")
A("    MOVE TXN-CUST-KEY OF SORTED-RECORD TO CUST-KEY.")
A("    MOVE \"WALK-IN CUSTOMER\" TO CUST-NAME.")
A("    MOVE \"ONLINE\" TO CUST-CITY.")
A("    MOVE 1000 TO CUST-CREDIT-LIMIT.")
A("    SET CUST-ACTIVE TO TRUE.")
A("    WRITE CUST-RECORD")
A("        INVALID KEY ADD 1 TO WK-IO-ERRORS")
A("    END-WRITE.")
A("    ADD 1 TO WK-CUST-NEW.")
A("")
A("PRINT-REPORT.")
A("    OPEN INPUT CUST-FILE.")
A("    OPEN OUTPUT REPORT-FILE.")
A("    PERFORM VARYING RX FROM 1 BY 1 UNTIL RX > 20")
A("        SET WK-I TO RX")
A("        SUBTRACT 1 FROM WK-I GIVING WK-REGION-CODE (RX)")
A("        MOVE ZERO TO WK-REGION-TOTAL (RX) WK-REGION-COUNT (RX)")
A("    END-PERFORM.")
A("    PERFORM PRINT-HEADING.")
A("    MOVE \"N\" TO SW-EOF.")
A("    PERFORM UNTIL AT-EOF")
A("        READ CUST-FILE NEXT RECORD")
A("            AT END SET AT-EOF TO TRUE")
A("            NOT AT END PERFORM PRINT-ONE")
A("        END-READ")
A("    END-PERFORM.")
A("    CLOSE CUST-FILE REPORT-FILE.")
A("PRINT-HEADING.")
A("    ADD 1 TO WK-PAGES.")
A("    MOVE WK-PAGES TO HL-PAGE.")
A("    WRITE REPORT-LINE FROM HEADING-LINE AFTER ADVANCING PAGE.")
A("PRINT-ONE.")
A("    MOVE CUST-KEY TO DL-KEY.")
A("    MOVE CUST-NAME TO NAME-WORK.")
A("    INSPECT NAME-WORK CONVERTING \"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"")
A("        TO \"abcdefghijklmnopqrstuvwxyz\".")
A("    MOVE ZERO TO TALLY-COUNT.")
A("    INSPECT NAME-WORK TALLYING TALLY-COUNT FOR ALL \"e\".")
A("    MOVE NAME-WORK TO DL-NAME.")
A("    MOVE CUST-CITY TO DL-CITY.")
A("    MOVE CUST-BALANCE TO DL-BALANCE.")
A("    MOVE CUST-TXN-COUNT TO DL-COUNT.")
A("    IF CUST-OVER-LIMIT")
A("        MOVE \"OVER LIMIT\" TO DL-FLAG")
A("        ADD 1 TO WK-OVER-LIMIT")
A("    ELSE")
A("        MOVE SPACES TO DL-FLAG")
A("    END-IF.")
A("    WRITE REPORT-LINE FROM DETAIL-LINE AFTER ADVANCING 1 LINE")
A("        AT END-OF-PAGE PERFORM PRINT-HEADING")
A("    END-WRITE.")
A("    ADD CUST-BALANCE TO WK-TOTAL-BALANCE.")
A("    ADD TALLY-COUNT TO WK-CHECKSUM.")
A("    SET RX TO 1.")
A("    SEARCH WK-REGION-ENTRY")
A("        WHEN RX = CUST-REGION + 1")
A("            ADD CUST-BALANCE TO WK-REGION-TOTAL (RX)")
A("            ADD 1 TO WK-REGION-COUNT (RX)")
A("    END-SEARCH.")
A("    ADD 1 TO WK-CUST-REPORTED.")
A("")
A("RUN-CALCULATIONS.")
A("    PERFORM VARYING WK-I FROM 1 BY 1 UNTIL WK-I > 10")
A("        MOVE ZERO TO WK-ACC (WK-I)")
A("    END-PERFORM.")
A("    PERFORM CALC-0001 THRU CALC-%04d." % paragraphs)
A("    PERFORM VARYING WK-I FROM 1 BY 1 UNTIL WK-I > 10")
A("        ADD WK-ACC (WK-I) TO WK-CHECKSUM")
A("    END-PERFORM.")
A("")
A("SHOW-TOTALS.")
A("    MOVE WK-TXN-WRITTEN TO SHOW-COUNT.")
A("    DISPLAY \"TRANSACTIONS WRITTEN  \" SHOW-COUNT.")
A("    MOVE WK-TXN-APPLIED TO SHOW-COUNT.")
A("    DISPLAY \"TRANSACTIONS APPLIED  \" SHOW-COUNT.")
A("    MOVE WK-CUST-NEW TO SHOW-COUNT.")
A("    DISPLAY \"NEW CUSTOMERS         \" SHOW-COUNT.")
A("    MOVE WK-CUST-REPORTED TO SHOW-COUNT.")
A("    DISPLAY \"CUSTOMERS REPORTED    \" SHOW-COUNT.")
A("    MOVE WK-OVER-LIMIT TO SHOW-COUNT.")
A("    DISPLAY \"OVER CREDIT LIMIT     \" SHOW-COUNT.")
A("    MOVE WK-PAGES TO SHOW-COUNT.")
A("    DISPLAY \"REPORT PAGES          \" SHOW-COUNT.")
A("    MOVE WK-IO-ERRORS TO SHOW-COUNT.")
A("    DISPLAY \"I-O ERRORS            \" SHOW-COUNT.")
A("    MOVE WK-TOTAL-DEBIT TO SHOW-AMOUNT.")
A("    DISPLAY \"TOTAL DEBIT     \" SHOW-AMOUNT.")
A("    MOVE WK-TOTAL-CREDIT TO SHOW-AMOUNT.")
A("    DISPLAY \"TOTAL CREDIT    \" SHOW-AMOUNT.")
A("    MOVE WK-TOTAL-BALANCE TO SHOW-AMOUNT.")
A("    DISPLAY \"TOTAL BALANCE   \" SHOW-AMOUNT.")
A("    PERFORM VARYING RX FROM 1 BY 2 UNTIL RX > 20")
A("        MOVE WK-REGION-TOTAL (RX) TO SHOW-AMOUNT")
A("        MOVE WK-REGION-COUNT (RX) TO SHOW-COUNT")
A("        DISPLAY \"REGION \" WK-REGION-CODE (RX) SHOW-COUNT")
A("            SHOW-AMOUNT")
A("    END-PERFORM.")
A("    MOVE WK-CHECKSUM TO SHOW-SUM.")
A("    DISPLAY \"CHECKSUM        \" SHOW-SUM.")
A("")
A("*  Generated calculation paragraphs")

for n in range(1, paragraphs + 1):
    slot = n % 10 + 1
    k1 = (n * 37) % 2000 - 1000
    k2 = n % 13 + 1
    k3 = (n * 101) % 997
    name = "CALC-%04d" % n
    if n % 2:
        # written out
        for line in CALC_BODY:
            line = (line.replace("CALC-NAME", name).replace("CALC-SLOT", str(slot))
                        .replace("CALC-K1", str(k1)).replace("CALC-K2", str(k2))
                        .replace("CALC-K3", str(k3)))
            A(line)
    else:
        A("COPY CALCPARA REPLACING CALC-NAME BY %s" % name)
        A("                        CALC-SLOT BY %d" % slot)
        A("                        CALC-K1 BY %d CALC-K2 BY %d" % (k1, k2))
        A("                        CALC-K3 BY %d." % k3)

write("LEDGER.cbl", src)
print("LEDGER.cbl: %d lines, %d paragraphs" % (len(src), paragraphs))
