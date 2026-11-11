; runtime/x86_64/windows/os.asm
; OS layer for x86-64 Windows: the OS interface (runtime.inc) on kernel32.
;
; Generated code and the arch layer use the System V convention on every
; target (runtime/ABI.md section 1); this layer converts at the kernel32
; boundary: Win64 argument registers (rcx, rdx, r8, r9, then the stack), 32
; bytes of shadow space, a 16-byte aligned stack. kernel32 keeps rbx, rbp,
; rsi, rdi and r12-r15, so nothing the System V caller relies on changes.
; Imported functions are called through their import address table slots,
; __imp_<name>, which the linker builds (docs/linker.md).

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

extern __imp_GetStdHandle
extern __imp_WriteFile
extern __imp_ReadFile
extern __imp_ExitProcess
extern __imp_CreateFileA
extern __imp_CloseHandle
extern __imp_SetFilePointerEx
extern __imp_GetFileSizeEx
extern __imp_SetEndOfFile
extern __imp_DeleteFileA
extern __imp_MoveFileExA
extern __imp_GetSystemTimeAsFileTime
extern __imp_GetEnvironmentVariableA
extern __imp_GetProcessHeap
extern __imp_HeapAlloc
extern __imp_HeapFree
extern __imp_GetLastError

; A frame for calling kernel32: the stack aligned, shadow space and room
; for stack arguments ([rsp+32], [rsp+40], [rsp+48]), locals at [rsp+64]..
; [rsp+120], an OVERLAPPED structure at [rsp+128]
%define FRAME 192
%macro ENTER_WIN 0
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, FRAME
%endmacro
%macro LEAVE_WIN 0
    mov rsp, rbp
    pop rbp
%endmacro
%define L0 [rsp + 64]
%define L1 [rsp + 72]
%define L2 [rsp + 80]
%define L3 [rsp + 88]
%define L4 [rsp + 96]
%define L5 [rsp + 104]
%define L6 [rsp + 112]

; ARG0 = fd (0-2: the standard handles) or a handle: the handle in rax
%macro HANDLE_OF 1
    mov rax, %1
    cmp rax, 3
    jae %%done
    lea r10, [rel std_handles]
    mov rax, [r10 + rax*8]
%%done:
%endmacro

; ARG0 = stack pointer at entry (not used: the environment comes from
; GetEnvironmentVariableA). The standard handles.
cobrt_os_init:
    ENTER_WIN
    mov ecx, -10                        ; STD_INPUT_HANDLE
    call [rel __imp_GetStdHandle]
    mov [rel std_handles], rax
    mov ecx, -11                        ; STD_OUTPUT_HANDLE
    call [rel __imp_GetStdHandle]
    mov [rel std_handles + 8], rax
    mov ecx, -12                        ; STD_ERROR_HANDLE
    call [rel __imp_GetStdHandle]
    mov [rel std_handles + 16], rax
    call [rel __imp_GetProcessHeap]
    mov [rel process_heap], rax
    LEAVE_WIN
    ret

; ARG0 = fd, ARG1 = buf, ARG2 = len -> bytes written, or <0
cobrt_os_write:
    ENTER_WIN
    HANDLE_OF ARG0
    mov rcx, rax
    mov r8, ARG2                        ; (ARG2 is rdx: before rdx is set)
    mov rdx, ARG1
    lea r9, L0                          ; bytes written
    mov qword L0, 0
    mov qword [rsp + 32], 0             ; no OVERLAPPED
    call [rel __imp_WriteFile]
    test eax, eax
    jz .error
    mov rax, L0
    LEAVE_WIN
    ret
.error:
    mov rax, -1
    LEAVE_WIN
    ret

; ARG0 = fd, ARG1 = buf, ARG2 = len -> bytes read, 0 at the end, or <0.
; From the standard input, carriage returns are dropped (CR LF lines).
cobrt_os_read:
    ENTER_WIN
    mov L1, ARG0
    mov L2, ARG1
    mov L3, ARG2
    HANDLE_OF ARG0
    mov rcx, rax
    mov r8, ARG2
    mov rdx, ARG1
    lea r9, L0                          ; bytes read
    mov qword L0, 0
    mov qword [rsp + 32], 0
    call [rel __imp_ReadFile]
    test eax, eax
    jz .error
    mov rax, L0
    cmp qword L1, 0
    jne .done
    ; drop CR characters from what the standard input gave
    mov rsi, L2
    mov rdi, rsi
    mov rcx, rax
.strip:
    test rcx, rcx
    jz .stripped
    mov dl, [rsi]
    inc rsi
    dec rcx
    cmp dl, 13
    je .strip
    mov [rdi], dl
    inc rdi
    jmp .strip
.stripped:
    mov rax, rdi
    sub rax, L2
    test rax, rax
    jnz .done
    cmp qword L0, 0                     ; only CRs: read again
    je .done
    mov rdi, L1
    mov rsi, L2
    mov rdx, L3
    LEAVE_WIN
    jmp cobrt_os_read
.done:
    LEAVE_WIN
    ret
