#!/bin/bash

# COBOL E2E Test Runner
# Usage: ./test_runner.sh <cbl_file>

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <cbl_file>"
    exit 1
fi

CBL_FILE=$1
BASENAME=$(basename "$CBL_FILE" .cbl)
ASM_FILE="${BASENAME}.asm"
OBJ_FILE="${BASENAME}.o"
RUNTIME_OBJ="runtime.o"
BIN_FILE="./${BASENAME}_bin"

echo "Testing $CBL_FILE..."

# 1. Compile COBOL to Assembly
./bin/cobalt -i "$CBL_FILE" -o "$ASM_FILE" > /dev/null
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed for $CBL_FILE"
    exit 1
fi

# 2. Assemble Assembly to Object
nasm -f elf64 "$ASM_FILE" -o "$OBJ_FILE"
if [ $? -ne 0 ]; then
    echo "Error: Assembly failed for $ASM_FILE"
    exit 1
fi

# 3. Ensure Runtime Object exists
if [ ! -f "$RUNTIME_OBJ" ]; then
    nasm -f elf64 "runtime/x86_64-linux-elf.asm" -o "$RUNTIME_OBJ"
fi

# 4. Link
ld "$OBJ_FILE" "$RUNTIME_OBJ" -o "$BIN_FILE"
if [ $? -ne 0 ]; then
    echo "Error: Linking failed"
    exit 1
fi

# 5. Run and capture output
OUTPUT=$($BIN_FILE)

# 6. Verify Output
EXPECTED_FILE="tests/${BASENAME}.expected"

if [ -f "$EXPECTED_FILE" ]; then
    echo "$OUTPUT" > "${BASENAME}.actual"
    # Using diff with -w to ignore whitespace issues for now, if necessary. 
    # Let's start strict and see.
    diff -u "$EXPECTED_FILE" "${BASENAME}.actual" > "${BASENAME}.diff"
    
    if [ $? -eq 0 ]; then
        echo "Test Passed!"
        rm "${BASENAME}.actual" "${BASENAME}.diff"
    else
        echo "Test Failed! Differences:"
        cat "${BASENAME}.diff"
        rm "${BASENAME}.actual" "${BASENAME}.diff"
        rm "$ASM_FILE" "$OBJ_FILE" "$BIN_FILE"
        exit 1
    fi
else
    echo "No expected output file found at $EXPECTED_FILE. Output:"
    echo "$OUTPUT"
fi

# Cleanup
rm "$ASM_FILE" "$OBJ_FILE" "$BIN_FILE"
