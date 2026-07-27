/* include/ir.h */
#ifndef COBALT_IR_H
#define COBALT_IR_H

#include "ast.h"
#include <stdbool.h>

typedef struct IrInstr IrInstr;

typedef enum {
    IR_DATA_VAR,            // var_name, size, decimal_places
    IR_DATA_LIT,            // lit_name, content, len
    IR_CALL_INIT,           // no args (runtime_init)
    IR_CALL_DISPLAY_STR,    // lit_name
    IR_CALL_DISPLAY_NUM,    // var_name, size, decimal_places, usage_type
    IR_CALL_DISPLAY_VAR_STR, // var_name, size, usage_type
    IR_EMIT_NEWLINE,        // no args (for DISPLAY statement newline)
    IR_CALL_MOVE_ALPHA,     // dest_var, src_lit, src_len, dest_size
    IR_CALL_MOVE_NUM,       // dest_var, src_lit, src_len, dest_size, decimals
    IR_CALL_DISPLAY_FILL,   // pad_char, size, decimal_places, usage_type
    IR_CALL_MOVE_FILL,      // dest_var, fill_char, size, usage_type
    IR_CALL_ACCEPT,         // dest_var, size, decimal_places, usage_type
    IR_CALL_ADD_TO,         // dest_var += src; var_name=dest, lit_name=src label, size=dest_size, lit_len=src_size
    IR_CALL_SUB_FROM,       // dest_var -= src; same field layout as ADD_TO
    IR_CALL_MUL_BY,         // dest_var *= src; same field layout
    IR_CALL_DIV_INTO,       // dest_var /= src (integer); same field layout
    IR_CALL_MOD_INTO,       // dest_var %= src (remainder); same field layout
    IR_LABEL,               // paragraph/branch label; var_name = label name
    IR_CALL_PARA,           // call a paragraph; var_name = paragraph name
    IR_RET,                 // return from paragraph subroutine
    IR_PERFORM_TIMES,       // loop: var_name=para, lit_name=count-var (NULL→literal), size=literal-count, lit_len=var-size
    IR_CALL_EXIT,           // no args
    IR_JUMP,                // unconditional jump; jump_target = label name
    IR_CMP_JUMP_IF_FALSE,   // compare left vs right and jump to jump_target if condition is false
                            //   var_name      = left operand (variable name, or raw literal string if left_is_literal)
                            //   size          = left variable size in bytes (0 for literals)
                            //   lit_name      = right operand (variable name, or raw literal string if right_is_literal)
                            //   lit_len       = right variable size in bytes (0 for literals)
                            //   cmp_op        = comparison operator string: "=", "<>", ">", "<", ">=", "<="
                            //   jump_target   = label to jump to when condition is FALSE
                            //   left_is_literal  = var_name is a raw immediate value, not a memory label
                            //   right_is_literal = lit_name is a raw immediate value, not a memory label
} IrOpcode;

struct IrInstr {
    IrOpcode op;
    const char *var_name;       // or dest_var, or left operand for CMP
    const char *lit_name;       // or src_lit, or right operand for CMP
    const char *content;
    int lit_len;                // or src_len, or right variable size for CMP
    int size;                   // or dest_size, or left variable size for CMP
    int decimal_places;
    char pad_char;
    AstUsageType usage_type;
    // Conditional branch fields (used by IR_CMP_JUMP_IF_FALSE and IR_JUMP)
    const char *cmp_op;         // comparison operator string
    const char *jump_target;    // label to jump to
    bool left_is_literal;       // var_name is an immediate value, not a memory address
    bool right_is_literal;      // lit_name is an immediate value, not a memory address
    IrInstr *next;
};

typedef struct {
    IrInstr *head;
    IrInstr *tail;
} IrProgram;

IrProgram *lower_to_ir(AstNode *program);

void ir_free(IrProgram *ir);

#endif

