; runtime/x86_64/common/display.asm
; DISPLAY support: literals, numeric and alphanumeric fields, fills, newline.

%include "runtime.inc"

section .text

global runtime_display_string
global runtime_display_numeric
global runtime_display_var_string
global runtime_display_fill
global runtime_display_newline

extern runtime_num_load

; ARG0 = null-terminated string pointer
runtime_display_string:
    push rbp
    mov rbp, rsp
    push ARG0

    ; Compute length
    mov rsi, [rbp-8]
    xor rcx, rcx
.strlen_loop:
    cmp byte [rsi + rcx], 0
    je .strlen_done
    inc rcx
    jmp .strlen_loop
.strlen_done:
    mov ARG2, rcx                       ; length
    mov ARG1, [rbp-8]                   ; string pointer
    mov ARG0, COBRT_STDOUT
    call cobrt_os_write

    leave
    ret

; ARG0 = field ptr, ARG1 = size, ARG2 = scale (digits after the implied point)
; ARG3 = usage, ARG4 = flags
; Writes the value without leading zeros (one zero before the point), with a
; leading '-' when negative.
runtime_display_numeric:
    push rbp
    mov rbp, rsp
    push r12
    push r13
    sub rsp, 48                         ; text buffer [rbp-64, rbp-16)

    mov r12, ARG2                       ; scale
    mov rax, ARG3
    mov r10, ARG4
    mov ARG2, rax                       ; usage
    mov ARG3, r10                       ; flags
    call runtime_num_load
    mov r13, rax                        ; value

    lea rdi, [rbp-16]                   ; fill the buffer from the right
    test rax, rax
    jns .abs
    neg rax
.abs:
    mov r8, 10
    xor ecx, ecx                        ; digits written
.digit_loop:
    cmp rcx, r12
    jne .digit
    test rcx, rcx
    jz .digit
    dec rdi
    mov byte [rdi], '.'
.digit:
    xor edx, edx
    div r8
    add dl, '0'
    dec rdi
    mov [rdi], dl
    inc rcx
    test rax, rax
    jnz .digit_loop
    cmp rcx, r12
    jbe .digit_loop                     ; at least one digit before the point

    test r13, r13
    jns .print
    dec rdi
    mov byte [rdi], '-'
.print:
    lea rax, [rbp-16]
    sub rax, rdi
    mov ARG2, rax                       ; length
    mov ARG1, rdi                       ; text
    mov ARG0, COBRT_STDOUT
    call cobrt_os_write

    mov r12, [rbp-8]
    mov r13, [rbp-16]
    leave
    ret

; ARG0 = variable pointer
; ARG1 = size
; ARG2 = usage_type (currently unused)
runtime_display_var_string:
    push rbp
    mov rbp, rsp

    mov rax, ARG0
    mov ARG2, ARG1                      ; length = size
    mov ARG1, rax                       ; buffer = variable
    mov ARG0, COBRT_STDOUT
    call cobrt_os_write

    leave
    ret

; ARG0 = fill_char
; ARG1 = size
; ARG2 = usage_type
runtime_display_fill:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push ARG0                           ; [rbp-8]  = fill_char
    push ARG1                           ; [rbp-16] = size
    push ARG2                           ; [rbp-24] = usage_type

    ; Create a temporary buffer on the stack for the output characters
    ; We need 'size' bytes for chars + 1 for null terminator
    mov r8, [rbp-16]                    ; get size
    add r8, 1                           ; add 1 for null terminator
    sub rsp, r8                         ; allocate buffer on stack
    mov rdi, rsp                        ; rdi = pointer to buffer

    ; Fill the buffer with fill_char
    mov al, [rbp-8]                     ; get fill_char
    mov rcx, [rbp-16]                   ; get size
    cld                                 ; clear direction flag
    rep stosb                           ; fill buffer
    mov byte [rdi], 0                   ; null-terminate after fill

    mov ARG2, [rbp-16]                  ; length is size
    mov ARG1, rsp                       ; pointer to buffer
    mov ARG0, COBRT_STDOUT
    call cobrt_os_write

    leave                               ; also releases the temp buffer
    ret

; Ends the current DISPLAY line.
runtime_display_newline:
    push rbp
    mov rbp, rsp

    mov ARG2, 1
    lea ARG1, [rel newline]
    mov ARG0, COBRT_STDOUT
    call cobrt_os_write

    leave
    ret

section .rodata
newline db 10
