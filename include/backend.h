/* include/backend.h */
#ifndef COBALT_BACKEND_H
#define COBALT_BACKEND_H

#include "ir.h"
#include "target.h"
#include <stdbool.h>

// A backend turns target-independent IR into an assembly file.
// Returns 0 on success, non-zero on failure.
typedef int (*BackendEmitFn)(IrProgram *ir, const char *output_filename, const TargetInfo *target);

// True if a code generator and runtime exist for this target
bool backend_supports(const TargetInfo *target);

// Dispatch to the target's backend
int backend_emit_object(IrProgram *ir, const char *output_filename, const TargetInfo *target);

// Per-target emitters (one per src/backend/<target>.c)
int x86_64_linux_emit(IrProgram *ir, const char *output_filename, const TargetInfo *target);

#endif
