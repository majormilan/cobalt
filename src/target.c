#include "target.h"
#include <stddef.h>
const TargetInfo target_infos[TARGET_COUNT] = {
    { TARGET_LINUX_X86_64_ELF,   OBJ_ELF,     "x86_64-linux-elf",   ""      },
    { TARGET_WINDOWS_X86_64_PE,  OBJ_PE_COFF, "x86_64-pc-windows-msvc", ".exe" }
};

const TargetInfo *target_get_info(TargetTriple t) {
    if (t < TARGET_COUNT) return &target_infos[t];
    return NULL;
}
