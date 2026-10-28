#!/bin/bash
# Compile, and where possible run, NIST CCVS85 programs of one module.
# Usage: tools/nist/run.sh [MODULE]      (default NC: the Nucleus)
#
# Prints one line per program and a summary. A program counts as compiled
# when cobalt, nasm and ld all succeed. Run results come from the
# "TESTS WERE EXECUTED SUCCESSFULLY" / "TEST(S) FAILED" lines of the
# program's report.log, or for monitor programs that DISPLAY their results,
# from PASS / FAIL in their output.

MODULE=${1:-NC}
TARGET=${TARGET:-x86_64-linux-elf}
ROOT=$(pwd)
SRC=obj/nist/src
WORK=obj/nist/work
RUNTIME="$ROOT/obj/runtime/$TARGET/libcobrt.a"

[ -f obj/nist/newcob.val ] || bash tools/nist/fetch.sh
[ -d "$SRC" ] || python3 -I tools/nist/extract.py obj/nist/newcob.val "$SRC" > /dev/null
mkdir -p "$WORK"

total=0; compiled=0; ran=0; passed=0; failed=0; deleted=0; inspect=0
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
        # Summary lines: "093 OF 093  TESTS WERE EXECUTED SUCCESSFULLY",
        # "NO  TEST(S) FAILED" or "002 TEST(S) FAILED", likewise DELETED and
        # REQUIRE INSPECTION
        ok=$(grep -o "[0-9]* OF *[0-9]* *TESTS WERE EXECUTED SUCCESSFULLY" "$dir/report.log" | head -1 | awk '{print $1}')
        count() { grep -o "\(NO\|[0-9]*\) *TEST(S) $1" "$dir/report.log" | head -1 | awk '{print ($1 == "NO") ? 0 : $1}'; }
        bad=$(count FAILED); del=$(count DELETED); insp=$(count "REQUIRE INSPECTION")
        p=$((10#${ok:-0})); b=$((10#${bad:-0})); d=$((10#${del:-0})); n=$((10#${insp:-0}))
        passed=$((passed + p)); failed=$((failed + b)); deleted=$((deleted + d)); inspect=$((inspect + n))
        if [ -z "$ok" ]; then
            printf "%-8s NO-SUMMARY    (report.log has no result summary)\n" "$name"
        else
            printf "%-8s RAN           %3d passed, %3d failed, %3d deleted, %3d inspect\n" "$name" $p $b $d $n
        fi
    elif grep -q "END OF TEST" "$dir/run.log"; then
        # Monitor ("M") programs DISPLAY their results instead of writing a report
        ran=$((ran+1))
        if grep -q "FAIL\*" "$dir/run.log" || ! grep -q "PASS" "$dir/run.log"; then
            failed=$((failed+1))
            printf "%-8s RAN (DISPLAY) see %s/run.log\n" "$name" "$dir"
        else
            passed=$((passed+1))
            printf "%-8s RAN (DISPLAY) PASS\n" "$name"
        fi
    else
        printf "%-8s NO-REPORT     (program ran but wrote no report.log)\n" "$name"
    fi
done
echo
echo "$MODULE: $total programs, $compiled compiled, $ran produced a report"
echo "Individual tests: $passed passed, $failed failed, $deleted deleted, $inspect require inspection"
