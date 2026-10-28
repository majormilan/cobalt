; runtime/x86_64/common/inspect.asm
; INSPECT TALLYING / REPLACING / CONVERTING (runtime/ABI.md section 9)

%include "runtime.inc"

section .text

global runtime_inspect

; Operand table entry (one per TALLYING / REPLACING operand)
%define E_KIND      0                   ; 0 CHARACTERS, 1 ALL, 2 LEADING, 3 FIRST
%define E_COUNTER   8                   ; TALLYING counter index, or -1 to replace
%define E_PAT       16                  ; pattern (not for CHARACTERS)
%define E_PLEN      24
%define E_REPL      32                  ; replacement (plen characters)
%define E_BEFORE    40                  ; BEFORE INITIAL delimiter, or 0
%define E_BLEN      48
%define E_AFTER     56                  ; AFTER INITIAL delimiter, or 0
%define E_ALEN      64
%define E_START     72                  ; region, set here
%define E_END       80
%define E_STATE     88                  ; LEADING: next position expected (-1 done); FIRST: 1 done
%define E_SIZE      96

%define K_CHARACTERS 0
%define K_ALL        1
%define K_LEADING    2
%define K_FIRST      3

; Position of the first occurrence of rdx (rcx bytes) in r12 (r13 bytes),
; or -1. Uses rax, r8, r9, r10.
find:
    xor eax, eax
.f_pos:
    lea r8, [rax + rcx]
    cmp r8, r13
    ja .f_none
    xor r9d, r9d
.f_cmp:
    cmp r9, rcx
    jae .f_found
    lea r8, [rax + r9]
    mov r10b, [r12 + r8]
    cmp r10b, [rdx + r9]
    jne .f_next
    inc r9
    jmp .f_cmp
.f_next:
    inc rax
    jmp .f_pos
.f_none:
    mov rax, -1
.f_found:
    ret

; ARG0 = subject, ARG1 = subject length, ARG2 = operand table,
; ARG3 = number of entries, ARG4 = counters (int64 per TALLYING counter)
runtime_inspect:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0                       ; subject
    mov r13, ARG1                       ; length
    mov r14, ARG2                       ; table
    mov r15, ARG3                       ; entries
    mov [rbp - 48], ARG4                ; counters

    ; Regions from the BEFORE / AFTER delimiters (fixed before the scan)
    xor ebx, ebx
.prep:
    cmp rbx, r15
    jae .scan_start
    imul rsi, rbx, E_SIZE
    add rsi, r14                        ; entry
    mov qword [rsi + E_START], 0
    mov [rsi + E_END], r13
    mov rdx, [rsi + E_AFTER]
    test rdx, rdx
    jz .no_after
    mov rcx, [rsi + E_ALEN]
    call find
    cmp rax, -1
    jne .after_found
    mov [rsi + E_START], r13            ; delimiter absent: empty region
    jmp .no_after
.after_found:
    add rax, [rsi + E_ALEN]
    mov [rsi + E_START], rax
.no_after:
    mov rdx, [rsi + E_BEFORE]
    test rdx, rdx
    jz .no_before
    mov rcx, [rsi + E_BLEN]
    call find
    cmp rax, -1
    je .no_before
    mov [rsi + E_END], rax
.no_before:
    mov qword [rsi + E_STATE], 0
    cmp qword [rsi + E_KIND], K_LEADING
    jne .prep_next
    mov rax, [rsi + E_START]
    mov [rsi + E_STATE], rax
.prep_next:
    inc rbx
    jmp .prep

    ; Scan: at each position the first operand that matches is applied
.scan_start:
    xor edi, edi                        ; rdi = position
.scan:
    cmp rdi, r13
    jae .done
    xor ebx, ebx                        ; entry index
.try:
    cmp rbx, r15
    jae .no_match
    imul rsi, rbx, E_SIZE
    add rsi, r14
    cmp rdi, [rsi + E_START]
    jb .try_next
    cmp rdi, [rsi + E_END]
    jae .try_next
    mov rax, [rsi + E_KIND]
    cmp rax, K_LEADING
    jne .not_leading
    cmp rdi, [rsi + E_STATE]            ; LEADING: contiguous from the region start
    jne .try_next
.not_leading:
    cmp rax, K_FIRST
    jne .not_first
    cmp qword [rsi + E_STATE], 1
    je .try_next
.not_first:
    mov rcx, 1                          ; CHARACTERS: one character
    cmp rax, K_CHARACTERS
    je .fits
    mov rcx, [rsi + E_PLEN]
.fits:
    lea rdx, [rdi + rcx]
    cmp rdx, [rsi + E_END]
    ja .mismatch
    cmp rax, K_CHARACTERS
    je .matched
    mov rdx, [rsi + E_PAT]
    xor r8d, r8d
.cmp:
    cmp r8, rcx
    jae .matched
    lea r9, [rdi + r8]
    mov r10b, [r12 + r9]
    cmp r10b, [rdx + r8]
    jne .mismatch
    inc r8
    jmp .cmp
.mismatch:
    cmp rax, K_LEADING
    jne .try_next
    mov qword [rsi + E_STATE], -1       ; LEADING ends at the first mismatch
.try_next:
    inc rbx
    jmp .try

.matched:
    mov rdx, [rsi + E_COUNTER]
    cmp rdx, -1
    je .replace
    mov r8, [rbp - 48]
    inc qword [r8 + rdx*8]
    jmp .advance
.replace:
    mov rdx, [rsi + E_REPL]
    xor r8d, r8d
.copy:
    cmp r8, rcx
    jae .advance
    mov r10b, [rdx + r8]
    lea r9, [rdi + r8]
    mov [r12 + r9], r10b
    inc r8
    jmp .copy
.advance:
    lea rdx, [rdi + rcx]
    cmp rax, K_LEADING
    jne .adv_first
    mov [rsi + E_STATE], rdx
.adv_first:
    cmp rax, K_FIRST
    jne .adv_pos
    mov qword [rsi + E_STATE], 1
.adv_pos:
    mov rdi, rdx
    jmp .scan
.no_match:
    inc rdi
    jmp .scan

.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
