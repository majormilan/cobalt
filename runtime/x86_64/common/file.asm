; runtime/x86_64/common/file.asm
; Files (runtime/ABI.md section 7). Every routine takes the file control
; block (FCB) of the file, sets its FILE STATUS item (if any) and returns the
; status code (0..99) in RET0. This file holds the routines generated code
; calls and the sequential organizations (record sequential, fixed or
; variable length, and line sequential); relative.asm and indexed.asm hold
; the other organizations.

%include "runtime.inc"
%include "common/file.inc"

section .text

global runtime_file_open
global runtime_file_close
global runtime_file_read
global runtime_file_write
global runtime_file_rewrite
global runtime_file_delete
global runtime_file_start
global cobrt_file_status
global cobrt_files_close_all
global cobrt_buf_alloc

extern cobrt_rel_open
extern cobrt_rel_close
extern cobrt_rel_read
extern cobrt_rel_write
extern cobrt_rel_rewrite
extern cobrt_rel_delete
extern cobrt_rel_start
extern cobrt_idx_open
extern cobrt_idx_close
extern cobrt_idx_read
extern cobrt_idx_write
extern cobrt_idx_rewrite
extern cobrt_idx_delete
extern cobrt_idx_start

; Set the status of the FCB in rbx to eax; returns it in eax. A status of 30
; or more on a file without a FILE STATUS item or USE procedure ends the run.
cobrt_file_status:
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
    jb .ret
    test qword [rbx + FCB_FLAGS], FILE_USE
    jz .fatal
.ret:
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
    call cobrt_files_close_all
    mov ARG0, 1
    call cobrt_os_exit

; The I-O buffer of the FCB in rbx (allocated once, kept after CLOSE);
; returns it in rax, or 0
cobrt_buf_alloc:
    mov rax, [rbx + FCB_BUF]
    test rax, rax
    jnz .done
    mov ARG0, FILE_BUFSZ
    call cobrt_os_alloc
    mov [rbx + FCB_BUF], rax
.done:
    ret

; Add the FCB in rbx to the list of open files
link_open:
    mov rax, [rel open_files]
    mov [rbx + FCB_NEXT], rax
    mov [rel open_files], rbx
    ret

; Remove the FCB in rbx from the list of open files
unlink_open:
    lea rcx, [rel open_files]
.find:
    mov rax, [rcx]
    test rax, rax
    jz .done
    cmp rax, rbx
    je .found
    lea rcx, [rax + FCB_NEXT]
    jmp .find
.found:
    mov rax, [rbx + FCB_NEXT]
    mov [rcx], rax
.done:
    ret

; Close every open file (end of the run unit)
cobrt_files_close_all:
    push rbx
.next:
    mov rbx, [rel open_files]
    test rbx, rbx
    jz .done
    mov ARG0, rbx
    xor ARG1, ARG1
    call close_file
    jmp .next
.done:
    pop rbx
    ret

; ── Buffered input ──────────────────────────────────────────────────────────
; In INPUT and I-O mode the buffer holds FCB_BUFLEN bytes of the file from
; offset FCB_BUFPOS; FCB_POS is the offset of the next byte to read.

; Refill the buffer at FCB_POS; returns the bytes now available
in_fill:
    mov rax, [rbx + FCB_POS]
    mov [rbx + FCB_BUFPOS], rax
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, [rbx + FCB_BUF]
    mov ARG2, FILE_BUFSZ
    mov ARG3, rax
    call cobrt_os_pread
    test rax, rax
    jns .ok
    xor eax, eax
.ok:
    mov [rbx + FCB_BUFLEN], rax
    ret

; Next byte of the file in eax, or -1 at the end
in_byte:
    mov rax, [rbx + FCB_POS]
    sub rax, [rbx + FCB_BUFPOS]
    cmp rax, [rbx + FCB_BUFLEN]
    jb .have
    call in_fill
    test rax, rax
    jz .eof
    xor eax, eax
