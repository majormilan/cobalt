; runtime/x86_64/common/numeric.asm
; Numeric core (runtime/ABI.md sections 5 and 6): conversion between field
; storage and signed 64-bit integers, and the arithmetic primitives that need
; 128-bit intermediates. Scales are tracked by the compiler; these routines
; see unscaled integers and explicit powers of ten.

%include "runtime.inc"

section .text

global runtime_num_load
global runtime_num_store
global runtime_num_rescale
global runtime_num_mul
global runtime_num_div

; ARG0 = field ptr, ARG1 = size, ARG2 = usage, ARG3 = flags
; Returns RET0 = the field's value as an unscaled signed integer.
runtime_num_load:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14

    mov r12, ARG0                       ; field ptr
    mov r13, ARG1                       ; size
    mov r14, ARG3                       ; flags
    mov rax, ARG2                       ; usage
    cmp rax, USAGE_COMP
    je .comp
    cmp rax, USAGE_COMP3
    je .comp3
    cmp rax, USAGE_COMP5
    je .comp5

    ; DISPLAY: one ASCII digit per byte, trailing overpunch if signed
    xor eax, eax
    xor ebx, ebx                        ; rbx = negative
    xor ecx, ecx                        ; index
.d_loop:
    cmp rcx, r13
    jge .d_done
    movzx edx, byte [r12 + rcx]
    lea r8, [rcx + 1]
    cmp r8, r13
    jne .d_plain                        ; only the last byte carries a sign
    test r14, NUM_SIGNED
    jz .d_plain
    cmp dl, 'p'                         ; 'p'..'y' = -0..-9
    jb .d_ebcdic
    cmp dl, 'y'
    ja .d_ebcdic
    sub dl, 'p'
    mov ebx, 1
    jmp .d_add
.d_ebcdic:                              ; '{', 'A'..'I' = +0..+9; '}', 'J'..'R' = -0..-9
    cmp dl, '{'
    jne .d_not_pos0
    xor edx, edx
    jmp .d_add
.d_not_pos0:
    cmp dl, '}'
    jne .d_not_neg0
    xor edx, edx
    mov ebx, 1
    jmp .d_add
.d_not_neg0:
    cmp dl, 'A'
    jb .d_plain
    cmp dl, 'I'
    ja .d_neg_letter
    sub dl, 'A' - 1
    jmp .d_add
.d_neg_letter:
    cmp dl, 'J'
    jb .d_plain
    cmp dl, 'R'
    ja .d_plain
    sub dl, 'J' - 1
    mov ebx, 1
    jmp .d_add
.d_plain:
    sub dl, '0'
    cmp dl, 9
    jbe .d_add
    xor edx, edx                        ; not a digit (e.g. a space): counts as 0
.d_add:
    imul rax, rax, 10
    add rax, rdx
    inc rcx
    jmp .d_loop
.d_done:
    test ebx, ebx
    jz .ret
    neg rax
    jmp .ret

    ; COMP: big-endian two's complement
.comp:
    xor eax, eax
    xor ecx, ecx
.c_loop:
    cmp rcx, r13
    jge .extend
    shl rax, 8
    movzx edx, byte [r12 + rcx]
    or rax, rdx
    inc rcx
    jmp .c_loop

    ; COMP-5: native (little-endian) two's complement
.comp5:
    xor eax, eax
    mov rcx, r13
.c5_loop:
    test rcx, rcx
    jz .extend
    dec rcx
    shl rax, 8
    movzx edx, byte [r12 + rcx]
    or rax, rdx
    jmp .c5_loop

.extend:                                ; sign-extend from size*8 bits if signed
    test r14, NUM_SIGNED
    jz .ret
    mov rcx, 8
    sub rcx, r13
    shl rcx, 3                          ; 64 - size*8
    shl rax, cl
    sar rax, cl
    jmp .ret

    ; COMP-3: two digits per byte, sign in the low nibble of the last byte
.comp3:
    xor eax, eax
    xor ecx, ecx
.p_loop:
    movzx edx, byte [r12 + rcx]
    mov r8d, edx
    shr r8d, 4                          ; high nibble: a digit
    imul rax, rax, 10
    add rax, r8
    lea r9, [rcx + 1]
    cmp r9, r13
    je .p_sign
    and edx, 0x0f                       ; low nibble: a digit
    imul rax, rax, 10
    add rax, rdx
    mov rcx, r9
    jmp .p_loop
