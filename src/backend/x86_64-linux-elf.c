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
    if (!esc) return NULL;
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

static char *sanitize_label_name(const char *name) {
    if (!name) return NULL;
    char *sanitized = strdup(name);
    if (!sanitized) return NULL;
    for (char *p = sanitized; *p; p++) {
        if (*p == '-') {
            *p = '_';
        }
    }
    return sanitized;
}

void backend_emit_object(IrProgram *ir, const char *filename, TargetTriple target) {
    (void)target;
    int perform_counter = 0; // unique label suffix for PERFORM TIMES loops

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
    fprintf(f, "extern runtime_display_fill\n"); // New
    fprintf(f, "extern runtime_move_fill\n");    // New
    fprintf(f, "extern runtime_display_var_string\n"); // New
    fprintf(f, "extern runtime_display_newline\n");
    fprintf(f, "extern runtime_accept\n");             // New
    fprintf(f, "extern runtime_add_to\n");
    fprintf(f, "extern runtime_sub_from\n");
    fprintf(f, "extern runtime_mul_by\n");
    fprintf(f, "extern runtime_div_into\n");
    fprintf(f, "extern runtime_mod_into\n");
    fprintf(f, "extern runtime_load_int\n");
    fprintf(f, "extern runtime_exit\n");
    fprintf(f, "\n"); // Add a single newline for readability if preferred

    fprintf(f, "section .bss\n");
    IrInstr *i = ir->head;
    while (i) {
        if (i->op == IR_DATA_VAR && i->var_name) {
            char *sanitized_name = sanitize_label_name(i->var_name);
            fprintf(f, "%s: resb %d\n", sanitized_name, i->size);
            free(sanitized_name);
        }
        i = i->next;
    }
    fprintf(f, "\n");

    fprintf(f, "section .data\n");
    i = ir->head;
    while (i) {
        if (i->op == IR_DATA_LIT && i->lit_name && i->content) {
            char *sanitized_name = sanitize_label_name(i->lit_name);
            char *esc = escape_string(i->content);
            fprintf(f, "%s: db %s, 0\n", sanitized_name, esc);
            free(esc);
            free(sanitized_name);
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
        char *sanitized_var_name = NULL;
        char *sanitized_lit_name = NULL;

        if (i->var_name) sanitized_var_name = sanitize_label_name(i->var_name);
        if (i->lit_name) sanitized_lit_name = sanitize_label_name(i->lit_name);

        switch (i->op) {
            case IR_CALL_DISPLAY_STR:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_lit_name);
                fprintf(f, "    call runtime_display_string\n");
                break;
            case IR_CALL_DISPLAY_NUM:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->decimal_places);
                fprintf(f, "    mov rcx, %d\n", i->usage_type); // Pass usage_type
                fprintf(f, "    call runtime_display_numeric\n");
                break;
            case IR_CALL_DISPLAY_FILL: // New
                fprintf(f, "    mov rdi, %d\n", i->pad_char);   // fill_char
                fprintf(f, "    mov rsi, %d\n", i->size);       // size (new)
                fprintf(f, "    mov rdx, %d\n", i->usage_type); // usage_type (new)
                fprintf(f, "    call runtime_display_fill\n");
                break;
            case IR_CALL_DISPLAY_VAR_STR:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name); // var_name
                fprintf(f, "    mov rsi, %d\n", i->size);               // size
                fprintf(f, "    mov rdx, %d\n", i->usage_type);         // usage_type
                fprintf(f, "    call runtime_display_var_string\n");
                break;
            case IR_EMIT_NEWLINE:
                fprintf(f, "    call runtime_display_newline\n\n");
                break;
            case IR_CALL_MOVE_ALPHA:
            case IR_CALL_MOVE_NUM:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    lea rsi, [%s]\n", sanitized_lit_name);
                fprintf(f, "    mov rdx, %d\n", i->lit_len);
                fprintf(f, "    mov rcx, %d\n", i->size);
                fprintf(f, "    mov r8, %d\n", i->usage_type); // Pass usage_type
                fprintf(f, "    call runtime_move_%s\n\n",
                        i->op == IR_CALL_MOVE_NUM ? "numeric" : "alphanum");
                break;
            case IR_CALL_MOVE_FILL: // New
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name); // dest_var
                fprintf(f, "    mov rsi, %d\n", i->pad_char);   // fill_char
                fprintf(f, "    mov rdx, %d\n", i->size);       // size
                fprintf(f, "    mov rcx, %d\n", i->usage_type); // usage_type
                fprintf(f, "    call runtime_move_fill\n\n");
                break;
            case IR_CALL_ACCEPT:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name); // dest_var
                fprintf(f, "    mov rsi, %d\n", i->size);               // size
                fprintf(f, "    mov rdx, %d\n", i->decimal_places);     // decimal_places
                fprintf(f, "    mov rcx, %d\n", i->usage_type);         // usage_type
                fprintf(f, "    mov r8, %d\n", (int)(unsigned char)i->pad_char); // pad_char
                fprintf(f, "    call runtime_accept\n");
                break;
            case IR_CALL_ADD_TO:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    lea rdx, [%s]\n", sanitized_lit_name);
                fprintf(f, "    mov rcx, %d\n", i->lit_len);
                fprintf(f, "    call runtime_add_to\n\n");
                break;
            case IR_CALL_SUB_FROM:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    lea rdx, [%s]\n", sanitized_lit_name);
                fprintf(f, "    mov rcx, %d\n", i->lit_len);
                fprintf(f, "    call runtime_sub_from\n\n");
                break;
            case IR_CALL_MUL_BY:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    lea rdx, [%s]\n", sanitized_lit_name);
                fprintf(f, "    mov rcx, %d\n", i->lit_len);
                fprintf(f, "    call runtime_mul_by\n\n");
                break;
            case IR_CALL_DIV_INTO:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    lea rdx, [%s]\n", sanitized_lit_name);
                fprintf(f, "    mov rcx, %d\n", i->lit_len);
                fprintf(f, "    call runtime_div_into\n\n");
                break;
            case IR_CALL_MOD_INTO:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    lea rdx, [%s]\n", sanitized_lit_name);
                fprintf(f, "    mov rcx, %d\n", i->lit_len);
                fprintf(f, "    call runtime_mod_into\n\n");
                break;
            case IR_LABEL:
                fprintf(f, "\n%s:\n", sanitized_var_name);
                break;
            case IR_CALL_PARA:
                fprintf(f, "    call %s\n\n", sanitized_var_name);
                break;
            case IR_RET:
                fprintf(f, "    ret\n\n");
                break;
            case IR_PERFORM_TIMES:
                if (!sanitized_lit_name) {
                    // Literal count: PERFORM para N TIMES
                    fprintf(f, "    mov rbx, %d\n", i->size);
                    fprintf(f, ".perform_%d:\n", perform_counter);
                    fprintf(f, "    call %s\n", sanitized_var_name);
                    fprintf(f, "    dec rbx\n");
                    fprintf(f, "    jnz .perform_%d\n\n", perform_counter);
                    perform_counter++;
                } else {
                    // Variable count: PERFORM para var TIMES
                    fprintf(f, "    lea rdi, [%s]\n", sanitized_lit_name);
                    fprintf(f, "    mov rsi, %d\n", i->lit_len);
                    fprintf(f, "    call runtime_load_int\n");
                    fprintf(f, "    mov rbx, rax\n");
                    fprintf(f, "    test rbx, rbx\n");
                    fprintf(f, "    jz .perform_%d_done\n", perform_counter);
                    fprintf(f, ".perform_%d:\n", perform_counter);
                    fprintf(f, "    call %s\n", sanitized_var_name);
                    fprintf(f, "    dec rbx\n");
                    fprintf(f, "    jnz .perform_%d\n", perform_counter);
                    fprintf(f, ".perform_%d_done:\n\n", perform_counter);
                    perform_counter++;
                }
                break;
            case IR_CALL_EXIT:
                fprintf(f, "    call runtime_exit\n");
                break;
            case IR_JUMP:
                fprintf(f, "    jmp %s\n\n", i->jump_target ? i->jump_target : "");
                break;
            case IR_CMP_JUMP_IF_FALSE: {
                // Load left operand into r11
                if (i->left_is_literal) {
                    fprintf(f, "    mov r11, %s\n", i->var_name ? i->var_name : "0");
                } else if (sanitized_var_name) {
                    fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                    fprintf(f, "    mov rsi, %d\n", i->size);
                    fprintf(f, "    call runtime_load_int\n");
                    fprintf(f, "    mov r11, rax\n");
                }
                // Load right operand into rax
                if (i->right_is_literal) {
                    fprintf(f, "    mov rax, %s\n", i->lit_name ? i->lit_name : "0");
                } else if (sanitized_lit_name) {
                    fprintf(f, "    lea rdi, [%s]\n", sanitized_lit_name);
                    fprintf(f, "    mov rsi, %d\n", i->lit_len);
                    fprintf(f, "    call runtime_load_int\n");
                }
                fprintf(f, "    cmp r11, rax\n");
                // Emit the INVERTED conditional jump (we jump when condition is FALSE)
                const char *jmp_instr = "jne"; // default: "=" inverted
                if (i->cmp_op) {
                    if      (strcmp(i->cmp_op, "=")  == 0) jmp_instr = "jne";
                    else if (strcmp(i->cmp_op, "<>") == 0) jmp_instr = "je";
                    else if (strcmp(i->cmp_op, ">")  == 0) jmp_instr = "jle";
                    else if (strcmp(i->cmp_op, "<")  == 0) jmp_instr = "jge";
                    else if (strcmp(i->cmp_op, ">=") == 0) jmp_instr = "jl";
                    else if (strcmp(i->cmp_op, "<=") == 0) jmp_instr = "jg";
                }
                fprintf(f, "    %s %s\n\n", jmp_instr, i->jump_target ? i->jump_target : "");
                break;
            }
            default:
                // Handle other IR opcodes if necessary, or do nothing for IR_DATA_VAR/IR_DATA_LIT as they are handled in .bss/.data sections
                break;
        }
        free(sanitized_var_name); // Free the sanitized name
        free(sanitized_lit_name); // Free the sanitized name
        i = i->next;
    }

    fclose(f);
}

