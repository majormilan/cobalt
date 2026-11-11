; runtime/x86_64/common/indexed.asm
; ORGANIZATION INDEXED (runtime/ABI.md section 7).
;
; The data file is an array of slots: an 8-byte record length (0: free
; slot), an 8-byte sequence number for each key (when the record got its
; value of that key: the order of duplicate alternate keys), and the record
; area. While the file is
; open, each key (the prime RECORD KEY and every ALTERNATE RECORD KEY) has an
; index in memory, built at OPEN: entries {sequence, slot, key bytes} in key
; order (then sequence order), kept in blocks of about a page, with a sorted
; array of the blocks.
;
; The program describes the keys in a key table (FCB_KEYS): the record area,
; the number of keys, then for each key its offset in the record, its length
; and 1 if duplicates are allowed. Key 0 is the prime key. FCB_KEYREF is the
; key of reference: the program sets it before a READ with KEY IS, START
; sets it.
; Called from file.asm with rbx = FCB; returns the status in eax.

%include "runtime.inc"
%include "common/file.inc"

; Key table
%define KT_AREA     0
%define KT_COUNT    8
%define KT_KEYS     16                  ; KT_KEY_* entries
%define KT_KEY_OFF  0
%define KT_KEY_LEN  8
%define KT_KEY_DUPS 16
%define KT_KEY_SIZE 24

; Organization state (FCB_PRIV)
%define PV_NEXTSEQ  0                   ; sequence number of the next record written
%define PV_FREE     8                   ; free slots (array of slot numbers)
%define PV_NFREE    16
%define PV_FCAP     24
%define PV_NSLOTS   32                  ; slots in the file
%define PV_CUR      40                  ; CUR_* bits: the position for READ NEXT
%define PV_CURSEQ   48
%define PV_CURKEY   56                  ; key value of the position (key of reference)
%define PV_ENTRY    64                  ; scratch index entry
%define PV_LASTKEY  72                  ; prime key last written (sequential access)
%define PV_HAVELAST 80
%define PV_MAXK     88                  ; longest key
%define PV_OLDREC   96                  ; scratch record (the record replaced or deleted)
%define PV_BYTES    104
%define PV_HDR      112                 ; slot header read (length, sequence numbers)
%define PV_HDR2     120                 ; slot header to write
%define PV_IX       128                 ; index of each key
%define PV_FIXED    128                 ; size without the index pointers

%define CUR_VALID   1                   ; PV_CURKEY / PV_CURSEQ hold a position
%define CUR_AFTER   2                   ; READ NEXT reads the entry after it
%define CUR_NONE    4                   ; no valid position (READ NEXT: 46)

; Index
%define IX_NBLK     0                   ; blocks in use
%define IX_CAP      8                   ; capacity of the block array
%define IX_BLKS     16                  ; block array (pointers)
%define IX_ESIZE    24                  ; entry size: 16 + key length
%define IX_KLEN     32
%define IX_BCAP     40                  ; entries per block
%define IX_BSIZE    48                  ; bytes of a block
%define IX_BYTES    64
; Block: a count, then the entries
%define BL_COUNT    0
%define BL_ENTRIES  8
; Entry
%define EN_SEQ      0
%define EN_SLOT     8
%define EN_KEY      16

section .text

global cobrt_idx_open
global cobrt_idx_close
global cobrt_idx_read
global cobrt_idx_write
global cobrt_idx_rewrite
global cobrt_idx_delete
global cobrt_idx_start

; ── Memory ──────────────────────────────────────────────────────────────────

; Copy ARG2 bytes from ARG1 to ARG0 (the areas do not overlap, or ARG0 is
; below ARG1)
copy_down:
    mov rcx, ARG2
    rep movsb
    ret

; Copy ARG2 bytes from ARG1 to ARG0 with ARG0 above ARG1 (overlapping)
copy_up:
    mov rcx, ARG2
    test rcx, rcx
    jz .done
    lea rdi, [ARG0 + rcx - 1]
    lea rsi, [ARG1 + rcx - 1]
    std
    rep movsb
    cld
.done:
    ret

; ── Index ───────────────────────────────────────────────────────────────────

; A new empty index for keys of ARG0 bytes; returns it in rax, or 0
index_new:
    push r12
    mov r12, ARG0
    mov ARG0, IX_BYTES
    call cobrt_os_alloc
    test rax, rax
    jz .done
    mov [rax + IX_KLEN], r12
    lea rcx, [r12 + 16]
    mov [rax + IX_ESIZE], rcx
    push rax
    mov eax, 4096 - BL_ENTRIES
    xor edx, edx
    div rcx
    cmp rax, 8
    jae .bcap
    mov eax, 8
.bcap:
    mov rcx, rax
    pop rax
    mov [rax + IX_BCAP], rcx
    imul rcx, [rax + IX_ESIZE]
    add rcx, BL_ENTRIES
    mov [rax + IX_BSIZE], rcx
.done:
    pop r12
    ret

; Free index ARG0
index_free:
    push r12
    push r13
    push r14
    mov r12, ARG0
    test r12, r12
    jz .done
    xor r13d, r13d
.blocks:
    cmp r13, [r12 + IX_NBLK]
    jae .array
    mov rax, [r12 + IX_BLKS]
    mov ARG0, [rax + r13*8]
    mov ARG1, [r12 + IX_BSIZE]
    call cobrt_os_free
    inc r13
    jmp .blocks
.array:
    mov ARG0, [r12 + IX_BLKS]
    test ARG0, ARG0
    jz .self
    mov ARG1, [r12 + IX_CAP]
    shl ARG1, 3
    call cobrt_os_free
.self:
    mov ARG0, r12
    mov ARG1, IX_BYTES
    call cobrt_os_free
.done:
    pop r14
    pop r13
    pop r12
    ret

