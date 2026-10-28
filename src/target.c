#include "target.h"
#include <stddef.h>
#include <string.h>

const TargetInfo target_infos[TARGET_COUNT] = {
    { TARGET_LINUX_X86_64_ELF,  OBJ_ELF,     ARCH_X86_64, OS_LINUX,   ABI_SYSV,
      "x86_64-linux-elf",       "",     "elf64", "_start" },
    { TARGET_WINDOWS_X86_64_PE, OBJ_PE_COFF, ARCH_X86_64, OS_WINDOWS, ABI_WIN64,
      "x86_64-pc-windows-msvc", ".exe", "win64", "mainCRTStartup" }
};

const TargetInfo *target_get_info(TargetTriple t) {
    if (t < TARGET_COUNT) return &target_infos[t];
    return NULL;
}

const TargetInfo *target_from_name(const char *name) {
    for (int t = 0; t < TARGET_COUNT; t++) {
        if (strcmp(target_infos[t].name, name) == 0) return &target_infos[t];
    }
    return NULL;
}
