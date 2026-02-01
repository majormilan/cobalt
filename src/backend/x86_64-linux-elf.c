/* src/backend/x86_64-linux-elf.c */
#include "backend.h"
#include "ir.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
static char *escape_string(const char *s) {
    if (!s) return strdup("\"\"");
    size_t len = strlen(s);
    size_t extra = 0;
    for (const char *p = s; *p; p++)
        if (*p == '"' || *p == '\\') extra++;
    char *esc = malloc(len + extra + 3);
    char *d = esc;
    *d++ = '"';
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') *d++ = '\\';
        *d++ = *p;
    }
    *d++ = '"';
    *d = '\0';
    return esc;
}

void backend_emit_object(IrProgram *ir, const char *filename, TargetTriple target) {
    (void)target;

    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open output file: %s\n", filename);
        return;
    }

    fprintf(f, ";; COBALT generated x86-64 Linux assembly\n\n");

    fprintf(f, "extern runtime_init\n");
    fprintf(f, "extern runtime_display_string\n");
    fprintf(f, "extern runtime_display_numeric\n");
    fprintf(f, "extern runtime_move_alphanum\n");
    fprintf(f, "extern runtime_move_numeric\n");
    fprintf(f, "extern runtime_exit\n\n");

    fprintf(f, "section .bss\n");
    IrInstr *i = ir->head;
    while (i) {
        if (i->op == IR_DATA_VAR && i->var_name) {
            fprintf(f, "%s resb %d\n", i->var_name, i->size);
        }
        i = i->next;
    }
    fprintf(f, "\n");

    fprintf(f, "section .data\n");
    i = ir->head;
    while (i) {
        if (i->op == IR_DATA_LIT && i->lit_name && i->content) {
            char *esc = escape_string(i->content);
            fprintf(f, "%s db %s, 0\n", i->lit_name, esc);
            free(esc);
        }
        i = i->next;
    }
    fprintf(f, "\n");

    fprintf(f, "section .text\n");
    fprintf(f, "global _start\n");
    fprintf(f, "_start:\n");
    fprintf(f, "    call runtime_init\n\n");

    i = ir->head;
    while (i) {
        switch (i->op) {
            case IR_CALL_DISPLAY_STR:
                fprintf(f, "    lea rdi, [%s]\n", i->lit_name);
                fprintf(f, "    call runtime_display_string\n\n");
                break;
            case IR_CALL_DISPLAY_NUM:
                fprintf(f, "    lea rdi, [%s]\n", i->var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->decimal_places);
                fprintf(f, "    call runtime_display_numeric\n\n");
                break;
            case IR_CALL_MOVE_ALPHA:
            case IR_CALL_MOVE_NUM:
                fprintf(f, "    lea rdi, [%s]\n", i->var_name);
                fprintf(f, "    lea rsi, [%s]\n", i->lit_name);
                fprintf(f, "    mov rdx, %d\n", i->lit_len);
                fprintf(f, "    mov rcx, %d\n", i->size);
                fprintf(f, "    call runtime_move_%s\n\n", i->op == IR_CALL_MOVE_NUM ? "numeric" : "alphanum");
                break;
            case IR_CALL_EXIT:
                fprintf(f, "    call runtime_exit\n");
                break;
            default:
                break;
        }
        i = i->next;
    }

    fclose(f);
}
