#!/usr/bin/env python3
"""Split the NIST CCVS85 suite (newcob.val) into source files.

Usage: python3 -I tools/nist/extract.py <newcob.val> <output-dir>

Each member starts with a line "*HEADER,<type>,<name>". COBOL programs are
written as <name>.CBL, copy library members (CLBRY) as <name>.CPY, data
members as <name>.DAT. The suite's own driver (EXEC85) replaces placeholder
"X-cards" (XXXXXnnn) with site values; the values used here are below.
"""
import os
import re
import sys

# X-card values (implementor-names, computer-names, file names)
X_CARDS = {
    "055": '"report.log"',      # print file of every test program
    "082": "X86-64",            # SOURCE-COMPUTER
    "083": "X86-64",            # OBJECT-COMPUTER
    "081": '"@#!%&~^`"',        # literal of characters outside the COBOL set
    "051": "SWITCH-1",          # implementor-names of switches
    "052": "SWITCH-2",
    "056": "CONSOLE",           # DISPLAY output device
    "057": "CONSOLE",           # ACCEPT input device
    "090": "66",                # ordinal position of "A" in the native (ASCII) set
    "091": "69",                # ordinal position of "D"
    "068": "8192",              # OBJECT-COMPUTER MEMORY SIZE (characters)
    "084": "STANDARD",          # LABEL RECORDS ARE ...
    "073": "C01",               # implementor-name of channel 1 (WRITE ADVANCING)
    "086": "PIC X(8)",          # PICTURE of a file label data item (VALUE OF)
    "047": "ALTLIB1",           # library-names of COPY ... OF/IN (SM207A)
    "048": "ALTLIB2",
}

# Library members that belong in a named library (a directory of the output
# directory) rather than the default one: member -> (library, text-name)
LIBRARIES = {"ALTLB": ("ALTLIB1", "ALTLB"), "ALTL1": ("ALTLIB2", "ALTLB")}

SUFFIX = {"COBOL": ".CBL", "CLBRY": ".CPY"}

# Lines with a letter in column 7 are optional variants that EXEC85 enables
# per site (for example S: run under EXEC85 control, Y: report paging,
# P/C: file-system specific clauses). Letters listed here are enabled (the
# letter becomes a space); all others are commented out. D is the standard
# debugging-line indicator and is left alone.
OPTIONS = {"A", "E", "H", "T"}  # A: SPECIAL-NAMES switches; E, H: CLOSE UNIT
                                # and CLOSE REEL on multi-reel files (the other
                                # choices, F and I, delete the test); T: 29-
                                # character indexed keys (IX; U would give 8)


def substitute(line):
    def repl(m):
        return X_CARDS.get(m.group(1), m.group(0))
    if len(line) > 6 and line[6].isalpha() and line[6] not in "Dd":
        line = line[:6] + (" " if line[6] in OPTIONS else "*") + line[7:]
    # Substitute in the program text area only, keeping the sequence field
    # (columns 73-80) in place: a value of another length is padded with, or
    # takes the place of, trailing spaces of the area
    area, rest = line[:72], line[72:]
    # XXXXDnnn and XXXXPnnn name the same file as XXXXXnnn
    area = re.sub(r"XXXX[DP](\d{3})", r"XXXXX\1", area)
    new = re.sub(r"XXXXX(\d{3})", repl, area)
    if new != area and rest:
        new = new.rstrip().ljust(72) if len(new.rstrip()) <= 72 else new
    return new + rest


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    current = None
    members = []                # [kind, name, lines] in suite order
    with open(src, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line.startswith("*HEADER,"):
                parts = line.split(",")
                members.append([parts[1].strip(), parts[2].strip(), []])
                continue
            if line.startswith("*END-OF,"):
                continue
            if members:
                members[-1][2].append(line)
    count = 0
    used = set()
    for kind, name, lines in members:
        is_data = kind not in SUFFIX    # data members are copied unchanged
        text = [line if is_data else substitute(line) for line in lines]
        # Some members hold a second program under the name of the first
        # (SM101A, whose report file SM102A reads): name it by its PROGRAM-ID
        if kind == "COBOL" and name in used:
            for k, line in enumerate(text):
                m = re.search(r"PROGRAM-ID\.\s*(\w+)?", line[6:72])
                if m:
                    pid = m.group(1) or (text[k + 1][6:72].strip().rstrip(".") if k + 1 < len(text) else "")
                    if pid:
                        name = pid
                    break
        used.add(name)
        path = os.path.join(out, name + SUFFIX.get(kind, ".DAT"))
        if kind == "CLBRY" and name in LIBRARIES:
            lib, tname = LIBRARIES[name]
            os.makedirs(os.path.join(out, lib), exist_ok=True)
            path = os.path.join(out, lib, tname + ".CPY")
        with open(path, "w", encoding="latin-1") as w:
            w.write("".join(line + "\n" for line in text))
        count += 1
    print(f"{count} members written to {out}")


if __name__ == "__main__":
    main()
