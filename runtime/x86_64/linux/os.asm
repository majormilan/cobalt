; runtime/x86_64/linux/os.asm
; OS layer for x86_64 Linux: thin wrappers around raw syscalls.

%define COBRT_OS_LAYER
%include "runtime.inc"

section .text

global cobrt_os_write
global cobrt_os_read
global cobrt_os_exit

; ARG0 = fd, ARG1 = buf, ARG2 = len
cobrt_os_write:
    mov rax, 1                          ; sys_write
    syscall
    ret

; ARG0 = fd, ARG1 = buf, ARG2 = len
cobrt_os_read:
    xor eax, eax                        ; sys_read
    syscall
    ret

; ARG0 = status
cobrt_os_exit:
    mov rax, 60                         ; sys_exit
    syscall
