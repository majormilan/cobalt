; runtime/x86_64/common/accept.asm
; ACCEPT support: read one line from standard input into a field.

%include "runtime.inc"

section .text

global runtime_accept

; Read one line from stdin into dest_var, padding to field size.
; Reads one byte at a time so it never consumes bytes beyond the \n.
; Skips \r to handle Windows-style line endings gracefully.
; If the user types more than 'size' characters, excess chars are discarded
; until \n is reached.
;
; ARG0 = dest_var (pointer)
; ARG1 = size     (field size in bytes)
; ARG2 = decimal_places (unused for reading)
; ARG3 = usage_type     (unused for reading)
; ARG4 = pad_char ('0' for numeric fields, ' ' for alphanumeric)
runtime_accept:
    push rbp
    mov rbp, rsp
    sub rsp, 8          ; [rbp-8] = 1-byte scratch for single-byte reads (+7 bytes padding for alignment)
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0       ; r12 = dest_var
    mov r13, ARG1       ; r13 = field size
    mov r14, ARG4       ; r14b = pad_char
    xor r15, r15        ; r15 = bytes stored so far

.read_loop:
    mov ARG0, COBRT_STDIN
    lea ARG1, [rbp-8]   ; scratch byte
    mov ARG2, 1
    call cobrt_os_read

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

