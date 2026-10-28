; runtime/x86_64/common/accept.asm
; ACCEPT support: read one line from standard input into a field.

%include "runtime.inc"

section .text

global runtime_accept
global runtime_accept_numeric

extern runtime_num_store

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


; Read one line from stdin and store it as a number.
; Accepts optional leading spaces, an optional sign, digits and an optional
; decimal point; the first other character ends the number. Fraction digits
; beyond the field scale are dropped.
;
; ARG0 = field ptr, ARG1 = size, ARG2 = digits, ARG3 = scale,
; ARG4 = usage, ARG5 = flags
runtime_accept_numeric:
    push rbp
    mov rbp, rsp
    sub rsp, 112                        ; saved args [rbp-48, rbp), line [rbp-112, rbp-48)
    mov [rbp-8], ARG0
    mov [rbp-16], ARG1
    mov [rbp-24], ARG2
    mov [rbp-32], ARG3
    mov [rbp-40], ARG4
    mov [rbp-48], ARG5

    lea ARG0, [rbp-112]                 ; read the line, space padded
    mov ARG1, 64
    xor ARG2, ARG2
    xor ARG3, ARG3
    mov ARG4, ' '
    call runtime_accept

    ; Device data is transferred left-justified: when the line is an unsigned
    ; integer, a DISPLAY item takes at most as many characters as it is long
    ; (the rest is truncated). Signed or decimal input is converted instead.
    mov rax, [rbp-40]
    cmp rax, USAGE_DISPLAY
    jne .an_scan
    xor ecx, ecx
.an_check:
    cmp rcx, 64
    jae .an_truncate
    movzx edx, byte [rbp-112 + rcx]
    inc rcx
    cmp dl, ' '
    je .an_check
    sub dl, '0'
    cmp dl, 9
    jbe .an_check
    jmp .an_scan                        ; not an unsigned integer
.an_truncate:
    mov rcx, [rbp-16]                   ; item size
    cmp rcx, 64
    jae .an_scan
.an_blank:
    mov byte [rbp-112 + rcx], ' '
    inc rcx
    cmp rcx, 64
    jb .an_blank
.an_scan:
    xor eax, eax                        ; value
    xor ecx, ecx                        ; index into the line
    xor r8d, r8d                        ; negative
    xor r9d, r9d                        ; fraction digits taken
    xor r10d, r10d                      ; decimal point seen
    mov r11, [rbp-32]                   ; scale
.an_skip:
    cmp rcx, 64
    jae .an_done
    movzx edx, byte [rbp-112 + rcx]
    cmp dl, ' '
    jne .an_sign
    inc rcx
    jmp .an_skip
.an_sign:
    cmp dl, '+'
    je .an_sign_next
    cmp dl, '-'
    jne .an_digits
    mov r8d, 1
.an_sign_next:
    inc rcx
.an_digits:
    cmp rcx, 64
    jae .an_done
    movzx edx, byte [rbp-112 + rcx]
    inc rcx
    cmp dl, '.'
    jne .an_not_point
    mov r10d, 1
    jmp .an_digits
.an_not_point:
    sub dl, '0'
    cmp dl, 9
    ja .an_done                         ; first other character ends the number
    test r10d, r10d
    jz .an_take
    cmp r9, r11
    jae .an_digits                      ; beyond the field scale: dropped
    inc r9
.an_take:
    imul rax, rax, 10
    add rax, rdx
    jmp .an_digits
.an_done:
    cmp r9, r11                         ; fewer fraction digits than the scale
    jae .an_sign_apply
    imul rax, rax, 10
    inc r9
    jmp .an_done
.an_sign_apply:
    test r8d, r8d
    jz .an_store
    neg rax
.an_store:
    mov ARG5, rax                       ; value
    mov ARG0, [rbp-8]
    mov ARG1, [rbp-16]
    mov ARG2, [rbp-24]                  ; digits
    mov ARG3, [rbp-40]                  ; usage
    mov ARG4, [rbp-48]                  ; flags
    and ARG4, ~NUM_KEEP
    call runtime_num_store
    leave
    ret

global runtime_accept_date

; Write n digits of value rax at [rdi + rcx], advancing rcx (n in r8)
put_digits:
    push rbx
    mov rbx, 10
    lea r9, [rcx + r8]                  ; end
    mov r10, r9
.pd_loop:
    cmp r10, rcx
    jbe .pd_done
    dec r10
    xor edx, edx
    div rbx
    add dl, '0'
    mov [rdi + r10], dl
    jmp .pd_loop
.pd_done:
    mov rcx, r9
    pop rbx
    ret