.have:
    mov rcx, [rbx + FCB_BUF]
    movzx eax, byte [rcx + rax]
    inc qword [rbx + FCB_POS]
    ret
.eof:
    mov eax, -1
    ret

; Copy up to ARG1 bytes of the file to ARG0; returns the bytes copied
in_bytes:
    push r12
    push r13
    push r14
    mov r12, ARG0                       ; destination
    mov r13, ARG1                       ; bytes wanted
    xor r14d, r14d                      ; bytes copied
.loop:
    test r13, r13
    jz .done
    mov rax, [rbx + FCB_POS]
    sub rax, [rbx + FCB_BUFPOS]
    mov rcx, [rbx + FCB_BUFLEN]
    cmp rax, rcx
    jb .have
    call in_fill
    test rax, rax
    jz .done
    mov rcx, rax
    xor eax, eax
.have:
    sub rcx, rax                        ; available
    cmp rcx, r13
    jbe .take
    mov rcx, r13
.take:
    mov rsi, [rbx + FCB_BUF]
    add rsi, rax
    mov rdi, r12
    add r12, rcx
    add r14, rcx
    sub r13, rcx
    add [rbx + FCB_POS], rcx
    rep movsb
    jmp .loop
.done:
    mov rax, r14
    pop r14
    pop r13
    pop r12
    ret

; ── Buffered output ─────────────────────────────────────────────────────────
; In OUTPUT and EXTEND mode the buffer holds FCB_BUFLEN bytes not yet written.

; Write the buffer out; returns <0 on error
out_flush:
    push r12
    push r13
    mov r12, [rbx + FCB_BUF]
    mov r13, [rbx + FCB_BUFLEN]
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
    mov qword [rbx + FCB_BUFLEN], 0
    xor eax, eax
    pop r13
    pop r12
    ret
.err:
    mov qword [rbx + FCB_BUFLEN], 0
    mov rax, -1
    pop r13
    pop r12
    ret

; Append ARG2 bytes from ARG1 to the output; returns <0 on error
write_bytes:
    push r12
    push r13
    mov r12, ARG1
    mov r13, ARG2
.loop:
    test r13, r13
    jz .ok
    mov rax, FILE_BUFSZ
    sub rax, [rbx + FCB_BUFLEN]
    jnz .room
    call out_flush
    test rax, rax
    js .err
    mov rax, FILE_BUFSZ
.room:
    cmp rax, r13
    jbe .take
    mov rax, r13
.take:
    mov rcx, rax
    mov rdi, [rbx + FCB_BUF]
    add rdi, [rbx + FCB_BUFLEN]
    mov rsi, r12
    add r12, rcx
    sub r13, rcx
    add [rbx + FCB_BUFLEN], rcx
    rep movsb
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

; Write one character (in dil)
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
    jle .done
    mov edi, r14d
    call write_char
    dec r15
    jmp .loop
.done:
    pop r15
    pop r14
    ret

; ── OPEN ────────────────────────────────────────────────────────────────────

; ARG0 = FCB, ARG1 = OPEN_* mode | OPT_* options
runtime_file_open:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rbx, ARG0
    mov r12, ARG1
    and r12, 0xFF                       ; mode
    mov r13, ARG1
    shr r13, 8                          ; options
    mov [rbx + FCB_LAST_MODE], r12
    mov eax, 41                         ; already open
    cmp qword [rbx + FCB_MODE], 0
    jne .status
    mov eax, 38                         ; closed WITH LOCK in this run
    cmp qword [rbx + FCB_LOCK], 0
    jne .status
    call cobrt_buf_alloc
    test rax, rax
    jz .error
    mov qword [rbx + FCB_STATE], 0
    mov qword [rbx + FCB_BUFLEN], 0
    mov qword [rbx + FCB_BUFPOS], 0
    mov qword [rbx + FCB_POS], 0
    test qword [rbx + FCB_FLAGS], ORG_RELATIVE
    jz .not_rel
    mov ARG0, r12
    call cobrt_rel_open
    jmp .opened