.error:
    call [rel __imp_GetLastError]
    cmp eax, 109                        ; ERROR_BROKEN_PIPE: the end of a pipe
    je .eof
    mov rax, -1
    LEAVE_WIN
    ret
.eof:
    xor eax, eax
    LEAVE_WIN
    ret

; ARG0 = status
cobrt_os_exit:
    ENTER_WIN
    mov rcx, ARG0
    call [rel __imp_ExitProcess]
    int3

; The OS_ERR_* code of the last error
os_error:
    sub rsp, 40
    call [rel __imp_GetLastError]
    add rsp, 40
    cmp eax, 2                          ; ERROR_FILE_NOT_FOUND
    je .not_found
    cmp eax, 3                          ; ERROR_PATH_NOT_FOUND
    je .not_found
    cmp eax, 5                          ; ERROR_ACCESS_DENIED
    je .permission
    cmp eax, 32                         ; ERROR_SHARING_VIOLATION
    je .permission
    mov rax, OS_ERR_OTHER
    ret
.not_found:
    mov rax, OS_ERR_NOT_FOUND
    ret
.permission:
    mov rax, OS_ERR_PERMISSION
    ret

; ARG0 = path, ARG1 = OS_OPEN_* mode -> handle, or OS_ERR_*
cobrt_os_open:
    ENTER_WIN
    mov L1, ARG1
    lea rax, [rel open_access]
    mov edx, [rax + ARG1*4]             ; dwDesiredAccess
    lea rax, [rel open_disposition]
    mov r10d, [rax + ARG1*4]
    mov rcx, ARG0                       ; lpFileName
    mov r8d, 7                          ; FILE_SHARE_READ | WRITE | DELETE
    xor r9d, r9d                        ; no security attributes
    mov [rsp + 32], r10                 ; dwCreationDisposition
    mov qword [rsp + 40], 0x80          ; FILE_ATTRIBUTE_NORMAL
    mov qword [rsp + 48], 0             ; no template
    call [rel __imp_CreateFileA]
    cmp rax, -1                         ; INVALID_HANDLE_VALUE
    je .error
    mov L0, rax
    ; the append modes start at the end
    mov rcx, L1
    cmp rcx, OS_OPEN_APPEND
    je .to_end
    cmp rcx, OS_OPEN_APPEND_OLD
    jne .done
.to_end:
    mov rcx, rax
    xor edx, edx
    xor r8d, r8d
    mov r9d, 2                          ; FILE_END
    call [rel __imp_SetFilePointerEx]
.done:
    mov rax, L0
    LEAVE_WIN
    ret
.error:
    call os_error
    LEAVE_WIN
    ret

; ARG0 = handle -> 0, or <0
cobrt_os_close:
    ENTER_WIN
    mov rcx, ARG0
    call [rel __imp_CloseHandle]
    test eax, eax
    jz .error
    xor eax, eax
    LEAVE_WIN
    ret
.error:
    mov rax, -1
    LEAVE_WIN
    ret

; SetFilePointerEx(handle, distance, &new, method) -> new position, or <0
%macro SEEK 3                           ; handle, distance, method
    mov rcx, %1
    mov rdx, %2
    lea r8, L0
    mov r9d, %3
    call [rel __imp_SetFilePointerEx]
    test eax, eax
    jz %%fail
    mov rax, L0
    jmp %%done
%%fail:
    mov rax, -1
%%done:
%endmacro

; ARG0 = handle, ARG1 = offset from the current position -> new position
cobrt_os_seek:
    ENTER_WIN
    mov L1, ARG0
    mov L2, ARG1
    SEEK L1, L2, 1                      ; FILE_CURRENT
    LEAVE_WIN
    ret

; ARG0 = handle -> the file size, or <0
cobrt_os_size:
    ENTER_WIN
    mov rcx, ARG0
    lea rdx, L0
    call [rel __imp_GetFileSizeEx]
    test eax, eax
    jz .error
    mov rax, L0
    LEAVE_WIN
    ret
.error:
    mov rax, -1
    LEAVE_WIN
    ret

; ARG0 = handle, ARG1 = buf, ARG2 = len, ARG3 = file offset: ReadFile or
; WriteFile at the offset (an OVERLAPPED structure gives it), the file
; position kept as it was (pread / pwrite do not move it)
%macro POSITIONED 1                     ; __imp_ReadFile or __imp_WriteFile
    ENTER_WIN
    mov L1, ARG0
    mov L2, ARG1
    mov L3, ARG2
    mov L4, ARG3
    SEEK L1, 0, 1                       ; the current position
    mov L5, rax
    ; OVERLAPPED: Internal, InternalHigh, Offset and OffsetHigh, hEvent
    lea r10, [rsp + 128]
    xor eax, eax
    mov [r10], rax
    mov [r10 + 8], rax
    mov rax, L4
    mov [r10 + 16], rax                 ; Offset and OffsetHigh
    mov qword [r10 + 24], 0             ; hEvent
    mov [rsp + 32], r10
    mov rcx, L1
    mov rdx, L2
    mov r8, L3
    lea r9, L6                          ; bytes transferred
    mov qword L6, 0
    call [rel %1]
    mov L0, rax                         ; (L0 is free: SEEK has used it)
    test eax, eax
    jnz %%ok
    call [rel __imp_GetLastError]
    cmp eax, 38                         ; ERROR_HANDLE_EOF: nothing at the offset
    jne %%fail
    mov qword L6, 0
    jmp %%ok
