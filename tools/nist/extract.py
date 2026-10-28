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
}

SUFFIX = {"COBOL": ".CBL", "CLBRY": ".CPY"}

# Lines with a letter in column 7 are optional variants that EXEC85 enables
# per site (for example S: run under EXEC85 control, Y: report paging,
# P/C: file-system specific clauses). Letters listed here are enabled (the
# letter becomes a space); all others are commented out. D is the standard
# debugging-line indicator and is left alone.
OPTIONS = set()


def substitute(line):
    def repl(m):
        return X_CARDS.get(m.group(1), m.group(0))
    if len(line) > 6 and line[6].isalpha() and line[6] not in "Dd":
        line = line[:6] + (" " if line[6] in OPTIONS else "*") + line[7:]
    return re.sub(r"XXXXX(\d{3})", repl, line)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    current = None
    is_data = False
    count = 0
    with open(src, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line.startswith("*HEADER,"):
                if current:
                    current.close()
                parts = line.split(",")
                kind, name = parts[1].strip(), parts[2].strip()
                path = os.path.join(out, name + SUFFIX.get(kind, ".DAT"))
                current = open(path, "w", encoding="latin-1")
                is_data = not kind in SUFFIX    # data members are copied unchanged
                count += 1
                continue
            if line.startswith("*END-OF,"):
                if current:
                    current.close()
                current = None
                continue
            if current:
                current.write((line if is_data else substitute(line)) + "\n")
    if current:
        current.close()
    print(f"{count} members written to {out}")


if __name__ == "__main__":
    main()
