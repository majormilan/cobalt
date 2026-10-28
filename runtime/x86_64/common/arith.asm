; runtime/x86_64/common/arith.asm
; Arithmetic on fixed-width ASCII digit fields (integer only for now).

%include "runtime.inc"

section .text

global runtime_load_int

; Convert a fixed-width ASCII digit buffer to a 64-bit integer.
; ARG0 = buffer pointer (NOT null-terminated, exactly ARG1 bytes)
; ARG1 = buffer size in bytes
; Returns: rax = integer value
runtime_load_int:
    push rbp
    mov rbp, rsp
    push r12
    push r13

    mov r12, ARG0       ; r12 = buffer ptr
    mov r13, ARG1       ; r13 = size
    xor rax, rax        ; result = 0
    xor r8, r8          ; index = 0
.load_loop:
    cmp r8, r13
    jge .load_done
    movzx rcx, byte [r12 + r8]
    sub cl, '0'
    imul rax, rax, 10
    add rax, rcx
    inc r8
    jmp .load_loop
.load_done:
    pop r13
    pop r12
    leave
    ret

global runtime_add_to

; Add src into dest in-place (dest += src), both as fixed-width ASCII digit buffers.
; ARG0 = dest_ptr  (ASCII digits, dest_size bytes, NOT null-terminated)
; ARG1 = dest_size
; ARG2 = src_ptr   (ASCII digits, src_size bytes, NOT null-terminated)
; ARG3 = src_size
runtime_add_to:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0       ; r12 = dest ptr
    mov r13, ARG1       ; r13 = dest_size
    mov r14, ARG2       ; r14 = src ptr
    mov r15, ARG3       ; r15 = src_size

    ; Convert src ASCII -> integer in rbx
    xor rbx, rbx
    xor r8, r8
.src_to_int:
    cmp r8, r15
    jge .src_int_done
    movzx rax, byte [r14 + r8]
    sub al, '0'
    imul rbx, rbx, 10
    add rbx, rax
    inc r8
    jmp .src_to_int
.src_int_done:

    ; Convert dest ASCII -> integer in r9
    xor r9, r9
    xor r8, r8
.dst_to_int:
    cmp r8, r13
    jge .dst_int_done
    movzx rax, byte [r12 + r8]
    sub al, '0'
    imul r9, r9, 10
    add r9, rax
    inc r8
    jmp .dst_to_int
.dst_int_done:

    ; sum = dest + src
    add r9, rbx

    ; Fill dest with '0'  (rdi and rcx are caller-saved, OK to clobber)
    mov rdi, r12
    mov al, '0'
    mov rcx, r13
    cld
    rep stosb

    ; Write sum back as ASCII right-aligned in dest (r9 = sum, r12 = dest, r13 = size)
    mov rbx, r13        ; rbx = position, starts at dest_size and goes left
    mov rax, r9
    test rax, rax
    jz .store_done      ; zero -> dest already filled with '0'
.digits_loop:
    dec rbx
    xor rdx, rdx
    mov rcx, 10
    div rcx             ; rax = quotient, rdx = remainder digit
    add dl, '0'
    mov [r12 + rbx], dl
    test rax, rax
    jz .store_done
    test rbx, rbx
    jnz .digits_loop    ; if rbx == 0 and rax != 0, overflow: truncate silently
.store_done:

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ── runtime_sub_from ──────────────────────────────────────────────────────────
; dest -= src  (same signature as runtime_add_to)
global runtime_sub_from
runtime_sub_from:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0       ; r12 = dest ptr
    mov r13, ARG1       ; r13 = dest_size
    mov r14, ARG2       ; r14 = src ptr
    mov r15, ARG3       ; r15 = src_size

    ; src → rbx
    xor rbx, rbx
    xor r8, r8
.sub_src_loop:
    cmp r8, r15
    jge .sub_src_done
    movzx rax, byte [r14 + r8]
    sub al, '0'
    imul rbx, rbx, 10
    add rbx, rax
    inc r8
    jmp .sub_src_loop
.sub_src_done:

    ; dest → r9
    xor r9, r9
    xor r8, r8
.sub_dst_loop:
    cmp r8, r13
    jge .sub_dst_done
    movzx rax, byte [r12 + r8]
    sub al, '0'
    imul r9, r9, 10
    add r9, rax
    inc r8
    jmp .sub_dst_loop
.sub_dst_done:

    ; result = dest - src (clamp to 0 if negative — unsigned semantics)
    sub r9, rbx
    jge .sub_pos
    xor r9, r9
.sub_pos:

    ; write back
    mov rdi, r12
    mov al, '0'
    mov rcx, r13
    cld
    rep stosb
    mov rbx, r13
    mov rax, r9
    test rax, rax
    jz .sub_store_done
