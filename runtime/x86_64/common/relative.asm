; runtime/x86_64/common/relative.asm
; ORGANIZATION RELATIVE (runtime/ABI.md section 7). The file is an array of
; slots, record number k at offset (k - 1) * slot size; a slot is an 8-byte
; record length (0: no record) and the record area. The record number of an
; operation is FCB_KEY: the program stores its RELATIVE KEY item there
; before, and takes it from there after, as the statement requires.
; Called from file.asm with rbx = FCB; returns the status in eax.

%include "runtime.inc"
%include "common/file.inc"

section .text

global cobrt_rel_open
global cobrt_rel_close
global cobrt_rel_read
global cobrt_rel_write
global cobrt_rel_rewrite
global cobrt_rel_delete
global cobrt_rel_start

; Slot size in rax
slot_size:
    mov rax, [rbx + FCB_SIZE]
    add rax, 8
    ret

; Number of slots in the file, in rax (or <0)
slot_count:
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_size
    test rax, rax
    js .done
    push rax
    call slot_size
    mov rcx, rax
    pop rax
    xor edx, edx
    div rcx
.done:
    ret

; Length of the record in slot ARG0 (1-based) in rax: 0 if the slot is
; empty or beyond the end of the file
slot_length:
    sub rsp, 8
    push rdi
    call slot_size
    pop rdi
    dec rdi
    imul rax, rdi                       ; file offset of the slot
    mov ARG3, rax
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, rsp
    mov qword [rsp], 0
    mov ARG2, 8
    call cobrt_os_pread
    cmp rax, 8
    mov rax, [rsp]
    je .done
    xor eax, eax
.done:
    add rsp, 8
    ret

; ARG0 = OPEN mode
cobrt_rel_open:
    push r12
    push r13
    sub rsp, 8
    mov r12, ARG0
    lea rax, [rel rel_open_modes]
    mov ARG1, [rax + r12*8]
    mov ARG0, [rbx + FCB_NAME]
    call cobrt_os_open
    test rax, rax
    jns .opened
    cmp rax, OS_ERR_NOT_FOUND
    jne .error
    mov eax, 35
    test qword [rbx + FCB_FLAGS], FILE_OPTIONAL
    jz .done
    ; OPTIONAL file that does not exist: created empty
    mov ARG0, [rbx + FCB_NAME]
    mov ARG1, OS_OPEN_RW_CREATE
    call cobrt_os_open
    test rax, rax
    js .error
    mov [rbx + FCB_FD], rax
    mov r13d, 5
    jmp .position
.opened:
    mov [rbx + FCB_FD], rax
    xor r13d, r13d
.position:
    ; Sequential READ starts at record 1; sequential WRITE (OUTPUT) writes
    ; record 1 first, EXTEND the record after the last one
    mov qword [rbx + FCB_POS], 1
    cmp r12, OPEN_EXTEND
    jne .ok
    call slot_count
    inc rax
    mov [rbx + FCB_POS], rax
.ok:
    mov eax, r13d
    jmp .done
.error:
    cmp rax, OS_ERR_PERMISSION
    mov eax, 30
    jne .done
    mov eax, 37
.done:
    add rsp, 8
    pop r13
    pop r12
    ret

cobrt_rel_close:
    sub rsp, 8
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_close
    add rsp, 8
    xor eax, eax
    ret

; Copy record ARG0 (whose length is ARG1) into the record area ARG2
read_slot:
    push r12
    push r13
    push r14
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    cmp r13, [rbx + FCB_SIZE]
    jbe .fits
    mov r13, [rbx + FCB_SIZE]
.fits:
    call slot_size
    lea rcx, [r12 - 1]
    imul rax, rcx
    lea ARG3, [rax + 8]
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r14
    mov ARG2, r13
    call cobrt_os_pread
    mov [rbx + FCB_RECLEN], r13
    mov [rbx + FCB_KEY], r12
    mov [rbx + FCB_RECPOS], r12
    lea rax, [r12 + 1]
    mov [rbx + FCB_POS], rax            ; the next sequential READ
    or qword [rbx + FCB_STATE], ST_READ_OK
    pop r14
    pop r13
    pop r12
    ret