; Compare the entry at ARG0 with the target: key ARG1, of which the first
; ARG2 bytes count, and with ARG4 set, sequence number ARG3 after equal keys.
; Returns eax < 0, 0, > 0 as the entry is below, equal to, above the target.
compare_entry:
    mov r9, ARG3                        ; sequence
    mov r10, ARG0                       ; entry
    lea rdi, [ARG0 + EN_KEY]
    mov rcx, ARG2
    test rcx, rcx
    jz .keys_equal
    repe cmpsb
    je .keys_equal
    movzx eax, byte [rdi - 1]
    movzx ecx, byte [rsi - 1]
    sub eax, ecx
    ret
.keys_equal:
    xor eax, eax
    test r8, r8
    jz .done
    mov rcx, [r10 + EN_SEQ]
    cmp rcx, r9
    je .done
    mov eax, 1
    ja .done
    mov eax, -1
.done:
    ret

; Find in index ARG0 the first entry above the target (ARG5 = 1) or at or
; above it (ARG5 = 0); the target as for compare_entry (ARG1 key, ARG2 bytes
; compared, ARG3 sequence, ARG4 compare sequences). Returns the block number
; in rax and the entry number in rdx; rax = the block count when there is no
; such entry.
index_find:
    push rbp
    mov rbp, rsp
    push r12
    push r13
    push r14
    push r15
    sub rsp, 64
    mov r12, ARG0
    mov [rbp - 48], ARG1                ; key
    mov [rbp - 56], ARG2                ; bytes
    mov [rbp - 64], ARG3                ; sequence
    mov [rbp - 72], ARG4                ; compare sequences
    mov [rbp - 80], ARG5                ; strictly above
    ; Blocks: the first whose last entry satisfies the condition
    xor r13d, r13d                      ; lo
    mov r14, [r12 + IX_NBLK]            ; hi
.blk_search:
    cmp r13, r14
    jae .blk_found
    lea r15, [r13 + r14]
    shr r15, 1                          ; mid
    mov rax, [r12 + IX_BLKS]
    mov rax, [rax + r15*8]
    mov rcx, [rax + BL_COUNT]
    dec rcx
    imul rcx, [r12 + IX_ESIZE]
    lea ARG0, [rax + BL_ENTRIES + rcx]
    call .compare
    test eax, eax
    jnz .blk_upper
    mov r14, r15
    jmp .blk_search
.blk_upper:
    lea r13, [r15 + 1]
    jmp .blk_search
.blk_found:
    cmp r13, [r12 + IX_NBLK]
    jae .end
    mov [rbp - 88], r13                 ; block number
    mov rax, [r12 + IX_BLKS]
    mov rax, [rax + r13*8]
    mov [rbp - 40], rax                 ; block
    xor r13d, r13d
    mov r14, [rax + BL_COUNT]
.ent_search:
    cmp r13, r14
    jae .ent_found
    lea r15, [r13 + r14]
    shr r15, 1
    mov rax, r15
    imul rax, [r12 + IX_ESIZE]
    mov rcx, [rbp - 40]
    lea ARG0, [rcx + BL_ENTRIES + rax]
    call .compare
    test eax, eax
    jnz .ent_upper
    mov r14, r15
    jmp .ent_search
.ent_upper:
    lea r13, [r15 + 1]
    jmp .ent_search
.ent_found:
    mov rax, [rbp - 88]
    mov rdx, r13
    jmp .done
.end:
    mov rax, [r12 + IX_NBLK]
    xor edx, edx
.done:
    add rsp, 64
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    ret
    ; eax = 0 if the entry ARG0 satisfies the condition, else 1
.compare:
    mov ARG1, [rbp - 48]
    mov ARG2, [rbp - 56]
    mov ARG3, [rbp - 64]
    mov ARG4, [rbp - 72]
    call compare_entry
    cmp qword [rbp - 80], 0
    jne .strict
    test eax, eax
    js .no
    xor eax, eax
    ret
.strict:
    test eax, eax
    jle .no
    xor eax, eax
    ret
.no:
    mov eax, 1
    ret

; Address of entry rdx of block rax in index r12, in rax
entry_at:
    mov rcx, [r12 + IX_BLKS]
    mov rax, [rcx + rax*8]
    imul rdx, [r12 + IX_ESIZE]
    lea rax, [rax + BL_ENTRIES + rdx]
    ret

; Make room in the block array of index r12 for one more block; rax = 0 on
; success
grow_blocks:
    push r13
    push r14
    mov rax, [r12 + IX_NBLK]
    cmp rax, [r12 + IX_CAP]
    jb .ok
    mov r13, [r12 + IX_CAP]
    shl r13, 1
    cmp r13, 16
    jae .cap
    mov r13d, 16
.cap:
    mov ARG0, r13
    shl ARG0, 3
    call cobrt_os_alloc
    test rax, rax
    jz .fail
    mov r14, rax
    mov ARG0, r14
    mov ARG1, [r12 + IX_BLKS]
    mov ARG2, [r12 + IX_NBLK]
    shl ARG2, 3
    test ARG1, ARG1
    jz .copied
    call copy_down
    mov ARG0, [r12 + IX_BLKS]
    mov ARG1, [r12 + IX_CAP]
    shl ARG1, 3
    call cobrt_os_free
.copied:
    mov [r12 + IX_BLKS], r14
    mov [r12 + IX_CAP], r13
.ok:
    xor eax, eax
    pop r14
    pop r13
    ret
.fail:
    mov eax, 1
    pop r14
    pop r13
    ret

; Insert block ARG1 at position ARG0 of the block array of index r12
insert_block:
    push r13
    push r14
    mov r13, ARG0
    mov r14, ARG1
    mov rax, [r12 + IX_BLKS]
    lea ARG0, [rax + r13*8 + 8]
    lea ARG1, [rax + r13*8]
    mov ARG2, [r12 + IX_NBLK]
    sub ARG2, r13
    shl ARG2, 3
    call copy_up
    mov rax, [r12 + IX_BLKS]
    mov [rax + r13*8], r14
    inc qword [r12 + IX_NBLK]
    pop r14
    pop r13
    ret

