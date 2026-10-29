# Optimization roadmap: beating GnuCOBOL at arithmetic

Goal: every case of `tools/bench/arith.sh` faster in COBALT than in GnuCOBOL
compiled with `-O2` (its best), with identical results.

## Baseline (2026-10-29)

`tools/bench/arith.sh`: 2,000,000 iterations of one statement in a
`PERFORM VARYING` loop; best of three runs; ratio = GnuCOBOL time / COBALT
time (above 1: COBALT faster). Integer items are `S9(9)` with an `S9(17)`
result, decimal items `S9(7)V99` with an `S9(15)V99` result. DIVIDE and
COMPUTE use ROUNDED.

| Usage | Scale | ADD | SUBTRACT | MULTIPLY | DIVIDE | COMPUTE |
|---|---|---|---|---|---|---|
| DISPLAY | 0 | 0.80 | 0.73 | 0.93 | 1.52 | 1.60 |
| DISPLAY | 2 | 0.87 | 0.83 | 0.98 | 1.55 | 1.59 |
| COMP | 0 | 1.00 | 1.01 | 0.80 | 2.24 | 2.28 |
| COMP | 2 | 1.00 | 1.06 | 1.24 | 2.46 | 2.47 |
| COMP-3 | 0 | 0.20 | 0.18 | 0.61 | 1.39 | 1.33 |
| COMP-3 | 2 | 0.39 | 0.36 | 0.70 | 1.37 | 1.43 |
| COMP-5 | 0 | 0.02 | 0.04 | 0.77 | 2.19 | 2.13 |
| COMP-5 | 2 | 1.00 | 1.07 | 1.24 | 2.33 | 2.53 |

All results agree (COMP-5 differs only in how GnuCOBOL formats DISPLAY of
COMP-5 items). COBALT already wins DIVIDE and COMPUTE everywhere: its 64-bit
scaled integers beat GnuCOBOL's general decimal arithmetic. It loses simple
ADD / SUBTRACT, most of all on COMP-3 (GnuCOBOL adds packed decimal
directly) and COMP-5 (GnuCOBOL emits C that gcc turns into one register
addition, and for this loop removes the loop altogether).

## Why COBALT is slow on simple statements

One `ADD A TO C` on COMP-5 items, plus the loop control, executes about 540
instructions per iteration:

- every operand is loaded and every result stored by a runtime call
  (`runtime_num_load` / `runtime_num_store`), each with five or six
  argument registers to set up, even for native binary items;
- values pass through a numeric stack in memory: each push is
  `sub rsp, 16 / mov [rsp], rax`, each pop the reverse;
- size-error flags are cleared, set and merged in memory around every store;
- the `PERFORM VARYING` counter is loaded, compared, incremented and stored
  the same way;
- conversions of DISPLAY and COMP-3 items run generic digit loops with a
  `div` instruction per digit.

## Plan

Each phase is measured with `tools/bench/arith.sh` and must keep the test
suite and the NIST results unchanged.

### Phase 0: tooling

- Linker: give `.text` symbols a type (function) and a size, so profilers
  (callgrind, perf) can name the runtime routines.
- `tools/bench/arith.sh`: more cases (MULTIPLY / DIVIDE with GIVING into
  several receivers, ON SIZE ERROR, large `S9(18)` items, mixed usages).

### Phase 1: native binary inline (COMP-5, index-names, COMP)

- Load COMP-5 with one `mov` (`movsx` for narrower items) and store with one
  `mov` after an inline digit-limit check; COMP the same plus `bswap`.
- Arithmetic of binary operands with immediates for literals
  (`add rax, 12345`), no runtime call.
- Keep `PERFORM VARYING` / `TIMES` counters of binary items in callee-saved
  registers (`rbx`, `r12` to `r15`) within the loop when the body allows.

Expected: COMP and COMP-5 ADD / SUBTRACT / MULTIPLY several times faster.

### Phase 2: register-based expressions

- Replace the memory numeric stack with register allocation over the
  expression tree (Sethi-Ullman numbering; spill to the stack only when the
  registers run out), keeping values in callee-saved registers across
  runtime calls.
- Compile-time range analysis: skip size-error checks and truncation when
  the operands' digits cannot overflow the receiver
  (`S9(9) + S9(9)` into `S9(17)`); keep the flags in a register, not memory.
- Fold constants and scale factors at compile time; multiply by a constant
  power of ten with `lea` / `imul`, divide by constants with
  multiply-by-reciprocal.

### Phase 3: decimal storage without the generic runtime

- DISPLAY: inline load of n digits (two digits per step with a 100-entry
  table, or SWAR on 8 bytes at once), inline store with reciprocal division
  by 100 instead of `div` per digit.
- COMP-3: two digits per byte through a 256-entry table on load, and byte
  pairs on store; ADD / SUBTRACT of packed items directly in BCD
  (byte-wise with carry) when scales match, like GnuCOBOL.
- Sign handling (overpunch, separate, packed nibble) specialized at compile
  time from the item's SIGN clause and dialect.

### Phase 4: IR optimizer

- Redundant load / store elimination: a field stored and loaded again in the
  same basic block is reused from its register.
- Register promotion of WORKING-STORAGE items inside loops whose body has
  no calls, aliasing (REDEFINES, reference modification) or I/O, storing
  back at the loop exit: this is what gcc does for GnuCOBOL's COMP-5 loop.
- Loop-invariant code motion and strength reduction of subscript
  arithmetic.

## Target

| Phase | Cases expected to turn faster than GnuCOBOL |
|---|---|
| 1 | COMP and COMP-5 ADD, SUBTRACT, MULTIPLY (except loops gcc removes) |
| 2 | everything else involving binary items; DISPLAY MULTIPLY |
| 3 | DISPLAY and COMP-3 ADD, SUBTRACT, MULTIPLY |
| 4 | COMP-5 ADD / SUBTRACT in tight loops |