; Current date or time (UTC) as digits.
; ARG0 = buffer, ARG1 = 0 DATE (YYMMDD), 1 DAY (YYDDD), 2 TIME (HHMMSShh),
; 3 DAY-OF-WEEK (1 = Monday ... 7 = Sunday)
runtime_accept_date:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 24
    mov r12, ARG0                       ; buffer
    mov r13, ARG1                       ; kind
    lea ARG0, [rsp]
    call cobrt_os_time
    mov rax, [rsp]                      ; seconds
    xor edx, edx
    mov rcx, 86400
    div rcx
    mov r14, rax                        ; days since 1970-01-01
    mov r15, rdx                        ; seconds of the day

    cmp r13, 2
    je .time
    cmp r13, 3
    je .weekday

    ; Civil date from days (Howard Hinnant's algorithm), days >= 0
    lea rax, [r14 + 719468]             ; z
    xor edx, edx
    mov rcx, 146097
    div rcx                             ; rax = era, rdx = doe
    mov rbx, rax                        ; era
    mov r8, rdx                         ; doe
    ; yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365
    mov rax, r8
    xor edx, edx
    mov rcx, 1460
    div rcx
    mov r9, r8
    sub r9, rax
    mov rax, r8
    xor edx, edx
    mov rcx, 36524
    div rcx
    add r9, rax
    mov rax, r8
    xor edx, edx
    mov rcx, 146096
    div rcx
    sub r9, rax
    mov rax, r9
    xor edx, edx
    mov rcx, 365
    div rcx
    mov r9, rax                         ; yoe
    imul r10, rbx, 400
    add r10, r9                         ; y = yoe + era * 400
    ; doy = doe - (365*yoe + yoe/4 - yoe/100)
    imul r11, r9, 365
    mov rax, r9
    shr rax, 2
    add r11, rax
    mov rax, r9
    xor edx, edx
    mov rcx, 100
    div rcx
    sub r11, rax
    mov rax, r8
    sub rax, r11                        ; doy
    mov [rsp + 16], rax
    ; mp = (5*doy + 2) / 153
    imul rax, rax, 5
    add rax, 2
    xor edx, edx
    mov rcx, 153
    div rcx
    mov r11, rax                        ; mp
    ; d = doy - (153*mp + 2)/5 + 1
    imul rax, r11, 153
    add rax, 2
    xor edx, edx
    mov rcx, 5
    div rcx
    mov rbx, [rsp + 16]
    sub rbx, rax
    inc rbx                             ; d
    ; m = mp < 10 ? mp + 3 : mp - 9; y += (m <= 2)
    lea rax, [r11 + 3]
    cmp r11, 10
    jb .month
    lea rax, [r11 - 9]
.month:
    mov r11, rax                        ; m
    cmp r11, 2
    ja .year_ok
    inc r10
.year_ok:
    cmp r13, 1
    je .day
    ; DATE: YYMMDD
    xor ecx, ecx
    mov rdi, r12
    mov rax, r10
    xor edx, edx
    push rcx
    mov rcx, 100
    div rcx
    pop rcx
    mov rax, rdx
    mov r8, 2
    call put_digits
    mov rax, r11
    mov r8, 2
    call put_digits
    mov rax, rbx
    mov r8, 2
    call put_digits
    jmp .done

.day:
    ; DAY: YYDDD; day of year = days - days(y-01-01) + 1
    lea rax, [r10 - 1]                  ; y - 1 (January counts in the year before)
    xor edx, edx
    mov rcx, 400
    div rcx                             ; rax = era, rdx = yoe
    imul rbx, rax, 146097
    mov r8, rdx
    imul r9, r8, 365
    mov rax, r8
    shr rax, 2
    add r9, rax
    mov rax, r8
    xor edx, edx
    mov rcx, 100
    div rcx
    sub r9, rax
    add r9, 306                         ; day of the "March year" of January 1
    add rbx, r9
    sub rbx, 719468                     ; days of January 1
    mov rax, r14
    sub rax, rbx
    inc rax
    push rax
    mov rax, r10
    xor edx, edx
    mov rcx, 100
    div rcx
    mov rax, rdx
    xor ecx, ecx
    mov rdi, r12
    mov r8, 2
    call put_digits
    pop rax
    mov r8, 3
    call put_digits
    jmp .done

.weekday:
    lea rax, [r14 + 3]                  ; 1970-01-01 was a Thursday
    xor edx, edx
    mov rcx, 7
    div rcx
    lea rax, [rdx + 1]
    xor ecx, ecx
    mov rdi, r12
    mov r8, 1
    call put_digits
    jmp .done

.time:
    ; HHMMSShh
    xor ecx, ecx
    mov rdi, r12
    mov rax, r15
    xor edx, edx
    mov rbx, 3600
    div rbx
    mov r14, rdx                        ; seconds within the hour
    mov r8, 2
    call put_digits
    mov rax, r14
    xor edx, edx
    mov rbx, 60
    div rbx
    mov r14, rdx
    mov r8, 2
    call put_digits
    mov rax, r14
    mov r8, 2
    call put_digits
    mov rax, [rsp + 8]                  ; nanoseconds
    xor edx, edx
    mov rbx, 10000000
    div rbx
    mov r8, 2
    call put_digits

.done:
    add rsp, 24
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
