; runtime/x86_64/common/file.asm
; Sequential files (runtime/ABI.md section 7). Every routine takes the file
; control block (FCB) of the file, sets its FILE STATUS item (if any) and
; returns the status code (0..99) in RET0.

%include "runtime.inc"

; File control block layout
%define FCB_FD          0               ; OS handle while open
%define FCB_MODE        8               ; 0 closed, OPEN_* while open
%define FCB_SIZE        16              ; record area size in bytes
%define FCB_FLAGS       24              ; FILE_* flags
%define FCB_NAME        32              ; NUL-terminated file name
%define FCB_STATUS      40              ; FILE STATUS item (2 bytes), or 0
%define FCB_STATE       48              ; ST_* bits

%define FILE_LINE       1               ; line sequential: text lines
%define FILE_OPTIONAL   2               ; SELECT OPTIONAL

%define ST_AT_END       1               ; an AT END condition was reported
%define ST_READ_OK      2               ; the last READ succeeded (for REWRITE)
%define ST_LINE_OPEN    4               ; text output: the current line has text
%define ST_ABSENT       8               ; OPTIONAL file that does not exist

%define OPEN_INPUT      1
%define OPEN_OUTPUT     2
%define OPEN_IO         3
%define OPEN_EXTEND     4

; WRITE advancing modes
%define ADV_NONE        0
%define ADV_BEFORE      1
%define ADV_AFTER       2
%define ADV_BEFORE_PAGE 3
%define ADV_AFTER_PAGE  4

section .text

global runtime_file_open
global runtime_file_close
global runtime_file_read
global runtime_file_write
global runtime_file_rewrite

; Set the status of the FCB in rbx to eax; returns it in eax. A status of 30
; or more on a file without a FILE STATUS item ends the run.
file_status:
    mov rcx, [rbx + FCB_STATUS]
    test rcx, rcx
    jz .no_item
    push rax
    xor edx, edx
    mov r8d, 10
    div r8d                             ; eax = tens, edx = units
    add al, '0'
    add dl, '0'
    mov [rcx], al
    mov [rcx + 1], dl
    pop rax
    ret
.no_item:
    cmp eax, 30
    jae .fatal
    ret
.fatal:
    push rax
    mov ARG0, COBRT_STDERR
    lea ARG1, [rel msg_io]
    mov ARG2, msg_io_len
    call cobrt_os_write
    mov rsi, [rbx + FCB_NAME]           ; file name
    xor edx, edx
.name_len:
    cmp byte [rsi + rdx], 0
    je .name_done
    inc rdx
    jmp .name_len
.name_done:
    mov ARG2, rdx
    mov ARG1, rsi
    mov ARG0, COBRT_STDERR
    call cobrt_os_write
    pop rax
    sub rsp, 8                          ; status digits
    xor edx, edx
    mov r8d, 10
    div r8d
    add al, '0'
    add dl, '0'
    mov byte [rsp], ' '
    mov [rsp + 1], al
    mov [rsp + 2], dl
    mov byte [rsp + 3], 10
    mov ARG0, COBRT_STDERR
    mov ARG1, rsp
    mov ARG2, 4
    call cobrt_os_write
    mov ARG0, 1
    call cobrt_os_exit

; Write ARG2 bytes from ARG1 to the open file in rbx; returns <0 on error
write_bytes:
    push r12
    push r13
    mov r12, ARG1
    mov r13, ARG2
.loop:
    test r13, r13
    jz .ok
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r12
    mov ARG2, r13
    call cobrt_os_write
    test rax, rax
    jle .err
    add r12, rax
    sub r13, rax
    jmp .loop
.ok:
    xor eax, eax
    pop r13
    pop r12
    ret
.err:
    mov rax, -1
    pop r13
    pop r12
    ret

; Write one character (in dil) to the open file in rbx
write_char:
    sub rsp, 8
    mov [rsp], dil
    mov ARG1, rsp
    mov ARG2, 1
    call write_bytes
    add rsp, 8
    ret

; Write the character in dil r12 times
write_chars:
    push r14
    push r15
    mov r14d, edi
    mov r15, r12
.loop:
    test r15, r15
    jz .done
    mov edi, r14d
    call write_char
    dec r15
    jmp .loop
.done:
    pop r15
    pop r14
    ret

