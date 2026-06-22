; runtime/x86_64-linux-elf.asm
; COBOL runtime for x86_64 Linux ELF
; Updated: newline after each display, proper trimming, correct decimal placement

section .text

global runtime_init
global runtime_display_string
global runtime_display_numeric
global runtime_move_alphanum
global runtime_move_numeric
global runtime_exit
global runtime_display_fill
global runtime_move_fill
global runtime_display_var_string

runtime_init:
    ret

; rdi = null-terminated string pointer
runtime_display_string:
    push rbp
    mov rbp, rsp
    push rdi

    ; Compute length
    xor rcx, rcx
.strlen_loop:
    cmp byte [rdi + rcx], 0
    je .strlen_done
    inc rcx
    jmp .strlen_loop
.strlen_done:
    mov rdx, rcx                        ; rdx = length

    mov rax, 1                          ; sys_write
    mov rdi, 1                          ; stdout
    pop rsi                             ; string pointer
    syscall

    leave
    ret

; rdi = variable pointer (raw digits)
; rsi = total size in bytes
; rdx = number of decimal places
; rcx = usage_type
runtime_display_numeric:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push rdi                            ; [rbp-8]  = var ptr
    push rsi                            ; [rbp-16] = size
    push rdx                            ; [rbp-24] = decimals
    push rcx                            ; [rbp-32] = usage_type

    sub rsp, 64                         ; temp buffer
    mov r8, rsp                         ; r8 = temp buffer

    mov r9, [rbp-16]                    ; r9 = size (from stack)
    ; Copy raw digits
    mov rcx, r9
    mov rsi, [rbp-8]                    ; var ptr (from stack)
    mov rdi, r8
    rep movsb
    mov byte [r8 + r9], 0               ; null-terminate

    ; Insert '.' if decimals > 0
    mov rdx, [rbp-24]                   ; decimals (from stack)
    test rdx, rdx
    jz .no_dot
    mov rcx, r9                         ; original size
    sub rcx, rdx                        ; position for '.'
    cmp rcx, 0
    jle .no_dot
    mov byte [r8 + rcx], '.'
    inc r9                              ; length += 1 for '.'
.no_dot:

    ; Trim leading '0's (keep one '0' if value is zero)
    mov rsi, r8
.trim_loop:
    cmp byte [rsi], '0'
    jne .trim_done
    inc rsi
    cmp byte [rsi], 0
    jne .trim_loop
    dec rsi                             ; point back to single '0'
.trim_done:

    ; If trimmed result starts with '.', prepend '0'
    cmp byte [rsi], '.'
    jne .print
    mov byte [r8], '0'
    mov rsi, r8
    inc r9

.print:
    ; Print trimmed string
    ; Calculate length of trimmed string
    mov rdx, 0
    mov r10, rsi    ; r10 points to trimmed start
.strlen_trimmed_loop:
    cmp byte [r10 + rdx], 0
    je .strlen_trimmed_done
    inc rdx
    jmp .strlen_trimmed_loop
.strlen_trimmed_done:
    ; rdx now holds the correct length of the trimmed string

    mov rax, 1
    mov rdi, 1
    syscall

    add rsp, 64 ; clean temp buffer
    pop rcx ; restore rcx (usage_type)
    pop rdx ; restore rdx (decimals)
    pop rsi ; restore rsi (size)
    pop rdi ; restore rdi (var ptr)
    leave
    ret

; rdi = var_name (pointer)
; rsi = size
; rdx = usage_type (currently unused)
runtime_display_var_string:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push rdi                            ; [rbp-8] = var_name
    push rsi                            ; [rbp-16] = size
    push rdx                            ; [rbp-24] = usage_type

    ; Call sys_write to display the string
    mov rax, 1                          ; sys_write
    mov rdi, 1                          ; stdout
    mov rsi, [rbp-8]                    ; var_name (from stack)
    mov rdx, [rbp-16]                   ; size (from stack)
    syscall

    ; Restore registers and clean stack
    pop rdx                             ; restore rdx (usage_type)
    pop rsi                             ; restore rsi (size)
    pop rdi                             ; restore rdi (var_name)
    leave
    ret

