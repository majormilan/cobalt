/* src/backend/x86_64-linux-elf.c */
#include "backend.h"
#include "ir.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

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

// Assembly label for a COBOL name or IR label. COBOL names are
// case-insensitive and may clash with assembler reserved words (DS, AX, ...),
// so labels are upper-cased and prefixed with "c_"; '-' becomes '_'.
static char *sanitize_label_name(const char *name) {
    if (!name) return NULL;
    char *sanitized = malloc(strlen(name) + 3);
    if (!sanitized) return NULL;
    char *d = sanitized;
    *d++ = 'c';
    *d++ = '_';
    for (const char *p = name; *p; p++) *d++ = (*p == '-') ? '_' : (char)toupper((unsigned char)*p);
    *d = '\0';
    return sanitized;
}

// Every routine in the runtime contract (runtime/routines.def)
static const char *const runtime_routines[] = {
#define RUNTIME_ROUTINE(name) #name,
#include "routines.def"
#undef RUNTIME_ROUTINE
};

// Stack and register discipline of generated code (see runtime/ABI.md):
// - rsp is 16-byte aligned throughout _start and every paragraph body, so
//   every runtime call is made with an aligned stack. _start is entered
//   aligned; a paragraph call pads the stack by 8 bytes so the callee is
//   entered the same way.
// - rbx holds the PERFORM ... TIMES counter. Each loop saves the previous
//   value on the stack (which also serves as the call padding), so loops nest.
// - The numeric stack machine (IR_NUM_*) keeps values on the machine stack in
//   16-byte slots, so rsp stays aligned while values are pending.
// - cobrt_tgt_err collects errors (overflow, division by zero) while one
//   receiver's value is computed; cobrt_stmt_err collects size errors of the
//   whole statement for ON SIZE ERROR.

// Numeric stack: push rax / pop into reg, one 16-byte slot per value
static void emit_push(FILE *f) {
    fprintf(f, "    sub rsp, 16\n");
    fprintf(f, "    mov [rsp], rax\n");
}

static void emit_pop(FILE *f, const char *reg) {
    fprintf(f, "    mov %s, [rsp]\n", reg);
    fprintf(f, "    add rsp, 16\n");
}

// Inverted conditional jump: taken when (a cmp_op b) is false
static const char *false_jump(const char *cmp_op) {
    if (!cmp_op || strcmp(cmp_op, "=") == 0) return "jne";
    if (strcmp(cmp_op, "<>") == 0) return "je";
    if (strcmp(cmp_op, ">")  == 0) return "jle";
    if (strcmp(cmp_op, "<")  == 0) return "jge";
    if (strcmp(cmp_op, ">=") == 0) return "jl";
    if (strcmp(cmp_op, "<=") == 0) return "jg";
    return "jne";
}

// Call a paragraph from aligned code
static void emit_para_call(FILE *f, const char *para) {
    fprintf(f, "    sub rsp, 8\n");
    fprintf(f, "    call %s\n", para);
    fprintf(f, "    add rsp, 8\n\n");
}