; ARG0 = FCB, ARG1 = OPEN_* mode
runtime_file_open:
    push rbx
    push r12
    push r13
    mov rbx, ARG0
    mov r12, ARG1
    mov eax, 41                         ; already open
    cmp qword [rbx + FCB_MODE], 0
    jne .status
    lea rax, [rel open_modes]
    mov r13, [rax + r12*8]              ; OS open mode
    mov ARG0, [rbx + FCB_NAME]
    mov ARG1, r13
    call cobrt_os_open
    test rax, rax
    js .failed
    mov [rbx + FCB_FD], rax
    mov [rbx + FCB_MODE], r12
    mov qword [rbx + FCB_STATE], 0
    xor eax, eax
    jmp .status
.failed:
    cmp rax, OS_ERR_NOT_FOUND
    jne .error
    test qword [rbx + FCB_FLAGS], FILE_OPTIONAL
    jz .not_found
    ; OPTIONAL file that does not exist: INPUT reads AT END, I-O creates it
    mov [rbx + FCB_MODE], r12
    cmp r12, OPEN_INPUT
    jne .create
    mov qword [rbx + FCB_STATE], ST_ABSENT
    mov eax, 5
    jmp .status
.create:
    mov ARG0, [rbx + FCB_NAME]
    mov ARG1, OS_OPEN_RW_CREATE
    call cobrt_os_open
    test rax, rax
    js .error_closed
    mov [rbx + FCB_FD], rax
    mov qword [rbx + FCB_STATE], 0
    mov eax, 5
    jmp .status
.error_closed:
    mov qword [rbx + FCB_MODE], 0
.error:
    mov eax, 30
    jmp .status
.not_found:
    mov eax, 35
.status:
    call file_status
    pop r13
    pop r12
    pop rbx
    ret

; ARG0 = FCB
runtime_file_close:
    push rbx
    push r12
    sub rsp, 8
    mov rbx, ARG0
    mov eax, 42                         ; not open
    cmp qword [rbx + FCB_MODE], 0
    je .status
    test qword [rbx + FCB_STATE], ST_ABSENT
    jnz .closed
    test qword [rbx + FCB_STATE], ST_LINE_OPEN
    jz .no_line
    mov edi, 10                         ; end the last text line
    call write_char
.no_line:
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_close
.closed:
    mov qword [rbx + FCB_MODE], 0
    mov qword [rbx + FCB_STATE], 0
    xor eax, eax
.status:
    call file_status
    add rsp, 8
    pop r12
    pop rbx
    ret

; ARG0 = FCB, ARG1 = record area
runtime_file_read:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rbx, ARG0
    mov r12, ARG1                       ; record area
    mov eax, 47                         ; not open for input
    mov rcx, [rbx + FCB_MODE]
    cmp rcx, OPEN_INPUT
    je .open_ok
    cmp rcx, OPEN_IO
    jne .status
.open_ok:
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov eax, 46                         ; read after AT END
    test qword [rbx + FCB_STATE], ST_AT_END
    jnz .status
    test qword [rbx + FCB_STATE], ST_ABSENT
    jnz .at_end
    mov r13, [rbx + FCB_SIZE]
    xor r14d, r14d                      ; bytes stored
    test qword [rbx + FCB_FLAGS], FILE_LINE
    jnz .line

    ; Record sequential: the next record-size bytes
.rec_loop:
    cmp r14, r13
    jae .ok
    mov ARG0, [rbx + FCB_FD]
    lea ARG1, [r12 + r14]
    mov ARG2, r13
    sub ARG2, r14
    call cobrt_os_read
    test rax, rax
    jle .rec_end
    add r14, rax
    jmp .rec_loop
.rec_end:
    test r14, r14
    jz .at_end
    jmp .pad                            ; a short last record is space padded

    ; Line sequential: one text line, space padded, excess dropped
.line:
    xor r15d, r15d                      ; characters seen on the line
.line_loop:
    sub rsp, 8
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, rsp
    mov ARG2, 1
    call cobrt_os_read
    movzx ecx, byte [rsp]
    add rsp, 8
    test rax, rax
    jle .line_eof
    cmp cl, 10
    je .pad
    cmp cl, 13
    je .line_loop
    inc r15
    cmp r14, r13
    jae .line_loop                      ; beyond the record: dropped
    mov [r12 + r14], cl
    inc r14
    jmp .line_loop
.line_eof:
    test r15, r15
    jz .at_end
.pad:
    cmp r14, r13
    jae .ok
    mov byte [r12 + r14], ' '
    inc r14
    jmp .pad
.ok:
    or qword [rbx + FCB_STATE], ST_READ_OK
    xor eax, eax
    jmp .status
.at_end:
    or qword [rbx + FCB_STATE], ST_AT_END
    mov eax, 10
