#!/usr/bin/env python3
"""Score a FIPS flagging program: the OBSOLETE messages it expects.

Usage: python3 -I tools/nist/flags.py <source.CBL> <compile.log>

The program marks each statement to flag with a comment line "Message
expected for above (or following) statement: OBSOLETE" (or NON-CONFORMING
STANDARD). An OBSOLETE expectation passes when cobalt --flag-obsolete gave
an OBSOLETE warning on a line of that statement (up to 6 lines before the
comment, or after it for "following"). NON-CONFORMING STANDARD (the FIPS
subsets) is not checked. Prints: passed failed unchecked spurious.
"""
import re
import sys


def main():
    src, log = sys.argv[1], sys.argv[2]
    with open(src, encoding="latin-1") as f:
        lines = f.read().split("\n")
    warned = set()
    with open(log, encoding="latin-1") as f:
        for line in f:
            line = re.sub(r"\x1b\[[0-9;]*m", "", line)
            m = re.search(r":(\d+):\d+: warning: OBSOLETE", line)
            if m:
                warned.add(int(m.group(1)))
    passed = failed = unchecked = 0
    used = set()
    for k, line in enumerate(lines, 1):
        m = re.search(r"message expected for (above|following) statement: *(\S.*)", line, re.I)
        if not m or len(line) < 7 or line[6] != "*":
            continue
        if not m.group(2).upper().startswith("OBSOLETE"):
            unchecked += 1
            continue
        if m.group(1).lower() == "above":
            window = range(k - 6, k)
        else:
            window = range(k + 1, k + 7)
        hit = [n for n in window if n in warned]
        if hit:
            passed += 1
            used.update(hit)
        else:
            failed += 1
    print(passed, failed, unchecked, len(warned - used))


if __name__ == "__main__":
    main()
