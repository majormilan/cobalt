; runtime/x86_64/common/compare.asm
; Comparisons that are not numeric.

%include "runtime.inc"

section .text

global runtime_compare_alnum
global runtime_compare_fill
global runtime_class_test

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

; Compare a field with a figurative constant (the character repeated).
; ARG0 = field, ARG1 = length, ARG2 = character
; Returns RET0 = -1 (field < constant), 0 (equal) or 1 (field > constant).
runtime_compare_fill:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    xor edx, edx                        ; index
.f_loop:
    cmp rdx, r11
    jae .equal
    mov al, [r10 + rdx]
    cmp al, r9b
    jb .less
    ja .greater
    inc rdx
    jmp .f_loop
.equal:
    xor eax, eax
    ret
.less:
    mov rax, -1
    ret
.greater:
    mov eax, 1
    ret

; Class test of a field.
; ARG0 = field, ARG1 = length, ARG2 = class:
;   0 NUMERIC (digits only), 1 NUMERIC signed DISPLAY (trailing overpunch
;   allowed), 2 ALPHABETIC, 3 ALPHABETIC-LOWER, 4 ALPHABETIC-UPPER
;   (letters and spaces), 5 NUMERIC packed decimal
; Returns RET0 = 1 if every character belongs to the class, else 0.
runtime_class_test:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    xor edx, edx                        ; index
    cmp r9, 5
    je .packed
.c_loop:
    cmp rdx, r11
    jae .yes
    movzx eax, byte [r10 + rdx]
    cmp r9, 2
    jae .alpha
    ; NUMERIC
    lea rcx, [rdx + 1]
    cmp rcx, r11
    jne .digit
    cmp r9, 1
    jne .digit
    cmp al, 'p'                         ; signed: the last byte may carry the sign
    jb .overpunch_letters
    cmp al, 'y'
    jbe .next
.overpunch_letters:
    cmp al, '{'
    je .next
    cmp al, '}'
    je .next
    cmp al, 'A'
    jb .digit
    cmp al, 'R'
    jbe .next
.digit:
    cmp al, '0'
    jb .no
    cmp al, '9'
    ja .no
    jmp .next
.alpha:
    cmp al, ' '
    je .next
    cmp r9, 4
    je .upper
    cmp al, 'a'
    jb .not_lower
    cmp al, 'z'
    jbe .next
.not_lower:
    cmp r9, 3
    je .no                              ; ALPHABETIC-LOWER: only a-z
.upper:
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    ja .no
.next:
    inc rdx
    jmp .c_loop

.packed:                                ; every nibble a digit, last nibble C, D or F
    cmp rdx, r11
    jae .yes
    movzx eax, byte [r10 + rdx]
    mov ecx, eax
    shr ecx, 4
    cmp ecx, 9
    ja .no
    and eax, 0x0f
    lea rcx, [rdx + 1]
    cmp rcx, r11
    je .packed_sign
    cmp eax, 9
    ja .no
    inc rdx
    jmp .packed
.packed_sign:
    cmp eax, 0x0c
    je .yes
    cmp eax, 0x0d
    je .yes
    cmp eax, 0x0f
    je .yes
.no:
    xor eax, eax
    ret
.yes:
    mov eax, 1
    ret
