; runtime/x86_64/common/move.asm
; MOVE support: alphanumeric, numeric and figurative-constant fills.

%include "runtime.inc"

section .text

global runtime_move_alphanum
global runtime_move_numeric
global runtime_move_fill
global runtime_replicate
global runtime_move_pattern

; ARG0 = dest buffer
; ARG1 = src buffer / literal
; ARG2 = src length
; ARG3 = dest size
; ARG4 = 1 for a JUSTIFIED RIGHT receiver (right-aligned, truncated on the left)
runtime_move_alphanum:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push ARG0                           ; [rbp-8]  = dest
    push ARG1                           ; [rbp-16] = src
    push ARG2                           ; [rbp-24] = src_len
    push ARG3                           ; [rbp-32] = dest_size
    push ARG4                           ; [rbp-40] = justified

    cld ; Clear direction flag for rep stosb

    ; Fill with spaces
    mov al, ' '
    mov rdi, [rbp-8]                    ; dest_var from stack
    mov rcx, [rbp-32]                   ; dest_size from stack
    rep stosb

    cmp qword [rbp-40], 0
    je .left
    ; JUSTIFIED RIGHT: the rightmost min(src_len, dest_size) characters of the
    ; source end at the right edge of the receiver
    mov rcx, [rbp-24]                   ; src_len
    mov rdx, [rbp-32]                   ; dest_size
    mov rsi, [rbp-16]
    mov rdi, [rbp-8]
    cmp rcx, rdx
    jbe .short_src
    add rsi, rcx                        ; long source: drop its leftmost characters
    sub rsi, rdx
    mov rcx, rdx
    rep movsb
    leave
    ret
.short_src:
    add rdi, rdx                        ; short source: pad on the left
    sub rdi, rcx
    rep movsb
    leave
    ret

.left:
    ; Copy source
    mov rdi, [rbp-8]                    ; dest_var
    mov rsi, [rbp-16]                   ; src_buffer
    mov rdx, [rbp-24]                   ; src_len
    mov rcx, [rbp-32]                   ; dest_size
    cmp rdx, rcx
    cmovbe rcx, rdx                     ; rcx = min(src_len, dest_size)
    rep movsb

    leave
    ret

; ARG0 = dest buffer
; ARG1 = src digits / literal
; ARG2 = src length
; ARG3 = dest size
; ARG4 = usage_type
runtime_move_numeric:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push ARG0                           ; [rbp-8]  = dest
    push ARG1                           ; [rbp-16] = src
    push ARG2                           ; [rbp-24] = src_len
    push ARG3                           ; [rbp-32] = dest_size
    push ARG4                           ; [rbp-40] = usage_type

    cld ; Clear direction flag for rep stosb

    ; Fill with '0'
    mov al, '0'
    mov rdi, [rbp-8]                    ; dest_var from stack
    mov rcx, [rbp-32]                   ; dest_size from stack
    rep stosb

    ; Right-align source
    mov rdi, [rbp-8]                    ; dest_var
    add rdi, [rbp-32]                   ; dest + dest_size
    sub rdi, [rbp-24]                   ; dest + (dest_size - src_len)
    mov rsi, [rbp-16]                   ; src_buffer
    mov rcx, [rbp-24]                   ; src_len
    rep movsb

    leave
    ret

; ARG0 = dest_var
; ARG1 = fill_char
; ARG2 = size
; ARG3 = usage_type
runtime_move_fill:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push ARG0                           ; [rbp-8]  = dest_var
    push ARG1                           ; [rbp-16] = fill_char
    push ARG2                           ; [rbp-24] = size
    push ARG3                           ; [rbp-32] = usage_type

    cld ; Clear direction flag for rep stosb

    mov rdi, [rbp-8]                    ; dest_var from stack
    mov al, [rbp-16]                    ; fill_char from stack
    mov rcx, [rbp-24]                   ; size from stack
    rep stosb

    leave
    ret

; Copy the first occurrence of a table over the following ones.
; ARG0 = first occurrence, ARG1 = occurrence size, ARG2 = number of occurrences
runtime_replicate:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    cmp r9, 1
    jbe .done
    dec r9
    imul r9, r11                        ; bytes to fill after the first occurrence
    xor ecx, ecx
.copy:
    cmp rcx, r9
    jae .done
    mov al, [r10]                       ; each byte repeats one occurrence later
    mov [r10 + r11], al
    inc r10
    inc rcx
    jmp .copy
.done:
    ret

; Fill a field with a pattern repeated (MOVE ALL "literal").
; ARG0 = field, ARG1 = field size, ARG2 = pattern, ARG3 = pattern length
runtime_move_pattern:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    mov r8, ARG3
    xor ecx, ecx                        ; field index
    xor edx, edx                        ; pattern index
.p_loop:
    cmp rcx, r11
    jae .p_done
    mov al, [r9 + rdx]
    mov [r10 + rcx], al
    inc rcx
    inc rdx
    cmp rdx, r8
    jb .p_loop
    xor edx, edx
    jmp .p_loop
.p_done:
    ret
