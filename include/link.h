/* include/link.h */
#ifndef COBALT_LINK_H
#define COBALT_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The COBALT linker: links ELF64 x86-64 relocatable objects into a static,
// non-PIE Linux executable (docs/linker.md). It replaces ld for the objects
// COBALT produces: the program's object and the runtime's.

typedef struct {
    const uint8_t *data;        // an ELF64 relocatable object in memory
    size_t size;
    const char *name;           // for messages
} LinkObject;

typedef struct {
    const char *entry;          // entry symbol, normally "_start"
    bool strip;                 // no section headers or symbol table
} LinkOptions;

// Link every object of `objects`, and each object of `library` that defines a
// symbol still needed (as an archive would), into the executable `path`.
// Reports errors on stderr and returns false when there were any.
bool link_executable(const LinkObject *objects, int nobjects,
                     const LinkObject *library, int nlibrary,
                     const LinkOptions *opts, const char *path);

// The global symbols an object defines whose names start with prefix: a
// malloc'ed list of malloc'ed names; returns how many
int link_object_globals(const LinkObject *obj, const char *prefix, char ***names);

#endif