int x86_64_linux_emit(IrProgram *ir, const char *filename, const TargetInfo *target) {
    int perform_counter = 0; // unique label suffix for PERFORM TIMES loops
    int store_counter = 0;   // unique label suffix for numeric stores

    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open output file: %s\n", filename);
        return 1;
    }

    fprintf(f, ";; COBALT generated x86-64 Linux assembly\n\n");

    for (size_t r = 0; r < sizeof(runtime_routines) / sizeof(runtime_routines[0]); r++) {
        fprintf(f, "extern %s\n", runtime_routines[r]);
    }
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
    fprintf(f, "cobrt_tgt_err: resb 1\n");
    fprintf(f, "cobrt_stmt_err: resb 1\n");
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
    fprintf(f, "global %s\n", target->entry_symbol);
    fprintf(f, "%s:\n", target->entry_symbol);
    fprintf(f, "    call runtime_init\n\n");

    i = ir->head;

    while (i) {
        char *sanitized_var_name = NULL;
        char *sanitized_lit_name = NULL;
        char *sanitized_jump = NULL;

        if (i->var_name) sanitized_var_name = sanitize_label_name(i->var_name);
        if (i->lit_name) sanitized_lit_name = sanitize_label_name(i->lit_name);
        if (i->jump_target) sanitized_jump = sanitize_label_name(i->jump_target);

        switch (i->op) {
            case IR_CALL_DISPLAY_STR:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_lit_name);
                fprintf(f, "    call runtime_display_string\n");
                break;
            case IR_CALL_DISPLAY_NUM:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->decimal_places);
                fprintf(f, "    mov rcx, %d\n", i->usage_type);
                fprintf(f, "    mov r8, %d\n", i->flags);
                fprintf(f, "    call runtime_display_numeric\n");
                break;
            case IR_CALL_DISPLAY_FILL:
                fprintf(f, "    mov rdi, %d\n", i->pad_char);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_display_fill\n");
                break;
            case IR_CALL_DISPLAY_VAR_STR:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->usage_type);
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
                fprintf(f, "    mov r8, %d\n", i->usage_type);
                fprintf(f, "    call runtime_move_%s\n\n",
                        i->op == IR_CALL_MOVE_NUM ? "numeric" : "alphanum");
                break;
            case IR_CALL_MOVE_FILL:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->pad_char);
                fprintf(f, "    mov rdx, %d\n", i->size);
                fprintf(f, "    mov rcx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_move_fill\n\n");
                break;
            case IR_CALL_ACCEPT:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    xor edx, edx\n");
                fprintf(f, "    xor ecx, ecx\n");
                fprintf(f, "    mov r8, %d\n", (int)(unsigned char)i->pad_char);
                fprintf(f, "    call runtime_accept\n");
                break;
            case IR_CALL_ACCEPT_NUM:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->digits);
                fprintf(f, "    mov rcx, %d\n", i->decimal_places);
                fprintf(f, "    mov r8, %d\n", i->usage_type);
                fprintf(f, "    mov r9, %d\n", i->flags);
                fprintf(f, "    call runtime_accept_numeric\n");
                break;
            case IR_LABEL:
                fprintf(f, "\n%s:\n", sanitized_var_name);
                break;
            case IR_CALL_PARA:
                emit_para_call(f, sanitized_var_name);
                break;
            case IR_RET:
                fprintf(f, "    ret\n\n");
                break;
            case IR_PERFORM_TIMES:
                if (!i->flags) {
                    // Literal count: PERFORM para N TIMES
                    if (i->imm <= 0) break; // zero iterations: nothing to emit
                    fprintf(f, "    push rbx\n"); // save outer counter; pads the calls below
                    fprintf(f, "    mov rbx, %lld\n", i->imm);
                } else {
                    // Count on the numeric stack: PERFORM para var TIMES
                    emit_pop(f, "rax");
                    fprintf(f, "    push rbx\n"); // save outer counter; pads the calls below
                    fprintf(f, "    mov rbx, rax\n");
                    fprintf(f, "    test rbx, rbx\n");
                    fprintf(f, "    jle .perform_%d_done\n", perform_counter);
                }
                fprintf(f, ".perform_%d:\n", perform_counter);
                fprintf(f, "    call %s\n", sanitized_var_name);
                fprintf(f, "    dec rbx\n");
                fprintf(f, "    jnz .perform_%d\n", perform_counter);
                fprintf(f, ".perform_%d_done:\n", perform_counter);
                fprintf(f, "    pop rbx\n\n");
                perform_counter++;
                break;
            case IR_CALL_EXIT:
                fprintf(f, "    call runtime_exit\n");
                break;
            case IR_JUMP:
                fprintf(f, "    jmp %s\n\n", sanitized_jump);
                break;
            case IR_NUM_LOAD:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->usage_type);
                fprintf(f, "    mov rcx, %d\n", i->flags);
                fprintf(f, "    call runtime_num_load\n");
                emit_push(f);
                break;
            case IR_NUM_CONST:
                fprintf(f, "    mov rax, %lld\n", i->imm);
                emit_push(f);
                break;
            case IR_NUM_RESCALE:
                emit_pop(f, "rdi");
                fprintf(f, "    mov rsi, %lld\n", i->imm);
                fprintf(f, "    mov rdx, %d\n", i->flags);
                fprintf(f, "    call runtime_num_rescale\n");
                fprintf(f, "    or [rel cobrt_tgt_err], dl\n");
                emit_push(f);
                break;
            case IR_NUM_ADD:
            case IR_NUM_SUB:
                emit_pop(f, "rcx");
                emit_pop(f, "rax");
                fprintf(f, "    %s rax, rcx\n", i->op == IR_NUM_ADD ? "add" : "sub");
                fprintf(f, "    seto dl\n");
                fprintf(f, "    or [rel cobrt_tgt_err], dl\n");
                emit_push(f);
                break;
            case IR_NUM_MUL:
            case IR_NUM_DIV:
                emit_pop(f, "rsi");
                emit_pop(f, "rdi");
                fprintf(f, "    mov rdx, %lld\n", i->imm);
                fprintf(f, "    call runtime_num_%s\n", i->op == IR_NUM_MUL ? "mul" : "div");
                fprintf(f, "    or [rel cobrt_tgt_err], dl\n");
                emit_push(f);
                break;
            case IR_NUM_NEG:
                fprintf(f, "    neg qword [rsp]\n");
                break;
            case IR_NUM_STORE:
                // An error while computing the value: leave the receiver unchanged
                emit_pop(f, "r9");
                fprintf(f, "    cmp byte [rel cobrt_tgt_err], 0\n");
                fprintf(f, "    jne .store_%d_done\n", store_counter);
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %d\n", i->digits);
                fprintf(f, "    mov rcx, %d\n", i->usage_type);
                fprintf(f, "    mov r8, %d\n", i->flags);
                fprintf(f, "    call runtime_num_store\n");
                fprintf(f, "    or [rel cobrt_tgt_err], al\n");
                fprintf(f, ".store_%d_done:\n", store_counter);
                fprintf(f, "    mov al, [rel cobrt_tgt_err]\n");
                fprintf(f, "    or [rel cobrt_stmt_err], al\n");
                fprintf(f, "    mov byte [rel cobrt_tgt_err], 0\n\n");
                store_counter++;
                break;
            case IR_NUM_CMP_JUMP_IF_FALSE:
                emit_pop(f, "rcx");
                emit_pop(f, "rax");
                fprintf(f, "    cmp rax, rcx\n");
                fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                break;
            case IR_ALNUM_CMP_JUMP_IF_FALSE:
                fprintf(f, "    lea rdi, [%s]\n", sanitized_var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    lea rdx, [%s]\n", sanitized_lit_name);
                fprintf(f, "    mov rcx, %d\n", i->lit_len);
                fprintf(f, "    call runtime_compare_alnum\n");
                fprintf(f, "    cmp rax, 0\n");
                fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                break;
            case IR_SIZE_ERR_CLEAR:
                fprintf(f, "    mov byte [rel cobrt_tgt_err], 0\n");
                fprintf(f, "    mov byte [rel cobrt_stmt_err], 0\n");
                break;
            case IR_JUMP_IF_NO_SIZE_ERR:
                fprintf(f, "    cmp byte [rel cobrt_stmt_err], 0\n");
                fprintf(f, "    je %s\n", sanitized_jump);
                break;
            default:
                // Handle other IR opcodes if necessary, or do nothing for IR_DATA_VAR/IR_DATA_LIT as they are handled in .bss/.data sections
                break;
        }
        free(sanitized_var_name);
        free(sanitized_lit_name);
        free(sanitized_jump);
        i = i->next;
    }

    if (fclose(f) != 0) {
        fprintf(stderr, "ERROR: Failed to write output file: %s\n", filename);
        return 1;
    }
    return 0;
}

