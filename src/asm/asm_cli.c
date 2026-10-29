/* src/asm/asm_cli.c: `cobalt as`, the assembler's command line.
 *
 * Accepts the nasm options the build uses, so it can stand in for nasm:
 *   cobalt as [-f elf64] [-D NAME[=value]] [-I dir/] [-MD depfile] [-MP] [-o out.o] input.asm */

#include "asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); buf = NULL; }
    if (buf) buf[len] = '\0';
    fclose(f);
    return buf;
}

static void usage(void) {
    fprintf(stderr, "usage: cobalt as [-f elf64] [-D NAME[=value]] [-I dir/] [-MD depfile] [-MP] [-o out.o] input.asm\n");
}

int asm_command(int argc, char **argv) {
    const char *input = NULL, *output = NULL, *depfile = NULL;
    bool phony = false;
    const char **defines = calloc((size_t)argc + 1, sizeof(char *));
    const char **includes = calloc((size_t)argc + 1, sizeof(char *));
    int nd = 0, ni = 0;
    for (int k = 1; k < argc; k++) {
        const char *a = argv[k];
        // option value either attached (-Dname) or as the next argument
        #define VALUE() (a[2] ? a + 2 : (k + 1 < argc ? argv[++k] : NULL))
        if (strcmp(a, "-MD") == 0 || strcmp(a, "-MF") == 0) depfile = k + 1 < argc ? argv[++k] : NULL;
        else if (strcmp(a, "-MP") == 0) phony = true;
        else if (strncmp(a, "-f", 2) == 0) {
            const char *f = VALUE();
            if (!f || strcmp(f, "elf64") != 0) { fprintf(stderr, "cobalt as: only -f elf64 is supported\n"); return 1; }
        } else if (strncmp(a, "-D", 2) == 0) defines[nd++] = VALUE();
        else if (strncmp(a, "-I", 2) == 0) includes[ni++] = VALUE();
        else if (strncmp(a, "-o", 2) == 0) output = VALUE();
        else if (a[0] == '-') { usage(); return 1; }
        else input = a;
        #undef VALUE
    }
    if (!input) { usage(); return 1; }

    char default_out[1024];
    if (!output) {
        snprintf(default_out, sizeof(default_out), "%s", input);
        char *dot = strrchr(default_out, '.');
        if (dot && !strchr(dot, '/')) strcpy(dot, ".o");
        else strncat(default_out, ".o", sizeof(default_out) - strlen(default_out) - 1);
        output = default_out;
    }
    char *source = read_file(input);
    if (!source) { fprintf(stderr, "cobalt as: cannot read %s\n", input); return 1; }

    AsmOptions opts = { includes, defines };
    AsmObject *obj = asm_assemble(source, input, &opts);
    free(source);
    if (!obj) return 1;
    if (!asm_write_elf64(obj, output)) {
        fprintf(stderr, "cobalt as: cannot write %s\n", output);
        asm_free(obj);
        return 1;
    }
    if (depfile) {
        FILE *d = fopen(depfile, "w");
        if (d) {
            const char *const *inc = asm_included_files(obj);
            fprintf(d, "%s: %s", output, input);
            for (int k = 0; inc[k]; k++) fprintf(d, " %s", inc[k]);
            fprintf(d, "\n");
            for (int k = 0; phony && inc[k]; k++) fprintf(d, "\n%s:\n", inc[k]);
            fclose(d);
        }
    }
    asm_free(obj);
    free(defines);
    free(includes);
    return 0;
}