; ARG0 = record area, ARG1 = READ_NEXT / READ_KEY
cobrt_rel_read:
    push r12
    push r13
    push r14
    mov r12, ARG0
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    cmp ARG1, READ_KEY
    je .random
    mov eax, 46                         ; no valid next record
    test qword [rbx + FCB_STATE], ST_AT_END
    jnz .done
    call slot_count
    mov r14, rax                        ; slots in the file
    mov r13, [rbx + FCB_POS]
.scan:
    cmp r13, r14
    jg .at_end
    mov ARG0, r13
    call slot_length
    test rax, rax
    jnz .found
    inc r13
    jmp .scan
.found:
    ; A record number too large for the RELATIVE KEY item (FCB_KEYS holds
    ; the largest it can take)
    mov rcx, [rbx + FCB_KEYS]
    test rcx, rcx
    jz .read
    cmp r13, rcx
    jbe .read
    lea rcx, [r13 + 1]
    mov [rbx + FCB_POS], rcx
    mov eax, 14
    jmp .done
.read:
    mov ARG0, r13
    mov ARG1, rax
    mov ARG2, r12
    call read_slot
    xor eax, eax
    jmp .done
.at_end:
    or qword [rbx + FCB_STATE], ST_AT_END
    mov eax, 10
    jmp .done
.random:
    mov r13, [rbx + FCB_KEY]
    mov eax, 23                         ; no such record
    test r13, r13
    jle .done
    mov ARG0, r13
    call slot_length
    test rax, rax
    mov ecx, 23
    jz .not_found
    mov ARG0, r13
    mov ARG1, rax
    mov ARG2, r12
    call read_slot
    and qword [rbx + FCB_STATE], ~ST_AT_END
    xor eax, eax
    jmp .done
.not_found:
    mov eax, ecx
.done:
    pop r14
    pop r13
    pop r12
    ret

; Write record ARG1 (length ARG2) into slot ARG0; returns <0 on error
write_slot:
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    call slot_size
    lea rcx, [r12 - 1]
    imul rax, rcx
    mov r15, rax                        ; slot offset
    mov [rsp], r14
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, rsp
    mov ARG2, 8
    mov ARG3, r15
    call cobrt_os_pwrite
    cmp rax, 8
    jne .error
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r13
    mov ARG2, [rbx + FCB_SIZE]          ; the whole area: slots stay full size
    lea ARG3, [r15 + 8]
    call cobrt_os_pwrite
    cmp rax, [rbx + FCB_SIZE]
    jne .error
    xor eax, eax
    jmp .done
.error:
    mov rax, -1
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Length ARG0 within the record limits? eax = 0 or 44
check_length:
    xor eax, eax
    cmp ARG0, [rbx + FCB_SIZE]
    ja .bad
    cmp ARG0, [rbx + FCB_MIN]
    jb .bad
    ret
.bad:
    mov eax, 44
    ret

; ARG0 = record, ARG1 = length
cobrt_rel_write:
    push r12
    push r13
    push r14
    mov r12, ARG0
    mov r13, ARG1
    mov eax, 48                         ; not open for output
    mov rcx, [rbx + FCB_MODE]
    cmp rcx, OPEN_INPUT
    je .done
    test rcx, rcx
    jz .done
    mov ARG0, r13
    call check_length
    test eax, eax
    jnz .done
    ; Sequential access, or OPEN OUTPUT / EXTEND with sequential access:
    ; the next record number; otherwise the RELATIVE KEY
    test qword [rbx + FCB_FLAGS], ACC_RANDOM | ACC_DYNAMIC
    jnz .by_key
    mov eax, 48
    cmp qword [rbx + FCB_MODE], OPEN_IO
    je .done                            ; sequential access cannot WRITE in I-O
    mov r14, [rbx + FCB_POS]
    inc qword [rbx + FCB_POS]
    mov [rbx + FCB_KEY], r14
    jmp .write
