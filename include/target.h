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

typedef enum {
    ARCH_X86_64
} TargetArch;

typedef enum {
    OS_LINUX,
    OS_WINDOWS
} TargetOs;

typedef enum {
    ABI_SYSV,               // System V AMD64
    ABI_WIN64               // Microsoft x64
} TargetAbi;

// Keep in sync with the TARGET mapping in the Makefile, which selects the
// runtime layers (runtime/<arch>/common + runtime/<arch>/<os>) per triple.
typedef struct {
    TargetTriple triple;
    ObjectFormat format;
    TargetArch arch;
    TargetOs os;
    TargetAbi abi;
    const char *name;           // "x86_64-linux-elf", etc.
    const char *exe_suffix;     // "" or ".exe"
    const char *nasm_format;    // value for nasm -f
    const char *entry_symbol;   // program entry point emitted by the backend
} TargetInfo;

extern const TargetInfo target_infos[TARGET_COUNT];

// Get info by triple enum
const TargetInfo *target_get_info(TargetTriple t);

// Get info by triple name; NULL if the name is unknown
const TargetInfo *target_from_name(const char *name);

#endif
