#!/usr/bin/env python3
"""Score a FIPS flagging program: the messages it expects.

Usage: python3 -I tools/nist/flags.py <source.CBL> <compile.log>

The program marks each statement to flag with a comment line "Message
expected for above (or following) statement: OBSOLETE" or "NON-CONFORMING
STANDARD". An expectation passes when cobalt gave a warning of that kind
(--flag-obsolete: OBSOLETE; --subset: NON-CONFORMING STANDARD) on a line of
that statement (up to 6 lines before the comment, or after it for
"following"). Prints: passed failed unchecked spurious (unchecked: other
kinds of message; spurious: warnings no expectation needed).
"""
import re
import sys


def main():
    src, log = sys.argv[1], sys.argv[2]
    with open(src, encoding="latin-1") as f:
        lines = f.read().split("\n")
    warned = {"OBSOLETE": set(), "NON-CONFORMING": set()}
    with open(log, encoding="latin-1") as f:
        for line in f:
            line = re.sub(r"\x1b\[[0-9;]*m", "", line)
            m = re.search(r":(\d+):\d+: warning: (OBSOLETE|NON-CONFORMING)", line)
            if m:
                warned[m.group(2)].add(int(m.group(1)))
    passed = failed = unchecked = 0
    used = set()
    for k, line in enumerate(lines, 1):
        m = re.search(r"message expected for (above|following) statement: *(\S.*)", line, re.I)
        if not m or len(line) < 7 or line[6] != "*":
            continue
        kind = m.group(2).upper()
        kind = "OBSOLETE" if kind.startswith("OBSOLETE") else "NON-CONFORMING" if kind.startswith("NON-CONFORMING") else None
        if not kind:
            unchecked += 1
            continue
        if m.group(1).lower() == "above":
            window = range(k - 6, k)
        else:
            window = range(k + 1, k + 7)
        hit = [n for n in window if n in warned[kind]]
        if hit:
            passed += 1
            used.update((kind, n) for n in hit)
        else:
            failed += 1
    spurious = sum(1 for k in warned for n in warned[k] if (k, n) not in used)
    print(passed, failed, unchecked, spurious)


if __name__ == "__main__":
    main()
