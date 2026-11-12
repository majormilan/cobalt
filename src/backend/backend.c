/* src/backend/backend.c */
#include "backend.h"
#include <stddef.h>
#include <stdlib.h>
#include "util.h"

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
    // generated into memory, then the pass over the text (peephole.c)
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = mem_stream_open(&buf, &len);
    if (!mem) return emit(ir, out, target);
    int status = emit(ir, mem, target);
    if (mem_stream_close(mem) != 0) status = 1;
    if (status == 0) {
        size_t n = 0;
        char *better = x86_reuse_loaded_values(buf, len, &n);
        if (better) fwrite(better, 1, n, out);
        else fwrite(buf, 1, len, out);
        free(better);
    }
    free(buf);
    return status;
}