.not_rel:
    test qword [rbx + FCB_FLAGS], ORG_INDEXED
    jz .sequential
    mov ARG0, r12
    call cobrt_idx_open
.opened:
    cmp eax, 10
    jae .status
    mov [rbx + FCB_MODE], r12
    push rax
    call link_open
    pop rax
    jmp .status

.sequential:
    lea rax, [rel open_modes]
    mov r14, [rax + r12*8]              ; OS open mode
    mov ARG0, [rbx + FCB_NAME]
    mov ARG1, r14
    call cobrt_os_open
    test rax, rax
    js .failed
    mov [rbx + FCB_FD], rax
    xor r15d, r15d                      ; status
    jmp .ready
.failed:
    cmp rax, OS_ERR_NOT_FOUND
    jne .error
    test qword [rbx + FCB_FLAGS], FILE_OPTIONAL
    jz .not_found
    ; OPTIONAL file that does not exist: INPUT reads AT END, I-O and EXTEND
    ; create it
    mov r15d, 5
    cmp r12, OPEN_INPUT
    jne .create
    mov qword [rbx + FCB_STATE], ST_ABSENT
    jmp .ready
.create:
    mov ARG0, [rbx + FCB_NAME]
    mov ARG1, OS_OPEN_RW_CREATE
    cmp r12, OPEN_EXTEND
    jne .create_rw
    mov ARG1, OS_OPEN_APPEND
.create_rw:
    call cobrt_os_open
    test rax, rax
    js .error
    mov [rbx + FCB_FD], rax
.ready:
    mov [rbx + FCB_MODE], r12
    call link_open
    ; REVERSED: read fixed-length records from the last one
    test r13, OPT_REVERSED
    jz .no_reverse
    test qword [rbx + FCB_STATE], ST_ABSENT
    jnz .no_reverse
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_size
    test rax, rax
    js .no_reverse
    xor edx, edx
    div qword [rbx + FCB_SIZE]
    mul qword [rbx + FCB_SIZE]          ; whole records only
    mov [rbx + FCB_POS], rax
    or qword [rbx + FCB_STATE], ST_REVERSED
.no_reverse:
    test r13, OPT_NO_REWIND
    jz .no_rewind
    test r15d, r15d
    jnz .no_rewind
    mov r15d, 7                         ; not a reel or unit
.no_rewind:
    ; LINAGE: the first page; LINAGE-COUNTER starts at 1
    test qword [rbx + FCB_FLAGS], FILE_LINAGE
    jz .no_linage
    call linage_page
    cmp r12, OPEN_OUTPUT
    jne .no_linage
    mov rcx, [rbx + FCB_LINAGE]
    mov r12, [rcx + LIN_TOP_A]          ; top margin
    mov edi, 10
    call write_chars
.no_linage:
    mov eax, r15d
    jmp .status
.error:                                 ; rax = the OS error
    mov qword [rbx + FCB_MODE], 0
    cmp rax, OS_ERR_PERMISSION
    mov eax, 30
    jne .status
    mov eax, 37                         ; the open mode is not permitted
    jmp .status
.not_found:
    mov eax, 35
.status:
    call cobrt_file_status
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; Start a LINAGE page: take the current LINAGE values (which the program
; stored in the LINAGE block) and set LINAGE-COUNTER to 1
linage_page:
    mov rcx, [rbx + FCB_LINAGE]
    mov rax, [rcx + LIN_LINES]
    mov [rcx + LIN_LINES_A], rax
    mov rax, [rcx + LIN_FOOTING]
    mov [rcx + LIN_FOOTING_A], rax
    mov rax, [rcx + LIN_TOP]
    mov [rcx + LIN_TOP_A], rax
    mov rax, [rcx + LIN_BOTTOM]
    mov [rcx + LIN_BOTTOM_A], rax
    mov qword [rcx + LIN_COUNTER], 1
    ret

; ── CLOSE ───────────────────────────────────────────────────────────────────

; ARG0 = FCB, ARG1 = CLOSE_* options
runtime_file_close:
    push rbx
    mov rbx, ARG0
    call close_file
    call cobrt_file_status
    pop rbx
    ret

