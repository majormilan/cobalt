; runtime/x86_64/common/edit.asm
; Editing (runtime/ABI.md section 8): numeric values into numeric edited
; items, alphanumeric data into alphanumeric edited items, and de-editing.

%include "runtime.inc"

extern cobrt_pow10

section .text

global runtime_edit_numeric
global runtime_edit_alnum
global runtime_deedit

%define EDIT_BLANK_ZERO 1               ; BLANK WHEN ZERO
%define EDIT_KEEP       2               ; ON SIZE ERROR: keep the item on size error

; Locals of runtime_edit_numeric (rbp relative)
%define L_NEG      -48                  ; value is negative
%define L_ERR      -56                  ; size error
%define L_FLOATPOS -64                  ; last suppressed floating position, or -1
%define L_SIG      -72                  ; significance has started
%define L_INSUPP   -80                  ; inside a suppression / floating string
%define L_FILL     -88                  ; suppression character (' ' or '*')
%define L_HAS9     -96
%define L_STAR     -104
%define L_DIG      -144                 ; 32 digit characters
%define L_OUT      -400                 ; 256 output characters

; ARG0 = item, ARG1 = edit mask, ARG2 = mask length (item size),
; ARG3 = value (unscaled, at the item's scale), ARG4 = flags
; (bit 0 BLANK WHEN ZERO, bit 1 keep on size error, bits 8-15 floating
; character). Returns RET0 = 1 on size error (high-order digits lost).
runtime_edit_numeric:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 360                        ; locals down to rbp-400

    mov r12, ARG0                       ; item
    mov r13, ARG1                       ; mask
    mov r14, ARG2                       ; length
    mov rax, ARG3                       ; value
    mov r15, ARG4                       ; flags

    xor ecx, ecx
    mov [rbp + L_NEG], rcx
    mov [rbp + L_ERR], rcx
    mov [rbp + L_SIG], rcx
    mov [rbp + L_INSUPP], rcx
    mov [rbp + L_HAS9], rcx
    mov [rbp + L_STAR], rcx
    mov qword [rbp + L_FLOATPOS], -1
    mov qword [rbp + L_FILL], ' '
    test rax, rax
    jns .positive
    neg rax
    mov qword [rbp + L_NEG], 1
.positive:

    ; Count digit positions (r8) and note 9 and * positions
    xor r8d, r8d
    xor ebx, ebx
.count:
    cmp rbx, r14
    jae .counted
    movzx ecx, byte [r13 + rbx]
    cmp cl, '9'
    jne .not9
    mov qword [rbp + L_HAS9], 1
    jmp .digitpos
.not9:
    cmp cl, '*'
    jne .notstar
    mov qword [rbp + L_STAR], 1
    mov qword [rbp + L_FILL], '*'
    jmp .digitpos
.notstar:
    cmp cl, 'Z'
    je .digitpos
    cmp cl, 'f'
    jne .next_count
.digitpos:
    inc r8
.next_count:
    inc rbx
    jmp .count
.counted:

    ; Keep the low-order r8 digits; losing others is a size error
    cmp r8, 18
    ja .digits
    lea rcx, [rel cobrt_pow10]
    mov rcx, [rcx + r8*8]
    cmp rax, rcx
    jb .digits
    mov qword [rbp + L_ERR], 1
    xor edx, edx
    div rcx
    mov rax, rdx
.digits:
    mov r9, rax                         ; zero test below
    mov rcx, r8
    mov r10d, 10
.digit_loop:
    test rcx, rcx
    jz .digits_done
    dec rcx
    xor edx, edx
    div r10
    add dl, '0'
    mov [rbp + L_DIG + rcx], dl
    jmp .digit_loop
.digits_done:
    test r9, r9
    jnz .nonzero
    mov qword [rbp + L_NEG], 0          ; no negative zero
.nonzero:

    test r15, EDIT_KEEP
    jz .edit
    cmp qword [rbp + L_ERR], 0
    jne .done

.edit:
    test r9, r9
    jnz .walk
    test r15, EDIT_BLANK_ZERO
    jnz .all_spaces
    cmp qword [rbp + L_HAS9], 0
    jne .walk
    ; Zero with only suppression positions: spaces, or asterisks around the
    ; decimal point with check protection
    cmp qword [rbp + L_STAR], 0
    je .all_spaces
    xor ebx, ebx
.stars:
    cmp rbx, r14
    jae .copy
    mov al, '*'
    cmp byte [r13 + rbx], '.'
    jne .star_put
    mov al, '.'
.star_put:
    mov [rbp + L_OUT + rbx], al
    inc rbx
    jmp .stars
.all_spaces:
    xor ebx, ebx
.spaces:
    cmp rbx, r14
    jae .copy
    mov byte [rbp + L_OUT + rbx], ' '
    inc rbx
    jmp .spaces

    ; Walk the mask; rbx = position, r10 = digit index
.walk:
    xor ebx, ebx
    xor r10d, r10d
.walk_loop:
    cmp rbx, r14
    jae .copy
    movzx ecx, byte [r13 + rbx]
    cmp cl, '9'
    je .digit
    cmp cl, 'Z'
    je .digit
    cmp cl, '*'
    je .digit
    cmp cl, 'f'
    je .digit
    cmp cl, 'F'
    je .float_start
    cmp cl, '.'
    je .point
    cmp cl, 'S'
    je .fixed_plus
    cmp cl, 'M'
    je .fixed_minus
    cmp cl, 'c'
    je .credit
    cmp cl, 'r'
    je .credit
    cmp cl, 'd'
    je .credit
    cmp cl, 'b'
    je .credit
    cmp cl, '$'
    je .literal
    ; Insertion: B 0 / ,  (suppressed inside a suppression string)
    cmp qword [rbp + L_SIG], 0
    jne .insert
    cmp qword [rbp + L_INSUPP], 0
    je .insert
    mov rax, [rbp + L_FILL]
    cmp qword [rbp + L_FLOATPOS], -1
    je .put
    mov [rbp + L_FLOATPOS], rbx         ; part of the floating string
    mov al, ' '
    jmp .put
.insert:
    mov al, cl
    cmp cl, 'B'
    jne .put
    mov al, ' '
    jmp .put
.literal:
    mov al, cl
    jmp .put
.digit:
    movzx eax, byte [rbp + L_DIG + r10]
    inc r10
    cmp qword [rbp + L_SIG], 0
    jne .put
    cmp cl, '9'
    je .significant
    cmp al, '0'
    jne .significant
    ; still suppressed
    mov qword [rbp + L_INSUPP], 1
    cmp cl, 'f'
    jne .supp_char
    mov [rbp + L_FLOATPOS], rbx
    mov al, ' '
    jmp .put
.supp_char:
    mov al, ' '
    cmp cl, '*'
    jne .put
    mov al, '*'
    jmp .put
.significant:
    mov qword [rbp + L_SIG], 1
    call .place_float
    jmp .put
.float_start:
    mov qword [rbp + L_INSUPP], 1
    mov [rbp + L_FLOATPOS], rbx
    mov al, ' '
    jmp .put
.point:
    cmp qword [rbp + L_SIG], 0
    jne .point_put
    mov qword [rbp + L_SIG], 1
    call .place_float
.point_put:
    mov al, '.'
    jmp .put
.fixed_plus:
    mov al, '+'
    cmp qword [rbp + L_NEG], 0
    je .put
    mov al, '-'
    jmp .put
.fixed_minus:
    mov al, ' '
    cmp qword [rbp + L_NEG], 0
    je .put
    mov al, '-'
    jmp .put
.credit:                                ; CR / DB when negative, else spaces
    mov al, ' '
    cmp qword [rbp + L_NEG], 0
    je .put
    mov al, cl
    sub al, 'a' - 'A'
.put:
    mov [rbp + L_OUT + rbx], al
    inc rbx
    jmp .walk_loop

    ; Put the floating symbol in the last suppressed floating position.
    ; Preserves rax (the character being stored).
.place_float:
    mov rdx, [rbp + L_FLOATPOS]
    cmp rdx, -1
    je .pf_done
    mov r11, r15
    shr r11, 8
    and r11, 0xff                       ; floating character
    cmp r11b, '$'
    je .pf_put
    cmp qword [rbp + L_NEG], 0
    jne .pf_minus
    cmp r11b, '+'
    je .pf_put
    mov r11b, ' '                       ; floating '-' on a positive value
    jmp .pf_put
.pf_minus:
    mov r11b, '-'
.pf_put:
    mov [rbp + L_OUT + rdx], r11b
.pf_done:
    ret

.copy:
    mov rdi, r12
    lea rsi, [rbp + L_OUT]
    mov rcx, r14
    cld
    rep movsb
.done:
    mov rax, [rbp + L_ERR]
    lea rsp, [rbp - 40]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; ARG0 = item, ARG1 = source, ARG2 = source length, ARG3 = edit mask
; (X for a source character, B 0 / insertion), ARG4 = mask length.
; Source characters fill the X positions from the left; missing ones are spaces.
runtime_edit_alnum:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    mov rax, ARG4
    mov r8, ARG3
    push rbx
    xor ecx, ecx                        ; mask position
    xor edx, edx                        ; source position
.ea_loop:
    cmp rcx, rax
    jae .ea_done
    mov bl, [r8 + rcx]
    cmp bl, 'X'
    jne .ea_insert
    mov bl, ' '
    cmp rdx, r9
    jae .ea_put
    mov bl, [r11 + rdx]
    inc rdx
    jmp .ea_put
.ea_insert:
    cmp bl, 'B'
    jne .ea_put
    mov bl, ' '
.ea_put:
    mov [r10 + rcx], bl
    inc rcx
    jmp .ea_loop
.ea_done:
    pop rbx
    ret

; ARG0 = numeric edited item, ARG1 = its length
; Returns RET0 = the value of its digits (unscaled), negative if the item
; shows a minus sign, CR or DB.
runtime_deedit:
    mov r10, ARG0
    mov r11, ARG1
    xor eax, eax
    xor r9d, r9d                        ; negative
    xor ecx, ecx
.dd_loop:
    cmp rcx, r11
    jae .dd_done
    movzx edx, byte [r10 + rcx]
    cmp dl, '-'
    je .dd_neg
    cmp dl, 'C'
    je .dd_neg
    cmp dl, 'D'
    je .dd_neg
    sub dl, '0'
    cmp dl, 9
    ja .dd_next
    imul rax, rax, 10
    add rax, rdx
    jmp .dd_next
.dd_neg:
    mov r9d, 1
.dd_next:
    inc rcx
    jmp .dd_loop
.dd_done:
    test r9d, r9d
    jz .dd_ret
    neg rax
.dd_ret:
    ret
