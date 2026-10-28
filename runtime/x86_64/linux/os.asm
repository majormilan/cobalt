; runtime/x86_64/linux/os.asm
; OS layer for x86_64 Linux: thin wrappers around raw syscalls.

%define COBRT_OS_LAYER
%include "runtime.inc"

section .text

global cobrt_os_write
global cobrt_os_read
global cobrt_os_exit
global cobrt_os_open
global cobrt_os_close
global cobrt_os_seek
global cobrt_os_time

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

; ARG0 = path, ARG1 = OS_OPEN_* mode
cobrt_os_open:
    lea rax, [rel open_flags]
    mov rsi, [rax + ARG1*8]             ; Linux open flags for the mode
    mov rdx, 0o644                      ; permissions of a created file
    mov rax, 2                          ; sys_open
    syscall
    test rax, rax
    jns .done
    cmp rax, -2                         ; -ENOENT
    je .not_found
    mov rax, OS_ERR_OTHER
    ret
.not_found:
    mov rax, OS_ERR_NOT_FOUND
.done:
    ret

; ARG0 = handle
cobrt_os_close:
    mov rax, 3                          ; sys_close
    syscall
    ret

; ARG0 = handle, ARG1 = offset relative to the current position
cobrt_os_seek:
    mov rdx, 1                          ; SEEK_CUR
    mov rax, 8                          ; sys_lseek
    syscall
    ret

; ARG0 = buffer for seconds and nanoseconds
cobrt_os_time:
    mov rsi, ARG0
    xor edi, edi                        ; CLOCK_REALTIME
    mov rax, 228                        ; sys_clock_gettime
    syscall
    ret

section .rodata
; O_RDONLY, O_WRONLY|O_CREAT|O_TRUNC, O_RDWR, O_WRONLY|O_CREAT|O_APPEND, O_RDWR|O_CREAT
open_flags: dq 0, 0x241, 2, 0x441, 0x42
