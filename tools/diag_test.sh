#!/bin/bash
# Diagnostics tests: each tests/diag/NAME.cbl is compiled (to an object, not
# run) and the compiler's messages are compared with tests/diag/NAME.err.
# tests/diag/NAME.flags holds extra options (e.g. -X pedantic -Wextra);
# tests/diag/NAME.src names another source to compile (the same program
# checked with other options), in place of NAME.cbl.
#
# Usage: tools/diag_test.sh [NAME...]     (default: all)
#        UPDATE=1 tools/diag_test.sh NAME  writes NAME.err from the output
#                                          (read it before committing!)
cd "$(dirname "$0")/.." || exit 1
mkdir -p obj/test/diag
passed=0; failed=0
shopt -s nullglob
if [ $# -gt 0 ]; then names=("$@"); else
    names=()
    for f in tests/diag/*.err; do names+=("$(basename "$f" .err)"); done
fi
for name in "${names[@]}"; do
    cbl="tests/diag/$name.cbl"
    [ -f "tests/diag/$name.src" ] && cbl="tests/diag/$(cat "tests/diag/$name.src")"
    flags=""
    [ -f "tests/diag/$name.flags" ] && flags=$(cat "tests/diag/$name.flags")
    out=$(./bin/cobalt -fdiagnostics-color=never $flags -c -i "$cbl" -o "obj/test/diag/$name.o" 2>&1)
    if [ -n "$UPDATE" ]; then
        printf '%s\n' "$out" > "tests/diag/$name.err"
        echo "  WROTE tests/diag/$name.err"
        continue
    fi
    if diff -u "tests/diag/$name.err" <(printf '%s\n' "$out") > "obj/test/diag/$name.diff"; then
        echo "  PASS  diag/$name"; passed=$((passed+1))
    else
        echo "  FAIL  diag/$name (see obj/test/diag/$name.diff)"; failed=$((failed+1))
    fi
done
[ -z "$UPDATE" ] && echo "Diagnostics: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
