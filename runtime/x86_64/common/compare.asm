; runtime/x86_64/common/compare.asm
; Comparisons that are not numeric.

%include "runtime.inc"

section .text

global runtime_compare_alnum

; Alphanumeric comparison in the native (ASCII) collating sequence; the
; shorter operand is padded with spaces.
; ARG0 = a, ARG1 = a length, ARG2 = b, ARG3 = b length
; Returns RET0 = -1 (a < b), 0 (a = b) or 1 (a > b).
runtime_compare_alnum:
    mov rax, ARG3
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    mov r8, rax
    ; r10 = a, r11 = a length, r9 = b, r8 = b length
    xor edx, edx                        ; index
.loop:
    cmp rdx, r11
    jb .compare
    cmp rdx, r8
    jae .equal
.compare:
    mov al, ' '
    cmp rdx, r11
    jae .a_done
    mov al, [r10 + rdx]
.a_done:
    mov cl, ' '
    cmp rdx, r8
    jae .b_done
    mov cl, [r9 + rdx]
.b_done:
    cmp al, cl
    jb .less
    ja .greater
    inc rdx
    jmp .loop
.equal:
    xor eax, eax
    ret
.less:
    mov rax, -1
    ret
.greater:
    mov eax, 1
    ret
