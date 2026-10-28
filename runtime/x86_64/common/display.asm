; runtime/x86_64/common/display.asm
; DISPLAY support: literals, numeric and alphanumeric fields, fills, newline.

%include "runtime.inc"

section .text

global runtime_display_string
global runtime_display_numeric
global runtime_display_var_string
global runtime_display_fill
global runtime_display_newline

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

; ARG0 = variable pointer (raw digits)
; ARG1 = total size in bytes
; ARG2 = number of decimal places
; ARG3 = usage_type
runtime_display_numeric:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push ARG0                           ; [rbp-8]  = var ptr
    push ARG1                           ; [rbp-16] = size
    push ARG2                           ; [rbp-24] = decimals
    push ARG3                           ; [rbp-32] = usage_type

    sub rsp, 64                         ; temp buffer
    mov r8, rsp                         ; r8 = temp buffer

    mov r9, [rbp-16]                    ; r9 = size (from stack)
    ; Copy raw digits
    mov rcx, r9
    mov rsi, [rbp-8]                    ; var ptr (from stack)
    mov rdi, r8
    rep movsb
    mov byte [r8 + r9], 0               ; null-terminate

    ; Insert '.' if decimals > 0
    mov rdx, [rbp-24]                   ; decimals (from stack)
    test rdx, rdx
    jz .no_dot
    mov rcx, r9                         ; original size
    sub rcx, rdx                        ; position for '.'
    cmp rcx, 0
    jle .no_dot
    mov byte [r8 + rcx], '.'
    inc r9                              ; length += 1 for '.'
.no_dot:

    ; Trim leading '0's (keep one '0' if value is zero)
    mov rsi, r8
.trim_loop:
    cmp byte [rsi], '0'
    jne .trim_done
    inc rsi
    cmp byte [rsi], 0
    jne .trim_loop
    dec rsi                             ; point back to single '0'
.trim_done:

    ; If trimmed result starts with '.', prepend '0'
    cmp byte [rsi], '.'
    jne .print
    mov byte [r8], '0'
    mov rsi, r8
    inc r9

.print:
    ; Calculate length of trimmed string
    xor rdx, rdx
.strlen_trimmed_loop:
    cmp byte [rsi + rdx], 0
    je .strlen_trimmed_done
    inc rdx
    jmp .strlen_trimmed_loop
.strlen_trimmed_done:
    mov ARG2, rdx                       ; length of trimmed string
    mov ARG1, rsi                       ; trimmed start
    mov ARG0, COBRT_STDOUT
    call cobrt_os_write

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