; Close the file in rbx with options ARG1; returns the status
close_file:
    push r12
    push r13
    sub rsp, 8
    mov r12, ARG1
    mov eax, 42                         ; not open
    cmp qword [rbx + FCB_MODE], 0
    je .done
    ; REEL / UNIT (FOR REMOVAL) on a disk file: no effect, the file stays open
    mov eax, 7
    test r12, CLOSE_REEL
    jnz .done
    call unlink_open
    mov rax, [rbx + FCB_FLAGS]
    test rax, ORG_RELATIVE
    jz .not_rel
    call cobrt_rel_close
    jmp .closed
.not_rel:
    test rax, ORG_INDEXED
    jz .sequential
    call cobrt_idx_close
    jmp .closed
.sequential:
    test qword [rbx + FCB_STATE], ST_ABSENT
    jnz .closed
    mov rax, [rbx + FCB_MODE]
    cmp rax, OPEN_OUTPUT
    je .output
    cmp rax, OPEN_EXTEND
    jne .close_fd
.output:
    test qword [rbx + FCB_STATE], ST_LINE_OPEN
    jz .flush
    mov edi, 10                         ; end the last text line
    call write_char
.flush:
    call out_flush
.close_fd:
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_close
.closed:
    mov qword [rbx + FCB_MODE], 0
    mov qword [rbx + FCB_STATE], 0
    test r12, CLOSE_LOCK
    jz .no_lock
    mov qword [rbx + FCB_LOCK], 1
.no_lock:
    xor eax, eax
    test r12, CLOSE_NO_REWIND
    jz .done
    mov eax, 7
.done:
    add rsp, 8
    pop r13
    pop r12
    ret

; ── READ ────────────────────────────────────────────────────────────────────

; ARG0 = FCB, ARG1 = record area, ARG2 = READ_* how (NEXT, or by key)
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
    mov rax, [rbx + FCB_FLAGS]
    test rax, ORG_RELATIVE
    jz .not_rel
    mov ARG0, r12
    mov ARG1, ARG2
    call cobrt_rel_read
    jmp .status
.not_rel:
    test rax, ORG_INDEXED
    jz .sequential
    mov ARG0, r12
    mov ARG1, ARG2
    call cobrt_idx_read
    jmp .status
.sequential:
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov eax, 46                         ; read after AT END
    test qword [rbx + FCB_STATE], ST_AT_END
    jnz .status
    test qword [rbx + FCB_STATE], ST_ABSENT
    jnz .at_end
    mov r13, [rbx + FCB_SIZE]
    xor r14d, r14d                      ; bytes stored
    xor r15d, r15d                      ; status
    mov rax, [rbx + FCB_POS]
    mov [rbx + FCB_RECPOS], rax
    test qword [rbx + FCB_STATE], ST_REVERSED
    jnz .reversed
    test qword [rbx + FCB_FLAGS], FILE_LINE
    jnz .line
    test qword [rbx + FCB_FLAGS], FILE_VARIABLE
    jnz .variable

    ; Fixed length: the next record-size bytes
    mov ARG0, r12
    mov ARG1, r13
    call in_bytes
    mov r14, rax
    test r14, r14
    jz .at_end
    mov [rbx + FCB_RECLEN], r14
    mov [rbx + FCB_LASTLEN], r14
    cmp r14, r13
    jae .ok
    mov r15d, 4                         ; a short last record: space padded
    jmp .pad

    ; Variable length: a 4-byte header (length, big-endian, in the first
    ; two bytes) before each record
.variable:
    sub rsp, 8
    mov ARG0, rsp
    mov ARG1, 4
    call in_bytes
    movzx ecx, byte [rsp]
    shl ecx, 8
    movzx edx, byte [rsp + 1]
    or ecx, edx
    add rsp, 8
    test rax, rax
    jz .at_end
    mov [rbx + FCB_RECLEN], rcx
    mov [rbx + FCB_LASTLEN], rcx
    mov r14, rcx
    cmp rcx, r13
    jbe .var_fits
    mov r14, r13
    mov r15d, 4                         ; longer than the record area
