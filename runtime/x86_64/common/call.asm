; runtime/x86_64/common/call.asm
; CALL and CANCEL (runtime/ABI.md section 12). The executable holds a table
; of the programs it contains, cobrt_program_table: the addresses of their
; descriptors, then 0. A descriptor is the program's name, the name's
; length, its entry and the address of its state (0: it is initialized on
; its next CALL). A program is entered with ARG0 = the number of arguments
; and ARG1 = the address of an array of their addresses.

%include "runtime.inc"

%define PD_NAME     0
%define PD_LEN      8
%define PD_ENTRY    16
%define PD_STATE    24

section .text

global runtime_call
global runtime_cancel
global runtime_external

extern cobrt_program_table
extern cobrt_files_close_all
extern cobrt_error_begin
extern cobrt_error_text
extern cobrt_error_end

; The descriptor of the program named by ARG0 (ARG1 bytes, trailing spaces
; ignored, case ignored) in rax, or 0
find_program:
    mov r8, ARG1
.trim:
    test r8, r8
    jz .trimmed
    cmp byte [ARG0 + r8 - 1], ' '
    jne .trimmed
    dec r8
    jmp .trim
.trimmed:
    lea r9, [rel cobrt_program_table]
.next:
    mov rax, [r9]
    test rax, rax
    jz .done
    add r9, 8
    cmp [rax + PD_LEN], r8
    jne .next
    mov r10, [rax + PD_NAME]
    xor ecx, ecx
.cmp:
    cmp rcx, r8
    jae .done                           ; found
    movzx edx, byte [ARG0 + rcx]
    movzx r11d, byte [r10 + rcx]
    cmp edx, r11d
    je .same
    or edx, 0x20                        ; letters without regard to case
    or r11d, 0x20
    cmp edx, r11d
    jne .next
    cmp edx, 'a'
    jb .next
    cmp edx, 'z'
    ja .next
.same:
    inc rcx
    jmp .cmp
.done:
    ret

; ARG0 = name, ARG1 = its length, ARG2 = number of arguments, ARG3 = their
; addresses, ARG4 = 1 if the CALL has ON EXCEPTION / OVERFLOW. Returns 0, or
; 1 when the program does not exist and ARG4 = 1 (otherwise the run ends).
runtime_call:
    push rbx
    push r12
    push r13
    mov r12, ARG2
    mov r13, ARG3
    mov rbx, ARG4
    push ARG0
    push ARG1
    call find_program
    pop ARG1
    pop ARG0
    test rax, rax
    jz .missing
    mov ARG0, r12
    mov ARG1, r13
    call [rax + PD_ENTRY]
    xor eax, eax
    jmp .done
.missing:
    mov eax, 1
    test rbx, rbx
    jnz .done
    ; No ON EXCEPTION: the run ends (the name without trailing spaces)
.trim_name:
    test ARG1, ARG1
    jz .trimmed_name
    cmp byte [ARG0 + ARG1 - 1], ' '
    jne .trimmed_name
    dec ARG1
    jmp .trim_name
.trimmed_name:
    push ARG0
    push ARG1
    call cobrt_error_begin
    lea ARG0, [rel msg_missing]
    call cobrt_error_text
    pop ARG2
    pop ARG1
    mov ARG0, COBRT_STDERR
    call cobrt_os_write
    lea ARG0, [rel msg_quote]
    call cobrt_error_text
    call cobrt_error_end
.done:
    pop r13
    pop r12
    pop rbx
    ret

; ARG0 = name, ARG1 = its length: the program is initialized on its next
; CALL (CANCEL of a program not in the run unit has no effect)
runtime_cancel:
    sub rsp, 8
    call find_program
    test rax, rax
    jz .done
    mov rax, [rax + PD_STATE]
    mov qword [rax], 0
.done:
    add rsp, 8
    ret

; EXTERNAL storage (runtime/ABI.md section 12): ARG0 = name, ARG1 = its
; length, ARG2 = size, ARG3 = what it starts as (or 0: zeros). Returns the
; storage of that name: allocated by the first program that asks for it.
; Each entry: next, name, length, storage.
runtime_external:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    mov r15, ARG3
    mov rbx, [rel externals]
.find:
    test rbx, rbx
    jz .new
    cmp [rbx + 16], r13
    jne .skip
    mov rsi, [rbx + 8]
    mov rdi, r12
    mov rcx, r13
    repe cmpsb
    jne .skip
    mov rax, [rbx + 24]
    jmp .done
.skip:
    mov rbx, [rbx]
    jmp .find
.new:
    lea ARG0, [r14 + 32]
    call cobrt_os_alloc
    test rax, rax
    jz .done
    mov rbx, rax
    mov [rbx + 8], r12                  ; the name stays in the program's data
    mov [rbx + 16], r13
    lea rdi, [rbx + 32]
    mov [rbx + 24], rdi
    test r15, r15
    jz .linked
    mov rsi, r15
    mov rcx, r14
    rep movsb
.linked:
    mov rax, [rel externals]
    mov [rbx], rax
    mov [rel externals], rbx
    mov rax, [rbx + 24]
.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; The value of RETURN-CODE (the EXTERNAL item a program using it declares),
; or 0 when no program has it: RET0, for the exit status of STOP RUN
global cobrt_return_code
cobrt_return_code:
    mov rcx, [rel externals]
.next:
    xor eax, eax
    test rcx, rcx
    jz .done
    cmp qword [rcx + 16], rc_name_len
    jne .skip
    mov rdx, [rcx + 8]
    mov rax, [rdx]                      ; the first 8 characters
    cmp rax, [rel rc_name]
    jne .skip
    movzx eax, word [rdx + 8]
    cmp ax, [rel rc_name + 8]
    jne .skip
    movzx eax, byte [rdx + 10]
    cmp al, [rel rc_name + 10]
    jne .skip
    mov rax, [rcx + 24]
    movsxd rax, dword [rax]             ; PIC S9(9) COMP-5
    ret
.skip:
    mov rcx, [rcx]
    jmp .next
.done:
    ret

section .bss
externals: resq 1

section .rodata
msg_missing: db "CALL of a program that is not in the run unit: '", 0
msg_quote: db "'", 0
rc_name: db "RETURN-CODE"
rc_name_len equ $ - rc_name
