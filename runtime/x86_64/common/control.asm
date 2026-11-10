; runtime/x86_64/common/control.asm
; Program startup and termination.

%define COBRT_CONTROL
%include "runtime.inc"

%define SWITCH_UNKNOWN 0xFF

section .text

global runtime_init
global cobrt_runtime_flags
global runtime_exit
global runtime_switch_status
global runtime_switch_set
extern cobrt_files_close_all

; ARG0 = stack pointer at program entry (for the environment),
; ARG1 = program-wide settings (RT_* bits)
runtime_init:
    mov [rel cobrt_runtime_flags], ARG1
    jmp cobrt_os_init

; ARG0 = switch number (1..9). Returns RET0 = 1 if switch n is on. A switch
; starts in the state of the environment variable COB_SWITCH_n (ON: on).
runtime_switch_status:
    lea r8, [rel switches]
    movzx eax, byte [r8 + ARG0]
    cmp al, SWITCH_UNKNOWN
    jne .known
    push ARG0
    sub rsp, 16                         ; name buffer; rsp stays 16-byte aligned
    mov rax, 'COB_SWIT'
    mov [rsp], rax
    mov dword [rsp + 8], 'CH_'
    mov rax, ARG0
    add al, '0'
    mov [rsp + 11], al
    mov byte [rsp + 12], 0
    mov ARG0, rsp
    call cobrt_os_getenv
    add rsp, 16
    pop ARG0
    xor ecx, ecx
    test rax, rax
    jz .store
    cmp byte [rax], 'O'
    jne .store
    cmp byte [rax + 1], 'N'
    jne .store
    cmp byte [rax + 2], 0
    jne .store
    mov ecx, 1
.store:
    lea r8, [rel switches]
    mov [r8 + ARG0], cl
    mov eax, ecx
.known:
    ret

; SET mnemonic-name TO ON / OFF: ARG0 = switch number (1..9), ARG1 = 1 on, 0 off
runtime_switch_set:
    lea r8, [rel switches]
    mov rax, ARG1
    mov [r8 + ARG0], al
    ret

; Terminates the program with status 0; open files are closed.
runtime_exit:
    sub rsp, 8
    call cobrt_files_close_all
    xor ARG0, ARG0
    call cobrt_os_exit


section .bss
cobrt_runtime_flags: resq 1

section .data
switches: times 10 db SWITCH_UNKNOWN    ; state of SWITCH-n, until first read
