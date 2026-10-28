/* include/ir.h */
#ifndef COBALT_IR_H
#define COBALT_IR_H

#include "ast.h"
#include <stdbool.h>

typedef struct IrInstr IrInstr;

// Flags of numeric fields, passed to the runtime unchanged (runtime/ABI.md)
#define IR_NUM_SIGNED 1     // PIC has S
#define IR_NUM_KEEP   2     // ON SIZE ERROR given: keep the field on size error
#define IR_NUM_SIGN_LEADING  4  // DISPLAY: sign on the first digit (or before it)
#define IR_NUM_SIGN_SEPARATE 8  // DISPLAY: the sign is a separate + or - character

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
    IR_CALL_REPLICATE,      // copy the first size bytes of var_name over the
                            //   following imm - 1 occurrences
    // Sequential files (runtime/ABI.md section 7). var_name is the file
    // control block "%f<n>"; every call leaves the status code where
    // IR_IO_JUMP can test it.
    IR_DATA_FILE,           // file control block var_name: lit_name = file name literal,
                            //   size = record area size, flags = FILE_* flags,
                            //   content = FILE STATUS item (or NULL)
    IR_FILE_OPEN,           // imm = OPEN mode (1 INPUT, 2 OUTPUT, 3 I-O, 4 EXTEND)
    IR_FILE_CLOSE,
    IR_FILE_READ,           // into the record area lit_name
    IR_FILE_WRITE,          // record lit_name, size bytes; imm = advancing mode;
                            //   lines = digits, or popped from the numeric stack if flags
    IR_FILE_REWRITE,        // record lit_name, size bytes
    IR_IO_JUMP,             // jump to jump_target if imm <= status <= size
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
    IrInstr *next;
};

typedef struct {
    IrInstr *head;
    IrInstr *tail;
} IrProgram;

IrProgram *lower_to_ir(AstNode *program);

void ir_free(IrProgram *ir);

#endif

