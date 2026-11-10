; runtime/x86_64/common/sort.asm
; SORT and MERGE (runtime/ABI.md section 7). The records released to a sort
; file are kept in memory (in chunks), sorted by a stable merge sort of
; pointers to them, and returned in order. MERGE is a sort of its input
; files' records in the order of the files: stability gives equal keys in
; that order, as the standard requires.
;
; Key table (from the program): the number of keys, the collating weights of
; alphanumeric keys (256 bytes, or 0: native order), then for each key its
; offset, length, usage, numeric flags, 1 for DESCENDING, 1 for numeric.

%include "runtime.inc"
%include "common/file.inc"

%define KT_COUNT    0
%define KT_WEIGHTS  8
%define KT_KEYS     16
%define KK_OFF      0
%define KK_LEN      8
%define KK_USAGE    16
%define KK_FLAGS    24
%define KK_DESC     32
%define KK_NUM      40
%define KK_SIZE     48

; Sort state (FCB_SORT)
%define SS_KEYS     0                   ; key table
%define SS_CHUNK    8                   ; current chunk
%define SS_USED     16                  ; bytes used in it
%define SS_CSIZE    24                  ; its size
%define SS_CHUNKS   32                  ; first chunk (chunks are linked by their first qword)
%define SS_PTRS     40                  ; record pointers
%define SS_COUNT    48
%define SS_CAP      56
%define SS_POS      64                  ; next record to return
%define SS_BYTES    72

%define CHUNK_SIZE  1048576

section .text

global runtime_sort_init
global runtime_sort_release
global runtime_sort_sort
global runtime_sort_return
global runtime_sort_end

extern runtime_num_load

; ARG0 = FCB of the sort file, ARG1 = key table
runtime_sort_init:
    push rbx
    push r12
    sub rsp, 8
    mov rbx, ARG0
    mov r12, ARG1
    call free_state
    mov ARG0, SS_BYTES
    call cobrt_os_alloc
    test rax, rax
    jz .error
    mov [rbx + FCB_SORT], rax
    mov [rax + SS_KEYS], r12
    mov qword [rbx + FCB_MODE], 5       ; a sort is in progress
    xor eax, eax
    jmp .done
.error:
    mov eax, 30
.done:
    add rsp, 8
    pop r12
    pop rbx
    ret

; ARG0 = FCB, ARG1 = record, ARG2 = length
runtime_sort_release:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rbx, ARG0
    mov r12, ARG1
    mov r13, ARG2
    mov r14, [rbx + FCB_SORT]
    mov eax, 30
    test r14, r14
    jz .done
    ; Room for the record: its length and its bytes, 8-byte aligned
    lea r15, [r13 + 15]
    and r15, -8
    mov rax, [r14 + SS_USED]
    add rax, r15
    cmp rax, [r14 + SS_CSIZE]
    jbe .room
    ; A new chunk (larger than the record), linked in front
    mov rax, CHUNK_SIZE
    lea rcx, [r15 + 8]
    cmp rcx, rax
    jbe .size
    mov rax, rcx
.size:
    push rax
    sub rsp, 8
    mov ARG0, rax
    call cobrt_os_alloc
    add rsp, 8
    pop rcx
    test rax, rax
    jz .error
    mov rdx, [r14 + SS_CHUNKS]
    mov [rax], rdx                      ; link
    mov [rax + 8], rcx                  ; its size (for freeing)
    mov [r14 + SS_CHUNKS], rax
    mov [r14 + SS_CHUNK], rax
    mov [r14 + SS_CSIZE], rcx
    mov qword [r14 + SS_USED], 16
.room:
    ; The pointer array grows by doubling
    mov rax, [r14 + SS_COUNT]
    cmp rax, [r14 + SS_CAP]
    jb .store
    mov rax, [r14 + SS_CAP]
    shl rax, 1
    cmp rax, 1024
    jae .cap
    mov eax, 1024
.cap:
    push rax
    sub rsp, 8
    mov ARG0, rax
    shl ARG0, 3
    call cobrt_os_alloc
    add rsp, 8
    pop rcx
    test rax, rax
    jz .error
    push rcx
    push rax
    mov rdi, rax
    mov rsi, [r14 + SS_PTRS]
    mov rcx, [r14 + SS_COUNT]
    test rsi, rsi
    jz .copied
    rep movsq
    mov ARG0, [r14 + SS_PTRS]
    mov ARG1, [r14 + SS_CAP]
    shl ARG1, 3
    call cobrt_os_free
