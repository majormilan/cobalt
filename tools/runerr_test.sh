#!/bin/bash
# Run-time error tests: each tests/runerr/NAME.cbl is compiled (with the
# options in NAME.flags) and run; its output (standard output and error)
# and exit status are compared with tests/runerr/NAME.out.
#
# Usage: tools/runerr_test.sh [NAME...]     (default: all)
#        UPDATE=1 tools/runerr_test.sh NAME  writes NAME.out from the run
cd "$(dirname "$0")/.." || exit 1
dir=obj/test/runerr
mkdir -p "$dir"
passed=0; failed=0
shopt -s nullglob
if [ $# -gt 0 ]; then names=("$@"); else
    names=()
    for f in tests/runerr/*.cbl; do names+=("$(basename "$f" .cbl)"); done
fi
for name in "${names[@]}"; do
    flags=""
    [ -f "tests/runerr/$name.flags" ] && flags=$(cat "tests/runerr/$name.flags")
    if ! ./bin/cobalt -fdiagnostics-color=never $flags -o "$dir/$name" "tests/runerr/$name.cbl" > "$dir/$name.compile" 2>&1; then
        echo "  FAIL  runerr/$name (does not compile, see $dir/$name.compile)"; failed=$((failed+1)); continue
    fi
    out=$(cd "$dir" && timeout 10 "./$name" < /dev/null 2>&1; echo "[exit $?]")
    if [ -n "$UPDATE" ]; then
        printf '%s\n' "$out" > "tests/runerr/$name.out"
        echo "  WROTE tests/runerr/$name.out"
        continue
    fi
    if diff -u "tests/runerr/$name.out" <(printf '%s\n' "$out") > "$dir/$name.diff"; then
        echo "  PASS  runerr/$name"; passed=$((passed+1))
    else
        echo "  FAIL  runerr/$name (see $dir/$name.diff)"; failed=$((failed+1))
    fi
done
[ -z "$UPDATE" ] && echo "Run-time errors: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