.p_sign:
    and edx, 0x0f
    cmp edx, 0x0d
    je .p_neg
    cmp edx, 0x0b
    jne .ret
.p_neg:
    neg rax

.ret:
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ARG0 = field ptr, ARG1 = size, ARG2 = digits (PIC digit positions),
; ARG3 = usage, ARG4 = flags, ARG5 = value (unscaled, already at field scale)
; Returns RET0 = 1 on size error (high-order digits lost), else 0.
; With NUM_KEEP set the field is left unchanged on size error.
runtime_num_store:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0                       ; field ptr
    mov r13, ARG1                       ; size
    mov rbx, ARG2                       ; digits (reused as COMP-3 index)
    mov r10, ARG3                       ; usage
    mov r14, ARG4                       ; flags
    mov r15, ARG5                       ; value

    ; rax = magnitude, r11 = negative (only for signed fields)
    xor r11d, r11d
    mov rax, r15
    test rax, rax
    jns .mag
    neg rax
    test r14, NUM_SIGNED
    jz .mag                             ; unsigned field: store the absolute value
    mov r11d, 1
.mag:
    xor r9d, r9d                        ; r9 = size error
    cmp r10, USAGE_COMP5
    je .s_comp5

    ; Decimal fields hold at most 10^digits - 1
    cmp rbx, 18
    ja .limit_ok
    lea rcx, [rel pow10]
    mov r8, [rcx + rbx*8]
    cmp rax, r8
    jb .limit_ok
    mov r9d, 1
    xor edx, edx
    div r8                              ; keep the low-order digits
    mov rax, rdx
.limit_ok:
    test r9d, r9d
    jz .write
    test r14, NUM_KEEP
    jnz .done
.write:
    test rax, rax
    jnz .dispatch
    xor r11d, r11d                      ; negative zero is stored as positive zero
.dispatch:
    cmp r10, USAGE_COMP
    je .s_comp
    cmp r10, USAGE_COMP3
    je .s_comp3

    ; DISPLAY: digits right to left, then the trailing overpunch
    mov rcx, r13
    mov r8, 10
.sd_loop:
    test rcx, rcx
    jz .sd_sign
    dec rcx
    xor edx, edx
    div r8
    add dl, '0'
    mov [r12 + rcx], dl
    jmp .sd_loop
.sd_sign:
    test r11d, r11d
    jz .done
    add byte [r12 + r13 - 1], 0x40      ; '0'..'9' -> 'p'..'y'
    jmp .done

    ; COMP: big-endian two's complement
.s_comp:
    test r11d, r11d
    jz .sc_write
    neg rax
.sc_write:
    mov rcx, r13
.sc_loop:
    test rcx, rcx
    jz .done
    dec rcx
    mov [r12 + rcx], al
    shr rax, 8
    jmp .sc_loop

    ; COMP-3: sign nibble C (+), D (-) or F (unsigned)
.s_comp3:
    mov r8, 10
    mov ecx, 0x0f
    test r14, NUM_SIGNED
    jz .sp_sign
    mov ecx, 0x0c
    test r11d, r11d
    jz .sp_sign
    mov ecx, 0x0d
.sp_sign:
    mov rbx, r13
    dec rbx                             ; last byte: lowest digit + sign
    xor edx, edx
    div r8
    shl edx, 4
    or edx, ecx
    mov [r12 + rbx], dl
.sp_loop:
    test rbx, rbx
    jz .done
    dec rbx
    xor edx, edx
    div r8
    mov ecx, edx                        ; low nibble
    xor edx, edx
    div r8
    shl edx, 4                          ; high nibble
    or edx, ecx
    mov [r12 + rbx], dl
    jmp .sp_loop

    ; COMP-5: the limit is the binary range of the field size
.s_comp5:
    mov rcx, r13
    shl rcx, 3                          ; bits
    test r14, NUM_SIGNED
    jnz .s5_signed
    cmp rcx, 64
    jae .s5_ok
    mov r8, 1
    shl r8, cl
    cmp rax, r8
    jb .s5_ok
    jmp .s5_err
.s5_signed:
    dec rcx
    mov r8, 1
    shl r8, cl                          ; 2^(bits-1)
    test r11d, r11d
    jz .s5_pos
    cmp rax, r8
    jbe .s5_ok
    jmp .s5_err
.s5_pos:
    cmp rax, r8
    jb .s5_ok
.s5_err:
    mov r9d, 1
    test r14, NUM_KEEP
    jnz .done