.sub_dig_loop:
    dec rbx
    xor rdx, rdx
    mov rcx, 10
    div rcx
    add dl, '0'
    mov [r12 + rbx], dl
    test rax, rax
    jz .sub_store_done
    test rbx, rbx
    jnz .sub_dig_loop
.sub_store_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ── runtime_mul_by ────────────────────────────────────────────────────────────
; dest *= src  (same signature)
global runtime_mul_by
runtime_mul_by:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    mov r15, ARG3

    xor rbx, rbx
    xor r8, r8
.mul_src_loop:
    cmp r8, r15
    jge .mul_src_done
    movzx rax, byte [r14 + r8]
    sub al, '0'
    imul rbx, rbx, 10
    add rbx, rax
    inc r8
    jmp .mul_src_loop
.mul_src_done:

    xor r9, r9
    xor r8, r8
.mul_dst_loop:
    cmp r8, r13
    jge .mul_dst_done
    movzx rax, byte [r12 + r8]
    sub al, '0'
    imul r9, r9, 10
    add r9, rax
    inc r8
    jmp .mul_dst_loop
.mul_dst_done:

    imul r9, rbx            ; r9 = dest * src (overflow silently truncated)

    mov rdi, r12
    mov al, '0'
    mov rcx, r13
    cld
    rep stosb
    mov rbx, r13
    mov rax, r9
    test rax, rax
    jz .mul_store_done
.mul_dig_loop:
    dec rbx
    xor rdx, rdx
    mov rcx, 10
    div rcx
    add dl, '0'
    mov [r12 + rbx], dl
    test rax, rax
    jz .mul_store_done
    test rbx, rbx
    jnz .mul_dig_loop
.mul_store_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ── runtime_div_into ──────────────────────────────────────────────────────────
; dest /= src  (integer division; dest unchanged if src == 0)
global runtime_div_into
runtime_div_into:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    mov r15, ARG3

    xor rbx, rbx
    xor r8, r8
.div_src_loop:
    cmp r8, r15
    jge .div_src_done
    movzx rax, byte [r14 + r8]
    sub al, '0'
    imul rbx, rbx, 10
    add rbx, rax
    inc r8
    jmp .div_src_loop
.div_src_done:
    test rbx, rbx
    jz .div_done            ; division by zero: leave dest unchanged

    xor r9, r9
    xor r8, r8
.div_dst_loop:
    cmp r8, r13
    jge .div_dst_done
    movzx rax, byte [r12 + r8]
    sub al, '0'
    imul r9, r9, 10
    add r9, rax
    inc r8
    jmp .div_dst_loop
.div_dst_done:

    mov rax, r9
    xor rdx, rdx
    div rbx                 ; rax = quotient, rdx = remainder
    mov r9, rax             ; save quotient NOW before al is clobbered

    mov rdi, r12
    mov al, '0'
    mov rcx, r13
    cld
    rep stosb
    mov rbx, r13
    test r9, r9
    jz .div_store_done
.div_dig_loop:
    dec rbx
    mov rax, r9
    xor rdx, rdx
    mov rcx, 10
    div rcx
    mov r9, rax
    add dl, '0'
    mov [r12 + rbx], dl
    test r9, r9
    jz .div_store_done
    test rbx, rbx
    jnz .div_dig_loop
.div_store_done:
.div_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ── runtime_mod_into ──────────────────────────────────────────────────────────
; dest %= src  (remainder; dest unchanged if src == 0)
global runtime_mod_into
runtime_mod_into:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    mov r15, ARG3

    xor rbx, rbx
    xor r8, r8
.mod_src_loop:
    cmp r8, r15
    jge .mod_src_done
    movzx rax, byte [r14 + r8]
    sub al, '0'
    imul rbx, rbx, 10
    add rbx, rax
    inc r8
    jmp .mod_src_loop
.mod_src_done:
    test rbx, rbx
    jz .mod_done

    xor r9, r9
    xor r8, r8
.mod_dst_loop:
    cmp r8, r13
    jge .mod_dst_done
    movzx rax, byte [r12 + r8]
    sub al, '0'
    imul r9, r9, 10
    add r9, rax
    inc r8
    jmp .mod_dst_loop
.mod_dst_done:

    mov rax, r9
    xor rdx, rdx
    div rbx                 ; rdx = remainder

    ; Store the remainder value in r9 for digit extraction
    mov r9, rdx

    mov rdi, r12
    mov al, '0'
    mov rcx, r13
    cld
    rep stosb
    mov rbx, r13
    test r9, r9
    jz .mod_store_done
.mod_dig_loop:
    dec rbx
    mov rax, r9
    xor rdx, rdx
    mov rcx, 10
    div rcx
    mov r9, rax             ; r9 = quotient
    add dl, '0'
    mov [r12 + rbx], dl
    test r9, r9
    jz .mod_store_done
    test rbx, rbx
    jnz .mod_dig_loop
.mod_store_done:
.mod_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

