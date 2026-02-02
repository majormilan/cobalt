/* include/ir.h */
#ifndef COBALT_IR_H
#define COBALT_IR_H

#include "ast.h"

typedef struct IrInstr IrInstr;

typedef enum {
    IR_DATA_VAR,            // var_name, size, decimal_places
    IR_DATA_LIT,            // lit_name, content, len
    IR_CALL_INIT,           // no args (runtime_init)
    IR_CALL_DISPLAY_STR,    // lit_name
    IR_CALL_DISPLAY_NUM,    // var_name, size, decimal_places
    IR_EMIT_NEWLINE,        // no args (for DISPLAY statement newline)
    IR_CALL_MOVE_ALPHA,     // dest_var, src_lit, src_len, dest_size
    IR_CALL_MOVE_NUM,       // dest_var, src_lit, src_len, dest_size, decimals
    IR_CALL_EXIT            // no args
} IrOpcode;

struct IrInstr {
    IrOpcode op;
    const char *var_name;       // or dest_var
    const char *lit_name;       // or src_lit
    const char *content;
    int lit_len;                // or src_len
    int size;                   // or dest_size
    int decimal_places;
    char pad_char;
    IrInstr *next;
};

typedef struct {
    IrInstr *head;
    IrInstr *tail;
} IrProgram;

IrProgram *lower_to_ir(AstNode *program);

void ir_free(IrProgram *ir);

#endif

