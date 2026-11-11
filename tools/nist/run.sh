#!/bin/bash
# Compile, and where possible run, NIST CCVS85 programs of one module.
# Usage: [COBALT_FLAGS="-D ibm"] [ASSEMBLER=nasm] tools/nist/run.sh [MODULE]
#        (default MODULE NC: the Nucleus. cobalt builds each executable
#        itself; ASSEMBLER=nasm builds with nasm and ld instead.)
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
# COMPILER=gnucobol builds with GnuCOBOL's cobc instead (for comparison)
COMPILER=${COMPILER:-cobalt}
[ "$COMPILER" = gnucobol ] && WORK=obj/nist/work-gnucobol
RUNTIME="$ROOT/obj/runtime/$TARGET/libcobrt.a"

[ -f obj/nist/newcob.val ] || bash tools/nist/fetch.sh
[ -d "$SRC" ] || python3 -I tools/nist/extract.py obj/nist/newcob.val "$SRC" > /dev/null
mkdir -p "$WORK"
FILES="$WORK/files-$MODULE"
rm -rf "$FILES"; mkdir -p "$FILES"

# Programs called by others are built with their callers (tools/nist/deps.py)
DEPS="$WORK/deps-$MODULE"
python3 -I tools/nist/deps.py "$SRC" "$MODULE" > "$DEPS"