.copied:
    pop rax
    pop rcx
    mov [r14 + SS_PTRS], rax
    mov [r14 + SS_CAP], rcx
.store:
    mov rdi, [r14 + SS_CHUNK]
    add rdi, [r14 + SS_USED]
    add [r14 + SS_USED], r15
    mov rax, [r14 + SS_PTRS]
    mov rcx, [r14 + SS_COUNT]
    mov [rax + rcx*8], rdi
    inc qword [r14 + SS_COUNT]
    mov [rdi], r13
    add rdi, 8
    mov rsi, r12
    mov rcx, r13
    rep movsb
    xor eax, eax
    jmp .done
.error:
    mov eax, 30
.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; Compare the records at ARG0 and ARG1 (their length qwords) by the keys;
; eax < 0, 0, > 0. rbx = sort state.
compare_records:
    push r12
    push r13
    push r14
    push r15
    push rbp
    lea r12, [ARG0 + 8]
    lea r13, [ARG1 + 8]
    mov r14, [rbx + SS_KEYS]
    xor r15d, r15d                      ; key
.key:
    cmp r15, [r14 + KT_COUNT]
    jae .equal
    mov rbp, r15
    imul rbp, rbp, KK_SIZE
    lea rbp, [r14 + KT_KEYS + rbp]
    cmp qword [rbp + KK_NUM], 0
    jne .numeric
    ; Alphanumeric: by collating weight
    mov rsi, r12
    add rsi, [rbp + KK_OFF]
    mov rdi, r13
    add rdi, [rbp + KK_OFF]
    mov rcx, [rbp + KK_LEN]
    mov r8, [r14 + KT_WEIGHTS]
.byte:
    test rcx, rcx
    jz .next
    movzx eax, byte [rsi]
    movzx edx, byte [rdi]
    test r8, r8
    jz .native
    movzx eax, byte [r8 + rax]
    movzx edx, byte [r8 + rdx]
.native:
    sub eax, edx
    jnz .differ
    inc rsi
    inc rdi
    dec rcx
    jmp .byte
.numeric:
    mov ARG0, r12
    add ARG0, [rbp + KK_OFF]
    mov ARG1, [rbp + KK_LEN]
    mov ARG2, [rbp + KK_USAGE]
    mov ARG3, [rbp + KK_FLAGS]
    call runtime_num_load
    push rax
    sub rsp, 8
    mov ARG0, r13
    add ARG0, [rbp + KK_OFF]
    mov ARG1, [rbp + KK_LEN]
    mov ARG2, [rbp + KK_USAGE]
    mov ARG3, [rbp + KK_FLAGS]
    call runtime_num_load
    add rsp, 8
    pop rcx
    cmp rcx, rax
    je .next
    mov eax, 1
    jg .differ
    mov eax, -1
.differ:
    cmp qword [rbp + KK_DESC], 0
    je .done
    neg eax
    jmp .done
.next:
    inc r15
    jmp .key
.equal:
    xor eax, eax
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; ARG0 = FCB: sort the records released
runtime_sort_sort:
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rbp
    sub rsp, 24
    mov rbx, [ARG0 + FCB_SORT]
    mov eax, 30
    test rbx, rbx
    jz .done
    mov qword [rbx + SS_POS], 0
    mov r12, [rbx + SS_COUNT]           ; n
    cmp r12, 2
    jb .ok
    ; Bottom-up merge sort: runs of width w from src to dst, then swap
    mov ARG0, r12
    shl ARG0, 3
    call cobrt_os_alloc
    test rax, rax
    jz .error
    mov [rsp], rax                      ; the second array
    mov r13, [rbx + SS_PTRS]            ; src
    mov r14, rax                        ; dst
    mov r15d, 1                         ; width
.pass:
    cmp r15, r12
    jae .sorted
    xor ebp, ebp                        ; start of the pair of runs
.pair:
    cmp rbp, r12
    jae .pass_done
    ; i = start, mid = min(start + w, n), end = min(start + 2w, n)
    lea rax, [rbp + r15]
    cmp rax, r12
    jbe .mid
    mov rax, r12
.mid:
    mov [rsp + 8], rax                  ; mid
    lea rcx, [rbp + r15*2]
    cmp rcx, r12
    jbe .end
    mov rcx, r12
