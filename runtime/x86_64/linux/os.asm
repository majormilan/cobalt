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
global cobrt_os_init
global cobrt_os_getenv
global cobrt_os_alloc
global cobrt_os_free
global cobrt_os_pread
global cobrt_os_pwrite
global cobrt_os_size
global cobrt_os_truncate
global cobrt_os_unlink
global cobrt_os_rename

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
    cmp rax, -13                        ; -EACCES
    je .permission
    mov rax, OS_ERR_OTHER
    ret
.permission:
    mov rax, OS_ERR_PERMISSION
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

; ARG0 = size; returns zeroed memory, or 0
cobrt_os_alloc:
    mov rsi, ARG0                       ; length
    xor edi, edi                        ; any address
    mov edx, 3                          ; PROT_READ | PROT_WRITE
    mov r10d, 0x22                      ; MAP_PRIVATE | MAP_ANONYMOUS
    mov r8, -1
    xor r9d, r9d
    mov eax, 9                          ; sys_mmap
    syscall
    cmp rax, -4096
    jb .ok
    xor eax, eax
.ok:
    ret

; ARG0 = memory from cobrt_os_alloc, ARG1 = its size
cobrt_os_free:
    mov eax, 11                         ; sys_munmap
    syscall
    ret

; ARG0 = handle, ARG1 = buf, ARG2 = len, ARG3 = file offset
cobrt_os_pread:
    mov r10, ARG3
    mov eax, 17                         ; sys_pread64
    syscall
    ret

; ARG0 = handle, ARG1 = buf, ARG2 = len, ARG3 = file offset
cobrt_os_pwrite:
    mov r10, ARG3
    mov eax, 18                         ; sys_pwrite64
    syscall
    ret

; ARG0 = handle; returns the file size, or <0
cobrt_os_size:
    xor esi, esi
    mov edx, 2                          ; SEEK_END
    mov eax, 8                          ; sys_lseek
    syscall
    ret

; ARG0 = handle, ARG1 = new size
cobrt_os_truncate:
    mov eax, 77                         ; sys_ftruncate
    syscall
    ret

; ARG0 = path
cobrt_os_unlink:
    mov eax, 87                         ; sys_unlink
    syscall
    ret

; ARG0 = old path, ARG1 = new path
cobrt_os_rename:
    mov eax, 82                         ; sys_rename
    syscall
    ret

; ARG0 = buffer for seconds and nanoseconds
cobrt_os_time:
    mov rsi, ARG0
    xor edi, edi                        ; CLOCK_REALTIME
    mov rax, 228                        ; sys_clock_gettime
    syscall
    ret

; ARG0 = stack pointer at _start: argc, argv[argc], NULL, envp..., NULL
cobrt_os_init:
    mov rax, [ARG0]                     ; argc
    lea rax, [ARG0 + rax*8 + 16]        ; envp
    mov [rel environment], rax
    ret

; ARG0 = name; returns the value of NAME=value in the environment, or 0
cobrt_os_getenv:
    mov r8, [rel environment]
    test r8, r8
    jz .none
.next_var:
    mov r9, [r8]
    test r9, r9
    jz .none
    xor ecx, ecx
.cmp:
    mov al, [ARG0 + rcx]
    test al, al
    jz .name_end
    cmp al, [r9 + rcx]
    jne .skip
    inc rcx
    jmp .cmp
.name_end:
    cmp byte [r9 + rcx], '='
    jne .skip
    lea rax, [r9 + rcx + 1]
    ret
.skip:
    add r8, 8
    jmp .next_var
.none:
    xor eax, eax
    ret

section .bss
environment: resq 1

section .rodata
; O_RDONLY, O_WRONLY|O_CREAT|O_TRUNC, O_RDWR, O_WRONLY|O_CREAT|O_APPEND, O_RDWR|O_CREAT,
; O_WRONLY|O_APPEND, O_RDWR|O_CREAT|O_TRUNC
open_flags: dq 0, 0x241, 2, 0x441, 0x42, 0x401, 0x242
