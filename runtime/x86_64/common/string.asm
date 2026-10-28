; runtime/x86_64/common/string.asm
; STRING and UNSTRING (runtime/ABI.md section 11)

%include "runtime.inc"

section .text

global runtime_string
global runtime_unstring_start
global runtime_unstring_next

; STRING source table entry
%define S_SRC       0                   ; sending item
%define S_LEN       8
%define S_DELIM     16                  ; delimiter, or 0 for DELIMITED BY SIZE
%define S_DLEN      24
%define S_SIZE      32

; UNSTRING state block
%define U_SRC       0                   ; sending item
%define U_LEN       8
%define U_DELIMS    16                  ; delimiter table
%define U_NDELIMS   24
%define U_POS       32                  ; next position to examine (from 0)
%define U_FIELD     40                  ; out: the field found
%define U_FLEN      48
%define U_DPTR      56                  ; out: the delimiter found, or 0
%define U_DLEN      64

; UNSTRING delimiter table entry
%define D_PTR       0
%define D_LEN       8
%define D_ALL       16                  ; 1: ALL (consecutive occurrences are one)
%define D_SIZE      24

; Compare rcx bytes at rsi and rdi. Returns ZF set when equal. Uses al, rdx.
match:
    xor edx, edx
.loop:
    cmp rdx, rcx
    je .equal
    mov al, [rsi + rdx]
    cmp al, [rdi + rdx]
    jne .differ
    inc rdx
    jmp .loop
.equal:
    xor eax, eax                        ; ZF = 1
    ret
.differ:
    or eax, 1                           ; ZF = 0
    ret

; STRING: ARG0 = receiving item, ARG1 = its length, ARG2 = source table,
; ARG3 = number of entries, ARG4 = pointer (position from 1).
; Returns RET0 = the new pointer; RET1 = 1 on overflow (the pointer is out of
; range, or the receiving item is full while characters remain to be sent).
runtime_string:
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0                       ; receiving item
    mov r13, ARG1                       ; its length
    mov r14, ARG2                       ; entry
    mov r15, ARG3                       ; entries left
    mov rbx, ARG4
    dec rbx                             ; rbx = position from 0
    js .s_bad_pointer
    cmp rbx, r13
    jae .s_bad_pointer
.s_entry:
    test r15, r15
    jz .s_done
    mov rsi, [r14 + S_SRC]
    mov rbp, [r14 + S_LEN]              ; characters to send
    mov rdi, [r14 + S_DELIM]
    mov rcx, [r14 + S_DLEN]
    test rdi, rdi
    jz .s_send
    test rcx, rcx
    jz .s_send
    ; DELIMITED BY a delimiter: send up to its first occurrence
    xor r8, r8                          ; candidate position
.s_find:
    mov r9, r8
    add r9, rcx
    cmp r9, [r14 + S_LEN]
    ja .s_send                          ; not found: send the whole item
    push rsi
    add rsi, r8
    call match
    pop rsi
    je .s_found
    inc r8
    jmp .s_find
.s_found:
    mov rbp, r8
.s_send:
    xor r8, r8
.s_copy:
    cmp r8, rbp
    jae .s_next
    cmp rbx, r13
    jae .s_overflow
    mov al, [rsi + r8]
    mov [r12 + rbx], al
    inc rbx
    inc r8
    jmp .s_copy
.s_next:
    add r14, S_SIZE
    dec r15
    jmp .s_entry
.s_done:
    lea rax, [rbx + 1]
    xor edx, edx
    jmp .s_ret
.s_overflow:
    lea rax, [rbx + 1]
    mov edx, 1
    jmp .s_ret
.s_bad_pointer:
    mov rax, ARG4                       ; unchanged
    mov edx, 1
.s_ret:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; UNSTRING: ARG0 = state block, ARG1 = pointer (position from 1).
; Returns RET0 = 1 and starts the scan there, or 0 when the pointer is out of
; range (an overflow condition).
runtime_unstring_start:
    mov rax, ARG1
    dec rax
    js .u_bad
    cmp rax, [ARG0 + U_LEN]
    jae .u_bad
    mov [ARG0 + U_POS], rax
    mov eax, 1
    ret
.u_bad:
    xor eax, eax
    ret

; UNSTRING: the next field for a receiving item. ARG0 = state block,
; ARG1 = size of the receiving item (the field length without delimiters).
; Returns RET0 = 1 with U_FIELD, U_FLEN, U_DPTR and U_DLEN set, or 0 when
; the sending item has been examined completely.
runtime_unstring_next:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, ARG0                       ; state
    mov r13, [r12 + U_POS]
    mov r14, [r12 + U_LEN]
    cmp r13, r14
    jae .n_exhausted
    mov qword [r12 + U_DPTR], 0
    mov qword [r12 + U_DLEN], 0
    cmp qword [r12 + U_NDELIMS], 0
    jne .n_scan
    ; No delimiters: as many characters as the receiving item holds
    mov rax, r14
    sub rax, r13
    cmp rax, ARG1
    jbe .n_fixed
    mov rax, ARG1
.n_fixed:
    mov rcx, [r12 + U_SRC]
    add rcx, r13
    mov [r12 + U_FIELD], rcx
    mov [r12 + U_FLEN], rax
    add r13, rax
    mov [r12 + U_POS], r13
    jmp .n_found_ret

.n_scan:
    mov r15, r13                        ; candidate position
.n_position:
    cmp r15, r14
    jae .n_to_end
    mov rbx, [r12 + U_DELIMS]
    mov r8, [r12 + U_NDELIMS]
.n_delim:
    test r8, r8
    jz .n_advance
    mov rcx, [rbx + D_LEN]
    test rcx, rcx
    jz .n_next_delim
    mov rax, r15
    add rax, rcx
    cmp rax, r14
    ja .n_next_delim
    mov rsi, [r12 + U_SRC]
    add rsi, r15
    mov rdi, [rbx + D_PTR]
    call match
    je .n_matched
.n_next_delim:
    add rbx, D_SIZE
    dec r8
    jmp .n_delim
.n_advance:
    inc r15
    jmp .n_position

.n_matched:
    ; The field is [r13, r15); the delimiter is entry rbx
    mov rax, [r12 + U_SRC]
    add rax, r13
    mov [r12 + U_FIELD], rax
    mov rax, r15
    sub rax, r13
    mov [r12 + U_FLEN], rax
    mov rax, [rbx + D_PTR]
    mov [r12 + U_DPTR], rax
    mov rcx, [rbx + D_LEN]
    mov [r12 + U_DLEN], rcx
    add r15, rcx
    cmp qword [rbx + D_ALL], 0
    je .n_matched_done
.n_all:                                 ; ALL: skip further occurrences
    mov rcx, [rbx + D_LEN]
    mov rax, r15
    add rax, rcx
    cmp rax, r14
    ja .n_matched_done
    mov rsi, [r12 + U_SRC]
    add rsi, r15
    mov rdi, [rbx + D_PTR]
    call match
    jne .n_matched_done
    add r15, [rbx + D_LEN]
    jmp .n_all
.n_matched_done:
    mov [r12 + U_POS], r15
    jmp .n_found_ret

.n_to_end:
    ; No delimiter: the rest of the sending item
    mov rax, [r12 + U_SRC]
    add rax, r13
    mov [r12 + U_FIELD], rax
    mov rax, r14
    sub rax, r13
    mov [r12 + U_FLEN], rax
    mov [r12 + U_POS], r14
.n_found_ret:
    mov eax, 1
    jmp .n_ret
.n_exhausted:
    xor eax, eax
.n_ret:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