total=0; compiled=0; ran=0; passed=0; failed=0; deleted=0; inspect=0; build_ms=0
for cbl in "$SRC"/$MODULE*.CBL; do
    name=$(basename "$cbl" .CBL)
    dep=$(grep "^$name " "$DEPS")
    if [ "${dep#* }" = sub ]; then
        printf "%-8s SUBPROGRAM    (built with the programs that call it)\n" "$name"
        continue
    fi
    extra=""
    for d in $(echo "$dep" | cut -d' ' -f3-); do extra="$extra -i $SRC/$d.CBL"; done
    total=$((total+1))
    dir="$WORK/$name"
    rm -rf "$dir"; mkdir -p "$dir"
    start=$(date +%s%N)
    if [ "$COMPILER" = gnucobol ]; then
        timeout 60 cobc -x -std=cobol85 -o "$dir/$name" "$cbl" > "$dir/compile.log" 2>&1
    elif [ -z "$ASSEMBLER" ]; then
        timeout 20 ./bin/cobalt $COBALT_FLAGS -i "$cbl" $extra -o "$dir/$name" > "$dir/compile.log" 2>&1
    else
        timeout 20 ./bin/cobalt $COBALT_FLAGS -A -i "$cbl" -o "$dir/$name.asm" > "$dir/compile.log" 2>&1 &&
        $ASSEMBLER -f elf64 "$dir/$name.asm" -o "$dir/$name.o" >> "$dir/compile.log" 2>&1 &&
        ld "$dir/$name.o" "$RUNTIME" -o "$dir/$name" >> "$dir/compile.log" 2>&1
    fi
    status=$?
    build_ms=$((build_ms + ($(date +%s%N) - start) / 1000000))
    if [ $status -ne 0 ]; then
        first=$(grep -m1 -i "error" "$dir/compile.log" | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-110)
        printf "%-8s COMPILE-FAIL  %s\n" "$name" "$first"
        continue
    fi
    compiled=$((compiled+1))
    # Programs that ACCEPT input get the suite's data member on stdin
    input=/dev/null
    [ -f "$SRC/$name.DAT" ] && input="$ROOT/$SRC/$name.DAT"
    # The operator's switch settings the suite expects: SWITCH-1 on
    # The object time debugging switch is on, except where the program
    # tests it off
    debug_switch=Y
    [ "$name" = DB102A ] && debug_switch=N
    # Programs of a module share their data files (one writes a file that a
    # later one reads), so they run in order in one directory; the report
    # moves to the program's own directory
    # Programs that test OPEN of an absent file (status 05 or 35 expected)
    # run with their files moved aside, restored afterwards: other programs
    # read what earlier ones wrote
    hidden=""
    if grep -q "ABSENT\|EXP. *05\|05 EXP\|EXP: 05\|NOT EXIST\|NOT PRESENT" "$cbl"; then
        # SQ203A tests its OPTIONAL files both present (XXXXX001) and absent
        keep=""
        [ "$name" = SQ203A ] && keep=XXXXX001
        for h in $(awk 'substr($0,7,1) != "*" && /SELECT/ {f=1} f && substr($0,7,1) != "*" && match(substr($0,8,65), /XXXXX[0-9][0-9][0-9]/) {print substr(substr($0,8,65), RSTART, RLENGTH); f=0}' "$cbl"); do
            [ "$h" = "$keep" ] && continue
            [ -f "$FILES/$h" ] && mv "$FILES/$h" "$FILES/$h.hidden" && hidden="$hidden $h"
        done
    fi
    (cd "$FILES" && rm -f report.log &&
     COB_SET_DEBUG=$debug_switch COB_SWITCH_1=ON timeout 10 "$ROOT/$dir/$name" < "$input" > "$ROOT/$dir/run.log" 2>&1
     [ -f report.log ] && mv report.log "$ROOT/$dir/")
    for h in $hidden; do mv -f "$FILES/$h.hidden" "$FILES/$h"; done
    if [ -f "$dir/report.log" ]; then
        ran=$((ran+1))
        # Summary lines: "093 OF 093  TESTS WERE EXECUTED SUCCESSFULLY",
        # "NO  TEST(S) FAILED" or "002 TEST(S) FAILED", likewise DELETED and
        # REQUIRE INSPECTION
        # (-a: a report can hold non-text bytes that tests wrote)
        ok=$(grep -ao "[0-9]* OF *[0-9]* *TESTS WERE EXECUTED SUCCESSFULLY" "$dir/report.log" | head -1 | awk '{print $1}')
        count() { grep -ao "\(NO\|[0-9]*\) *TEST(S) $1" "$dir/report.log" | head -1 | awk '{print ($1 == "NO") ? 0 : $1}'; }
        bad=$(count FAILED); del=$(count DELETED); insp=$(count "REQUIRE INSPECTION")
        # Programs made only of visual checks: "15 TESTS REQUIRE VISUAL INSPECTION"
        if [ -z "$ok" ]; then
            insp=$(grep -ao "[0-9]* *TESTS REQUIRE VISUAL INSPECTION" "$dir/report.log" | head -1 | awk '{print $1}')
            [ -n "$insp" ] && ok=0
        fi
        p=$((10#${ok:-0})); b=$((10#${bad:-0})); d=$((10#${del:-0})); n=$((10#${insp:-0}))
        passed=$((passed + p)); failed=$((failed + b)); deleted=$((deleted + d)); inspect=$((inspect + n))
        if [ -z "$ok" ]; then
            printf "%-8s NO-SUMMARY    (report.log has no result summary)\n" "$name"
        else
            printf "%-8s RAN           %3d passed, %3d failed, %3d deleted, %3d inspect\n" "$name" $p $b $d $n
        fi
    elif grep -aq "END OF TEST" "$dir/run.log"; then
        # Monitor ("M") programs DISPLAY their results instead of writing a report
        ran=$((ran+1))
        if grep -aq "FAIL\*" "$dir/run.log" || ! grep -aq "PASS" "$dir/run.log"; then
            failed=$((failed+1))
            printf "%-8s RAN (DISPLAY) see %s/run.log\n" "$name" "$dir"
        else
            passed=$((passed+1))
            printf "%-8s RAN (DISPLAY) PASS\n" "$name"
        fi
    elif grep -aqi "Message expected for \(above\|following\) statement" "$SRC/$name.CBL"; then
        # FIPS flagging programs check compiler messages (OBSOLETE,
        # NON-CONFORMING ...), which COBALT does not issue: not counted
        printf "%-8s FLAGGING      (checks compiler flagging messages: not applicable)\n" "$name"
    else
        printf "%-8s NO-REPORT     (program ran but wrote no report.log)\n" "$name"
    fi
done
echo
echo "$MODULE: $total programs, $compiled compiled, $ran produced a report"
echo "Individual tests: $passed passed, $failed failed, $deleted deleted, $inspect require inspection"
echo "Build time ($COMPILER${ASSEMBLER:+ with $ASSEMBLER and ld}): $build_ms ms for $total programs"