; Insert the entry at ARG1 into index ARG0; rax = 0 on success
index_insert:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0
    mov r13, ARG1                       ; entry
.retry:
    cmp qword [r12 + IX_NBLK], 0
    jne .find
    ; The first block
    call grow_blocks
    test rax, rax
    jnz .fail
    mov ARG0, [r12 + IX_BSIZE]
    call cobrt_os_alloc
    test rax, rax
    jz .fail
    mov rcx, [r12 + IX_BLKS]
    mov [rcx], rax
    mov qword [r12 + IX_NBLK], 1
    xor r14d, r14d                      ; the empty block's first entry
    xor r15d, r15d
    jmp .in_block
.find:
    mov ARG0, r12
    lea ARG1, [r13 + EN_KEY]
    mov ARG2, [r12 + IX_KLEN]
    mov ARG3, [r13 + EN_SEQ]
    mov ARG4, 1
    xor ARG5, ARG5
    call index_find
    mov r14, rax                        ; block
    mov r15, rdx                        ; entry
    cmp r14, [r12 + IX_NBLK]
    jb .in_block
    ; After every entry: the end of the last block
    lea r14, [r14 - 1]
    mov rax, [r12 + IX_BLKS]
    mov rax, [rax + r14*8]
    mov r15, [rax + BL_COUNT]
.in_block:
    mov rax, [r12 + IX_BLKS]
    mov rbp, [rax + r14*8]              ; block
    mov rax, [rbp + BL_COUNT]
    cmp rax, [r12 + IX_BCAP]
    jb .room
    ; Full: move the upper half to a new block, then try again
    call grow_blocks
    test rax, rax
    jnz .fail
    mov ARG0, [r12 + IX_BSIZE]
    call cobrt_os_alloc
    test rax, rax
    jz .fail
    push rax
    sub rsp, 8
    mov rcx, [rbp + BL_COUNT]
    shr rcx, 1                          ; entries kept
    mov rdx, [rbp + BL_COUNT]
    sub rdx, rcx                        ; entries moved
    mov [rax + BL_COUNT], rdx
    mov [rbp + BL_COUNT], rcx
    imul rcx, [r12 + IX_ESIZE]
    lea ARG1, [rbp + BL_ENTRIES + rcx]
    lea ARG0, [rax + BL_ENTRIES]
    imul rdx, [r12 + IX_ESIZE]
    mov ARG2, rdx
    call copy_down
    add rsp, 8
    pop ARG1
    lea ARG0, [r14 + 1]
    call insert_block
    jmp .retry
.room:
    ; Open a gap at entry r15 and store the entry there
    mov rcx, [r12 + IX_ESIZE]
    mov rax, r15
    imul rax, rcx
    lea ARG1, [rbp + BL_ENTRIES + rax]
    lea ARG0, [ARG1 + rcx]
    mov ARG2, [rbp + BL_COUNT]
    sub ARG2, r15
    imul ARG2, rcx
    push ARG1
    sub rsp, 8
    call copy_up
    add rsp, 8
    pop ARG0
    mov ARG1, r13
    mov ARG2, [r12 + IX_ESIZE]
    call copy_down
    inc qword [rbp + BL_COUNT]
    xor eax, eax
    jmp .done
.fail:
    mov eax, 1
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Remove entry ARG2 of block ARG1 from index ARG0
index_remove:
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    mov rax, [r12 + IX_BLKS]
    mov r15, [rax + r13*8]              ; block
    mov rcx, [r12 + IX_ESIZE]
    mov rax, r14
    imul rax, rcx
    lea ARG0, [r15 + BL_ENTRIES + rax]
    lea ARG1, [ARG0 + rcx]
    mov ARG2, [r15 + BL_COUNT]
    sub ARG2, r14
    dec ARG2
    imul ARG2, rcx
    call copy_down
    dec qword [r15 + BL_COUNT]
    jnz .done
    ; An empty block leaves the array
    mov ARG0, r15
    mov ARG1, [r12 + IX_BSIZE]
    call cobrt_os_free
    mov rax, [r12 + IX_BLKS]
    lea ARG0, [rax + r13*8]
    lea ARG1, [rax + r13*8 + 8]
    mov ARG2, [r12 + IX_NBLK]
    sub ARG2, r13
    dec ARG2
    shl ARG2, 3
    call copy_down
    dec qword [r12 + IX_NBLK]
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; ── Records and keys ────────────────────────────────────────────────────────

; Slot header size (length and a sequence number per key) in rax
header_size:
    mov rax, [rbx + FCB_KEYS]
    mov rax, [rax + KT_COUNT]
    lea rax, [rax*8 + 8]
    ret

; Slot size in rax
slot_size:
    call header_size
    add rax, [rbx + FCB_SIZE]
    ret

; File offset of slot ARG0 (1-based) in rax
slot_offset:
    push rdi
    call slot_size
    pop rdi
    lea rcx, [rdi - 1]
    imul rax, rcx
    ret

; Read slot ARG0 into the record area ARG1 and its header into PV_HDR:
; rax = record length (0: free slot or error), rdx = its sequence numbers
read_slot:
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0
    mov r13, ARG1
    mov r15, [rbx + FCB_PRIV]
    mov r15, [r15 + PV_HDR]
    mov qword [r15], 0
    call slot_offset
    mov r14, rax
    call header_size
    mov [rsp], rax
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r15
    mov ARG2, rax
    mov ARG3, r14
    call cobrt_os_pread
    cmp rax, [rsp]
    jne .none
    mov rcx, [r15]
    test rcx, rcx
    jz .none
    cmp rcx, [rbx + FCB_SIZE]
    jbe .fits
    mov rcx, [rbx + FCB_SIZE]
    mov [r15], rcx
.fits:
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r13
    mov ARG2, rcx
    mov ARG3, r14
    add ARG3, [rsp]
    call cobrt_os_pread
    mov rax, [r15]
    lea rdx, [r15 + 8]
    jmp .done