.end:
    mov [rsp + 16], rcx                 ; end
    ; Merge src[start, mid) and src[mid, end) into dst[start, end)
    push rbp                            ; k (output index) = start
    push rbp                            ; i = start
    mov rax, [rsp + 24]                 ; mid (2 pushes moved rsp)
    push rax                            ; j = mid
    sub rsp, 8
    ; frame: [rsp+8] j, [rsp+16] i, [rsp+24] k, [rsp+32] array2, [rsp+40] mid, [rsp+48] end
.merge:
    mov rcx, [rsp + 16]                 ; i
    mov rdx, [rsp + 8]                  ; j
    cmp rcx, [rsp + 40]
    jae .take_j
    cmp rdx, [rsp + 48]
    jae .take_i
    mov ARG0, [r13 + rcx*8]
    mov ARG1, [r13 + rdx*8]
    call compare_records
    test eax, eax
    jg .take_j                          ; equal keys: the earlier one first
.take_i:
    mov rcx, [rsp + 16]
    cmp rcx, [rsp + 40]
    jae .merged
    mov rax, [r13 + rcx*8]
    inc qword [rsp + 16]
    jmp .put
.take_j:
    mov rdx, [rsp + 8]
    cmp rdx, [rsp + 48]
    jae .merged
    mov rax, [r13 + rdx*8]
    inc qword [rsp + 8]
.put:
    mov rcx, [rsp + 24]
    mov [r14 + rcx*8], rax
    inc qword [rsp + 24]
    jmp .merge
.merged:
    add rsp, 8
    pop rax
    pop rax
    pop rax
    mov rbp, [rsp + 16]                 ; the next pair starts at end
    jmp .pair
.pass_done:
    xchg r13, r14
    shl r15, 1
    jmp .pass
.sorted:
    ; The sorted pointers are in r13: keep them, free the other array
    mov rax, [rbx + SS_PTRS]
    cmp r13, rax
    je .free_other
    mov rcx, [rbx + SS_CAP]
    mov [rbx + SS_PTRS], r13
    mov ARG0, rax
    mov ARG1, rcx
    shl ARG1, 3
    mov [rbx + SS_CAP], r12
    call cobrt_os_free
    jmp .ok
.free_other:
    mov ARG0, r14
    mov ARG1, r12
    shl ARG1, 3
    call cobrt_os_free
.ok:
    xor eax, eax
    jmp .done
.error:
    mov eax, 30
.done:
    add rsp, 24
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ARG0 = FCB, ARG1 = record area: the next record in order (status 10 at
; the end); its length in FCB_RECLEN
runtime_sort_return:
    push rbx
    mov rbx, ARG0
    mov rdi, ARG1                       ; record area
    mov rax, [rbx + FCB_SORT]
    test rax, rax
    jz .error
    mov rcx, [rax + SS_POS]
    cmp rcx, [rax + SS_COUNT]
    jae .at_end
    inc qword [rax + SS_POS]
    mov rax, [rax + SS_PTRS]
    mov rsi, [rax + rcx*8]
    mov rcx, [rsi]
    add rsi, 8
    mov [rbx + FCB_RECLEN], rcx
    mov rdx, [rbx + FCB_SIZE]
    cmp rcx, rdx
    jbe .copy
    mov rcx, rdx
.copy:
    sub rdx, rcx
    rep movsb
    mov rcx, rdx                        ; the rest of the area: spaces
    mov al, ' '
    rep stosb
    xor eax, eax
    pop rbx
    ret
.at_end:
    mov eax, 10
    pop rbx
    ret
.error:
    mov eax, 30
    pop rbx
    ret

; ARG0 = FCB: the sort is over
runtime_sort_end:
    push rbx
    mov rbx, ARG0
    call free_state
    mov qword [rbx + FCB_MODE], 0
    xor eax, eax
    pop rbx
    ret

; Free the sort state of the FCB in rbx
free_state:
    push r12
    push r13
    sub rsp, 8
    mov r12, [rbx + FCB_SORT]
    test r12, r12
    jz .done
    mov r13, [r12 + SS_CHUNKS]
.chunk:
    test r13, r13
    jz .ptrs
    mov ARG0, r13
    mov ARG1, [r13 + 8]
    mov r13, [r13]
    call cobrt_os_free
    jmp .chunk
.ptrs:
    mov ARG0, [r12 + SS_PTRS]
    test ARG0, ARG0
    jz .self
    mov ARG1, [r12 + SS_CAP]
    shl ARG1, 3
    call cobrt_os_free
.self:
    mov ARG0, r12
    mov ARG1, SS_BYTES
    call cobrt_os_free
    mov qword [rbx + FCB_SORT], 0
.done:
    add rsp, 8
    pop r13
    pop r12
    ret