.var_fits:
    cmp rcx, [rbx + FCB_MIN]
    jae .var_min
    mov r15d, 4                         ; shorter than the minimum
.var_min:
    push rcx
    sub rsp, 8
    mov ARG0, r12
    mov ARG1, r14
    call in_bytes
    add rsp, 8
    pop rcx
    sub rcx, r14                        ; skip what does not fit
    add [rbx + FCB_POS], rcx
    jmp .ok

    ; REVERSED: the record before the current position
.reversed:
    mov rax, [rbx + FCB_POS]
    cmp rax, r13
    jb .at_end
    sub rax, r13
    mov [rbx + FCB_POS], rax
    mov [rbx + FCB_RECPOS], rax
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r12
    mov ARG2, r13
    mov ARG3, rax
    call cobrt_os_pread
    cmp rax, r13
    jne .io_error
    mov [rbx + FCB_RECLEN], r13
    mov [rbx + FCB_LASTLEN], r13
    jmp .ok

    ; Line sequential: one text line, space padded, excess dropped
.line:
    xor r15d, r15d                      ; characters seen on the line
.line_loop:
    call in_byte
    test eax, eax
    js .line_eof
    cmp al, 10
    je .line_end
    cmp al, 13
    je .line_loop
    cmp al, 12
    je .line_loop                       ; form feeds of a printed file
    inc r15
    cmp r14, r13
    jae .line_loop                      ; beyond the record: dropped
    mov [r12 + r14], al
    inc r14
    jmp .line_loop
.line_eof:
    test r15, r15
    jz .at_end
.line_end:
    mov [rbx + FCB_RECLEN], r14
    mov [rbx + FCB_LASTLEN], r14
    xor r15d, r15d
.pad:
    cmp r14, r13
    jae .ok
    mov byte [r12 + r14], ' '
    inc r14
    jmp .pad
.ok:
    or qword [rbx + FCB_STATE], ST_READ_OK
    mov eax, r15d
    jmp .status
.io_error:
    mov eax, 30
    jmp .status
.at_end:
    or qword [rbx + FCB_STATE], ST_AT_END
    mov eax, 10
.status:
    call cobrt_file_status
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ── WRITE ───────────────────────────────────────────────────────────────────

; ARG0 = FCB, ARG1 = record, ARG2 = record length (-1: FCB_RECLEN, set by
; the program from RECORD VARYING DEPENDING ON), ARG3 = advancing mode,
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
    and qword [rbx + FCB_STATE], ~(ST_EOP | ST_READ_OK)
    cmp r14, -1
    jne .have_len
    mov r14, [rbx + FCB_RECLEN]
.have_len:
    mov rax, [rbx + FCB_FLAGS]
    test rax, ORG_RELATIVE | ORG_INDEXED
    jz .sequential
    mov ARG0, r13
    mov ARG1, r14
    test rax, ORG_RELATIVE
    jz .indexed
    call cobrt_rel_write
    jmp .status
.indexed:
    call cobrt_idx_write
    jmp .status
.sequential:
    mov eax, 48                         ; not open for output
    mov rcx, [rbx + FCB_MODE]
    cmp rcx, OPEN_OUTPUT
    je .open_ok
    cmp rcx, OPEN_EXTEND
    jne .status
.open_ok:
    mov eax, 44                         ; record length out of range
    cmp r14, [rbx + FCB_SIZE]
    ja .status
    test qword [rbx + FCB_FLAGS], FILE_VARIABLE
    jz .length_ok
    cmp r14, [rbx + FCB_MIN]
    jb .status