.none:
    xor eax, eax
    lea rdx, [r15 + 8]
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Write the header ARG2 (length 0 frees the slot) and the record ARG1 into
; slot ARG0; rax <0 on error
write_slot:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    call slot_offset
    mov r15, rax
    call header_size
    mov rbp, rax
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r14
    mov ARG2, rbp
    mov ARG3, r15
    call cobrt_os_pwrite
    cmp rax, rbp
    jne .error
    cmp qword [r14], 0
    je .ok
    mov ARG0, [rbx + FCB_FD]
    mov ARG1, r13
    mov ARG2, [rbx + FCB_SIZE]
    lea ARG3, [r15 + rbp]
    call cobrt_os_pwrite
    cmp rax, [rbx + FCB_SIZE]
    jne .error
.ok:
    xor eax, eax
    jmp .done
.error:
    mov rax, -1
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Key ARG0: rax = its key table entry
key_entry:
    mov rax, ARG0
    imul rax, rax, KT_KEY_SIZE
    add rax, [rbx + FCB_KEYS]
    add rax, KT_KEYS
    ret

; Index of key ARG0 in rax
key_index:
    mov rax, [rbx + FCB_PRIV]
    mov rax, [rax + PV_IX + ARG0*8]
    ret

; Build the scratch entry for key ARG0 of record ARG1, slot ARG2, with the
; key's sequence number from the array ARG3; returns it in rax
make_entry:
    push r12
    push r13
    mov r12, [rbx + FCB_PRIV]
    mov r12, [r12 + PV_ENTRY]
    mov [r12 + EN_SLOT], ARG2
    mov rax, [ARG3 + ARG0*8]
    mov [r12 + EN_SEQ], rax
    mov r13, ARG1
    call key_entry
    mov ARG2, [rax + KT_KEY_LEN]
    mov ARG1, r13
    add ARG1, [rax + KT_KEY_OFF]
    lea ARG0, [r12 + EN_KEY]
    call copy_down
    mov rax, r12
    pop r13
    pop r12
    ret

; Find the entry of key ARG0 whose key equals that in record ARG1 (the
; first of equal keys); rax = block, rdx = entry, or rax = -1
find_equal:
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0
    mov r13, ARG1
    call key_entry
    mov r14, rax
    mov ARG0, r12
    call key_index
    mov r15, rax
    mov ARG0, r15
    mov ARG1, r13
    add ARG1, [r14 + KT_KEY_OFF]
    mov ARG2, [r14 + KT_KEY_LEN]
    xor ARG3, ARG3
    xor ARG4, ARG4
    xor ARG5, ARG5
    call index_find
    cmp rax, [r15 + IX_NBLK]
    jae .none
    mov [rsp], rax
    push rdx
    push r12
    mov r12, r15
    call entry_at
    pop r12
    lea ARG0, [rax]
    mov ARG1, r13
    add ARG1, [r14 + KT_KEY_OFF]
    mov ARG2, [r14 + KT_KEY_LEN]
    xor ARG4, ARG4
    call compare_entry
    pop rdx
    test eax, eax
    jnz .none
    mov rax, [rsp]
    jmp .done
.none:
    mov rax, -1
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Remove the entries of record ARG0 (in memory; slot ARG1, sequence numbers
; ARG2) from every index
remove_keys:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    xor r15d, r15d                      ; key
.key:
    mov rax, [rbx + FCB_KEYS]
    cmp r15, [rax + KT_COUNT]
    jae .done
    mov ARG0, r15
    mov ARG1, r12
    mov ARG2, r13
    mov ARG3, r14
    call make_entry
    mov rbp, rax
    mov ARG0, r15
    call key_index
    push rax
    sub rsp, 8
    mov ARG0, rax
    lea ARG1, [rbp + EN_KEY]
    mov ARG2, [rax + IX_KLEN]
    mov ARG3, [rbp + EN_SEQ]
    mov ARG4, 1
    xor ARG5, ARG5
    call index_find
    add rsp, 8
    pop r9
    cmp rax, [r9 + IX_NBLK]
    jae .next
    push r9
    push rax
    push rdx
    push r12
    mov r12, r9
    call entry_at
    pop r12
    mov ARG0, rax
    lea ARG1, [rbp + EN_KEY]
    mov rax, [rsp + 16]
    mov ARG2, [rax + IX_KLEN]
    mov ARG3, [rbp + EN_SEQ]
    mov ARG4, 1
    call compare_entry
    pop ARG2
    pop ARG1
    pop ARG0
    test eax, eax
    jnz .next                           ; not in the index
    call index_remove
.next:
    inc r15
    jmp .key
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Add the entries of record ARG0 (slot ARG1, sequence numbers ARG2) to every
; index; rax = 0 on success
add_keys:
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    xor r15d, r15d
.key:
    mov rax, [rbx + FCB_KEYS]
    cmp r15, [rax + KT_COUNT]
    jae .ok
    mov ARG0, r15
    mov ARG1, r12
    mov ARG2, r13
    mov ARG3, r14
    call make_entry
    mov [rsp], rax
    mov ARG0, r15
    call key_index
    mov ARG0, rax
    mov ARG1, [rsp]
    call index_insert
    test rax, rax
    jnz .fail
    inc r15
    jmp .key
.ok:
    xor eax, eax
    jmp .done
.fail:
    mov eax, 1
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Whether key ARG0 has an entry equal to the key in record ARG1 for
; another record than slot ARG2: rax = 1 or 0
other_equal:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r13, ARG1
    mov r14, ARG2
    mov rbp, ARG0
    call key_entry
    mov r15, rax                        ; key table entry
    mov ARG0, rbp
    mov ARG1, r13
    call find_equal
    cmp rax, -1
    je .none
    push rax
    push rdx
    mov ARG0, rbp
    call key_index
    mov r12, rax                        ; index
    pop rdx
    pop rax
