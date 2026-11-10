#!/bin/bash
# Arithmetic benchmark: COBALT against GnuCOBOL.
# Usage: [CASES=basic|more|all] tools/bench/arith.sh [iterations]
#        (default all cases, 2000000 iterations)
#
# For each usage (DISPLAY, COMP, COMP-3, COMP-5) and operation (ADD,
# SUBTRACT, MULTIPLY, DIVIDE, COMPUTE), integer and with two decimals,
# generates the same loop ("basic" cases), and for statements beyond one
# operation ("more" cases: several receivers, ON SIZE ERROR, 18-digit
# items, mixed usages), builds it with both compilers (GnuCOBOL with -O2,
# its best; COBALT with -D gnucobol so DISPLAY formats match), runs each
# $RUNS times (default 5) and reports the best time, process start
# included. Both must print the same result.

N=${1:-2000000}
OUT=obj/bench
mkdir -p "$OUT"
RUNS=${RUNS:-5}
best() {   # best of $RUNS runs, in microseconds; the program's output in $OUT/$2.out
    local b=999999999
    for k in $(seq $RUNS); do
        local s=$(date +%s%N)
        "$1" > "$OUT/$2.out" 2>/dev/null
        local t=$(( ($(date +%s%N) - s) / 1000 ))
        [ $t -lt $b ] && b=$t
    done
    echo $b
}
CASES=${CASES:-all}

# run_case NAME DECLARATIONS STATEMENT [AFTER-LOOP STATEMENT] [DISPLAYED ITEM]
run_case() {
    local name=$1 decls=$2 stmt=$3 after=${4:-} shown=${5:-C}
    {
        cat <<COBOL
       IDENTIFICATION DIVISION.
       PROGRAM-ID. BENCH.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  I  PIC S9(9) COMP-5.
COBOL
        printf '%s\n' "$decls"
        cat <<COBOL
       PROCEDURE DIVISION.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > $N
$stmt
           END-PERFORM.
$after
           DISPLAY $shown.
           STOP RUN.
COBOL
    } > "$OUT/$name.cbl"
    if ! ./bin/cobalt -D gnucobol -i "$OUT/$name.cbl" -o "$OUT/$name.cobalt" > "$OUT/$name.cobalt.log" 2>&1; then
        printf "%-28s COBALT compile failed\n" "$name"; return
    fi
    if ! cobc -x -O2 -o "$OUT/$name.gnucobol" "$OUT/$name.cbl" > "$OUT/$name.gnucobol.log" 2>&1; then
        printf "%-28s GnuCOBOL compile failed\n" "$name"; return
    fi
    local tc tg same ratio
    tc=$(best "$OUT/$name.cobalt" "$name.cobalt")
    tg=$(best "$OUT/$name.gnucobol" "$name.gnucobol")
    same=$(cmp -s "$OUT/$name.cobalt.out" "$OUT/$name.gnucobol.out" && echo "same" || echo "DIFFERS: $(head -c 40 $OUT/$name.cobalt.out) vs $(head -c 40 $OUT/$name.gnucobol.out)")
    ratio=$(awk -v c=$tc -v g=$tg 'BEGIN { if (c == 0) c = 1; printf "%.2fx", g / c }')
    printf "%-28s %10.2f %10.2f %8s  %s\n" "$name" $(awk -v t=$tc 'BEGIN{print t/1000}') $(awk -v t=$tg 'BEGIN{print t/1000}') "$ratio" "$same"
}

printf "%-28s %10s %10s %8s  %s\n" "case ($N iterations)" "COBALT ms" "GnuCOBOL" "ratio" "result"

if [ "$CASES" = basic ] || [ "$CASES" = all ]; then
for usage in DISPLAY COMP COMP-3 COMP-5; do
  for scale in 0 2; do
    if [ $scale = 0 ]; then pic="S9(9)"; acc="S9(17)"; a=12345; b=678; else pic="S9(7)V99"; acc="S9(15)V99"; a=123.45; b=6.78; fi
    for op in ADD SUBTRACT MULTIPLY DIVIDE COMPUTE; do
      case $op in
        ADD)      stmt="ADD A TO C" ;;
        SUBTRACT) stmt="SUBTRACT B FROM C" ;;
        MULTIPLY) stmt="MULTIPLY A BY B GIVING C" ;;
        DIVIDE)   stmt="DIVIDE B INTO A GIVING C ROUNDED" ;;
        COMPUTE)  stmt="COMPUTE C ROUNDED = (A * B + C) / 3" ;;
      esac
      run_case "${usage//-/}_${op}_s$scale" "       77  A  PIC $pic $usage VALUE $a.
       77  B  PIC $pic $usage VALUE $b.
       77  C  PIC $acc $usage VALUE 0." "               $stmt"
    done
  done
done
fi

if [ "$CASES" = more ] || [ "$CASES" = all ]; then
# Results are shown through an edited item, which both compilers format alike
for usage in DISPLAY COMP-3 COMP-5; do
  u=${usage//-/}
  common="       77  A  PIC S9(7)V99 $usage VALUE 123.45.
       77  B  PIC S9(7)V99 $usage VALUE 6.78.
       77  C  PIC S9(15)V99 $usage VALUE 0.
       77  D  PIC S9(15)V99 $usage VALUE 0.
       77  E  PIC S9(9) $usage VALUE 0.
       77  R  PIC -(15)9.99."
  run_case "${u}_GIVING2" "$common" "               ADD A B GIVING C D
               ADD C TO D" "           MOVE D TO R." R
  run_case "${u}_SIZE_ERROR" "$common
       77  S  PIC S9(5)V99 $usage VALUE 0." "               ADD A TO S ON SIZE ERROR ADD 1 TO E
               END-ADD" "           MOVE E TO R." R
  run_case "${u}_DIGITS18" "       77  A  PIC S9(18) $usage VALUE 123456789012.
       77  C  PIC S9(18) $usage VALUE 0.
       77  R  PIC -(18)9." "               ADD A TO C
               SUBTRACT 7 FROM C" "           MOVE C TO R." R
  run_case "${u}_MUL_DIV" "$common" "               COMPUTE C ROUNDED = A * B / 7 + C / 1000" "           MOVE C TO R." R
done
run_case MIXED_USAGES "       77  A  PIC S9(7)V99 COMP-3 VALUE 123.45.
       77  B  PIC S9(5)V9 VALUE 6.7.
       77  C  PIC S9(13)V99 COMP-5 VALUE 0.
       77  D  PIC S9(13)V9 COMP VALUE 0.
       77  R  PIC -(13)9.99." "               COMPUTE C = A * B + C
               ADD B TO D
               SUBTRACT A FROM D" "           MOVE C TO R." R
fi