.length_ok:
    test qword [rbx + FCB_FLAGS], FILE_LINAGE
    jnz .linage
    test r15, r15
    jnz .text
    test qword [rbx + FCB_FLAGS], FILE_LINE
    jnz .text
    test qword [rbx + FCB_FLAGS], FILE_VARIABLE
    jnz .variable

    ; Record sequential, fixed length: the whole record area
    mov ARG1, r13
    mov ARG2, [rbx + FCB_SIZE]
    call write_bytes
    test rax, rax
    js .error
    jmp .ok

.variable:
    sub rsp, 8
    mov qword [rsp], 0                  ; big-endian length, two NUL bytes
    mov ecx, r14d
    mov [rsp + 1], cl
    shr ecx, 8
    mov [rsp], cl
    mov ARG1, rsp
    mov ARG2, 4
    call write_bytes
    add rsp, 8
    test rax, rax
    js .error
    mov ARG1, r13
    mov ARG2, r14
    call write_bytes
    test rax, rax
    js .error
    jmp .ok

    ; LINAGE: AFTER n moves down n lines, then prints; BEFORE n prints, then
    ; moves down. Moving past the page body goes to the first line of the
    ; next page (the rest of the page body, the bottom and top margins).
    ; The end-of-page condition: LINAGE-COUNTER reached the footing area,
    ; or a new page started.
.linage:
    cmp r15, ADV_NONE
    jne .lin_mode
    mov r15, ADV_BEFORE
    mov r12, 1
.lin_mode:
    cmp r15, ADV_AFTER
    je .lin_after
    cmp r15, ADV_AFTER_PAGE
    je .lin_after_page
    test qword [rbx + FCB_STATE], ST_LINE_OPEN
    jz .lin_before_print
    ; BEFORE after AFTER: a printer prints on the same line (LINAGE-COUNTER
    ; does not change); a text file needs a new line for it
    mov edi, 10
    call write_char
.lin_before_print:
    call .print
    and qword [rbx + FCB_STATE], ~ST_LINE_OPEN
    cmp r15, ADV_BEFORE_PAGE
    je .lin_before_page
    call .lin_advance
    jmp .lin_eop
.lin_before_page:
    call .lin_new_page
    jmp .lin_eop
.lin_after:
    call .lin_advance
    call .print
    or qword [rbx + FCB_STATE], ST_LINE_OPEN
    jmp .lin_eop
.lin_after_page:
    call .lin_new_page
    call .print
    or qword [rbx + FCB_STATE], ST_LINE_OPEN
.lin_eop:
    mov rcx, [rbx + FCB_LINAGE]
    mov rax, [rcx + LIN_FOOTING_A]
    test rax, rax
    jz .ok
    cmp [rcx + LIN_COUNTER], rax
    jb .ok
    or qword [rbx + FCB_STATE], ST_EOP
    jmp .ok

    ; Move down r12 lines within the page, or to the next page
.lin_advance:
    mov rcx, [rbx + FCB_LINAGE]
    mov rax, [rcx + LIN_COUNTER]
    add rax, r12
    cmp rax, [rcx + LIN_LINES_A]
    ja .lin_new_page
    mov [rcx + LIN_COUNTER], rax
    mov edi, 10
    jmp write_chars                     ; returns to .lin_advance's caller

    ; To the first line of the next page
.lin_new_page:
    mov rcx, [rbx + FCB_LINAGE]
    mov r12, [rcx + LIN_LINES_A]
    sub r12, [rcx + LIN_COUNTER]
    add r12, [rcx + LIN_BOTTOM_A]
    inc r12                             ; rest of the body, bottom margin
    call linage_page
    mov rcx, [rbx + FCB_LINAGE]
    add r12, [rcx + LIN_TOP_A]          ; top margin of the new page
    or qword [rbx + FCB_STATE], ST_EOP
    mov edi, 10
    jmp write_chars

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
    call cobrt_file_status
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ── REWRITE ─────────────────────────────────────────────────────────────────

; ARG0 = FCB, ARG1 = record, ARG2 = record length (-1: FCB_RECLEN)
runtime_file_rewrite:
    push rbx
    push r12
    push r13
    mov rbx, ARG0
    mov r12, ARG1
    mov r13, ARG2
    cmp r13, -1
    jne .have_len
    mov r13, [rbx + FCB_RECLEN]
