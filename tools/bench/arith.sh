#!/bin/bash
# Arithmetic benchmark: COBALT against GnuCOBOL.
# Usage: tools/bench/arith.sh [iterations]      (default 2000000)
#
# For each usage (DISPLAY, COMP, COMP-3, COMP-5) and operation (ADD,
# SUBTRACT, MULTIPLY, DIVIDE, COMPUTE), integer and with two decimals,
# generates the same loop, builds it with both compilers (GnuCOBOL with -O2,
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
printf "%-28s %10s %10s %8s  %s\n" "case ($N iterations)" "COBALT ms" "GnuCOBOL" "ratio" "result"
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
      name="${usage//-/}_${op}_s$scale"
      cat > "$OUT/$name.cbl" <<COBOL
       IDENTIFICATION DIVISION.
       PROGRAM-ID. BENCH.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       77  I  PIC S9(9) COMP-5.
       77  A  PIC $pic $usage VALUE $a.
       77  B  PIC $pic $usage VALUE $b.
       77  C  PIC $acc $usage VALUE 0.
       PROCEDURE DIVISION.
           PERFORM VARYING I FROM 1 BY 1 UNTIL I > $N
               $stmt
           END-PERFORM.
           DISPLAY C.
           STOP RUN.
COBOL
      if ! ./bin/cobalt -D gnucobol -i "$OUT/$name.cbl" -o "$OUT/$name.cobalt" > "$OUT/$name.cobalt.log" 2>&1; then
          printf "%-28s COBALT compile failed\n" "$name"; continue
      fi
      if ! cobc -x -O2 -o "$OUT/$name.gnucobol" "$OUT/$name.cbl" > "$OUT/$name.gnucobol.log" 2>&1; then
          printf "%-28s GnuCOBOL compile failed\n" "$name"; continue
      fi
      tc=$(best "$OUT/$name.cobalt" "$name.cobalt")
      tg=$(best "$OUT/$name.gnucobol" "$name.gnucobol")
      same=$(cmp -s "$OUT/$name.cobalt.out" "$OUT/$name.gnucobol.out" && echo "same" || echo "DIFFERS: $(head -c 40 $OUT/$name.cobalt.out) vs $(head -c 40 $OUT/$name.gnucobol.out)")
      ratio=$(awk -v c=$tc -v g=$tg 'BEGIN { if (c == 0) c = 1; printf "%.2fx", g / c }')
      printf "%-28s %10.2f %10.2f %8s  %s\n" "$name" $(awk -v t=$tc 'BEGIN{print t/1000}') $(awk -v t=$tg 'BEGIN{print t/1000}') "$ratio" "$same"
    done
  done
done
