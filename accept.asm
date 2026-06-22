;; COBALT generated x86-64 Linux assembly

extern runtime_init
extern runtime_display_string
extern runtime_display_numeric
extern runtime_move_alphanum
extern runtime_move_numeric
extern runtime_display_fill
extern runtime_move_fill
extern runtime_display_var_string
extern runtime_accept
extern runtime_add_to
extern runtime_exit

section .bss
WS_NAME: resb 10
WS_AGE: resb 2
WS_CODE: resb 5

section .data
initlit_0: db "INIT", 0
lit_1: db "Enter your name (10 chars): ", 0
lit_2: db "Enter your age (2 digits): ", 0
lit_3: db "Hello, ", 0
lit_4: db "! You are ", 0
lit_5: db " years old.", 0
lit_6: db "Code: ", 0
newline: db 10, 0

section .text
global _start
_start:
    call runtime_init

    lea rdi, [WS_CODE]
    lea rsi, [initlit_0]
    mov rdx, 4
    mov rcx, 5
    mov r8, 0
    call runtime_move_alphanum

    lea rdi, [lit_1]
    call runtime_display_string
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    lea rdi, [WS_NAME]
    mov rsi, 10
    mov rdx, 0
    mov rcx, 0
    call runtime_accept
    lea rdi, [lit_2]
    call runtime_display_string
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    lea rdi, [WS_AGE]
    mov rsi, 2
    mov rdx, 0
    mov rcx, 0
    call runtime_accept
    lea rdi, [lit_3]
    call runtime_display_string
    lea rdi, [WS_NAME]
    mov rsi, 10
    mov rdx, 0
    call runtime_display_var_string
    lea rdi, [lit_4]
    call runtime_display_string
    lea rdi, [WS_AGE]
    mov rsi, 2
    mov rdx, 0
    mov rcx, 0
    call runtime_display_numeric
    lea rdi, [lit_5]
    call runtime_display_string
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    lea rdi, [lit_6]
    call runtime_display_string
    lea rdi, [WS_CODE]
    mov rsi, 5
    mov rdx, 0
    call runtime_display_var_string
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    call runtime_exit
