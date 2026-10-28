#!/bin/bash
# Compile, and where possible run, NIST CCVS85 programs of one module.
# Usage: tools/nist/run.sh [MODULE]      (default NC: the Nucleus)
#
# Prints one line per program and a summary. A program counts as compiled
# when cobalt, nasm and ld all succeed. Run results come from the
# "TESTS WERE EXECUTED SUCCESSFULLY" / "TEST(S) FAILED" lines of the
# program's report.log.

MODULE=${1:-NC}
TARGET=${TARGET:-x86_64-linux-elf}
ROOT=$(pwd)
SRC=obj/nist/src
WORK=obj/nist/work
RUNTIME="$ROOT/obj/runtime/$TARGET/libcobrt.a"

[ -f obj/nist/newcob.val ] || bash tools/nist/fetch.sh
[ -d "$SRC" ] || python3 -I tools/nist/extract.py obj/nist/newcob.val "$SRC" > /dev/null
mkdir -p "$WORK"

total=0; compiled=0; ran=0; passed=0; failed=0
for cbl in "$SRC"/$MODULE*.CBL; do
    name=$(basename "$cbl" .CBL)
    total=$((total+1))
    dir="$WORK/$name"
    rm -rf "$dir"; mkdir -p "$dir"
    if ! timeout 20 ./bin/cobalt -i "$cbl" -o "$dir/$name.asm" > "$dir/compile.log" 2>&1 ||
       ! nasm -f elf64 "$dir/$name.asm" -o "$dir/$name.o" >> "$dir/compile.log" 2>&1 ||
       ! ld "$dir/$name.o" "$RUNTIME" -o "$dir/$name" >> "$dir/compile.log" 2>&1; then
        first=$(grep -m1 -i "error" "$dir/compile.log" | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-110)
        printf "%-8s COMPILE-FAIL  %s\n" "$name" "$first"
        continue
    fi
    compiled=$((compiled+1))
    (cd "$dir" && timeout 10 "./$name" < /dev/null > run.log 2>&1)
    if [ -f "$dir/report.log" ]; then
        ran=$((ran+1))
        ok=$(grep -o "[0-9]* OF *[0-9]* TESTS WERE EXECUTED SUCCESSFULLY" "$dir/report.log" | head -1)
        bad=$(grep -o "[0-9]* TEST(S) FAILED" "$dir/report.log" | head -1 | grep -o "^[0-9]*")
        p=$(echo "$ok" | awk '{print $1+0}')
        passed=$((passed + ${p:-0})); failed=$((failed + ${bad:-0}))
        printf "%-8s RAN           %s, %s failed\n" "$name" "$ok" "${bad:-?}"
    else
        printf "%-8s NO-REPORT     (program ran but wrote no report.log)\n" "$name"
    fi
done
echo
echo "$MODULE: $total programs, $compiled compiled, $ran produced a report"
echo "Individual tests: $passed passed, $failed failed"
