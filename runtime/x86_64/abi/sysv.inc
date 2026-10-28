; runtime/x86_64/abi/sysv.inc
; System V AMD64 calling convention (Linux, BSD, macOS).
; Arch-layer code reads incoming arguments through ARGn so the same source
; can be assembled for another x86_64 ABI by swapping this file.

%define ARG0 rdi
%define ARG1 rsi
%define ARG2 rdx
%define ARG3 rcx
%define ARG4 r8
%define ARG5 r9
%define RET0 rax
