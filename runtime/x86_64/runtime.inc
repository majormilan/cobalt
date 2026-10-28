; runtime/x86_64/runtime.inc
; Common include for every x86_64 runtime source file.
; Select the calling convention with -DABI_SYSV (more ABIs to come).

%ifdef ABI_SYSV
    %include "abi/sysv.inc"
%else
    %error "No x86_64 ABI selected (expected -DABI_SYSV)"
%endif

; OS layer interface, implemented in runtime/x86_64/<os>/os.asm.
; The arch layer never issues syscalls directly; it calls these instead.
;   cobrt_os_write(ARG0 = fd, ARG1 = buf, ARG2 = len) -> RET0 = bytes written or <0
;   cobrt_os_read (ARG0 = fd, ARG1 = buf, ARG2 = len) -> RET0 = bytes read, 0 = EOF, <0 = error
;   cobrt_os_exit (ARG0 = status)                     -> does not return
; They follow the normal ABI rules (caller-saved registers may be clobbered).
; Callers need not align the stack; an OS layer that calls into a system
; library must realign it itself.
; The OS layer itself defines COBRT_OS_LAYER before including this file.
%ifndef COBRT_OS_LAYER
    extern cobrt_os_write
    extern cobrt_os_read
    extern cobrt_os_exit
%endif

%define COBRT_STDIN  0
%define COBRT_STDOUT 1
