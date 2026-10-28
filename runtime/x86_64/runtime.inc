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
;   cobrt_os_open (ARG0 = NUL-terminated path, ARG1 = OS_OPEN_* mode)
;                                                     -> RET0 = handle, or OS_ERR_*
;   cobrt_os_close(ARG0 = handle)                     -> RET0 = 0 or <0
;   cobrt_os_seek (ARG0 = handle, ARG1 = offset from the current position)
;                                                     -> RET0 = new position or <0
; They follow the normal ABI rules (caller-saved registers may be clobbered).
; Callers need not align the stack; an OS layer that calls into a system
; library must realign it itself.
; The OS layer itself defines COBRT_OS_LAYER before including this file.
%ifndef COBRT_OS_LAYER
    extern cobrt_os_write
    extern cobrt_os_read
    extern cobrt_os_exit
    extern cobrt_os_open
    extern cobrt_os_close
    extern cobrt_os_seek
%endif

%define COBRT_STDIN  0
%define COBRT_STDOUT 1
%define COBRT_STDERR 2

; cobrt_os_open modes and errors (OS independent)
%define OS_OPEN_READ        0           ; existing file, read only
%define OS_OPEN_WRITE       1           ; create or truncate, write only
%define OS_OPEN_READWRITE   2           ; existing file, read and write
%define OS_OPEN_APPEND      3           ; create if missing, write at the end
%define OS_OPEN_RW_CREATE   4           ; create if missing, read and write
%define OS_ERR_NOT_FOUND   -1
%define OS_ERR_OTHER       -2

; Field usage codes and numeric flags (runtime/ABI.md section 5)
%define USAGE_DISPLAY 0
%define USAGE_COMP    1                 ; big-endian binary
%define USAGE_COMP3   2                 ; packed decimal
%define USAGE_COMP5   3                 ; native binary
%define NUM_SIGNED    1                 ; PIC has S
%define NUM_KEEP      2                 ; on size error, leave the field unchanged
%define NUM_SIGN_LEADING  4             ; DISPLAY: sign on (or before) the first digit
%define NUM_SIGN_SEPARATE 8             ; DISPLAY: sign is a separate + or - character