.loop:
    cmp rax, [r12 + IX_NBLK]
    jae .none
    push rax
    push rdx
    call entry_at
    mov rbp, rax
    mov ARG0, rax
    mov ARG1, r13
    add ARG1, [r15 + KT_KEY_OFF]
    mov ARG2, [r15 + KT_KEY_LEN]
    xor ARG4, ARG4
    call compare_entry
    pop rdx
    pop rcx
    test eax, eax
    jnz .none                           ; past the equal entries
    cmp [rbp + EN_SLOT], r14
    jne .found
    inc rdx
    mov rax, [r12 + IX_BLKS]
    mov rax, [rax + rcx*8]
    cmp rdx, [rax + BL_COUNT]
    mov rax, rcx
    jb .loop
    inc rax
    xor edx, edx
    jmp .loop
.found:
    mov eax, 1
    jmp .done
.none:
    xor eax, eax
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Check the keys of record ARG0 before it is written in place of slot ARG1
; (0: a new record): eax = 22 if a key that does not allow duplicates is
; already in another record, 2 if a duplicate alternate key is allowed, 0
check_keys:
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0
    mov r13, ARG1
    xor r14d, r14d                      ; key
    xor r15d, r15d                      ; result
.key:
    mov rax, [rbx + FCB_KEYS]
    cmp r14, [rax + KT_COUNT]
    jae .done
    mov ARG0, r14
    mov ARG1, r12
    mov ARG2, r13
    call other_equal
    test rax, rax
    jz .next
    mov ARG0, r14
    call key_entry
    cmp qword [rax + KT_KEY_DUPS], 0
    je .duplicate
    mov r15d, 2
    jmp .next
.duplicate:
    mov r15d, 22
    jmp .done
.next:
    inc r14
    jmp .key
.done:
    mov eax, r15d
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; ── OPEN and CLOSE ──────────────────────────────────────────────────────────

; ARG0 = OPEN mode
cobrt_idx_open:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0
    lea rax, [rel idx_open_modes]
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
    mov ARG0, [rbx + FCB_NAME]
    mov ARG1, OS_OPEN_RW_CREATE
    call cobrt_os_open
    test rax, rax
    js .error
    mov [rbx + FCB_FD], rax
    mov r13d, 5
    jmp .state
.opened:
    mov [rbx + FCB_FD], rax
    xor r13d, r13d                      ; status
.state:
    ; Organization state: fixed part, index pointers, the scratch areas
    mov rax, [rbx + FCB_KEYS]
    mov r14, [rax + KT_COUNT]
    xor r15d, r15d                      ; longest key
    xor ecx, ecx
.maxk:
    cmp rcx, r14
    jae .maxk_done
    mov rdx, rcx
    imul rdx, rdx, KT_KEY_SIZE
    mov rdx, [rax + KT_KEYS + rdx + KT_KEY_LEN]
    cmp rdx, r15
    jbe .maxk_next
    mov r15, rdx
.maxk_next:
    inc rcx
    jmp .maxk
.maxk_done:
    lea rbp, [PV_FIXED + r14*8]         ; offset of the scratch areas
    lea ARG0, [rbp + r15*2 + 16]
    add ARG0, r15
    add ARG0, [rbx + FCB_SIZE]
    lea rcx, [r14*8 + 8]
    lea ARG0, [ARG0 + rcx*2]            ; the two slot headers
    push ARG0
    sub rsp, 8
    call cobrt_os_alloc
    add rsp, 8
    pop rcx
    test rax, rax
    jz .error_close
    mov [rbx + FCB_PRIV], rax
    mov [rax + PV_BYTES], rcx
    mov [rax + PV_MAXK], r15
    lea rcx, [rax + rbp]
    mov [rax + PV_CURKEY], rcx
    add rcx, r15
    mov [rax + PV_LASTKEY], rcx
    add rcx, r15
    mov [rax + PV_ENTRY], rcx
    add rcx, r15
    add rcx, 16
    mov [rax + PV_OLDREC], rcx
    add rcx, [rbx + FCB_SIZE]
    mov [rax + PV_HDR], rcx
    lea rcx, [rcx + r14*8 + 8]
    mov [rax + PV_HDR2], rcx
    mov qword [rax + PV_NEXTSEQ], 1
    ; An index for each key
    xor r15d, r15d
.indexes:
    cmp r15, r14
    jae .load
    mov ARG0, r15
    call key_entry
    mov ARG0, [rax + KT_KEY_LEN]
    call index_new
    test rax, rax
    jz .error_close
    mov rcx, [rbx + FCB_PRIV]
    mov [rcx + PV_IX + r15*8], rax
    inc r15
    jmp .indexes
.load:
    mov qword [rbx + FCB_KEYREF], 0
    ; The records of the file: each in every index; free slots listed
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_size
    test rax, rax
    js .error_close
    push rax
    call slot_size
    mov rcx, rax
    pop rax
    xor edx, edx
    div rcx
    mov r14, rax                        ; slots
    mov rax, [rbx + FCB_PRIV]
    mov [rax + PV_NSLOTS], r14
    mov r15d, 1
.slot:
    cmp r15, r14
    ja .loaded
    mov rax, [rbx + FCB_PRIV]
    mov ARG0, r15
    mov ARG1, [rax + PV_OLDREC]
    call read_slot
    test rax, rax
    jz .free_slot
    mov rbp, rdx                        ; sequence numbers
    ; The next sequence number: above every one in the file
    mov rax, [rbx + FCB_PRIV]
    mov rcx, [rbx + FCB_KEYS]
    mov rcx, [rcx + KT_COUNT]
.seq:
    dec rcx
    js .seq_ok
    mov rdx, [rbp + rcx*8]
    cmp rdx, [rax + PV_NEXTSEQ]
    jb .seq
    inc rdx
    mov [rax + PV_NEXTSEQ], rdx
    jmp .seq
.seq_ok:
    mov ARG0, [rax + PV_OLDREC]
    mov ARG1, r15
    mov ARG2, rbp
    call add_keys
    test rax, rax
    jnz .error_close
    jmp .next_slot