; rdi = dest buffer
; rsi = src buffer / literal
; rdx = src length
; rcx = dest size
; r8 = usage_type
runtime_move_alphanum:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push rdi                            ; [rbp-8]  = rdi (dest)
    push rsi                            ; [rbp-16] = rsi (src)
    push rdx                            ; [rbp-24] = rdx (src_len)
    push rcx                            ; [rbp-32] = rcx (dest_size)
    push r8                             ; [rbp-40] = r8 (usage_type)

    cld ; Clear direction flag for rep stosb

    ; Fill with spaces
    mov al, ' '
    mov rdi, [rbp-8]                    ; dest_var from stack
    mov rcx, [rbp-32]                   ; dest_size from stack
    rep stosb

    ; Copy source
    mov rdi, [rbp-8]                    ; dest_var
    mov rsi, [rbp-16]                   ; src_buffer
    mov rdx, [rbp-24]                   ; src_len
    mov rcx, [rbp-32]                   ; dest_size
    cmp rdx, rcx
    cmovbe rcx, rdx                     ; rcx = min(src_len, dest_size)
    rep movsb

    ; Restore registers and clean stack
    pop r8                              ; restore r8 (usage_type)
    pop rcx                             ; restore rcx (dest_size)
    pop rdx                             ; restore rdx (src_len)
    pop rsi                             ; restore rsi (src)
    pop rdi                             ; restore rdi (dest)
    leave
    ret

; rdi = dest buffer
; rsi = src digits / literal
; rdx = src length
; rcx = dest size
; r8 = usage_type
runtime_move_numeric:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push rdi                            ; [rbp-8] = rdi (dest)
    push rsi                            ; [rbp-16] = rsi (src)
    push rdx                            ; [rbp-24] = rdx (src_len)
    push rcx                            ; [rbp-32] = rcx (dest_size)
    push r8                             ; [rbp-40] = r8 (usage_type)

    cld ; Clear direction flag for rep stosb

    ; Fill with '0'
    mov al, '0'
    mov rdi, [rbp-8]                    ; dest_var from stack
    mov rcx, [rbp-32]                   ; dest_size from stack
    rep stosb

    ; Right-align source
    mov rdi, [rbp-8]                    ; dest_var
    add rdi, [rbp-32]                   ; dest + dest_size
    sub rdi, [rbp-24]                   ; dest + (dest_size - src_len)
    mov rsi, [rbp-16]                   ; src_buffer
    mov rcx, [rbp-24]                   ; src_len
    rep movsb

    ; Restore registers and clean stack
    pop r8                              ; restore r8 (usage_type)
    pop rcx                             ; restore rcx (dest_size)
    pop rdx                             ; restore rdx (src_len)
    pop rsi                             ; restore rsi (src)
    pop rdi                             ; restore rdi (dest)
    leave
    ret

global runtime_load_int

; Convert a fixed-width ASCII digit buffer to a 64-bit integer.
; rdi = buffer pointer (NOT null-terminated, exactly rsi bytes)
; rsi = buffer size in bytes
; Returns: rax = integer value
runtime_load_int:
    push rbp
    mov rbp, rsp
    push r12
    push r13

    mov r12, rdi        ; r12 = buffer ptr
    mov r13, rsi        ; r13 = size
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
; rdi = dest_ptr  (ASCII digits, dest_size bytes, NOT null-terminated)
; rsi = dest_size
; rdx = src_ptr   (ASCII digits, src_size bytes, NOT null-terminated)
; rcx = src_size
runtime_add_to:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi        ; r12 = dest ptr
    mov r13, rsi        ; r13 = dest_size
    mov r14, rdx        ; r14 = src ptr
    mov r15, rcx        ; r15 = src_size

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

    mov r12, rdi        ; r12 = dest ptr
    mov r13, rsi        ; r13 = dest_size
    mov r14, rdx        ; r14 = src ptr
    mov r15, rcx        ; r15 = src_size

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

    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    mov r15, rcx

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

    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    mov r15, rcx

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

    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    mov r15, rcx

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

runtime_exit:
    mov rax, 60
    xor rdi, rdi
    syscall