.by_key:
    mov r14, [rbx + FCB_KEY]
    mov eax, 24                         ; not a record number
    test r14, r14
    jle .done
    mov ARG0, r14
    call slot_length
    test rax, rax
    mov eax, 22                         ; a record with that number exists
    jnz .done
.write:
    mov ARG0, r14
    mov ARG1, r12
    mov ARG2, r13
    call write_slot
    test rax, rax
    mov eax, 30
    js .done
    xor eax, eax
.done:
    pop r14
    pop r13
    pop r12
    ret

; ARG0 = record, ARG1 = length
cobrt_rel_rewrite:
    push r12
    push r13
    push r14
    mov r12, ARG0
    mov r13, ARG1
    mov ARG0, r13
    call check_length
    test eax, eax
    jnz .done
    test qword [rbx + FCB_FLAGS], ACC_RANDOM | ACC_DYNAMIC
    jnz .by_key
    mov eax, 43                         ; no successful READ before
    test qword [rbx + FCB_STATE], ST_READ_OK
    jz .done
    mov r14, [rbx + FCB_RECPOS]
    jmp .write
.by_key:
    mov r14, [rbx + FCB_KEY]
    mov eax, 23
    test r14, r14
    jle .done
    mov ARG0, r14
    call slot_length
    test rax, rax
    mov eax, 23                         ; no such record
    jz .done
.write:
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov ARG0, r14
    mov ARG1, r12
    mov ARG2, r13
    call write_slot
    test rax, rax
    mov eax, 30
    js .done
    xor eax, eax
.done:
    pop r14
    pop r13
    pop r12
    ret

cobrt_rel_delete:
    push r12
    push r13
    sub rsp, 8
    test qword [rbx + FCB_FLAGS], ACC_RANDOM | ACC_DYNAMIC
    jnz .by_key
    mov eax, 43                         ; no successful READ before
    test qword [rbx + FCB_STATE], ST_READ_OK
    jz .done
    mov r12, [rbx + FCB_RECPOS]
    jmp .delete
.by_key:
    mov r12, [rbx + FCB_KEY]
    mov eax, 23
    test r12, r12
    jle .done
    mov ARG0, r12
    call slot_length
    test rax, rax
    mov eax, 23
    jz .done
.delete:
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    call slot_size
    lea rcx, [r12 - 1]
    imul rax, rcx
    mov qword [rsp], 0
    mov ARG3, rax
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, rsp
    mov ARG2, 8
    call cobrt_os_pwrite
    cmp rax, 8
    mov eax, 30
    jne .done
    xor eax, eax
.done:
    add rsp, 8
    pop r13
    pop r12
    ret

; ARG0 = START_* condition on the RELATIVE KEY
cobrt_rel_start:
    push r12
    push r13
    push r14
    mov r12, ARG0
    mov r13, [rbx + FCB_KEY]
    cmp r12, START_GT
    jne .from
    inc r13
.from:
    test r13, r13
    jg .positive
    cmp r12, START_EQ
    je .not_found
    mov r13d, 1
.positive:
    call slot_count
    mov r14, rax
.scan:
    cmp r13, r14
    jg .not_found
    mov ARG0, r13
    call slot_length
    test rax, rax
    jnz .found
    cmp r12, START_EQ
    je .not_found
    inc r13
    jmp .scan
.found:
    mov [rbx + FCB_POS], r13            ; the next sequential READ reads it
    mov [rbx + FCB_KEY], r13
    and qword [rbx + FCB_STATE], ~ST_AT_END
    xor eax, eax
    jmp .done
.not_found:
    or qword [rbx + FCB_STATE], ST_AT_END ; no valid position
    mov eax, 23
.done:
    pop r14
    pop r13
    pop r12
    ret

section .rodata
; cobrt_os_open mode for each OPEN mode: INPUT reads, OUTPUT creates (read
; and write: WRITE checks existing slots), I-O and EXTEND update
rel_open_modes: dq 0, OS_OPEN_READ, OS_OPEN_RW_TRUNC, OS_OPEN_READWRITE, OS_OPEN_READWRITE
