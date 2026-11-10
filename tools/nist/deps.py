#!/usr/bin/env python3
"""Programs that call other programs: which sources each one needs.

Usage: python3 -I tools/nist/deps.py <src-dir> <module>

For each source of the module prints "NAME main SOURCE..." (the sources of
the programs it may call, transitively) or "NAME sub" when another source
calls the program it holds (it is built with its callers, not on its own).
A program may be called by a literal (CALL "X") or through a data item, so
every quoted name of a program in another source counts.
"""
import os
import re
import sys


def program_ids(text):
    ids = []
    lines = text.split("\n")
    for k, line in enumerate(lines):
        area = line[6:72] if len(line) > 6 else ""
        if len(line) > 6 and line[6] == "*":
            continue
        m = re.search(r"PROGRAM-ID\.\s*([A-Z0-9-]+)?", area)
        if m:
            name = m.group(1)
            if not name and k + 1 < len(lines):
                name = lines[k + 1][6:72].strip().split(" ")[0].rstrip(".")
            if name:
                ids.append(name)
    return ids


def quoted(text):
    names = set()
    for line in text.split("\n"):
        if len(line) > 6 and line[6] == "*":
            continue
        for m in re.finditer(r'"([A-Z0-9-]+)"', line[6:72]):
            names.add(m.group(1))
    return names


def main():
    src, module = sys.argv[1], sys.argv[2]
    texts = {}
    for f in sorted(os.listdir(src)):
        if f.startswith(module) and f.endswith(".CBL"):
            with open(os.path.join(src, f), encoding="latin-1") as fh:
                texts[f[:-4]] = fh.read()
    owner = {}                      # program-id -> source
    for name, text in texts.items():
        for pid in program_ids(text):
            owner.setdefault(pid, name)
    calls = {name: {owner[q] for q in quoted(text) if q in owner and owner[q] != name}
             for name, text in texts.items()}
    called = set()
    for name in texts:
        called |= calls[name]
    for name in texts:
        if name in called:
            print(name, "sub")
            continue
        need, todo = [], list(calls[name])
        while todo:
            n = todo.pop()
            if n in need or n == name:
                continue
            need.append(n)
            todo.extend(calls[n])
        print(name, "main", *sorted(need))


if __name__ == "__main__":
    main()
