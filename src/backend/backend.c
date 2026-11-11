/* src/backend/backend.c */
#include "backend.h"
#include <stddef.h>

static BackendEmitFn backend_for(const TargetInfo *target) {
    if (!target) return NULL;
    switch (target->triple) {
        case TARGET_LINUX_X86_64_ELF:
        case TARGET_WINDOWS_X86_64_PE: return x86_64_linux_emit;   // (the same code; the OS layer differs)
        default:                      return NULL;
    }
}

bool backend_supports(const TargetInfo *target) {
    return backend_for(target) != NULL;
}

int backend_emit_assembly(IrProgram *ir, FILE *out, const TargetInfo *target) {
    BackendEmitFn emit = backend_for(target);
    if (!emit) return 1;
    return emit(ir, out, target);
}