.status:
    call file_status
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ARG0 = FCB, ARG1 = record, ARG2 = record length, ARG3 = advancing mode,
; ARG4 = advancing lines
runtime_file_write:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rbx, ARG0
    mov r13, ARG1                       ; record
    mov r14, ARG2                       ; length
    mov r15, ARG3                       ; advancing mode
    mov r12, ARG4                       ; advancing lines
    mov eax, 48                         ; not open for output
    mov rcx, [rbx + FCB_MODE]
    cmp rcx, OPEN_OUTPUT
    je .open_ok
    cmp rcx, OPEN_EXTEND
    jne .status
.open_ok:
    test r15, r15
    jnz .text
    test qword [rbx + FCB_FLAGS], FILE_LINE
    jnz .text

    ; Record sequential: the whole record area
    mov ARG1, r13
    mov ARG2, [rbx + FCB_SIZE]
    call write_bytes
    test rax, rax
    js .error
    xor eax, eax
    jmp .status

    ; Text: printer semantics. AFTER n moves down n lines and prints, so
    ; the line stays open; BEFORE n (and no ADVANCING) prints and moves
    ; down n lines. A new line starts only where the cursor is not already
    ; at the start of a line.
.text:
    cmp r15, ADV_NONE
    jne .have_mode
    mov r15, ADV_BEFORE
    mov r12, 1
.have_mode:
    cmp r15, ADV_AFTER
    je .after
    cmp r15, ADV_AFTER_PAGE
    je .after_page
    ; BEFORE: print on a fresh line
    test qword [rbx + FCB_STATE], ST_LINE_OPEN
    jz .print_before
    mov edi, 10
    call write_char
.print_before:
    call .print
    cmp r15, ADV_BEFORE_PAGE
    je .before_page
    mov edi, 10
    call write_chars                    ; r12 newlines
    and qword [rbx + FCB_STATE], ~ST_LINE_OPEN
    test r12, r12
    jnz .ok
    or qword [rbx + FCB_STATE], ST_LINE_OPEN
    jmp .ok
.before_page:
    mov edi, 12                         ; form feed
    call write_char
    and qword [rbx + FCB_STATE], ~ST_LINE_OPEN
    jmp .ok
.after:
    test qword [rbx + FCB_STATE], ST_LINE_OPEN
    jnz .after_lines
    test r12, r12
    jz .after_print
    dec r12                             ; the cursor already starts a line
.after_lines:
    mov edi, 10
    call write_chars
.after_print:
    call .print
    or qword [rbx + FCB_STATE], ST_LINE_OPEN
    jmp .ok
.after_page:
    test qword [rbx + FCB_STATE], ST_LINE_OPEN
    jz .ff
    mov edi, 10
    call write_char
.ff:
    mov edi, 12
    call write_char
    call .print
    or qword [rbx + FCB_STATE], ST_LINE_OPEN
    jmp .ok

    ; Print the record without its trailing spaces
.print:
    mov rcx, r14
.trim:
    test rcx, rcx
    jz .trimmed
    cmp byte [r13 + rcx - 1], ' '
    jne .trimmed
    dec rcx
    jmp .trim
.trimmed:
    mov ARG1, r13
    mov ARG2, rcx
    jmp write_bytes                     ; returns to .print's caller

.ok:
    xor eax, eax
    jmp .status
.error:
    mov eax, 30
.status:
    call file_status
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ARG0 = FCB, ARG1 = record, ARG2 = record length
runtime_file_rewrite:
    push rbx
    push r12
    push r13
    mov rbx, ARG0
    mov r12, ARG1
    mov eax, 49                         ; not open I-O
    cmp qword [rbx + FCB_MODE], OPEN_IO
    jne .status
    mov eax, 43                         ; no successful READ before
    test qword [rbx + FCB_STATE], ST_READ_OK
    jz .status
    mov eax, 30                         ; text files cannot be rewritten
    test qword [rbx + FCB_FLAGS], FILE_LINE
    jnz .status
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov r13, [rbx + FCB_SIZE]
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r13
    neg ARG1
    call cobrt_os_seek
    test rax, rax
    js .error
    mov ARG1, r12
    mov ARG2, r13
    call write_bytes
    test rax, rax
    js .error
    xor eax, eax
    jmp .status
.error:
    mov eax, 30
.status:
    call file_status
    pop r13
    pop r12
    pop rbx
    ret

section .rodata
; cobrt_os_open mode for each OPEN mode (index OPEN_*)
open_modes: dq 0, OS_OPEN_READ, OS_OPEN_WRITE, OS_OPEN_READWRITE, OS_OPEN_APPEND
msg_io: db "COBALT runtime: I/O error on file "
msg_io_len equ $ - msg_io