.free_slot:
    mov ARG0, r15
    call push_free
.next_slot:
    inc r15
    jmp .slot
.loaded:
    mov eax, r13d
    jmp .done
.error_close:
    call release
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_close
    mov eax, 30
    jmp .done
.error:
    cmp rax, OS_ERR_PERMISSION
    mov eax, 30
    jne .done
    mov eax, 37
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; Add slot ARG0 to the free slots
push_free:
    push r12
    push r13
    push r14
    mov r12, ARG0
    mov r13, [rbx + FCB_PRIV]
    mov rax, [r13 + PV_NFREE]
    cmp rax, [r13 + PV_FCAP]
    jb .store
    mov r14, [r13 + PV_FCAP]
    shl r14, 1
    cmp r14, 64
    jae .cap
    mov r14d, 64
.cap:
    mov ARG0, r14
    shl ARG0, 3
    call cobrt_os_alloc
    test rax, rax
    jz .done                            ; the slot stays unused
    push rax
    sub rsp, 8
    mov ARG0, rax
    mov ARG1, [r13 + PV_FREE]
    mov ARG2, [r13 + PV_NFREE]
    shl ARG2, 3
    test ARG1, ARG1
    jz .copied
    call copy_down
    mov ARG0, [r13 + PV_FREE]
    mov ARG1, [r13 + PV_FCAP]
    shl ARG1, 3
    call cobrt_os_free
.copied:
    add rsp, 8
    pop rax
    mov [r13 + PV_FREE], rax
    mov [r13 + PV_FCAP], r14
.store:
    mov rax, [r13 + PV_FREE]
    mov rcx, [r13 + PV_NFREE]
    mov [rax + rcx*8], r12
    inc qword [r13 + PV_NFREE]
.done:
    pop r14
    pop r13
    pop r12
    ret

; Free the organization state
release:
    push r12
    push r13
    push r14
    mov r12, [rbx + FCB_PRIV]
    test r12, r12
    jz .done
    xor r13d, r13d
.ix:
    mov rax, [rbx + FCB_KEYS]
    cmp r13, [rax + KT_COUNT]
    jae .free_list
    mov ARG0, [r12 + PV_IX + r13*8]
    call index_free
    inc r13
    jmp .ix
.free_list:
    mov ARG0, [r12 + PV_FREE]
    test ARG0, ARG0
    jz .self
    mov ARG1, [r12 + PV_FCAP]
    shl ARG1, 3
    call cobrt_os_free
.self:
    mov ARG0, r12
    mov ARG1, [r12 + PV_BYTES]
    call cobrt_os_free
    mov qword [rbx + FCB_PRIV], 0
.done:
    pop r14
    pop r13
    pop r12
    ret

cobrt_idx_close:
    sub rsp, 8
    call release
    mov ARG0, [rbx + FCB_FD]
    call cobrt_os_close
    add rsp, 8
    xor eax, eax
    ret

; ── READ ────────────────────────────────────────────────────────────────────

; Read the record of the entry at ARG0 into the area ARG1 and make it the
; position for READ NEXT; eax = status
read_entry:
    push r12
    push r13
    push r14
    mov r12, ARG0
    mov r13, ARG1
    mov ARG0, [r12 + EN_SLOT]
    mov ARG1, r13
    call read_slot
    test rax, rax
    jz .error
    mov [rbx + FCB_RECLEN], rax
    mov rax, [r12 + EN_SLOT]
    mov [rbx + FCB_RECPOS], rax
    or qword [rbx + FCB_STATE], ST_READ_OK
    mov r14, [rbx + FCB_PRIV]
    mov rax, [r12 + EN_SEQ]
    mov [r14 + PV_CURSEQ], rax
    mov qword [r14 + PV_CUR], CUR_VALID | CUR_AFTER
    mov ARG0, [rbx + FCB_KEYREF]
    call key_entry
    mov ARG2, [rax + KT_KEY_LEN]
    lea ARG1, [r12 + EN_KEY]
    mov ARG0, [r14 + PV_CURKEY]
    call copy_down
    xor eax, eax
    jmp .done
.error:
    mov eax, 30
.done:
    pop r14
    pop r13
    pop r12
    ret

; ARG0 = record area, ARG1 = READ_NEXT / READ_KEY
cobrt_idx_read:
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r12, ARG0
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov r14, [rbx + FCB_PRIV]
    cmp ARG1, READ_KEY
    je .random
    mov eax, 46                         ; no valid next record
    test qword [r14 + PV_CUR], CUR_NONE
    jnz .done
    test qword [rbx + FCB_STATE], ST_AT_END
    jnz .done
    mov ARG0, [rbx + FCB_KEYREF]
    call key_index
    mov r15, rax
    xor eax, eax                        ; no position: the first entry
    xor edx, edx
    test qword [r14 + PV_CUR], CUR_VALID
    jz .have_pos
    mov ARG0, r15
    mov ARG1, [r14 + PV_CURKEY]
    mov ARG2, [r15 + IX_KLEN]
    mov ARG3, [r14 + PV_CURSEQ]
    mov ARG4, 1
    xor ARG5, ARG5
    test qword [r14 + PV_CUR], CUR_AFTER
    jz .find
    mov ARG5, 1
.find:
    call index_find
.have_pos:
    cmp rax, [r15 + IX_NBLK]
    jae .at_end
    push r12
    mov r12, r15
    call entry_at
    pop r12
    mov ARG0, rax
    mov ARG1, r12
    call read_entry
    jmp .done
.at_end:
    or qword [rbx + FCB_STATE], ST_AT_END
    mov eax, 10
    jmp .done
