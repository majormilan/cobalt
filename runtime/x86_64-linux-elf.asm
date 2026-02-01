; runtime/x86_64-linux-elf.asm
; COBOL runtime for x86_64 Linux ELF
; Updated: newline after each display, proper trimming, correct decimal placement

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
    push rbp
    mov rbp, rsp
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

    ; Newline after string
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    leave
    ret

; rdi = variable pointer (raw digits)
; rsi = total size in bytes
; rdx = number of decimal places
runtime_display_numeric:
    push rbp
    mov rbp, rsp
    push rdi                            ; [rbp-8]  = var ptr
    push rsi                            ; [rbp-16] = size
    push rdx                            ; [rbp-24] = decimals

    sub rsp, 64                         ; temp buffer
    mov r8, rsp                         ; r8 = temp buffer

    mov r9, [rbp-16]                    ; r9 = original size

    ; Copy raw digits
    mov rcx, r9
    mov rsi, [rbp-8]                    ; var ptr
    mov rdi, r8
    rep movsb
    mov byte [r8 + r9], 0               ; null-terminate

    ; Insert '.' if decimals > 0
    mov rdx, [rbp-24]                   ; decimals
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
    ; Print trimmed string
    mov rdx, r9                         ; updated length
    mov rax, 1
    mov rdi, 1
    syscall

    ; Newline after number
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    leave
    ret

; rdi = dest buffer
; rsi = src buffer / literal
; rdx = src length
; rcx = dest size
runtime_move_alphanum:
    push rbp
    mov rbp, rsp
    push rdi                            ; save dest

    ; Fill with spaces
    mov al, ' '
    push rcx
    rep stosb
    pop rcx

    ; Copy source
    pop rdi                             ; dest
    cmp rdx, rcx
    cmovbe rcx, rdx
    rep movsb

    leave
    ret

; rdi = dest buffer
; rsi = src digits / literal
; rdx = src length
; rcx = dest size
runtime_move_numeric:
    push rbp
    mov rbp, rsp
    push rdi                            ; save dest

    ; Fill with '0'
    mov al, '0'
    push rcx
    rep stosb
    pop rcx

    ; Right-align source
    pop rdi                             ; dest
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
