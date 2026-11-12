/* include/backend.h */
#ifndef COBALT_BACKEND_H
#define COBALT_BACKEND_H

#include "ir.h"
#include "target.h"
#include <stdbool.h>
#include <stdio.h>

// A backend turns target-independent IR into assembly source, written to out
// (a file, or memory for the built-in assembler).
// Returns 0 on success, non-zero on failure.
typedef int (*BackendEmitFn)(IrProgram *ir, FILE *out, const TargetInfo *target);

// True if a code generator and runtime exist for this target
bool backend_supports(const TargetInfo *target);

// Dispatch to the target's backend
int backend_emit_assembly(IrProgram *ir, FILE *out, const TargetInfo *target);

// Per-target emitters (one per src/backend/<target>.c)
int x86_64_linux_emit(IrProgram *ir, FILE *out, const TargetInfo *target);

// x86-64 peephole pass over the emitted text (src/backend/peephole.c)
char *x86_reuse_loaded_values(const char *text, size_t len, size_t *out_len);

#endif
