/* include/ir.h */
#ifndef COBALT_IR_H
#define COBALT_IR_H

#include "ast.h"
#include "config.h"
#include <stdbool.h>

typedef struct IrInstr IrInstr;

// Flags of numeric fields, passed to the runtime unchanged (runtime/ABI.md)
#define IR_NUM_SIGNED 1     // PIC has S
#define IR_NUM_KEEP   2     // ON SIZE ERROR given: keep the field on size error
#define IR_NUM_SIGN_LEADING  4  // DISPLAY: sign on the first digit (or before it)
#define IR_NUM_SIGN_SEPARATE 8  // DISPLAY: the sign is a separate + or - character
#define IR_NUM_NOTRUNC 16   // COMP: limited by its bytes, not its PICTURE digits

#define MAX_PROCS 65536     // sections and paragraphs of a program

// Program-wide runtime settings passed to runtime_init (runtime/ABI.md)
#define IR_RT_SIGN_EBCDIC 1 // overpunched signs '{' 'A'..'I' / '}' 'J'..'R'

typedef enum {
    IR_DATA_VAR,            // storage of one record: var_name ("%r<n>"), size
    IR_DATA_LIT,            // lit_name, content
    IR_CALL_INIT,           // no args (runtime_init)
    IR_CALL_DISPLAY_STR,    // lit_name
    IR_CALL_DISPLAY_VAR_STR, // var_name, size, usage_type
    IR_EMIT_NEWLINE,        // no args (for DISPLAY statement newline)
    IR_CALL_MOVE_ALPHA,     // dest var_name, src lit_name, lit_len = src length, size = dest size;
                            //   flags = 1 for a JUSTIFIED RIGHT receiver
    IR_CALL_MOVE_NUM,       // same fields: alphanumeric source into a numeric DISPLAY field
    IR_CALL_DISPLAY_FILL,   // pad_char, size
    IR_CALL_MOVE_FILL,      // var_name, pad_char, size; with lit_name/lit_len: fill with
                            //   that pattern repeated (ALL "literal")
    IR_CALL_ACCEPT,         // var_name, size (alphanumeric field)
    IR_CALL_ACCEPT_NUM,     // var_name, size, digits, decimal_places (scale), usage_type, flags
    IR_CALL_ACCEPT_DATE,    // current date or time into the "$numtext" buffer:
                            //   imm 0 DATE, 1 DAY, 2 TIME, 3 DAY-OF-WEEK
    IR_LABEL,               // branch label; var_name = label name
    IR_CALL_EXIT,           // no args
    IR_JUMP,                // unconditional jump; jump_target = label name

    // Procedures (runtime/ABI.md section 2). Procedures are laid out in source
    // order; each has a return slot that an active PERFORM sets.
    IR_PROC_ENTRY,          // start of procedure imm; var_name = its name
    IR_PROC_EXIT,           // end of procedure imm: return to a PERFORM if one waits
    IR_PERFORM,             // perform procedures imm THRU proc_end, then continue
    IR_GOTO,                // jump to procedure imm
    IR_GOTO_ALTERABLE,      // jump through the target slot of paragraph imm (initially
                            // procedure proc_end, or -1: GO TO without a procedure)
    IR_ALTER,               // set the target slot of paragraph imm to procedure proc_end
    IR_TIMES_BEGIN,         // loop var_name (label) ... jump_target (exit label);
                            //   count = imm, or popped from the numeric stack if flags
    IR_TIMES_END,           // repeat loop var_name until the count is exhausted

    // Numeric stack machine. Values are signed 64-bit integers; the scale of
    // every value is known at compile time (runtime/ABI.md section 6).
    IR_NUM_LOAD,            // push a field: var_name, size, usage_type, flags
    IR_NUM_CONST,           // push imm
    IR_NUM_RESCALE,         // top *= 10^imm (imm < 0 divides); flags = 1 to round
    IR_NUM_ADD,             // pop b, pop a, push a + b
    IR_NUM_SUB,             // pop b, pop a, push a - b
    IR_NUM_MUL,             // pop b, pop a, push (a * b) / 10^imm
    IR_NUM_DIV,             // pop b, pop a, push (a * 10^imm) / b
    IR_NUM_NEG,             // top = -top
    IR_NUM_TO_TEXT,         // pop v; write |v| as imm digits to buffer var_name
    IR_NUM_TRUNC,           // top = top rem 10^imm (keep the low-order imm digits)
    IR_NUM_DUP,             // push a copy of the top
    IR_NUM_DROP,            // pop and discard
    IR_NUM_SWAP,            // exchange the two top values
    IR_ERR_SAVE,            // remember the errors of a value used for several receivers
    IR_ERR_RESTORE,         // start a receiver with the remembered errors
    IR_NUM_STORE,           // pop into a field: var_name, size, digits, usage_type, flags;
                            //   with lit_name: edit into a numeric edited item using
                            //   that edit mask (size = mask length, flags = edit flags)
    IR_NUM_DEEDIT,          // push the value of numeric edited item var_name (size)
    IR_CALL_EDIT_ALNUM,     // edit lit_name (lit_len bytes) into var_name (size bytes)
                            //   using the alphanumeric edit mask content
    IR_NUM_CMP_JUMP_IF_FALSE,   // pop b, pop a; jump to jump_target unless (a cmp_op b)
    IR_ALNUM_CMP_JUMP_IF_FALSE, // var_name/size vs lit_name/lit_len (data labels);
                                //   jump to jump_target unless (a cmp_op b)
    IR_FILL_CMP_JUMP_IF_FALSE,  // field var_name/size vs a figurative constant
                                //   (pad_char repeated, or the lit_name/lit_len
                                //   pattern repeated); jump unless (a cmp_op b)
    IR_CLASS_JUMP,          // class test of var_name/size, class code imm; jump to
                            //   jump_target when the result equals flags (1 or 0);
                            //   class 9: members lit_name, lit_len characters
    // Field addresses computed at run time (subscripts, reference modification).
    // Field operands name their storage as "%r<record>+<offset>", or as
    // "*A_<slot>[+<offset>]" when the address is in address slot A_<slot>.
    // A negative size -(k+1) means the length is in length slot N_<k>.
    IR_ADDR_SET,            // A_<imm> = address of var_name
    IR_ADDR_ADD,            // pop v; A_<imm> += (v - 1) * size
    IR_LEN_SET,             // pop v; N_<imm> = v
    IR_SET_WHERE,           // cobrt_where = &var_name (a data literal: "FILE:LINE: run-time error in program P: ")
    IR_CHECK_RANGE,         // -fcheck: the top value v (kept) must be size..limit, limit = imm, or popped
                            //   first when flags & 1; else runtime_check_fail(var_name (where),
                            //   lit_name (what), v, size, limit)
    IR_CALL_REPLICATE,      // copy the first size bytes of var_name over the
                            //   following imm - 1 occurrences
    // Sequential files (runtime/ABI.md section 7). var_name is the file
    // control block "%f<n>"; every call leaves the status code where
    // IR_IO_JUMP can test it.
    IR_DATA_FILE,           // file control block var_name: lit_name = file name literal,
                            //   size = record area size, flags = FILE_*, ORG_* and ACC_*
                            //   bits, digits = shortest record, content = FILE STATUS
                            //   item (or NULL), jump_target = key table (or NULL),
                            //   cmp_op = LINAGE block (or NULL)
    IR_DATA_QWORDS,         // initialized qwords var_name: content = numbers and data
                            //   labels separated by spaces
    IR_FILE_OPEN,           // imm = OPEN mode (1 INPUT, 2 OUTPUT, 3 I-O, 4 EXTEND) |
                            //   options << 8 (1 REVERSED, 2 NO REWIND)
    IR_FILE_CLOSE,          // imm = options (1 REEL/UNIT, 2 NO REWIND, 4 LOCK)
    IR_FILE_READ,           // into the record area lit_name; imm = 0 next, 1 by key
    IR_FILE_DELETE,
    IR_FILE_START,          // imm = condition (0 =, 1 >, 2 >=), digits = key number,
                            //   size = key bytes compared
    IR_FILE_EOP_JUMP,       // jump to jump_target if the last WRITE raised end-of-page
    // SORT and MERGE: var_name is the sort file's control block
    IR_SORT_INIT,           // key table lit_name
    IR_SORT_RELEASE,        // record lit_name, size bytes; content: the control block
                            //   whose record length is the length instead (or NULL)
    IR_SORT_SORT,
    IR_SORT_RETURN,         // into the record area lit_name (status 10 at the end)
    IR_SORT_END,
    IR_FILE_WRITE,          // record lit_name, size bytes; imm = advancing mode;
                            //   lines = digits, or popped from the numeric stack if
                            //   flags & 1; flags & 2: the length is in the FCB
    IR_FILE_REWRITE,        // record lit_name, size bytes; flags & 2: the length is in the FCB
    IR_IO_JUMP,             // jump to jump_target if imm <= status <= size
    IR_FILE_MODE_JUMP,      // jump to jump_target unless file var_name was last opened in mode imm
    IR_DATA_BSS,            // zeroed storage var_name of size bytes
    IR_STORE_QWORD,         // store into the 8 bytes at var_name: flags 0: imm,
                            //   1: the address of lit_name, 2: the length size
    IR_CALL_INSPECT,        // subject var_name (size bytes), operand table lit_name
                            //   with imm entries, counters content (runtime/ABI.md 9)
    IR_DATA_BYTES,          // initialized data var_name: size bytes of content
    IR_CALL_SET_COLLATION,  // program collating sequence: weight table var_name
    IR_SWITCH_JUMP,         // jump to jump_target if switch imm is on (flags 1) / off (0)
    IR_NUM_POW,             // pop exponent (scale size), base (scale digits); push base ** exponent at scale imm
    IR_CALL_STRING,         // pop pointer; STRING into var_name (size bytes) from the source
                            // table lit_name (imm entries); push the new pointer, overflow
                            // in cobrt_stmt_err
    IR_CALL_UNSTRING_START, // pop pointer; start the UNSTRING state block var_name;
                            // jump to jump_target when the pointer is out of range
    IR_CALL_UNSTRING_NEXT,  // next field of state var_name for a receiver of size bytes;
                            // jump to jump_target when the sending item is exhausted
    IR_UNSTRING_SLOTS,      // A_<imm>/N_<imm> = the field (flags 0) or delimiter (flags 1)
                            // found in state var_name
    IR_UNSTRING_END,        // all receivers done: overflow if characters remain
    IR_UNSTRING_OVERFLOW,   // cobrt_stmt_err = the overflow condition of state var_name
    IR_SWITCH_SET,          // set switch imm on (flags 1) / off (0)
    IR_PROGRAM_ENTRY,       // a subprogram's entry (instead of IR_CALL_INIT): jumps to
                            //   jump_target when it needs no initialization
    IR_RESET_PERFORM,       // no PERFORM is active (the return slots are cleared)
    IR_EXIT_PROGRAM,        // return to the caller (a no-op in the main program)
    IR_CALL_ARG,            // CALL argument imm is the address of var_name
    IR_CALL_PROGRAM,        // CALL the program named by lit_name (lit_len bytes) or by
                            //   the field var_name (size), with imm arguments; status 1 in
                            //   cobrt_io_status when it does not exist and flags = 1 (ON
                            //   EXCEPTION given), else the run ends
    IR_CANCEL_PROGRAM,      // CANCEL the program named as for IR_CALL_PROGRAM
    IR_USE_ENTRY,           // the entry var_name (a global symbol) of a USE GLOBAL section,
                            //   for the programs nested in this one; then its IR_PERFORM
    IR_USE_RETURN,          //   and the return to the program whose I-O raised it
    IR_CALL_USE,            // call the USE GLOBAL entry var_name ("@symbol")
    IR_FUNC_ARG,            // argument imm of the next IR_FUNC_CALL: flags 0: the binary
                            //   value at var_name of scale digits; 1: the alphanumeric
                            //   field var_name of size bytes
    IR_FUNC_CALL,           // intrinsic function imm with size arguments into var_name
                            //   (lit_len bytes; numeric: binary of scale digits)
    IR_DEBUG_SWITCH_JUMP,   // jump to jump_target unless the object time debugging switch is on
    IR_ALTER_SLOT_JUMP,     // jump to jump_target unless paragraph imm's GO TO goes to proc_end
    IR_EXTERNAL,            // A_<imm> = the EXTERNAL storage named lit_name (lit_len bytes)
                            //   of size bytes; content: a template it starts as (or NULL)
    IR_SIZE_ERR_CLEAR,      // start of a statement that stores numeric results
    IR_JUMP_IF_NO_SIZE_ERR, // jump to jump_target if no size error since the clear
} IrOpcode;

