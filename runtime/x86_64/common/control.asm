; runtime/x86_64/common/control.asm
; Program startup and termination.

%include "runtime.inc"

section .text

global runtime_init
global runtime_exit

runtime_init:
    ret

; Terminates the program with status 0.
runtime_exit:
    xor ARG0, ARG0
    call cobrt_os_exit
