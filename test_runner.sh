#!/bin/bash

# COBOL E2E Test Runner
# Usage: [TARGET=<triple>] ./test_runner.sh <cbl_file>
#
# Build artifacts go to obj/test/<triple>/ and are kept for inspection.
# If tests/<name>.input exists it is fed to the program on stdin,
# otherwise stdin is empty (so ACCEPT never blocks).
# A program running longer than TEST_TIMEOUT seconds (default 10) fails.
# If tests/<name>.flags exists, its contents are extra compiler options.

if [ "$#" -ne 1 ]; then
    echo "Usage: [TARGET=<triple>] $0 <cbl_file>"
    exit 1
fi

TARGET=${TARGET:-x86_64-linux-elf}
TEST_TIMEOUT=${TEST_TIMEOUT:-10}

case "$TARGET" in
    x86_64-linux-elf) NASM_FMT=elf64 ;;
    *)
        echo "Error: Unsupported TARGET '$TARGET'"
        exit 1
        ;;
esac

CBL_FILE=$1
BASENAME=$(basename "$CBL_FILE" .cbl)
BUILD_DIR="obj/test/$TARGET"
ASM_FILE="$BUILD_DIR/${BASENAME}.asm"
OBJ_FILE="$BUILD_DIR/${BASENAME}.o"
BIN_FILE="$BUILD_DIR/${BASENAME}"
RUNTIME_LIB="obj/runtime/$TARGET/libcobrt.a"
INPUT_FILE="tests/${BASENAME}.input"
EXPECTED_FILE="tests/${BASENAME}.expected"
FLAGS_FILE="tests/${BASENAME}.flags"
FLAGS=""
[ -f "$FLAGS_FILE" ] && FLAGS=$(cat "$FLAGS_FILE")

echo "Testing $CBL_FILE ($TARGET)..."

if [ ! -f "$RUNTIME_LIB" ]; then
    echo "Error: $RUNTIME_LIB not found, run 'make runtime TARGET=$TARGET'"
    exit 1
fi

mkdir -p "$BUILD_DIR"

# 1. Compile COBOL to Assembly
./bin/cobalt -t "$TARGET" $FLAGS -i "$CBL_FILE" -o "$ASM_FILE" > /dev/null
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed for $CBL_FILE"
    exit 1
fi

# 2. Assemble Assembly to Object
nasm -f "$NASM_FMT" "$ASM_FILE" -o "$OBJ_FILE"
if [ $? -ne 0 ]; then
    echo "Error: Assembly failed for $ASM_FILE"
    exit 1
fi

# 3. Link against the target runtime
ld "$OBJ_FILE" "$RUNTIME_LIB" -o "$BIN_FILE"
if [ $? -ne 0 ]; then
    echo "Error: Linking failed"
    exit 1
fi

# 4. Run and capture output
if [ -f "$INPUT_FILE" ]; then
    OUTPUT=$(timeout "$TEST_TIMEOUT" "$BIN_FILE" < "$INPUT_FILE")
else
    OUTPUT=$(timeout "$TEST_TIMEOUT" "$BIN_FILE" < /dev/null)
fi
if [ $? -eq 124 ]; then
    echo "Error: $BIN_FILE did not finish within ${TEST_TIMEOUT}s"
    exit 1
fi

# 5. Verify Output
if [ -f "$EXPECTED_FILE" ]; then
    if diff -u "$EXPECTED_FILE" <(echo "$OUTPUT"); then
        echo "Test Passed!"
    else
        echo "Test Failed!"
        exit 1
    fi
else
    echo "No expected output file found at $EXPECTED_FILE. Output:"
    echo "$OUTPUT"
fi
