/* src/backend/x86_64-linux-elf.c */
#include "backend.h"
#include "ir.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

// Write a NUL-terminated data literal as a list of byte values, which is safe
// for any character (NASM strings would need escaping for quotes)
static void emit_bytes(FILE *f, const char *s) {
    fprintf(f, "db ");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) fprintf(f, "%u, ", *p);
    fprintf(f, "0\n");
}

// Assembly label for a COBOL name or IR label. COBOL names are
// case-insensitive and may clash with assembler reserved words (DS, AX, ...),
// so labels are upper-cased and prefixed with "c_"; '-' becomes '_'.
static char *sanitize_label_name(const char *name) {
    if (!name) return NULL;
    char *sanitized = malloc(strlen(name) + 3);
    if (!sanitized) return NULL;
    char *d = sanitized;
    if (name[0] == '$') {
        // Compiler-generated label: "$if_end_3" becomes "L_if_end_3"
        sprintf(sanitized, "L_%s", name + 1);
        return sanitized;
    }
    if (name[0] == '%') {
        // Record storage "%r3+12" becomes "D_3+12"; a file control block
        // "%f2" becomes "F_2"
        sprintf(sanitized, "%c_%s", name[1] == 'f' ? 'F' : 'D', name + 2);
        return sanitized;
    }
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
// - Procedures are laid out in source order and entered by jumps. Each has a
//   return slot R_<n>: PERFORM saves the slot of the range's last procedure,
//   points it at the return label and jumps to the first procedure; the end
//   of every procedure jumps through its slot when it is set.
// - rsp is 16-byte aligned everywhere (_start is entered aligned and every
//   push uses a 16-byte slot), so every runtime call is made aligned.
// - rbx holds the PERFORM ... TIMES counter. Each loop saves the previous
//   value in a stack slot, so loops nest.
// - The numeric stack machine (IR_NUM_*) keeps values on the machine stack in
//   16-byte slots, so rsp stays aligned while values are pending.
// - cobrt_tgt_err collects errors (overflow, division by zero) while one
//   receiver's value is computed; cobrt_stmt_err collects size errors of the
//   whole statement for ON SIZE ERROR.

// Load the address of a field operand into reg. Operands are static
// ("%r3+12", literal labels) or held in an address slot ("*A_5[+offset]").
static void emit_addr(FILE *f, const char *reg, const char *name) {
    if (name[0] == '*') {
        char *rest;
        long slot = strtol(name + 3, &rest, 10);    // after "*A_"
        fprintf(f, "    mov %s, [rel A_%ld]\n", reg, slot);
        if (*rest) fprintf(f, "    add %s, 0%s\n", reg, rest);
        return;
    }
    char *label = sanitize_label_name(name);
    fprintf(f, "    lea %s, [%s]\n", reg, label);
    free(label);
}

// Load a field length into reg; -(k+1) means the length is in slot N_<k>
static void emit_size(FILE *f, const char *reg, int size) {
    if (size >= 0) fprintf(f, "    mov %s, %d\n", reg, size);
    else fprintf(f, "    mov %s, [rel N_%d]\n", reg, -size - 1);
}

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

int x86_64_linux_emit(IrProgram *ir, const char *filename, const TargetInfo *target) {
    int perform_counter = 0; // unique label suffix for PERFORM return points
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
    // One return slot per procedure
    int num_procs = 0;
    for (i = ir->head; i; i = i->next)
        if (i->op == IR_PROC_ENTRY && i->imm + 1 > num_procs) num_procs = (int)i->imm + 1;
    for (int p = 0; p < num_procs; p++) fprintf(f, "R_%d: resq 1\n", p);
    // Address and length slots of field references computed at run time
    int num_slots = 0;
    for (i = ir->head; i; i = i->next)
        if (i->op == IR_ADDR_SET && i->imm + 1 > num_slots) num_slots = (int)i->imm + 1;
    for (int k = 0; k < num_slots; k++) fprintf(f, "A_%d: resq 1\nN_%d: resq 1\n", k, k);
    fprintf(f, "cobrt_io_status: resq 1\n");
    fprintf(f, "cobrt_tgt_err: resb 1\n");
    fprintf(f, "cobrt_stmt_err: resb 1\n");
    fprintf(f, "\n");

    fprintf(f, "section .data\n");
    i = ir->head;
    while (i) {
        if (i->op == IR_DATA_LIT && i->lit_name && i->content) {
            char *sanitized_name = sanitize_label_name(i->lit_name);
            fprintf(f, "%s: ", sanitized_name);
            emit_bytes(f, i->content);
            free(sanitized_name);
        }
        i = i->next;
    }
    // File control blocks (runtime/ABI.md section 7)
    for (i = ir->head; i; i = i->next) {
        if (i->op != IR_DATA_FILE) continue;
        char *fcb = sanitize_label_name(i->var_name);
        char *name = sanitize_label_name(i->lit_name);
        char *status = i->content ? sanitize_label_name(i->content) : NULL;
        fprintf(f, "align 8\n%s: dq 0, 0, %d, %d, %s, %s, 0, 0\n",
                fcb, i->size, i->flags, name, status ? status : "0");
        free(fcb);
        free(name);
        free(status);
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
                emit_addr(f, "rdi", i->lit_name);
                fprintf(f, "    call runtime_display_string\n");
                break;
            case IR_CALL_DISPLAY_NUM:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->decimal_places);
                fprintf(f, "    mov rcx, %d\n", i->usage_type);
                fprintf(f, "    mov r8, %d\n", i->flags);
                fprintf(f, "    call runtime_display_numeric\n");
                break;
            case IR_CALL_DISPLAY_FILL:
                fprintf(f, "    mov rdi, %d\n", i->pad_char);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_display_fill\n");
                break;
            case IR_CALL_DISPLAY_VAR_STR:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_display_var_string\n");
                break;
            case IR_EMIT_NEWLINE:
                fprintf(f, "    call runtime_display_newline\n\n");
                break;
            case IR_CALL_MOVE_ALPHA:
            case IR_CALL_MOVE_NUM:
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                emit_size(f, "rdx", i->lit_len);
                emit_size(f, "rcx", i->size);
                fprintf(f, "    mov r8, %d\n", i->flags);  // JUSTIFIED RIGHT
                fprintf(f, "    call runtime_move_%s\n\n",
                        i->op == IR_CALL_MOVE_NUM ? "numeric" : "alphanum");
                break;
            case IR_CALL_MOVE_FILL:
                if (i->lit_name) {
                    emit_addr(f, "rdi", i->var_name);
                    emit_size(f, "rsi", i->size);
                    emit_addr(f, "rdx", i->lit_name);
                    fprintf(f, "    mov rcx, %d\n", i->lit_len);
                    fprintf(f, "    call runtime_move_pattern\n\n");
                    break;
                }
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    mov rsi, %d\n", i->pad_char);
                emit_size(f, "rdx", i->size);
                fprintf(f, "    mov rcx, %d\n", i->usage_type);
                fprintf(f, "    call runtime_move_fill\n\n");
                break;
            case IR_CALL_ACCEPT:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    xor edx, edx\n");
                fprintf(f, "    xor ecx, ecx\n");
                fprintf(f, "    mov r8, %d\n", (int)(unsigned char)i->pad_char);
                fprintf(f, "    call runtime_accept\n");
                break;
            case IR_CALL_ACCEPT_NUM:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->digits);
                fprintf(f, "    mov rcx, %d\n", i->decimal_places);
                fprintf(f, "    mov r8, %d\n", i->usage_type);
                fprintf(f, "    mov r9, %d\n", i->flags);
                fprintf(f, "    call runtime_accept_numeric\n");
                break;
            case IR_LABEL:
                fprintf(f, "\n%s:\n", sanitized_var_name);
                break;
            case IR_PROC_ENTRY:
                fprintf(f, "\n; %s\nP_%lld:\n", i->var_name, i->imm);
                break;
            case IR_PROC_EXIT:
                // Return to the PERFORM waiting for the end of this procedure
                fprintf(f, "    mov rax, [rel R_%lld]\n", i->imm);
                fprintf(f, "    test rax, rax\n");
                fprintf(f, "    jz L_proc_end_%lld\n", i->imm);
                fprintf(f, "    jmp rax\n");
                fprintf(f, "L_proc_end_%lld:\n", i->imm);
                break;
            case IR_PERFORM:
                fprintf(f, "    mov rax, [rel R_%d]\n", i->proc_end);
                fprintf(f, "    sub rsp, 16\n");
                fprintf(f, "    mov [rsp], rax\n");
                fprintf(f, "    lea rax, [rel L_perform_%d]\n", perform_counter);
                fprintf(f, "    mov [rel R_%d], rax\n", i->proc_end);
                fprintf(f, "    jmp P_%lld\n", i->imm);
                fprintf(f, "L_perform_%d:\n", perform_counter);
                fprintf(f, "    mov rax, [rsp]\n");
                fprintf(f, "    add rsp, 16\n");
                fprintf(f, "    mov [rel R_%d], rax\n\n", i->proc_end);
                perform_counter++;
                break;
            case IR_GOTO:
                fprintf(f, "    jmp P_%lld\n\n", i->imm);
                break;
            case IR_TIMES_BEGIN:
                if (i->flags) emit_pop(f, "rax");
                else fprintf(f, "    mov rax, %lld\n", i->imm);
                fprintf(f, "    sub rsp, 16\n");
                fprintf(f, "    mov [rsp], rbx\n"); // save the outer counter
                fprintf(f, "    mov rbx, rax\n");
                fprintf(f, "    test rbx, rbx\n");
                fprintf(f, "    jle %s\n", sanitized_jump);
                fprintf(f, "%s:\n", sanitized_var_name);
                break;
            case IR_TIMES_END:
                fprintf(f, "    dec rbx\n");
                fprintf(f, "    jnz %s\n", sanitized_var_name);
                fprintf(f, "%s:\n", sanitized_jump);
                fprintf(f, "    mov rbx, [rsp]\n");
                fprintf(f, "    add rsp, 16\n\n");
                break;
            case IR_CALL_EXIT:
                fprintf(f, "    call runtime_exit\n");
                break;
            case IR_JUMP:
                fprintf(f, "    jmp %s\n\n", sanitized_jump);
                break;
            case IR_NUM_LOAD:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
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
                fprintf(f, "    jne L_store_%d\n", store_counter);
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", i->digits);
                fprintf(f, "    mov rcx, %d\n", i->usage_type);
                fprintf(f, "    mov r8, %d\n", i->flags);
                fprintf(f, "    call runtime_num_store\n");
                fprintf(f, "    or [rel cobrt_tgt_err], al\n");
                fprintf(f, "L_store_%d:\n", store_counter);
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
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                emit_addr(f, "rdx", i->lit_name);
                emit_size(f, "rcx", i->lit_len);
                fprintf(f, "    call runtime_compare_alnum\n");
                fprintf(f, "    cmp rax, 0\n");
                fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                break;
            case IR_FILL_CMP_JUMP_IF_FALSE:
                if (i->lit_name) {
                    emit_addr(f, "rdi", i->var_name);
                    emit_size(f, "rsi", i->size);
                    emit_addr(f, "rdx", i->lit_name);
                    fprintf(f, "    mov rcx, %d\n", i->lit_len);
                    fprintf(f, "    call runtime_compare_pattern\n");
                    fprintf(f, "    cmp rax, 0\n");
                    fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                    break;
                }
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %d\n", (int)(unsigned char)i->pad_char);
                fprintf(f, "    call runtime_compare_fill\n");
                fprintf(f, "    cmp rax, 0\n");
                fprintf(f, "    %s %s\n\n", false_jump(i->cmp_op), sanitized_jump);
                break;
            case IR_CLASS_JUMP:
                emit_addr(f, "rdi", i->var_name);
                emit_size(f, "rsi", i->size);
                fprintf(f, "    mov rdx, %lld\n", i->imm);
                fprintf(f, "    call runtime_class_test\n");
                fprintf(f, "    test rax, rax\n");
                fprintf(f, "    %s %s\n\n", i->flags ? "jnz" : "jz", sanitized_jump);
                break;
            case IR_ADDR_SET:
                emit_addr(f, "rax", i->var_name);
                fprintf(f, "    mov [rel A_%lld], rax\n", i->imm);
                break;
            case IR_ADDR_ADD:
                emit_pop(f, "rax");
                fprintf(f, "    dec rax\n");
                fprintf(f, "    imul rax, rax, %d\n", i->size);
                fprintf(f, "    add [rel A_%lld], rax\n", i->imm);
                break;
            case IR_LEN_SET:
                emit_pop(f, "rax");
                fprintf(f, "    mov [rel N_%lld], rax\n", i->imm);
                break;
            case IR_CALL_REPLICATE:
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    mov rsi, %d\n", i->size);
                fprintf(f, "    mov rdx, %lld\n", i->imm);
                fprintf(f, "    call runtime_replicate\n");
                break;
            case IR_FILE_OPEN:
            case IR_FILE_CLOSE:
                emit_addr(f, "rdi", i->var_name);
                fprintf(f, "    mov rsi, %lld\n", i->imm);
                fprintf(f, "    call runtime_file_%s\n", i->op == IR_FILE_OPEN ? "open" : "close");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_FILE_READ:
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                fprintf(f, "    call runtime_file_read\n");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_FILE_WRITE:
                if (i->flags) emit_pop(f, "r8");
                else fprintf(f, "    mov r8, %d\n", i->digits);
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                emit_size(f, "rdx", i->size);
                fprintf(f, "    mov rcx, %lld\n", i->imm);
                fprintf(f, "    call runtime_file_write\n");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_FILE_REWRITE:
                emit_addr(f, "rdi", i->var_name);
                emit_addr(f, "rsi", i->lit_name);
                emit_size(f, "rdx", i->size);
                fprintf(f, "    call runtime_file_rewrite\n");
                fprintf(f, "    mov [rel cobrt_io_status], rax\n\n");
                break;
            case IR_IO_JUMP:
                fprintf(f, "    mov rax, [rel cobrt_io_status]\n");
                fprintf(f, "    cmp rax, %lld\n", i->imm);
                fprintf(f, "    jl L_io_%d\n", store_counter);
                fprintf(f, "    cmp rax, %d\n", i->size);
                fprintf(f, "    jle %s\n", sanitized_jump);
                fprintf(f, "L_io_%d:\n", store_counter);
                store_counter++;
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

