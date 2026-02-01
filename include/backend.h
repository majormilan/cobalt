/* include/backend.h */
#ifndef COBALT_BACKEND_H
#define COBALT_BACKEND_H

#include "ir.h"
#include "target.h"

void backend_emit_object(IrProgram *ir, const char *output_filename, TargetTriple target);

#endif