.have_len:
    mov eax, 49                         ; not open I-O
    cmp qword [rbx + FCB_MODE], OPEN_IO
    jne .status
    mov rax, [rbx + FCB_FLAGS]
    test rax, ORG_RELATIVE | ORG_INDEXED
    jz .sequential
    mov ARG0, r12
    mov ARG1, r13
    test rax, ORG_RELATIVE
    jz .indexed
    call cobrt_rel_rewrite
    jmp .status
.indexed:
    call cobrt_idx_rewrite
    jmp .status
.sequential:
    mov eax, 43                         ; no successful READ before
    test qword [rbx + FCB_STATE], ST_READ_OK
    jz .status
    mov eax, 44                         ; another length than the record read
    cmp r13, [rbx + FCB_LASTLEN]
    jne .status
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov rax, [rbx + FCB_RECPOS]
    test qword [rbx + FCB_FLAGS], FILE_VARIABLE
    jz .at
    add rax, 4                          ; after the length header
.at:
    test qword [rbx + FCB_FLAGS], FILE_LINE
    jnz .text
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r12
    mov ARG2, r13
    mov ARG3, rax
    call cobrt_os_pwrite
    cmp rax, r13
    jne .error
    mov qword [rbx + FCB_BUFLEN], 0     ; the buffer may hold the old record
    xor eax, eax
    jmp .status
.text:
    mov eax, 30                         ; a text line cannot be rewritten
    jmp .status
.error:
    mov eax, 30
.status:
    and qword [rbx + FCB_STATE], ~ST_READ_OK ; REWRITE was the last I-O statement
    call cobrt_file_status
    pop r13
    pop r12
    pop rbx
    ret

; ── DELETE, START (relative and indexed files) ──────────────────────────────

; ARG0 = FCB
runtime_file_delete:
    push rbx
    mov rbx, ARG0
    mov eax, 49                         ; not open I-O
    cmp qword [rbx + FCB_MODE], OPEN_IO
    jne .status
    mov rax, [rbx + FCB_FLAGS]
    test rax, ORG_RELATIVE
    jz .not_rel
    call cobrt_rel_delete
    jmp .status
.not_rel:
    test rax, ORG_INDEXED
    jz .sequential
    call cobrt_idx_delete
    jmp .status
.sequential:
    mov eax, 30
.status:
    and qword [rbx + FCB_STATE], ~ST_READ_OK ; DELETE was the last I-O statement
    call cobrt_file_status
    pop rbx
    ret

; ARG0 = FCB, ARG1 = START_* condition, ARG2 = key (indexed: key number),
; ARG3 = key length compared (indexed: the leading part of the key)
runtime_file_start:
    push rbx
    mov rbx, ARG0
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov rax, [rbx + FCB_MODE]
    cmp rax, OPEN_INPUT
    je .open_ok
    cmp rax, OPEN_IO
    mov eax, 47                         ; not open INPUT or I-O
    jne .status
.open_ok:
    mov rax, [rbx + FCB_FLAGS]
    test rax, ORG_RELATIVE
    jz .not_rel
    mov ARG0, ARG1
    call cobrt_rel_start
    jmp .status
.not_rel:
    test rax, ORG_INDEXED
    jz .sequential
    mov ARG0, ARG1
    mov ARG1, ARG2
    mov ARG2, ARG3
    call cobrt_idx_start
    jmp .status
.sequential:
    mov eax, 30
.status:
    call cobrt_file_status
    pop rbx
    ret

section .bss
open_files: resq 1                      ; FCBs of the open files (FCB_NEXT chain)

section .rodata
; cobrt_os_open mode for each OPEN mode (index OPEN_*)
open_modes: dq 0, OS_OPEN_READ, OS_OPEN_WRITE, OS_OPEN_READWRITE, OS_OPEN_APPEND_OLD
msg_io: db "COBALT runtime: I/O error on file "
msg_io_len equ $ - msg_io
