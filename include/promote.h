/* include/promote.h
 * Register promotion of numeric fields in loops (docs/optimization.md,
 * Phase 4). Target-independent analysis of the IR: a backend asks for the
 * loops whose fields can live in registers and does the code generation.
 */
#ifndef COBALT_PROMOTE_H
#define COBALT_PROMOTE_H

#include "ir.h"
#include <stdbool.h>

#define PROMOTE_MAX_FIELDS 8

// One promoted field: a numeric item that only the loop's own numeric loads
// and stores touch, so its value can stay in a register for the whole loop
typedef struct {
    const IrInstr *load;        // a load of the field (its size, usage, flags)
    const IrInstr *store;       // a store into it (digits, flags), or NULL
    const char *var_name;       // "%r<n>+<offset>"
    int uses;                   // loads and stores in the loop
} PromotedField;

// A loop: LABEL head ... JUMP head, entered only by falling into the head,
// left only by jumps to labels outside it. The body has no calls other than
// numeric runtime routines, so nothing else can observe the fields.
typedef struct {
    const IrInstr *head;        // the IR_LABEL
    const IrInstr *back;        // the IR_JUMP back to it
    PromotedField fields[PROMOTE_MAX_FIELDS];
    int nfields;                // ordered by uses, most used first
} PromoteLoop;

typedef struct {
    PromoteLoop *loops;
    int nloops;
} PromotePlan;

// Find the promotable loops of a program. accept(load, store) tells whether
// the backend can keep a field of that kind in a register.
PromotePlan promote_plan(const IrProgram *ir,
                         bool (*accept)(const IrInstr *load, const IrInstr *store));

void promote_free(PromotePlan *plan);

// Flow queries over the IR: labels resolved once per program
typedef struct IrFlow IrFlow;

IrFlow *ir_flow_new(const IrProgram *ir);
void ir_flow_free(IrFlow *flow);

// Whether the size error byte of the statement (cobrt_stmt_err) as it is
// after the instruction at position pos (0 = the first of the program) is
// never read: every path reaches another
// IR_SIZE_ERR_CLEAR (or the end of the run) first. Conservative: any
// instruction the analysis does not know counts as a reader.
bool ir_size_err_dead_after(IrFlow *flow, int pos);

#endif