.random:
    ; The key of reference has its value in the record area
    mov ARG0, [rbx + FCB_KEYREF]
    mov ARG1, r12
    call find_equal
    cmp rax, -1
    je .not_found
    mov r15, rax
    mov ARG0, [rbx + FCB_KEYREF]
    call key_index
    push r12
    mov r12, rax
    mov rax, r15
    call entry_at
    pop r12
    mov ARG0, rax
    mov ARG1, r12
    call read_entry
    and qword [rbx + FCB_STATE], ~ST_AT_END
    and qword [r14 + PV_CUR], ~CUR_NONE
    jmp .done
.not_found:
    mov eax, 23
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; ── WRITE, REWRITE, DELETE ──────────────────────────────────────────────────

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
cobrt_idx_write:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0
    mov r13, ARG1
    mov r14, [rbx + FCB_PRIV]
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
    ; Sequential access: prime keys in ascending order
    test qword [rbx + FCB_FLAGS], ACC_RANDOM | ACC_DYNAMIC
    jnz .keys
    mov eax, 48
    cmp qword [rbx + FCB_MODE], OPEN_IO
    je .done
    xor ARG0, ARG0
    call key_entry
    mov r15, rax
    cmp qword [r14 + PV_HAVELAST], 0
    jne .compare_last
    cmp qword [rbx + FCB_MODE], OPEN_EXTEND
    jne .keys
    ; EXTEND: after the highest prime key in the file
    xor ARG0, ARG0
    call key_index
    cmp qword [rax + IX_NBLK], 0
    je .keys
    push r12
    mov r12, rax
    mov rax, [r12 + IX_NBLK]
    dec rax
    mov rcx, [r12 + IX_BLKS]
    mov rcx, [rcx + rax*8]
    mov rdx, [rcx + BL_COUNT]
    dec rdx
    call entry_at
    pop r12
    lea ARG1, [rax + EN_KEY]
    jmp .compare_with
.compare_last:
    mov ARG1, [r14 + PV_LASTKEY]
.compare_with:
    mov rdi, ARG1                       ; the last key
    mov rsi, r12
    add rsi, [r15 + KT_KEY_OFF]         ; the new key
    mov rcx, [r15 + KT_KEY_LEN]
    repe cmpsb                          ; flags of new - last
    mov eax, 21                         ; not above the last prime key
    jbe .done
.keys:
    mov ARG0, r12
    xor ARG1, ARG1
    call check_keys
    cmp eax, 22
    je .done
    mov rbp, rax                        ; 0, or 2 (duplicate alternate key)
    ; A free slot, or a new one at the end
    cmp qword [r14 + PV_NFREE], 0
    je .new_slot
    dec qword [r14 + PV_NFREE]
    mov rax, [r14 + PV_FREE]
    mov rcx, [r14 + PV_NFREE]
    mov r15, [rax + rcx*8]
    jmp .write
.new_slot:
    inc qword [r14 + PV_NSLOTS]
    mov r15, [r14 + PV_NSLOTS]
.write:
    ; The header: the length, every key with the next sequence number
    mov rdx, [r14 + PV_HDR2]
    mov [rdx], r13
    mov rax, [r14 + PV_NEXTSEQ]
    inc qword [r14 + PV_NEXTSEQ]
    mov rcx, [rbx + FCB_KEYS]
    mov rcx, [rcx + KT_COUNT]
.seqs:
    mov [rdx + rcx*8], rax
    dec rcx
    jnz .seqs
    mov ARG0, r15
    mov ARG1, r12
    call write_slot
    test rax, rax
    js .error
    mov ARG0, r12
    mov ARG1, r15
    mov rax, [r14 + PV_HDR2]
    lea ARG2, [rax + 8]
    call add_keys
    test rax, rax
    jnz .error
    ; The prime key written last (sequential access)
    xor ARG0, ARG0
    call key_entry
    mov ARG2, [rax + KT_KEY_LEN]
    mov ARG1, r12
    add ARG1, [rax + KT_KEY_OFF]
    mov ARG0, [r14 + PV_LASTKEY]
    call copy_down
    mov qword [r14 + PV_HAVELAST], 1
    mov eax, ebp
    jmp .done
.error:
    mov eax, 30
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

; The slot of the record to replace (ARG1 = 0) or delete (ARG1 = 1). With
; sequential access: the record last read (REWRITE: its prime key must be
; the one in record area ARG0, else 21); otherwise the record with the prime
; key in record area ARG0 (else 23). rax = slot, or 0 with the status in edx.
target_slot:
    push r12
    push r13
    push r14
    mov r12, ARG0
    test qword [rbx + FCB_FLAGS], ACC_RANDOM | ACC_DYNAMIC
    jnz .by_key
    mov edx, 43                         ; no successful READ before
    test qword [rbx + FCB_STATE], ST_READ_OK
    jz .none
    mov rax, [rbx + FCB_RECPOS]
    test ARG1, ARG1
    jnz .done                           ; DELETE: the record read
    mov r14, rax
    call .prime_slot
    mov edx, 21                         ; the prime key changed
    cmp rax, r14
    jne .none
    jmp .done
.by_key:
    call .prime_slot
    mov edx, 23                         ; no record with this key
    test rax, rax
    jz .none
    jmp .done
.none:
    xor eax, eax
.done:
    pop r14
    pop r13
    pop r12
    ret
    ; The slot of the record whose prime key is in record area r12, or 0
.prime_slot:
    xor ARG0, ARG0
    mov ARG1, r12
    call find_equal
    cmp rax, -1
    je .no_prime
    push r12
    push rax
    push rdx
    xor ARG0, ARG0
    call key_index
    mov r12, rax
    pop rdx
    pop rax
    call entry_at
    pop r12
    mov rax, [rax + EN_SLOT]
    ret
.no_prime:
    xor eax, eax
    ret

; ARG0 = record, ARG1 = length
cobrt_idx_rewrite:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0
    mov r13, ARG1
    mov r14, [rbx + FCB_PRIV]
    mov ARG0, r13
    call check_length
    test eax, eax
    jnz .done
    mov ARG0, r12
    xor ARG1, ARG1
    call target_slot
    test rax, rax
    jnz .found
    mov eax, edx
    jmp .done
