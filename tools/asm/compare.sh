#!/bin/bash
# Compare cobalt's assembler (`cobalt as`) with nasm: assemble each file with both and diff the
# code (bytes and relocations), the data sections and the section sizes.
# Usage: tools/asm/compare.sh file.asm...   (runtime files get the runtime's
# -D / -I options). Prints one line per file and a summary; the first
# differences of a file are kept in obj/asmcmp/<name>.diff.

OUT=obj/asmcmp
mkdir -p "$OUT"
same=0; differ=0; failed=0
for f in "$@"; do
    name=$(basename "$f" .asm)
    flags=""
    case "$f" in runtime/x86_64/*) flags="-D ABI_SYSV -I runtime/x86_64/" ;; esac
    if ! nasm -f elf64 $flags "$f" -o "$OUT/$name.nasm.o" 2> "$OUT/$name.nasm.err"; then
        echo "$name: nasm failed"; failed=$((failed+1)); continue
    fi
    if ! ./bin/cobalt as -f elf64 $flags "$f" -o "$OUT/$name.cas.o" 2> "$OUT/$name.cas.err"; then
        echo "$name: cobalt as failed: $(head -1 "$OUT/$name.cas.err")"; failed=$((failed+1)); continue
    fi
    dump() {
        objdump -d -r -w "$1" | tail -n +4
        objdump -s -j .data -j .rodata "$1" 2>/dev/null | tail -n +4
        objdump -h -w "$1" | awk '$2 ~ /^\.(text|data|bss|rodata)$/ {print $2, $3}' | sort
    }
    if diff <(dump "$OUT/$name.nasm.o") <(dump "$OUT/$name.cas.o") > "$OUT/$name.diff"; then
        same=$((same+1)); rm -f "$OUT/$name.diff"
    else
        echo "$name: differs ($(grep -c '^<' "$OUT/$name.diff") lines, see $OUT/$name.diff)"; differ=$((differ+1))
    fi
done
echo "identical: $same, different: $differ, failed: $failed"