struct IrInstr {
    IrOpcode op;
    const char *var_name;       // or dest_var, or left operand for CMP
    const char *lit_name;       // or src_lit, or right operand for CMP
    const char *content;
    int lit_len;                // or src_len, or right operand size for CMP
    int size;                   // or dest_size, or left operand size for CMP
    int decimal_places;
    int digits;                 // digit positions of a numeric field
    int flags;                  // IR_NUM_* flags, or opcode-specific flag
    long long imm;              // immediate value, shift, count or procedure
    int proc_end;               // last procedure of a PERFORM range
    char pad_char;
    AstUsageType usage_type;
    const char *cmp_op;         // comparison operator string
    const char *jump_target;    // label to jump to
    const char *symbol;         // IR_DATA_VAR, IR_DATA_FILE: also a global symbol for it
    IrInstr *next;
};

typedef struct {
    IrInstr *head;
    IrInstr *tail;
    int runtime_flags;          // IR_RT_* settings for runtime_init
    // Inter-program communication (runtime/ABI.md section 12)
    bool subprogram;            // entered by CALL (else the run unit's main program)
    char *program_name;         // PROGRAM-ID
    char *entry_symbol;         // global entry of a subprogram
    char *info_symbol;          // global program descriptor (for the program table)
    bool initial;               // INITIAL program: initialized on every CALL
    int nusing;                 // PROCEDURE DIVISION USING: address slot of each
    int using_slots[64];
    int addr_slots;             // address slots used (A_0 ...)
} IrProgram;

// How a program is compiled
typedef struct {
    bool main;                  // the run unit's main program (the executable's entry)
    AstNode *ancestors[16];     // the programs containing it, innermost first: their
    int nancestors;             //   GLOBAL items and files are visible in it
} IrUnit;

IrProgram *lower_to_ir(AstNode *program, const Dialect *dialect, const IrUnit *unit);

// -fcheck: subscripts, OCCURS DEPENDING ON values and reference modification
// are checked at run time
extern bool ir_runtime_checks;

// The symbol names of a program's entry and descriptor
void ir_program_symbols(const char *program_name, char *entry, char *info, size_t size);

void ir_free(IrProgram *ir);

#endif

