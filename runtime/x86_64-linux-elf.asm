; runtime/x86_64-linux-elf.asm - Fully fixed COBOL runtime
; Assemble: nasm -f elf64 runtime/x86_64-linux-elf.asm -o runtime.o

section .text

global runtime_init
global runtime_display_string
global runtime_display_numeric
global runtime_move_alphanum
global runtime_move_numeric
global runtime_exit

runtime_init:
    ret

; rdi = null-terminated string pointer
runtime_display_string:
    push rdi

    ; Compute length
    xor rcx, rcx
.strlen_loop:
    cmp byte [rdi + rcx], 0
    je .strlen_done
    inc rcx
    jmp .strlen_loop
.strlen_done:
    mov rdx, rcx                        ; rdx = length

    mov rax, 1                          ; sys_write
    mov rdi, 1                          ; stdout
    pop rsi                             ; string pointer
    syscall

    ; Newline
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    ret

; rdi = variable pointer (raw digits)
; rsi = total size in bytes
; rdx = decimal places
runtime_display_numeric:
    push rbp
    mov rbp, rsp
    push rdi                            ; save var pointer
    push rsi                            ; save size
    push rdx                            ; save decimals

    sub rsp, 64                         ; temp buffer (max 32 digits + . + \0)
    mov r8, rsp                         ; r8 = temp buffer

    ; Copy raw digits
    mov rcx, rsi                        ; size
    mov rsi, rdi                        ; var pointer
    mov rdi, r8
    rep movsb
    mov byte [r8 + rsi], 0              ; null-terminate

    ; Insert '.' if decimals > 0
    pop rdx                             ; decimals
    test rdx, rdx
    jz .no_dot
    mov rcx, rsi                        ; size
    sub rcx, rdx                        ; position for '.'
    cmp rcx, 0
    jle .no_dot
    mov byte [r8 + rcx], '.'
.no_dot:

    ; Trim leading '0's (keep at least one '0' if value is zero)
    mov rsi, r8
.trim_loop:
    cmp byte [rsi], '0'
    jne .trim_done
    inc rsi
    cmp byte [rsi], 0
    jne .trim_loop
    dec rsi                             ; point to single '0'
.trim_done:

    ; If after trim we have '.' as first char, prepend '0'
    cmp byte [rsi], '.'
    jne .no_prepend_zero
    mov byte [r8], '0'
    mov rsi, r8

.no_prepend_zero:
    ; Print trimmed string
    mov rdi, rsi
    xor rcx, rcx
.strlen_num:
    cmp byte [rdi + rcx], 0
    je .print_num
    inc rcx
    jmp .strlen_num
.print_num:
    mov rdx, rcx
    mov rax, 1
    mov rdi, 1
    syscall

    ; Newline
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    leave
    ret

; rdi = dest buffer
; rsi = src buffer/literal
; rdx = src len
; rcx = dest size
runtime_move_alphanum:
    push rbp
    mov rbp, rsp

    ; Copy min(src_len, dest_size)
    cmp rdx, rcx
    cmovbe rcx, rdx
    rep movsb

    ; Pad right with spaces
    mov rcx, [rbp + 32]                 ; original dest_size (pushed rcx)
    sub rcx, rdx
    jle .done_alpha
    mov al, ' '
    rep stosb

.done_alpha:
    leave
    ret

; rdi = dest buffer
; rsi = src digits/literal
; rdx = src len
; rcx = dest size
runtime_move_numeric:
    push rbp
    mov rbp, rsp
    push rdi                            ; save dest

    ; Fill entire dest with '0'
    mov al, '0'
    rep stosb

    ; Right-align src
    pop rdi                             ; dest start
    add rdi, rcx                        ; dest + size
    sub rdi, rdx                        ; dest + (size - src_len)
    mov rcx, rdx
    rep movsb

    leave
    ret

runtime_exit:
    mov rax, 60
    xor rdi, rdi
    syscall

section .data
newline db 10, 0
