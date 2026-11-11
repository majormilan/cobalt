# Code generation and optimization

How cobalt turns COBOL arithmetic, data movement and loops into fast,
compact x86-64 code, and how it keeps executables small. Results never
change: every optimization gives the values, truncation and size errors
the standard requires.

## The numeric core

Every numeric value is a signed 64-bit integer scaled by a power of ten
(`runtime/ABI.md` section 6). The IR expresses arithmetic as a stack machine
(`IR_NUM_*`); the backend runs that stack at compile time:

- **Virtual stack in registers.** Values live in callee-saved registers
  (`r12` to `r15`) across runtime calls and spill to the machine stack only
  when the registers run out or control flow joins.
- **Value bounds.** Every value carries a bound on its magnitude. An
  operation that cannot overflow 64 bits gets no overflow check
  (`S9(9) + S9(17)`), a value that fits its receiver no size check, and a
  truncation to the receiver's digits is skipped when the bound fits.
- **Constant folding.** Literals, scale factors and truncations are
  computed at compile time: `MOVE 5 TO B` is a store of the constant.
- **Division and multiplication inline.** Division by a constant (powers of
  ten for rescaling and ROUNDED, literal divisors) is a multiplication by
  the reciprocal, found at compile time for the value's range (65-bit
  multipliers included); division by a field is one `idiv`, or `mul` /
  `div` on a 128-bit dividend when 64 bits may not suffice. Only
  exponentiation calls the runtime.
- **Size-error flags tracked.** The error bytes are not tested or cleared
  while they are known to be zero; an addition's overflow jumps straight to
  its store's size-error block; the statement's flag is not maintained when
  no ON SIZE ERROR reads it. Error paths are out of line.

- **Constants across statements.** After a store of a constant that fits,
  a load of the same field gives the constant (no call), and the virtual
  stack folds what follows; a comparison of two constants becomes a jump or
  nothing. The known values are forgotten at a store into the same record,
  at any instruction that may change memory or transfer control, and at a
  label that something jumps to.
- **Loaded values reused** (`src/backend/peephole.c`, a pass over the
  generated assembly). A numeric item loaded into a callee-saved register is
  taken from that register by a later load of the same item, while nothing
  can have changed it: no write to the register, no call but the loads and
  the console output, no write to memory other than the stack, the error
  bytes and the PERFORM slots, no label, unconditional jump or return in
  between. The same pass drops instructions after an unconditional jump,
  up to the next label (branches decided by folded constants).
- **Error bytes written lazily.** The clears of the target and value bytes
  are written only before code that can set them, or a path that reads them;
  a statement byte nothing reads (no ON SIZE ERROR) is not updated.

## Storage

- **Binary items** (COMP, COMP-4, COMP-5, index-names) are loaded and stored
  inline: one `mov` (`movsx` for narrower items, `bswap` for big-endian
  COMP), with an inline limit check; a value that does not fit takes the
  runtime path, which truncates and reports the size error.
- **DISPLAY items** go through `runtime_num_load` and `runtime_num_store`,
  which convert 8 digits at a time (SWAR on 8 bytes: validated as digits,
  then combined in lanes; stores by lane-wise division), the sign digit on
  its own; invalid data takes a digit loop. The compiler calls the `_p`
  entry points, whose arguments are packed in one register.
- **Descriptors.** A load or store of a static item passes one descriptor:
  8 bytes in `.rodata`, emitted once per item and kind of call (its address
  and packed arguments), so a call loads one register.
- **PACKED-DECIMAL stores** that may not fit are one call: the runtime
  checks the value and continues into `runtime_num_store` when it does not
  fit. Out-of-line slow paths are emitted only when something jumps to them.
- **PACKED-DECIMAL items** convert 16 digits at a time
  (`runtime_packed_load`, `runtime_packed_store`), with exact reads and
  writes of 1 to 10 bytes.
- Static addresses load with `mov r32, imm32` (every address is below
  2 GiB).

## Loops

- **Register promotion** (`src/ir/promote.c`). In an innermost loop whose
  body makes no calls other than numeric ones and does not reach the
  fields' bytes otherwise (REDEFINES, subscripts into their record), the
  fields stay in registers and are written back at every exit. Loops whose
  head test is on promoted fields and constants are rotated (the test
  repeated at the bottom). Fields beyond the four registers live in 8-byte
  binary slots.
- **Final values.** A counting loop whose stores all have the form
  F := F + (invariant) runs T times and leaves F + T * invariant. The
  compiler checks at run time that the first and last values fit (every
  intermediate value is affine in the iteration number, so all then fit)
  and otherwise runs the loop.
- **Counter ranges.** `VARYING I FROM 1 BY 1 UNTIL I > N` with a known
  limit never overflows its item, so its store needs no check.

## Executable size

- The runtime is linked object by object, so a program holds only the code
  it references. The end of the run closes files through a pointer the
  first OPEN sets, and RELATIVE and INDEXED files reach their code through a
  table in their file control blocks (`runtime/ABI.md` section 7): a program
  without files links no file code.
- The symbol table names procedures and runtime routines, not the
  compiler's local labels; `--strip` removes it.
- The main program's VALUE clauses are its initial storage: its records
  are emitted with their bytes, computed when compiling as the runtime
  would store them (every usage and sign form, both sign conventions), so
  no code sets them. A called program keeps code, which CANCEL and INITIAL
  need; a record with a value the compiler cannot encode keeps code for its
  later values, so their order is kept.
- A MOVE of a literal into a small alphanumeric item stores its final bytes
  as immediates; other alphanumeric moves pass their lengths packed in one
  register (`runtime_move_alphanum_p`).
- PERFORM saves the return slot with `push` / `pop` and stores the return
  address as an immediate; a procedure end that no PERFORM range ends at has
  no return-slot test.
- A statement that moves no number clears no size-error byte, and a clear
  that no store reads is left out.

## Compile time

- Data names are found through a hash index (case-insensitive, in
  declaration order, so ambiguity rules are those of a scan).
- The assembler sends instruction lines straight to the instruction parser;
  directives are recognized with one hash lookup.
- The time grows linearly with the size of the program.

## Not done yet

- Reuse of the value a field was just stored with (a store whose value is
  known to fit).
- Removing the remaining clears of the target error byte (those before code
  that may set it).
- Constant stores into DISPLAY and PACKED-DECIMAL items as bytes computed
  at compile time.
- Inline loads and stores of short DISPLAY items.
- Sign handling specialized at compile time from the SIGN clause and
  dialect.
- Loop-invariant code motion and strength reduction of subscripts.
- Encoding instructions directly instead of through assembly text.

## Measuring

`tools/bench/arith.sh` times single arithmetic statements in loops against
GnuCOBOL; `tools/bench/compile.py` writes a large program for timing the
compiler. Every change must keep `make test` and the NIST suite unchanged.