.found:
    mov r15, rax                        ; slot
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov ARG0, r12
    mov ARG1, r15
    call check_keys
    cmp eax, 22
    je .done
    ; The record replaced: its keys leave the indexes
    mov ARG0, r15
    mov ARG1, [r14 + PV_OLDREC]
    call read_slot
    mov rbp, rdx                        ; its sequence numbers
    mov ARG0, [r14 + PV_OLDREC]
    mov ARG1, r15
    mov ARG2, rbp
    call remove_keys
    ; The new header: a key whose value changed goes after its duplicates
    ; (a new sequence number), the others keep their place
    mov rdx, [r14 + PV_HDR2]
    mov [rdx], r13
    push r13
    push r15
    mov r15, [r14 + PV_NEXTSEQ]
    inc qword [r14 + PV_NEXTSEQ]
    xor r13d, r13d                      ; key
.seq:
    mov rax, [rbx + FCB_KEYS]
    cmp r13, [rax + KT_COUNT]
    jae .seq_done
    mov ARG0, r13
    call key_entry
    mov rcx, [rax + KT_KEY_LEN]
    mov rsi, [rax + KT_KEY_OFF]
    lea rdi, [r12 + rsi]
    add rsi, [r14 + PV_OLDREC]
    repe cmpsb
    mov rax, [rbp + r13*8]              ; unchanged: the old number
    je .keep
    mov rax, r15
.keep:
    mov rdx, [r14 + PV_HDR2]
    mov [rdx + r13*8 + 8], rax
    inc r13
    jmp .seq
.seq_done:
    pop r15
    pop r13
    mov ARG0, r15
    mov ARG1, r12
    mov ARG2, [r14 + PV_HDR2]
    call write_slot
    test rax, rax
    js .error
    mov ARG0, r12
    mov ARG1, r15
    mov rax, [r14 + PV_HDR2]
    lea ARG2, [rax + 8]
    call add_keys
    test rax, rax
    jnz .error
    xor eax, eax
    jmp .done
.error:
    mov eax, 30
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

cobrt_idx_delete:
    push r12
    push r13
    push r14
    mov r14, [rbx + FCB_PRIV]
    mov rax, [rbx + FCB_KEYS]
    mov ARG0, [rax + KT_AREA]
    mov ARG1, 1
    call target_slot
    test rax, rax
    jnz .found
    mov eax, edx
    jmp .done
.found:
    mov r12, rax
    and qword [rbx + FCB_STATE], ~ST_READ_OK
    mov ARG0, r12
    mov ARG1, [r14 + PV_OLDREC]
    call read_slot
    mov r13, rdx
    mov ARG0, [r14 + PV_OLDREC]
    mov ARG1, r12
    mov ARG2, r13
    call remove_keys
    mov ARG2, [r14 + PV_HDR2]
    mov qword [ARG2], 0                 ; a free slot
    mov ARG0, r12
    xor ARG1, ARG1
    call write_slot
    test rax, rax
    js .error
    mov ARG0, r12
    call push_free
    xor eax, eax
    jmp .done
.error:
    mov eax, 30
.done:
    pop r14
    pop r13
    pop r12
    ret

; ── START ───────────────────────────────────────────────────────────────────

; ARG0 = START_* condition, ARG1 = key number, ARG2 = leading bytes of the
; key compared (its value in the record area)
cobrt_idx_start:
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    mov [rbx + FCB_KEYREF], r13
    mov rbp, [rbx + FCB_PRIV]
    mov ARG0, r13
    call key_entry
    mov r15, rax                        ; key table entry
    mov ARG0, r13
    call key_index
    push rax
    sub rsp, 8
    mov ARG0, rax
    mov rax, [rbx + FCB_KEYS]
    mov ARG1, [rax + KT_AREA]
    add ARG1, [r15 + KT_KEY_OFF]
    mov ARG2, r14
    xor ARG3, ARG3
    xor ARG4, ARG4
    xor ARG5, ARG5
    cmp r12, START_GT
    jne .find
    mov ARG5, 1
.find:
    call index_find
    add rsp, 8
    pop rcx                             ; index
    cmp rax, [rcx + IX_NBLK]
    jae .not_found
    push r12
    mov r12, rcx
    call entry_at
    pop r12
    mov r15, rax                        ; entry
    cmp r12, START_EQ
    jne .found
    mov ARG0, r15
    mov rax, [rbx + FCB_KEYS]
    mov ARG1, [rax + KT_AREA]
    mov ARG0, r13
    call key_entry
    add ARG1, [rax + KT_KEY_OFF]
    mov ARG0, r15
    mov ARG2, r14
    xor ARG4, ARG4
    call compare_entry
    test eax, eax
    jnz .not_found
.found:
    ; READ NEXT reads this entry
    mov rax, [r15 + EN_SEQ]
    mov [rbp + PV_CURSEQ], rax
    mov qword [rbp + PV_CUR], CUR_VALID
    mov ARG0, r13
    call key_entry
    mov ARG2, [rax + KT_KEY_LEN]
    lea ARG1, [r15 + EN_KEY]
    mov ARG0, [rbp + PV_CURKEY]
    call copy_down
    and qword [rbx + FCB_STATE], ~ST_AT_END
    xor eax, eax
    jmp .done
.not_found:
    mov qword [rbp + PV_CUR], CUR_NONE
    mov eax, 23
.done:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    ret

section .rodata
; cobrt_os_open mode for each OPEN mode
idx_open_modes: dq 0, OS_OPEN_READ, OS_OPEN_RW_TRUNC, OS_OPEN_READWRITE, OS_OPEN_READWRITE

; The operations of the organization, for file.asm (FCB_ORG)
global cobrt_idx_ops
section .rodata
align 8
cobrt_idx_ops:
    dq cobrt_idx_open, cobrt_idx_close, cobrt_idx_read, cobrt_idx_write
    dq cobrt_idx_rewrite, cobrt_idx_delete, cobrt_idx_start
