#ifndef COBALT_TARGET_H
#define COBALT_TARGET_H

typedef enum {
    TARGET_LINUX_X86_64_ELF,
    TARGET_WINDOWS_X86_64_PE,
    TARGET_COUNT
} TargetTriple;

typedef enum {
    OBJ_ELF,
    OBJ_PE_COFF
} ObjectFormat;

typedef struct {
    TargetTriple triple;
    ObjectFormat format;
    const char *name;           // "x86_64-linux-elf", etc.
    const char *exe_suffix;     // "" or ".exe"
} TargetInfo;

extern const TargetInfo target_infos[TARGET_COUNT];

// Get info by triple enum
const TargetInfo *target_get_info(TargetTriple t);

#endif