.s5_ok:
    test r11d, r11d
    jz .s5_write
    neg rax
.s5_write:
    xor ecx, ecx
.s5_loop:
    cmp rcx, r13
    jae .done
    mov [r12 + rcx], al
    shr rax, 8
    inc rcx
    jmp .s5_loop

.done:
    mov eax, r9d
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ARG0 = value, ARG1 = shift (power of ten, may be negative), ARG2 = rounded
; Returns RET0 = value * 10^shift, RET1 = 1 on overflow.
; A negative shift truncates toward zero, or rounds half away from zero when
; ARG2 is non-zero.
runtime_num_rescale:
    mov r10, ARG0                       ; value
    mov r11, ARG1                       ; shift
    mov r9, ARG2                        ; rounded
    mov rax, r10
    xor edx, edx
    test r11, r11
    jz .r_ret
    js .r_down
    cmp r11, 18
    ja .r_up_big
    lea rcx, [rel pow10]
    imul rax, [rcx + r11*8]
    jo .r_err
    jmp .r_ok
.r_up_big:
    test rax, rax
    jz .r_ok
    jmp .r_err
.r_down:
    neg r11
    cmp r11, 18
    ja .r_zero
    lea rcx, [rel pow10]
    mov r8, [rcx + r11*8]
    cqo
    idiv r8                             ; rax = quotient, rdx = remainder
    test r9, r9
    jz .r_ok
    mov rcx, rdx
    test rcx, rcx
    jns .r_absrem
    neg rcx
.r_absrem:
    shl rcx, 1
    cmp rcx, r8
    jb .r_ok                            ; remainder below one half
    test r10, r10
    js .r_round_neg
    inc rax
    jmp .r_ok
.r_round_neg:
    dec rax
    jmp .r_ok
.r_zero:
    xor eax, eax
.r_ok:
    xor edx, edx
.r_ret:
    ret
.r_err:
    xor eax, eax
    mov edx, 1
    ret

; ARG0 = a, ARG1 = b, ARG2 = shift (0..18)
; Returns RET0 = (a * b) / 10^shift truncated toward zero, using a 128-bit
; product; RET1 = 1 on overflow.
runtime_num_mul:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    mov r8, r10
    xor r8, r11                         ; bit 63 = sign of the result
    mov rax, r10
    test rax, rax
    jns .m_a
    neg rax
.m_a:
    mov rcx, r11
    test rcx, rcx
    jns .m_b
    neg rcx
.m_b:
    mul rcx                             ; rdx:rax = |a| * |b|
    test r9, r9
    jz .m_noshift
    cmp r9, 18
    ja .m_err
    lea rcx, [rel pow10]
    mov rcx, [rcx + r9*8]
    cmp rdx, rcx
    jae .m_err                          ; quotient would not fit in 64 bits
    div rcx
    jmp .m_check
.m_noshift:
    test rdx, rdx
    jnz .m_err
.m_check:
    test rax, rax
    js .m_err
    test r8, r8
    jns .m_ok
    neg rax
.m_ok:
    xor edx, edx
    ret
.m_err:
    xor eax, eax
    mov edx, 1
    ret

; ARG0 = a, ARG1 = b, ARG2 = shift (0..18)
; Returns RET0 = (a * 10^shift) / b truncated toward zero, using a 128-bit
; dividend; RET1 = 1 on division by zero or overflow.
runtime_num_div:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    test r11, r11
    jz .v_err
    cmp r9, 18
    ja .v_err
    mov r8, r10
    xor r8, r11                         ; bit 63 = sign of the result
    mov rax, r10
    test rax, rax
    jns .v_a
    neg rax
.v_a:
    mov rcx, r11
    test rcx, rcx
    jns .v_b
    neg rcx
.v_b:
    lea rdx, [rel pow10]
    mul qword [rdx + r9*8]              ; rdx:rax = |a| * 10^shift
    cmp rdx, rcx
    jae .v_err                          ; quotient would not fit in 64 bits
    div rcx
    test rax, rax
    js .v_err
    test r8, r8
    jns .v_ok
    neg rax
.v_ok:
    xor edx, edx
    ret
.v_err:
    xor eax, eax
    mov edx, 1
    ret

section .rodata
align 8
pow10:
    dq 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000
    dq 1000000000, 10000000000, 100000000000, 1000000000000
    dq 10000000000000, 100000000000000, 1000000000000000
    dq 10000000000000000, 100000000000000000, 1000000000000000000
