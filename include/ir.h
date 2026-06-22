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
    IR_LABEL,               // paragraph label; var_name = label name
    IR_CALL_PARA,           // call a paragraph; var_name = paragraph name
    IR_RET,                 // return from paragraph subroutine
    IR_PERFORM_TIMES,       // loop: var_name=para, lit_name=count-var (NULL→literal), size=literal-count, lit_len=var-size
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
    AstUsageType usage_type; // For data variables: DISPLAY, COMP, etc.
    IrInstr *next;
};

typedef struct {
    IrInstr *head;
    IrInstr *tail;
} IrProgram;

IrProgram *lower_to_ir(AstNode *program);

void ir_free(IrProgram *ir);

#endif