; rdi = fill_char
; rsi = size
; rdx = usage_type
runtime_display_fill:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push rdi ; [rbp-8] = fill_char
    push rsi ; [rbp-16] = size
    push rdx ; [rbp-24] = usage_type

    ; Create a temporary buffer on the stack for the output characters
    ; We need 'size' bytes for chars + 1 for null terminator
    mov r8, [rbp-16] ; get size
    add r8, 1        ; add 1 for null terminator
    sub rsp, r8      ; allocate buffer on stack
    mov rdi, rsp     ; rdi = pointer to buffer

    ; Fill the buffer with fill_char
    mov al, [rbp-8]  ; get fill_char
    mov rcx, [rbp-16] ; get size
    cld              ; clear direction flag
    rep stosb        ; fill buffer
    mov byte [rdi], 0 ; null-terminate after fill (rdi points to end after rep stosb)

    ; Call sys_write
    mov rax, 1       ; sys_write
    mov rdi, 1       ; stdout
    mov rsi, rsp     ; pointer to buffer
    mov rdx, [rbp-16] ; length is size
    syscall

    add rsp, r8      ; clean up temp buffer
    pop rdx          ; restore rdx (usage_type)
    pop rsi          ; restore rsi (size)
    pop rdi          ; restore rdi (fill_char)
    leave
    ret

; rdi = dest_var
; rsi = fill_char
; rdx = size
; rcx = usage_type
runtime_move_fill:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push rdi                            ; [rbp-8] = dest_var
    push rsi                            ; [rbp-16] = fill_char
    push rdx                            ; [rbp-24] = size
    push rcx                            ; [rbp-32] = usage_type

    cld ; Clear direction flag for rep stosb

    mov rdi, [rbp-8]                    ; dest_var from stack
    mov al, [rbp-16]                    ; fill_char from stack
    mov rcx, [rbp-24]                   ; size from stack
    
    ; Fill the destination with the character
    rep stosb

    ; Restore registers and clean stack
    pop rcx                             ; restore rcx (usage_type)
    pop rdx                             ; restore rdx (size)
    pop rsi                             ; restore rsi (fill_char)
    pop rdi                             ; restore rdi (dest_var)
    leave
    ret


global runtime_accept

; Read one line from stdin into dest_var, padding to field size.
; Reads one byte at a time so it never consumes bytes beyond the \n.
; Skips \r to handle Windows-style line endings gracefully.
; If the user types more than 'size' characters, excess chars are discarded
; until \n is reached.
;
; rdi = dest_var (pointer)
; rsi = size     (field size in bytes)
; rdx = decimal_places (unused for reading)
; rcx = usage_type     (unused for reading)
; r8  = pad_char ('0' for numeric fields, ' ' for alphanumeric)
runtime_accept:
    push rbp
    mov rbp, rsp
    sub rsp, 8          ; [rbp-8] = 1-byte scratch for single-byte reads (+7 bytes padding for alignment)
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi        ; r12 = dest_var
    mov r13, rsi        ; r13 = field size
    mov r14b, r8b       ; r14b = pad_char
    xor r15, r15        ; r15 = bytes stored so far

.read_loop:
    mov rax, 0          ; sys_read
    mov rdi, 0          ; stdin
    lea rsi, [rbp-8]    ; scratch byte
    mov rdx, 1
    syscall

    test rax, rax
    jle .pad_remaining  ; EOF or error: stop reading

    movzx rbx, byte [rbp-8]

    cmp bl, 10          ; \n
    je .pad_remaining

    cmp bl, 13          ; \r — skip (handle Windows/mixed line endings)
    je .read_loop

    cmp r15, r13        ; field already full?
    jge .read_loop      ; discard extra chars, keep draining until \n

    mov [r12 + r15], bl
    inc r15
    jmp .read_loop

.pad_remaining:
    ; Numeric fields: right-align input, pad left with '0'
    ; Alphanumeric: left-align, pad right with ' '
    cmp r14b, '0'
    je .right_align_numeric

    ; Alphanumeric path: pad [r15..r13) with ' '
    cmp r15, r13
    jge .accept_done
    lea rdi, [r12 + r15]
    mov al, ' '
    mov rcx, r13
    sub rcx, r15
    cld
    rep stosb
    jmp .accept_done

.right_align_numeric:
    cmp r15, r13
    jge .accept_done        ; field exactly filled, already in place
    ; Shift bytes [r12 .. r12+r15) right to [r12+r13-r15 .. r12+r13)
    ; Use backward copy (std) to handle overlap safely.
    ; dst last byte = r12 + r13 - 1
    ; src last byte = r12 + r15 - 1
    mov rdi, r12
    add rdi, r13
    dec rdi
    mov rsi, r12
    add rsi, r15
    dec rsi
    mov rcx, r15
    std
    rep movsb
    cld
    ; Fill [r12 .. r12+r13-r15) with '0'
    mov rdi, r12
    mov al, '0'
    mov rcx, r13
    sub rcx, r15
    rep stosb

.accept_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

section .data
newline db 10, 0
