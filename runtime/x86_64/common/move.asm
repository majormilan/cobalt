; runtime/x86_64/common/move.asm
; MOVE support: alphanumeric, numeric and figurative-constant fills.

%include "runtime.inc"

section .text

global runtime_move_alphanum
global runtime_move_numeric
global runtime_move_fill

; ARG0 = dest buffer
; ARG1 = src buffer / literal
; ARG2 = src length
; ARG3 = dest size
; ARG4 = usage_type
runtime_move_alphanum:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push ARG0                           ; [rbp-8]  = dest
    push ARG1                           ; [rbp-16] = src
    push ARG2                           ; [rbp-24] = src_len
    push ARG3                           ; [rbp-32] = dest_size
    push ARG4                           ; [rbp-40] = usage_type

    cld ; Clear direction flag for rep stosb

    ; Fill with spaces
    mov al, ' '
    mov rdi, [rbp-8]                    ; dest_var from stack
    mov rcx, [rbp-32]                   ; dest_size from stack
    rep stosb

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
