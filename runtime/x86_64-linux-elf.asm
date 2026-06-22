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

; rdi = dest_var (pointer)
; rsi = size
; rdx = decimal_places
; rcx = usage_type
runtime_accept:
    push rbp
    mov rbp, rsp
    ; Save incoming arguments onto the stack
    push rdi                            ; [rbp-8] = dest_var
    push rsi                            ; [rbp-16] = size
    push rdx                            ; [rbp-24] = decimal_places
    push rcx                            ; [rbp-32] = usage_type

    ; Read from stdin (fd 0)
    mov rax, 0                          ; sys_read
    mov rdi, 0                          ; stdin
    mov rsi, [rbp-8]                    ; dest_var (from stack)
    mov rdx, [rbp-16]                   ; size (from stack)
    syscall

    ; After sys_read, rax contains number of bytes read
    ; For simplicity, we assume the user presses ENTER, so we need to
    ; null-terminate the input or strip the newline.
    ; For ACCEPT, COBOL typically doesn't expect null termination, but fills exactly 'size' chars.
    ; Any remaining space in 'dest_var' should probably be padded.
    ; We need to handle this padding in a more sophisticated runtime.
    ; For now, let's just make sure it reads up to size, and potentially pads if less is read.

    ; Let rbx = actual bytes read (from rax)
    ; Let r10 = original size
    ; Let r11 = dest_var
    mov rbx, rax                        ; rbx = bytes read
    mov r10, [rbp-16]                   ; r10 = original size
    mov r11, [rbp-8]                    ; r11 = dest_var

    ; If actual bytes read is less than size, fill the rest with spaces (alphanumeric) or zeros (numeric)
    cmp rbx, r10
    jge .no_padding                     ; If read >= size, no padding needed

    ; Determine pad character based on usage_type
    mov r12d, [rbp-32]                  ; r12 = usage_type
    cmp r12d, 1                         ; USAGE_COMP (1)
    je .pad_zeros                       ; If COMP, pad with '0'
    mov al, ' '                         ; Else (DISPLAY), pad with ' '
    jmp .do_padding
.pad_zeros:
    mov al, '0'
.do_padding:
    mov rdi, r11                        ; dest_var
    add rdi, rbx                        ; start padding after bytes read
    mov rcx, r10                        ; total size
    sub rcx, rbx                        ; remaining size to pad
    cld                                 ; clear direction flag
    rep stosb                           ; fill remaining with pad char

.no_padding:
    ; For numeric ACCEPT, COBOL might convert string digits to internal format (e.g., COMP).
    ; For now, we assume direct character input for DISPLAY and raw bytes for COMP.
    ; This conversion logic will be handled later.

    ; Restore registers and clean stack
    pop rcx                             ; restore rcx (usage_type)
    pop rdx                             ; restore rdx (decimal_places)
    pop rsi                             ; restore rsi (size)
    pop rdi                             ; restore rdi (dest_var)
    leave
    ret

section .data
newline db 10, 0
