; runtime/x86_64/common/error.asm
; Run-time errors (runtime/ABI.md section 15). The message goes to standard
; error and starts with where the program is:
;   PAYROLL.cbl:42: run-time error in program PAYROLL: <what>
; then open files are closed and the run ends with status 1.
;
; cobrt_where holds the address of that prefix (a zero-terminated string the
; compiler emits); generated code sets it before the statements that can end
; the run (CALL, the I-O statements). 0: no statement is known.

%include "runtime.inc"

section .text

global cobrt_where
global cobrt_error_begin
global cobrt_error_text
global cobrt_error_number
global cobrt_error_end
global runtime_check_fail
extern cobrt_close_files

; Write the zero-terminated string ARG0 to standard error
cobrt_error_text:
    push rbx
    mov rbx, ARG0
    xor eax, eax
.len:
    cmp byte [rbx + rax], 0
    je .write
    inc rax
    jmp .len
.write:
    mov ARG1, rbx
    mov ARG2, rax
    mov ARG0, COBRT_STDERR
    call cobrt_os_write
    pop rbx
    ret

; Write the signed integer ARG0 in decimal to standard error
cobrt_error_number:
    push rbx
    sub rsp, 32
    mov rax, ARG0
    xor ebx, ebx                        ; 1: negative
    test rax, rax
    jns .digits
    neg rax
    mov ebx, 1
.digits:
    lea r8, [rsp + 32]                  ; the digits are written backwards
    mov rcx, 10
.next:
    xor edx, edx
    div rcx
    add dl, '0'
    dec r8
    mov [r8], dl
    test rax, rax
    jnz .next
    test ebx, ebx
    jz .write
    dec r8
    mov byte [r8], '-'
.write:
    lea rax, [rsp + 32]
    sub rax, r8
    mov ARG1, r8
    mov ARG2, rax
    mov ARG0, COBRT_STDERR
    call cobrt_os_write
    add rsp, 32
    pop rbx
    ret

; The start of a message: where the program is
cobrt_error_begin:
    sub rsp, 8
    mov ARG0, [rel cobrt_where]
    test ARG0, ARG0
    jnz .known
    lea ARG0, [rel msg_runtime]
.known:
    call cobrt_error_text
    add rsp, 8
    ret

; The end of a message: the run ends
cobrt_error_end:
    sub rsp, 8
    lea ARG0, [rel msg_nl]
    call cobrt_error_text
    mov rax, [rel cobrt_close_files]
    test rax, rax
    jz .closed
    call rax
.closed:
    mov ARG0, 1
    call cobrt_os_exit

; A failed range check (-fcheck): ARG0 = where (the prefix), ARG1 = what
; was checked ("the subscript of 'E'"), ARG2 = its value, ARG3 and ARG4 = the
; lowest and highest valid values. Does not return.
;   <where><what> is <value>, out of range <low> to <high>
runtime_check_fail:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rel cobrt_where], ARG0
    mov r12, ARG1
    mov r13, ARG2
    mov r14, ARG3
    mov r15, ARG4
    call cobrt_error_begin
    mov ARG0, r12
    call cobrt_error_text
    lea ARG0, [rel msg_is]
    call cobrt_error_text
    mov ARG0, r13
    call cobrt_error_number
    lea ARG0, [rel msg_range]
    call cobrt_error_text
    mov ARG0, r14
    call cobrt_error_number
    lea ARG0, [rel msg_to]
    call cobrt_error_text
    mov ARG0, r15
    call cobrt_error_number
    call cobrt_error_end

section .bss
cobrt_where: resq 1

section .rodata
msg_runtime: db "run-time error: ", 0
msg_is:      db " is ", 0
msg_range:   db ", out of range ", 0
msg_to:      db " to ", 0
msg_nl:      db 10, 0
