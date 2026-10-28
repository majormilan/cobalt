; runtime/x86_64/common/control.asm
; Program startup and termination.

%include "runtime.inc"

section .text

global runtime_init
global runtime_exit
global runtime_switch_status

; ARG0 = stack pointer at program entry (for the environment)
runtime_init:
    jmp cobrt_os_init

; ARG0 = switch number (1..9). Returns RET0 = 1 if switch n is on: the
; environment variable COB_SWITCH_n is ON.
runtime_switch_status:
    sub rsp, 24
    mov rax, 'COB_SWIT'
    mov [rsp], rax
    mov dword [rsp + 8], 'CH_'
    mov rax, ARG0
    add al, '0'
    mov [rsp + 11], al
    mov byte [rsp + 12], 0
    mov ARG0, rsp
    call cobrt_os_getenv
    add rsp, 24
    test rax, rax
    jz .off
    cmp byte [rax], 'O'
    jne .off
    cmp byte [rax + 1], 'N'
    jne .off
    cmp byte [rax + 2], 0
    jne .off
    mov eax, 1
    ret
.off:
    xor eax, eax
    ret

; Terminates the program with status 0.
runtime_exit:
    xor ARG0, ARG0
    call cobrt_os_exit
