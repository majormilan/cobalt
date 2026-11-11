#!/bin/bash
# Build and run the examples, as `make examples` does: each example
# directory is copied to obj/examples/NAME, built with the cobalt arguments
# in its build file, run, and its standard output and the files it writes
# compared with expected/stdout and expected/FILE.
# Usage: examples/run.sh [NAME...]     (default: all)
#        TARGET=x86_64-pc-windows-msvc examples/run.sh   (built for Windows)
cd "$(dirname "$0")/.." || exit 1
COBALT=${COBALT:-$PWD/bin/cobalt}
TARGET=${TARGET:-}
names=("$@")
if [ ${#names[@]} -eq 0 ]; then
    for d in examples/*/; do [ -f "$d/build" ] && names+=("$(basename "$d")"); done
fi
passed=0; failed=0
for name in "${names[@]}"; do
    src=examples/$name
    work=obj/examples/$name
    mkdir -p "$work"
    cp -r "$src"/. "$work"/
    read -r -a args < "$src/build"
    exe=$(basename "${args[0]}" .cbl)
    targs=()
    if [ -n "$TARGET" ]; then targs=(-t "$TARGET"); [[ "$TARGET" == *windows* ]] && exe="$exe.exe"; fi
    ok=1
    if ! (cd "$work" && "$COBALT" -fdiagnostics-color=never "${targs[@]}" "${args[@]}" > build.log 2>&1); then
        echo "  FAIL  $name (build)"; cat "$work/build.log"; ok=0
    elif ! (cd "$work" && timeout 20 "./$exe" > stdout.out 2>&1 < /dev/null); then
        echo "  FAIL  $name (run)"; cat "$work/stdout.out"; ok=0
    else
        for want in "$src"/expected/*; do
            file=$(basename "$want")
            got=$work/$file
            [ "$file" = stdout ] && got=$work/stdout.out
            if ! diff -u --strip-trailing-cr "$want" "$got" > "$work/$file.diff" 2>&1; then
                echo "  FAIL  $name ($file)"; cat "$work/$file.diff"; ok=0
            fi
        done
    fi
    if [ $ok -eq 1 ]; then echo "  PASS  $name"; passed=$((passed+1)); else failed=$((failed+1)); fi
done
echo "Examples: $passed passed, $failed failed"
[ $failed -eq 0 ]