%%fail:
    mov qword L6, -1
%%ok:
    mov rax, L5
    mov L4, rax
    SEEK L1, L4, 0                      ; back to where the file was (FILE_BEGIN)
    mov rax, L6
    LEAVE_WIN
    ret
%endmacro

cobrt_os_pread:
    POSITIONED __imp_ReadFile

cobrt_os_pwrite:
    POSITIONED __imp_WriteFile

; ARG0 = handle, ARG1 = new size -> 0, or <0
cobrt_os_truncate:
    ENTER_WIN
    mov L1, ARG0
    mov L2, ARG1
    SEEK L1, 0, 1                       ; the current position
    mov L3, rax
    SEEK L1, L2, 0                      ; to the new end (FILE_BEGIN)
    mov rcx, L1
    call [rel __imp_SetEndOfFile]
    mov L4, rax
    SEEK L1, L3, 0                      ; back
    mov rax, L4
    test eax, eax
    jz .error
    xor eax, eax
    LEAVE_WIN
    ret
.error:
    mov rax, -1
    LEAVE_WIN
    ret

; ARG0 = path -> 0, or <0
cobrt_os_unlink:
    ENTER_WIN
    mov rcx, ARG0
    call [rel __imp_DeleteFileA]
    test eax, eax
    jz .error
    xor eax, eax
    LEAVE_WIN
    ret
.error:
    call os_error
    LEAVE_WIN
    ret

; ARG0 = old path, ARG1 = new path (replaced if it exists) -> 0, or <0
cobrt_os_rename:
    ENTER_WIN
    mov rcx, ARG0
    mov rdx, ARG1
    mov r8d, 1                          ; MOVEFILE_REPLACE_EXISTING
    call [rel __imp_MoveFileExA]
    test eax, eax
    jz .error
    xor eax, eax
    LEAVE_WIN
    ret
.error:
    call os_error
    LEAVE_WIN
    ret

; ARG0 = 16-byte buffer: seconds since 1970-01-01 UTC and nanoseconds
cobrt_os_time:
    ENTER_WIN
    mov L1, ARG0
    lea rcx, L0
    call [rel __imp_GetSystemTimeAsFileTime]
    mov rax, L0                         ; 100 ns units since 1601-01-01
    mov rcx, 116444736000000000         ; ... at 1970-01-01
    sub rax, rcx
    xor edx, edx
    mov rcx, 10000000
    div rcx
    mov r8, L1
    mov [r8], rax                       ; seconds
    imul rdx, rdx, 100
    mov [r8 + 8], rdx                   ; nanoseconds
    LEAVE_WIN
    ret

; ARG0 = name -> its value (NUL-terminated, in memory of its own), or 0
cobrt_os_getenv:
    ENTER_WIN
    mov L1, ARG0
    mov rcx, [rel process_heap]
    xor edx, edx
    mov r8d, 32768                      ; the largest value
    call [rel __imp_HeapAlloc]
    test rax, rax
    jz .none
    mov L2, rax
    mov rcx, L1
    mov rdx, rax
    mov r8d, 32768
    call [rel __imp_GetEnvironmentVariableA]
    test eax, eax
    jnz .found
    call [rel __imp_GetLastError]
    cmp eax, 203                        ; ERROR_ENVVAR_NOT_FOUND
    jne .found                          ; (an empty value)
    mov rcx, [rel process_heap]
    xor edx, edx
    mov r8, L2
    call [rel __imp_HeapFree]
.none:
    xor eax, eax
    LEAVE_WIN
    ret
.found:
    mov rax, L2
    LEAVE_WIN
    ret

; ARG0 = size -> zeroed memory, or 0
cobrt_os_alloc:
    ENTER_WIN
    mov r8, ARG0
    mov rcx, [rel process_heap]
    mov edx, 8                          ; HEAP_ZERO_MEMORY
    call [rel __imp_HeapAlloc]
    LEAVE_WIN
    ret

; ARG0 = memory from cobrt_os_alloc, ARG1 = its size
cobrt_os_free:
    ENTER_WIN
    mov r8, ARG0
    mov rcx, [rel process_heap]
    xor edx, edx
    call [rel __imp_HeapFree]
    LEAVE_WIN
    ret

section .bss
std_handles:  resq 3
process_heap: resq 1

section .rodata
; By OS_OPEN_* mode: dwDesiredAccess (GENERIC_READ 0x80000000, GENERIC_WRITE
; 0x40000000) and dwCreationDisposition (CREATE_ALWAYS 2, OPEN_EXISTING 3,
; OPEN_ALWAYS 4)
open_access:      dd 0x80000000, 0x40000000, 0xC0000000, 0x40000000, 0xC0000000, 0x40000000, 0xC0000000
open_disposition: dd 3, 2, 3, 4, 4, 3, 2
