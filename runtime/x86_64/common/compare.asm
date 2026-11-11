; runtime/x86_64/common/compare.asm
; Comparisons that are not numeric.

%include "runtime.inc"

section .text

global runtime_compare_alnum
global runtime_compare_fill
global runtime_class_test
global runtime_compare_pattern
global runtime_set_collation
global cobrt_collation

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
    call collate_pair
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
    mov cl, r9b
    call collate_pair
    cmp al, cl
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
;   (letters and spaces), 5 NUMERIC packed decimal, 6 NUMERIC signed with
;   SIGN LEADING (overpunch on the first digit), 7 NUMERIC SIGN TRAILING
;   SEPARATE, 8 NUMERIC SIGN LEADING SEPARATE
; Returns RET0 = 1 if every character belongs to the class, else 0.
runtime_class_test:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    xor edx, edx                        ; index
    cmp r9, 9
    je .user_class
    cmp r9, 5
    je .packed
    cmp r9, 6
    je .lead_overpunch
    cmp r9, 7
    je .sep_trailing
    cmp r9, 8
    je .sep_leading
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

    ; SIGN LEADING: the first byte may carry the overpunch, then digits
.lead_overpunch:
    test r11, r11
    jz .no
    movzx eax, byte [r10]
    call .is_overpunch_or_digit
    test eax, eax
    jz .no
    mov edx, 1
    mov r9, 0                           ; the rest: plain digits
    jmp .c_loop
    ; Separate sign: + or - first or last, the rest digits
.sep_trailing:
    test r11, r11
    jz .no
    dec r11
    movzx eax, byte [r10 + r11]
    jmp .sep_check
.sep_leading:
    test r11, r11
    jz .no
    movzx eax, byte [r10]
    inc r10
    dec r11
.sep_check:
    cmp al, '+'
    je .sep_digits
    cmp al, '-'
    jne .no
.sep_digits:
    test r11, r11
    jz .no
    xor r9d, r9d
    jmp .c_loop

; al = character; returns eax = 1 if a digit or an overpunched digit
.is_overpunch_or_digit:
    cmp al, '0'
    jb .iod_letters
    cmp al, '9'
    jbe .iod_yes
    cmp al, 'p'
    jb .iod_letters
    cmp al, 'y'
    jbe .iod_yes
.iod_letters:
    cmp al, '{'
    je .iod_yes
    cmp al, '}'
    je .iod_yes
    cmp al, 'A'
    jb .iod_no
    cmp al, 'R'
    jbe .iod_yes
.iod_no:
    xor eax, eax
    ret
.iod_yes:
    mov eax, 1
    ret

    ; CLASS from SPECIAL-NAMES: every character in the member list
    ; (ARG3 = members, ARG4 = number of members)
.user_class:
    mov rax, ARG4                       ; read before ARG3 (ARG4 is r8 under SysV)
    mov r8, ARG3
    mov r9, rax
.uc_loop:
    cmp rdx, r11
    jae .yes
    mov al, [r10 + rdx]
    xor ecx, ecx
.uc_member:
    cmp rcx, r9
    jae .no
    cmp al, [r8 + rcx]
    je .uc_next
    inc rcx
    jmp .uc_member
.uc_next:
    inc rdx
    jmp .uc_loop

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

; Compare a field with a pattern repeated (ALL "literal").
; ARG0 = field, ARG1 = length, ARG2 = pattern, ARG3 = pattern length
; Returns RET0 = -1, 0 or 1.
runtime_compare_pattern:
    mov rax, ARG3
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    mov r8, rax
    push rbx
    xor ecx, ecx                        ; field index
    xor edx, edx                        ; pattern index
.cp_loop:
    cmp rcx, r11
    jae .cp_equal
    mov al, [r10 + rcx]
    push rcx
    mov cl, [r9 + rdx]
    call collate_pair
    mov bl, cl
    pop rcx
    cmp al, bl
    jb .cp_less
    ja .cp_greater
    inc rcx
    inc rdx
    cmp rdx, r8
    jb .cp_loop
    xor edx, edx
    jmp .cp_loop
.cp_equal:
    pop rbx
    xor eax, eax
    ret
.cp_less:
    pop rbx
    mov rax, -1
    ret
.cp_greater:
    pop rbx
    mov eax, 1
    ret

; Program collating sequence: ARG0 = 256-byte weight table (0 for native)
runtime_set_collation:
    mov [rel cobrt_collation], ARG0
    ret

; Translate al and cl to their collating weights (unchanged when native).
; Preserves every other register.
collate_pair:
    push r11
    mov r11, [rel cobrt_collation]
    test r11, r11
    jz .cp_native
    movzx eax, al
    movzx ecx, cl
    mov al, [r11 + rax]
    mov cl, [r11 + rcx]
.cp_native:
    pop r11
    ret

section .bss
cobrt_collation: resq 1
