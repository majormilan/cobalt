; runtime/x86_64/common/numeric.asm
; Numeric core (runtime/ABI.md sections 5 and 6): conversion between field
; storage and signed 64-bit integers, and the arithmetic primitives that need
; 128-bit intermediates. Scales are tracked by the compiler; these routines
; see unscaled integers and explicit powers of ten.

%include "runtime.inc"

; Division by 10 as a multiplication: floor(x / 10) = (x * RECIP10) >> 67
; for every unsigned 64-bit x
%define RECIP10 0xCCCCCCCCCCCCCCCD

section .text

global runtime_num_load
global runtime_num_addw
global runtime_num_store
global runtime_num_load_p
extern runtime_check_data_fail
global runtime_num_store_p
global runtime_packed_store_c
global runtime_num_load_d
global runtime_num_store_d
global runtime_packed_load_d
global runtime_packed_store_cd
global runtime_packed_store_d
global runtime_packed_load
global runtime_packed_store
global runtime_num_rescale
global runtime_num_mul
global runtime_num_div
global cobrt_pow10
global runtime_num_to_digits
global runtime_num_pow

; Packed decimal (COMP-3) conversions work on 16 nibbles at a time (SWAR).
; Internal routines: only rax, rcx and rdx change.
;
; bcd_to_bin: rax = the binary value of the BCD number in rax (16 nibbles,
; most significant first in the usual hex reading). Clobbers rcx, rdx.
; Invalid nibbles (A to F) count with their value, as in a digit loop.
bcd_to_bin:
    mov rcx, 0x0F0F0F0F0F0F0F0F
    mov rdx, rax
    shr rdx, 4
    and rax, rcx
    and rdx, rcx
    lea rdx, [rdx + rdx*4]
    lea rax, [rax + rdx*2]              ; bytes: 10 * high + low nibble
    mov rcx, 0x00FF00FF00FF00FF
    mov rdx, rax
    shr rdx, 8
    and rax, rcx
    and rdx, rcx
    imul rdx, rdx, 100
    add rax, rdx                        ; 16-bit lanes: four digits
    mov rcx, 0x0000FFFF0000FFFF
    mov rdx, rax
    shr rdx, 16
    and rax, rcx
    and rdx, rcx
    imul rdx, rdx, 10000
    add rax, rdx                        ; 32-bit lanes: eight digits
    mov rdx, rax
    shr rdx, 32
    mov eax, eax
    imul rdx, rdx, 100000000
    add rax, rdx
    ret

; bin8_to_bcd: rax = the 8-nibble BCD form of eax (below 10^8).
; Clobbers rcx, rdx.
bin8_to_bcd:
    mov edx, eax
    mov ecx, 0xD1B71759
    imul rdx, rcx
    shr rdx, 45                         ; q = n / 10000
    imul ecx, edx, 10000
    sub eax, ecx                        ; r = n - q * 10000
    shl rdx, 32
    or rax, rdx                         ; 32-bit lanes: q, r
    imul rdx, rax, 10486
    shr rdx, 20
    mov rcx, 0x0000007F0000007F
    and rdx, rcx                        ; each lane / 100
    imul rcx, rdx, 100
    sub rax, rcx
    shl rdx, 16
    or rax, rdx                         ; 16-bit lanes: two digits each
    imul rdx, rax, 103
    shr rdx, 10
    mov rcx, 0x000F000F000F000F
    and rdx, rcx                        ; each lane / 10
    lea rcx, [rdx + rdx*2]
    lea rax, [rax + rcx*2]              ; t + 6 * (t / 10): a BCD byte per lane
    mov rdx, rax
    shr rdx, 8
    or rax, rdx
    mov rcx, 0x0000FFFF0000FFFF
    and rax, rcx
    mov rdx, rax
    shr rdx, 16
    or eax, edx
    ret

; DISPLAY conversions, 8 digits at a time (SWAR). Internal routines.
;
; ascii_to_bin: rdi = digits, rsi = count (0 to 18). Returns rax = value and
; rdx = 0, or rdx = 1 when a byte is not a digit '0' to '9'.
; Changes rax, rcx, rdx, rsi, rdi, r8, r10.
ascii_to_bin:
    xor eax, eax
.ab_chunk:
    cmp rsi, 8
    jb .ab_tail
    mov r8, [rdi]
    mov r10, 0x3030303030303030
    mov rcx, 0xF0F0F0F0F0F0F0F0
    mov rdx, r8
    and rdx, rcx
    cmp rdx, r10
    jne .ab_bad                         ; a byte outside 0x30..0x3F
    mov rdx, 0x0606060606060606
    add rdx, r8
    and rdx, rcx
    cmp rdx, r10
    jne .ab_bad                         ; a byte in 0x3A..0x3F
    mov rcx, 0x0F0F0F0F0F0F0F0F
    and r8, rcx                         ; digits, the first in the lowest byte
    lea rdx, [r8 + r8*4]
    add rdx, rdx
    shr r8, 8
    add r8, rdx
    mov rcx, 0x00FF00FF00FF00FF
    and r8, rcx                         ; 16-bit lanes: two digits
    imul rdx, r8, 100
    shr r8, 16
    add r8, rdx
    mov rcx, 0x0000FFFF0000FFFF
    and r8, rcx                         ; 32-bit lanes: four digits
    imul rdx, r8, 10000
    shr r8, 32
    add r8, rdx
    mov r8d, r8d                        ; eight digits
    imul rax, rax, 100000000
    add rax, r8
    add rdi, 8
    sub rsi, 8
    jmp .ab_chunk
.ab_tail:
    test rsi, rsi
    jz .ab_ok
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .ab_bad
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    dec rsi
    jmp .ab_tail
.ab_ok:
    xor edx, edx
    ret
.ab_bad:
    mov edx, 1
    ret

; bin8_to_ascii: rax = the 8 ASCII digits of eax (below 10^8), the first
; in the lowest byte. Changes rcx, rdx.
bin8_to_ascii:
    mov edx, eax
    mov ecx, 0xD1B71759
    imul rdx, rcx
    shr rdx, 45                         ; q = n / 10000
    imul ecx, edx, 10000
    sub eax, ecx                        ; r = n - q * 10000
    shl rax, 32
    or rax, rdx                         ; 32-bit lanes: q, then r
    imul rdx, rax, 10486
    shr rdx, 20
    mov rcx, 0x0000007F0000007F
    and rdx, rcx                        ; each lane / 100
    imul rcx, rdx, 100
    sub rax, rcx
    shl rax, 16
    or rax, rdx                         ; 16-bit lanes: two digits each
    imul rdx, rax, 103
    shr rdx, 10
    mov rcx, 0x000F000F000F000F
    and rdx, rcx                        ; each lane / 10
    imul rcx, rdx, 10
    sub rax, rcx
    shl rax, 8
    or rax, rdx                         ; a digit per byte
    mov rcx, 0x3030303030303030
    or rax, rcx
    ret

; write_tail: write the last rcx (1 to 7) of the 8 digits in rax at rdi,
; with exact stores. Changes rax, rcx, rdx.
write_tail:
    mov rdx, rcx
    neg rcx
    add rcx, 8
    shl rcx, 3
    shr rax, cl                         ; the digits wanted from byte 0
    cmp rdx, 4
    jb .wt_short
    mov [rdi], eax
    lea rcx, [rdx*8 - 32]
    shr rax, cl
    mov [rdi + rdx - 4], eax
    ret
.wt_short:
    cmp rdx, 2
    jb .wt_one
    mov [rdi], ax
    lea rcx, [rdx*8 - 16]
    shr eax, cl
    mov [rdi + rdx - 2], ax
    ret
.wt_one:
    mov [rdi], al
    ret

; ascii_store: write the value rax (below 10^count) as rsi (1 to 18) ASCII
; digits at rdi. Changes rax, rcx, rdx, r8, r10.
ascii_store:
    mov r10, rax
    xor r8d, r8d                        ; digits above the low 16
    cmp rsi, 16
    jbe .as_split8
    mov rax, 0x734aca5f6226f0b          ; / 10^16 for values below 2^60
    mul r10
    shr rdx, 48
    mov r8, rdx
    mov rcx, 10000000000000000
    imul rdx, rcx
    sub r10, rdx
.as_split8:
    mov rax, 0xabcc77118461d            ; / 10^8 for values below 2^54
    mul r10
    shr rdx, 14
    imul rcx, rdx, 100000000
    sub r10, rcx                        ; r10 = low 8 digits, rdx = next 8
    push rdx
    mov eax, r10d
    call bin8_to_ascii
    cmp rsi, 8
    jb .as_low_part
    mov [rdi + rsi - 8], rax
    pop rdx
    cmp rsi, 8
    je .as_ret
    mov eax, edx
    call bin8_to_ascii
    lea rcx, [rsi - 8]
    cmp rcx, 8
    jb write_tail                       ; 9 to 15 digits
    mov [rdi + rsi - 16], rax
    cmp rsi, 16
    je .as_ret
    imul ecx, r8d, 103
    shr ecx, 10                         ; tens of the top digits
    imul edx, ecx, 10
    sub r8d, edx
    add ecx, '0'
    add r8d, '0'
    cmp rsi, 17
    je .as_17
    mov [rdi], cl
    mov [rdi + 1], r8b
    ret
.as_17:
    mov [rdi], r8b
.as_ret:
    ret
.as_low_part:
    add rsp, 8
    mov rcx, rsi
    jmp write_tail

; decode_overpunch: dl = a digit that may carry the sign. Returns edx = its
; digit (anything else counts as 0) and sets ebx = 1 when it is negative:
; 'p'..'y' = -0..-9; '{' 'A'..'I' = +0..+9; '}' 'J'..'R' = -0..-9.
decode_overpunch:
    cmp dl, 'p'
    jb .do_ebcdic
    cmp dl, 'y'
    ja .do_ebcdic
    sub dl, 'p'
    mov ebx, 1
    jmp .do_ret
.do_ebcdic:
    cmp dl, '{'
    jne .do_not_pos0
    xor edx, edx
    ret
.do_not_pos0:
    cmp dl, '}'
    jne .do_not_neg0
    xor edx, edx
    mov ebx, 1
    ret
.do_not_neg0:
    cmp dl, 'A'
    jb .do_plain
    cmp dl, 'I'
    ja .do_neg_letter
    sub dl, 'A' - 1
    jmp .do_ret
.do_neg_letter:
    cmp dl, 'J'
    jb .do_plain
    cmp dl, 'R'
    ja .do_plain
    sub dl, 'J' - 1
    mov ebx, 1
    jmp .do_ret
.do_plain:
    sub dl, '0'
    cmp dl, 9
    jbe .do_ret
    xor edx, edx
    ret
.do_ret:
    movzx edx, dl
    ret

; ARG0 = COMP-3 item, ARG1 = its size (1 to 10 bytes)
; Returns RET0 = its value: the bytes as one big-endian number, converted 16
; nibbles at a time, with exact reads (overlapping loads, never past the
; item). Changes only caller-saved registers.
runtime_packed_load:
    mov rdi, ARG0
    mov rsi, ARG1
    movzx r8d, byte [rdi + rsi - 1]
    and r8d, 0x0f                       ; sign nibble
    cmp rsi, 8
    ja .pl_long
    cmp rsi, 4
    jb .pl_short
    ; 4..8 bytes: (first dword << 8(n-4)) | last dword; the overlap agrees
    mov eax, [rdi]
    bswap eax
    lea rcx, [rsi*8 - 32]
    shl rax, cl
    mov edx, [rdi + rsi - 4]
    bswap edx
    or rax, rdx
    jmp .pl_conv
.pl_short:
    cmp rsi, 2
    jb .pl_one
    movzx eax, word [rdi]
    rol ax, 8
    lea rcx, [rsi*8 - 16]
    shl eax, cl
    movzx edx, word [rdi + rsi - 2]
    rol dx, 8
    or eax, edx
    jmp .pl_conv
.pl_one:
    movzx eax, byte [rdi]
.pl_conv:
    shr rax, 4                          ; drop the sign nibble
    call bcd_to_bin
    jmp .pl_sign_apply
.pl_long:
    ; 9 or 10 bytes: the last 8 hold 15 digits, the first one or two the rest
    mov rax, [rdi + rsi - 8]
    bswap rax
    shr rax, 4
    call bcd_to_bin
    mov r9, rax
    movzx eax, byte [rdi]
    cmp rsi, 9
    je .pl_long_hi
    movzx eax, word [rdi]
    rol ax, 8
.pl_long_hi:
    call bcd_to_bin
    mov rcx, 1000000000000000
    imul rax, rcx
    add rax, r9
.pl_sign_apply:
    mov rdx, rax
    neg rdx
    cmp r8d, 0x0d
    cmove rax, rdx
    cmp r8d, 0x0b
    cmove rax, rdx
    ret

; The same routines called with a descriptor of the item (8 bytes the
; compiler emits once per item and kind of call: its address, dd, then the
; packed arguments, dd) in ARG0, and the value to store in ARG1: one
; register to load at each call.
runtime_num_load_d:
    mov esi, [ARG0 + 4]
    mov edi, [ARG0]
    jmp runtime_num_load_p

runtime_num_store_d:
    mov r9, ARG1
    mov esi, [ARG0 + 4]
    mov edi, [ARG0]
    jmp runtime_num_store_p

runtime_packed_load_d:
    mov esi, [ARG0 + 4]
    mov edi, [ARG0]
    jmp runtime_packed_load

runtime_packed_store_cd:                ; checked: size | digits << 8 | flags << 16
    mov rdx, ARG1
    mov esi, [ARG0 + 4]
    mov edi, [ARG0]
    jmp runtime_packed_store_c

runtime_packed_store_d:                 ; known to fit: size | flags << 16
    mov rdx, ARG1
    mov esi, [ARG0 + 4]
    mov ecx, esi
    shr ecx, 16
    and esi, 0xFFFF
    mov edi, [ARG0]
    jmp runtime_packed_store

; A packed decimal store that checks the value: ARG0 = the item, ARG1 =
; size | digits << 8 | flags << 16, ARG2 = value (at the item's scale).
; A value below 10^digits goes to runtime_packed_store; another to
; runtime_num_store, which truncates it (or keeps the item, NUM_KEEP) and
; reports the size error. Returns RET0 = 1 on size error, else 0.
runtime_packed_store_c:
    mov r10, ARG2
    neg r10
    cmovs r10, ARG2                     ; |value|
    mov ecx, esi
    shr ecx, 8
    and ecx, 0xFF                       ; digits
    lea r11, [rel pow10]
    cmp r10, [r11 + rcx*8]
    jae .too_large
    mov ecx, esi
    shr ecx, 16                         ; flags
    and esi, 0xFF                       ; size
    sub rsp, 8
    call runtime_packed_store
    add rsp, 8
    xor eax, eax
    ret
.too_large:
    mov r9, ARG2                        ; value
    mov r8d, esi
    shr r8d, 16                         ; flags
    mov edx, esi
    shr edx, 8
    and edx, 0xFF                       ; digits
    mov ecx, USAGE_COMP3
    and esi, 0xFF                       ; size
    jmp runtime_num_store

; ARG0 = COMP-3 item, ARG1 = its size (1 to 10 bytes), ARG2 = value,
; ARG3 = flags (NUM_SIGNED: sign nibble C or D, else F and the magnitude).
; The value must fit the item (at most 18 digits, below 10^digits).
; The nibbles are the value's digits in BCD followed by the sign, written
; with exact stores. Changes only caller-saved registers.
runtime_packed_store:
    mov r11, ARG0
    mov r9, ARG1
    mov rax, ARG2
    mov r10d, 0x0f
    test ARG3, NUM_SIGNED
    jz .ps_abs
    mov r10d, 0x0c
    test rax, rax
    jns .ps_mag
    mov r10d, 0x0d
.ps_abs:
    mov rdx, rax
    neg rdx
    cmovns rax, rdx                     ; magnitude
.ps_mag:
    mov rsi, rax
    xor edi, edi                        ; digits above the low 16
    cmp r9, 8
    jbe .ps_split8                      ; at most 15 digits
    mov rax, 0x734aca5f6226f0b          ; / 10^16 for values below 2^60
    mul rsi
    shr rdx, 48
    mov edi, edx
    mov rcx, 10000000000000000
    imul rdx, rcx
    sub rsi, rdx                        ; below 10^16
.ps_split8:
    mov rax, 0xabcc77118461d            ; / 10^8 for values below 2^54
    mul rsi
    shr rdx, 14
    imul rcx, rdx, 100000000
    sub rsi, rcx                        ; rsi = low 8 digits, rdx = high 8
    mov eax, edx
    call bin8_to_bcd
    mov r8, rax
    mov eax, esi
    call bin8_to_bcd
    shl r8, 32
    or r8, rax                          ; the low 16 digits in BCD
    mov rax, r8
    shl rax, 4
    or rax, r10                         ; last 8 bytes: 15 digits and the sign
    cmp r9, 8
    ja .ps_long
    ; n <= 8 bytes: the low n bytes of rax, big-endian
    bswap rax
    lea rcx, [r9*8]
    neg rcx
    add rcx, 64
    shr rax, cl                         ; the item's bytes from byte 0
    cmp r9, 4
    jb .ps_short
    mov [r11], eax
    lea rcx, [r9*8 - 32]
    shr rax, cl
    mov [r11 + r9 - 4], eax
    ret
.ps_short:
    cmp r9, 2
    jb .ps_one
    mov [r11], ax
    lea rcx, [r9*8 - 16]
    shr eax, cl
    mov [r11 + r9 - 2], ax
    ret
.ps_one:
    mov [r11], al
    ret
.ps_long:
    bswap rax
    mov [r11 + r9 - 8], rax
    ; the first byte or two: the digits above the low 16, then the top
    ; digit of the low 16
    imul ecx, edi, 103
    shr ecx, 10
    lea ecx, [rcx + rcx*2]
    lea edi, [rdi + rcx*2]              ; top digits (below 100) in BCD
    shl edi, 4
    shr r8, 60
    or edi, r8d
    cmp r9, 9
    je .ps_long9
    rol di, 8
    mov [r11], di
    ret
.ps_long9:
    mov [r11], dil
    ret

; runtime_num_load and runtime_num_store with their arguments packed in
; ARG1, one immediate for the compiler to load (a field of up to 255 bytes):
; load: size | usage << 16 | flags << 24; store: size | digits << 8 |
; usage << 16 | flags << 24 (the value in ARG5, as for runtime_num_store)
runtime_num_load_p:
    mov ARG3, ARG1
    shr ARG3, 24
    mov ARG2, ARG1
    shr ARG2, 16
    and ARG2, 0xFF
    and ARG1, 0xFF
    jmp runtime_num_load

runtime_num_store_p:
    mov ARG4, ARG1
    shr ARG4, 24
    mov ARG3, ARG1
    shr ARG3, 16
    and ARG3, 0xFF
    mov ARG2, ARG1
    shr ARG2, 8
    and ARG2, 0xFF
    and ARG1, 0xFF
    jmp runtime_num_store

; ARG0 = field ptr, ARG1 = size, ARG2 = usage, ARG3 = flags
; Returns RET0 = the field's value as an unscaled signed integer.
runtime_num_load:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14

    mov r12, ARG0                       ; field ptr
    mov r13, ARG1                       ; size
    mov r14, ARG3                       ; flags
    mov rax, ARG2                       ; usage
    cmp rax, USAGE_COMP
    je .comp
    cmp rax, USAGE_COMP3
    je .comp3
    cmp rax, USAGE_COMP5
    je .comp5

    ; DISPLAY: one ASCII digit per byte. A signed item has an overpunched
    ; sign on its last digit, or its first with SIGN LEADING, or a separate
    ; + or - character before or after the digits.
    xor eax, eax
    xor ebx, ebx                        ; rbx = negative
    mov r9, -1                          ; r9 = index of the overpunched digit
    test r14, NUM_SIGNED
    jz .d_start
    test r14, NUM_SIGN_SEPARATE
    jz .d_overpunch
    dec r13                             ; digits exclude the sign character
    test r14, NUM_SIGN_LEADING
    jz .d_sep_trailing
    movzx edx, byte [r12]
    inc r12
    jmp .d_sep_sign
.d_sep_trailing:
    movzx edx, byte [r12 + r13]
.d_sep_sign:
    cmp dl, '-'
    jne .d_start
    mov ebx, 1
    jmp .d_start
.d_overpunch:
    lea r9, [r13 - 1]
    test r14, NUM_SIGN_LEADING
    jz .d_start
    xor r9d, r9d
.d_start:
    ; unsigned text of more than 18 characters (an alphanumeric field read as
    ; a number, UNSTRING): its low-order 18 digits
    cmp r13, 18
    jbe .d_fits
    cmp r9, -1
    jne .d_fits
    lea rdx, [r13 - 18]
    add r12, rdx
    mov r13d, 18
.d_fits:
    ; 1 to 18 digits: 8 at a time, the sign digit on its own; any byte that
    ; is not a digit takes the digit loop
    lea rdx, [r13 - 1]
    cmp rdx, 18
    jae .d_slow
    test r9, r9
    js .d_fast_plain
    jz .d_fast_lead
    mov rdi, r12                        ; trailing sign digit
    lea rsi, [r13 - 1]
    call ascii_to_bin
    test edx, edx
    jnz .d_slow
    imul rax, rax, 10
    mov r10, rax
    movzx edx, byte [r12 + r13 - 1]
    call decode_overpunch
    lea rax, [r10 + rdx]
    jmp .d_done
.d_fast_lead:
    lea rdi, [r12 + 1]
    lea rsi, [r13 - 1]
    call ascii_to_bin
    test edx, edx
    jnz .d_slow
    mov r10, rax
    movzx edx, byte [r12]
    call decode_overpunch
    lea rcx, [rel pow10]
    lea r8, [r13 - 1]
    mov rax, [rcx + r8*8]
    imul rax, rdx
    add rax, r10
    jmp .d_done
.d_fast_plain:
    mov rdi, r12
    mov rsi, r13
    call ascii_to_bin
    test edx, edx
    jz .d_done
.d_slow:
    xor eax, eax
    xor ecx, ecx                        ; index
.d_loop:
    cmp rcx, r13
    jge .d_done
    movzx edx, byte [r12 + rcx]
    cmp rcx, r9
    jne .d_plain                        ; only the overpunched digit carries a sign
    cmp dl, 'p'                         ; 'p'..'y' = -0..-9
    jb .d_ebcdic
    cmp dl, 'y'
    ja .d_ebcdic
    sub dl, 'p'
    mov ebx, 1
    jmp .d_add
.d_ebcdic:                              ; '{', 'A'..'I' = +0..+9; '}', 'J'..'R' = -0..-9
    cmp dl, '{'
    jne .d_not_pos0
    xor edx, edx
    jmp .d_add
.d_not_pos0:
    cmp dl, '}'
    jne .d_not_neg0
    xor edx, edx
    mov ebx, 1
    jmp .d_add
.d_not_neg0:
    cmp dl, 'A'
    jb .d_plain
    cmp dl, 'I'
    ja .d_neg_letter
    sub dl, 'A' - 1
    jmp .d_add
.d_neg_letter:
    cmp dl, 'J'
    jb .d_plain
    cmp dl, 'R'
    ja .d_plain
    sub dl, 'J' - 1
    mov ebx, 1
    jmp .d_add
.d_plain:
    sub dl, '0'
    cmp dl, 9
    jbe .d_add
    test qword [rel cobrt_runtime_flags], RT_CHECK_DATA
    jnz .d_bad                          ; -fcheck: the run ends
    xor edx, edx                        ; not a digit (e.g. a space): counts as 0
.d_add:
    imul rax, rax, 10
    add rax, rdx
    inc rcx
    jmp .d_loop
.d_done:
    test ebx, ebx
    jz .ret
    neg rax
    jmp .ret
.d_bad:
    mov ARG0, r12                       ; the item's digits
    mov ARG1, r13                       ; their number
    call runtime_check_data_fail        ; does not return

    ; COMP: big-endian two's complement
.comp:
    xor eax, eax
    xor ecx, ecx
.c_loop:
    cmp rcx, r13
    jge .extend
    shl rax, 8
    movzx edx, byte [r12 + rcx]
    or rax, rdx
    inc rcx
    jmp .c_loop

    ; COMP-5: native (little-endian) two's complement
.comp5:
    xor eax, eax
    mov rcx, r13
.c5_loop:
    test rcx, rcx
    jz .extend
    dec rcx
    shl rax, 8
    movzx edx, byte [r12 + rcx]
    or rax, rdx
    jmp .c5_loop

.extend:                                ; sign-extend from size*8 bits if signed
    test r14, NUM_SIGNED
    jz .ret
    mov rcx, 8
    sub rcx, r13
    shl rcx, 3                          ; 64 - size*8
    shl rax, cl
    sar rax, cl
    jmp .ret

    ; COMP-3: two digits per byte, sign in the low nibble of the last byte.
    ; Up to 10 bytes: the bytes as one big-endian number, converted 16
    ; nibbles at a time; exact reads (overlapping loads), never past the item.
.comp3:
    lea rax, [r13 - 1]
    cmp rax, 10
    jae .p_slow                         ; sizes 1..10: the fast routine
    mov ARG0, r12
    mov ARG1, r13
    call runtime_packed_load
    jmp .ret
.p_slow:
    xor eax, eax
    xor ecx, ecx
.p_loop:
    movzx edx, byte [r12 + rcx]
    mov r8d, edx
    shr r8d, 4                          ; high nibble: a digit
    imul rax, rax, 10
    add rax, r8
    lea r9, [rcx + 1]
    cmp r9, r13
    je .p_sign
    and edx, 0x0f                       ; low nibble: a digit
    imul rax, rax, 10
    add rax, rdx
    mov rcx, r9
    jmp .p_loop
.p_sign:
    and edx, 0x0f
    cmp edx, 0x0d
    je .p_neg
    cmp edx, 0x0b
    jne .ret
.p_neg:
    neg rax

.ret:
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ARG0 = field ptr, ARG1 = size, ARG2 = digits (PIC digit positions),
; ARG3 = usage, ARG4 = flags, ARG5 = value (unscaled, already at field scale)
; Returns RET0 = 1 on size error (high-order digits lost), else 0.
; With NUM_KEEP set the field is left unchanged on size error.
runtime_num_store:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, ARG0                       ; field ptr
    mov r13, ARG1                       ; size
    mov rbx, ARG2                       ; digits (reused as COMP-3 index)
    mov r10, ARG3                       ; usage
    mov r14, ARG4                       ; flags
    mov r15, ARG5                       ; value

    ; rax = magnitude, r11 = negative (only for signed fields)
    xor r11d, r11d
    mov rax, r15
    test rax, rax
    jns .mag
    neg rax
    test r14, NUM_SIGNED
    jz .mag                             ; unsigned field: store the absolute value
    mov r11d, 1
.mag:
    xor r9d, r9d                        ; r9 = size error
    cmp r10, USAGE_COMP5
    je .s_comp5
    cmp r10, USAGE_COMP
    jne .decimal_limit
    test r14, NUM_NOTRUNC
    jnz .s_comp5                        ; limited by its bytes, like COMP-5
.decimal_limit:

    ; Decimal fields hold at most 10^digits - 1
    cmp rbx, 18
    ja .limit_ok
    lea rcx, [rel pow10]
    mov r8, [rcx + rbx*8]
    cmp rax, r8
    jb .limit_ok
    mov r9d, 1
    xor edx, edx
    div r8                              ; keep the low-order digits
    mov rax, rdx
.limit_ok:
    test r9d, r9d
    jz .write
    test r14, NUM_KEEP
    jnz .done
.write:
    test rax, rax
    jnz .dispatch
    xor r11d, r11d                      ; negative zero is stored as positive zero
.dispatch:
    cmp r10, USAGE_COMP
    je .s_comp
    cmp r10, USAGE_COMP3
    je .s_comp3

    ; DISPLAY: digits right to left, then the sign (see runtime_num_load)
    mov rbx, r12                        ; first digit
    mov rcx, r13                        ; number of digits
    test r14, NUM_SIGNED
    jz .sd_digits
    test r14, NUM_SIGN_SEPARATE
    jz .sd_digits
    dec rcx
    test r14, NUM_SIGN_LEADING
    jz .sd_digits
    inc rbx
.sd_digits:
    lea rdx, [rcx - 1]
    cmp rdx, 18
    jae .sd_slow                        ; 1 to 18 digits: 8 at a time
    mov rdi, rbx
    mov rsi, rcx
    call ascii_store
    jmp .sd_sign
.sd_slow:
    mov r8, RECIP10
.sd_loop:
    test rcx, rcx
    jz .sd_sign
    test rax, rax
    jz .sd_zeros                        ; the rest are leading zeros
    dec rcx
    mov rsi, rax
    mul r8                              ; rdx = value * RECIP10 >> 64
    shr rdx, 3                          ; quotient by 10
    lea rax, [rdx + rdx*4]
    add rax, rax
    sub rsi, rax                        ; remainder: the digit
    add sil, '0'
    mov [rbx + rcx], sil
    mov rax, rdx
    jmp .sd_loop
.sd_zeros:
    dec rcx
    mov byte [rbx + rcx], '0'
    jnz .sd_zeros
.sd_sign:
    test r14, NUM_SIGNED
    jz .done
    test r14, NUM_SIGN_SEPARATE
    jz .sd_overpunch
    mov dl, '+'
    test r11d, r11d
    jz .sd_put
    mov dl, '-'
.sd_put:
    test r14, NUM_SIGN_LEADING
    jz .sd_put_trailing
    mov [r12], dl
    jmp .done
.sd_put_trailing:
    mov [r12 + r13 - 1], dl
    jmp .done
.sd_overpunch:
    test qword [rel cobrt_runtime_flags], RT_SIGN_EBCDIC
    jnz .sd_ebcdic
    test r11d, r11d
    jz .done
    test r14, NUM_SIGN_LEADING
    jz .sd_op_trailing
    add byte [r12], 0x40                ; '0'..'9' -> 'p'..'y'
    jmp .done
.sd_op_trailing:
    add byte [r12 + r13 - 1], 0x40
    jmp .done
.sd_ebcdic:
    ; Both signs overpunched: '{' 'A'..'I' = +0..+9, '}' 'J'..'R' = -0..-9
    lea r8, [r12 + r13 - 1]
    test r14, NUM_SIGN_LEADING
    jz .sde_digit
    mov r8, r12
.sde_digit:
    movzx eax, byte [r8]
    sub al, '0'
    test r11d, r11d
    jnz .sde_neg
    mov dl, '{'
    test al, al
    jz .sde_put
    lea edx, [rax + 'A' - 1]
    jmp .sde_put
.sde_neg:
    mov dl, '}'
    test al, al
    jz .sde_put
    lea edx, [rax + 'J' - 1]
.sde_put:
    mov [r8], dl
    jmp .done

    ; COMP: big-endian two's complement
.s_comp:
    test r11d, r11d
    jz .sc_write
    neg rax
.sc_write:
    mov rcx, r13
.sc_loop:
    test rcx, rcx
    jz .done
    dec rcx
    mov [r12 + rcx], al
    shr rax, 8
    jmp .sc_loop

    ; COMP-3: sign nibble C (+), D (-) or F (unsigned)
.s_comp3:
    mov r8, RECIP10
    mov ecx, 0x0f
    test r14, NUM_SIGNED
    jz .sp_sign
    mov ecx, 0x0c
    test r11d, r11d
    jz .sp_sign
    mov ecx, 0x0d
.sp_sign:
    lea rdx, [r13 - 1]
    cmp rdx, 10
    jae .sp_slow                        ; sizes 1..10: the fast routine
    mov rdx, rax                        ; the value, signed again
    test r11d, r11d
    jz .sp_fast
    neg rdx
.sp_fast:
    mov ARG0, r12
    mov ARG1, r13
    mov ARG2, rdx
    mov ARG3, r14
    mov ebx, r9d                        ; the size error survives the call
    call runtime_packed_store
    mov r9d, ebx
    jmp .done
.sp_slow:
    mov rbx, r13
    dec rbx                             ; last byte: lowest digit + sign
    mov rsi, rax                        ; rax / 10, remainder in rsi
    mul r8
    shr rdx, 3
    lea rax, [rdx + rdx*4]
    add rax, rax
    sub rsi, rax
    mov rax, rdx
    shl esi, 4
    or esi, ecx
    mov [r12 + rbx], sil
.sp_loop:
    test rbx, rbx
    jz .done
    dec rbx
    mov rsi, rax                        ; rax / 10, remainder in rsi
    mul r8
    shr rdx, 3
    lea rax, [rdx + rdx*4]
    add rax, rax
    sub rsi, rax
    mov rax, rdx
    mov ecx, esi                        ; low nibble
    mov rsi, rax                        ; rax / 10, remainder in rsi
    mul r8
    shr rdx, 3
    lea rax, [rdx + rdx*4]
    add rax, rax
    sub rsi, rax
    mov rax, rdx
    shl esi, 4                          ; high nibble
    or esi, ecx
    mov [r12 + rbx], sil
    jmp .sp_loop

    ; COMP-5: the limit is the binary range of the field size
.s_comp5:
    mov rcx, r13
    shl rcx, 3                          ; bits
    test r14, NUM_SIGNED
    jnz .s5_signed
    cmp rcx, 64
    jae .s5_ok
    mov r8, 1
    shl r8, cl
    cmp rax, r8
    jb .s5_ok
    jmp .s5_err
.s5_signed:
    dec rcx
    mov r8, 1
    shl r8, cl                          ; 2^(bits-1)
    test r11d, r11d
    jz .s5_pos
    cmp rax, r8
    jbe .s5_ok
    jmp .s5_err
.s5_pos:
    cmp rax, r8
    jb .s5_ok
.s5_err:
    mov r9d, 1
    test r14, NUM_KEEP
    jnz .done
.s5_ok:
    cmp r10, USAGE_COMP
    je .s_comp                          ; big-endian
    test r11d, r11d
    jz .s5_write
    neg rax
.s5_write:
    xor ecx, ecx
.s5_loop:
    cmp rcx, r13
    jae .done
    mov [r12 + rcx], al
    shr rax, 8
    inc rcx
    jmp .s5_loop

.done:
    mov eax, r9d
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ARG0 = value, ARG1 = shift (power of ten, may be negative), ARG2 = rounded
; Returns RET0 = value * 10^shift, RET1 = 1 on overflow.
; A negative shift truncates toward zero, or rounds half away from zero when
; ARG2 is non-zero.
runtime_num_rescale:
    mov r10, ARG0                       ; value
    mov r11, ARG1                       ; shift
    mov r9, ARG2                        ; rounded
    mov rax, r10
    xor edx, edx
    test r11, r11
    jz .r_ret
    js .r_down
    cmp r11, 18
    ja .r_up_big
    lea rcx, [rel pow10]
    imul rax, [rcx + r11*8]
    jo .r_err
    jmp .r_ok
.r_up_big:
    test rax, rax
    jz .r_ok
    jmp .r_err
.r_down:
    neg r11
    cmp r11, 18
    ja .r_zero
    lea rcx, [rel pow10]
    mov r8, [rcx + r11*8]
    cqo
    idiv r8                             ; rax = quotient, rdx = remainder
    test r9, r9
    jz .r_ok
    mov rcx, rdx
    test rcx, rcx
    jns .r_absrem
    neg rcx
.r_absrem:
    shl rcx, 1
    cmp rcx, r8
    jb .r_ok                            ; remainder below one half
    test r10, r10
    js .r_round_neg
    inc rax
    jmp .r_ok
.r_round_neg:
    dec rax
    jmp .r_ok
.r_zero:
    xor eax, eax
.r_ok:
    xor edx, edx
.r_ret:
    ret
.r_err:
    xor eax, eax
    mov edx, 1
    ret

; ARG0 = a, ARG1 = b, ARG2 = shift (0..18)
; Returns RET0 = (a * b) / 10^shift truncated toward zero, using a 128-bit
; product; RET1 = 1 on overflow.
runtime_num_mul:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    mov r8, r10
    xor r8, r11                         ; bit 63 = sign of the result
    mov rax, r10
    test rax, rax
    jns .m_a
    neg rax
.m_a:
    mov rcx, r11
    test rcx, rcx
    jns .m_b
    neg rcx
.m_b:
    mul rcx                             ; rdx:rax = |a| * |b|
    test r9, r9
    jz .m_noshift
    cmp r9, 36
    ja .m_err
    cmp r9, 18
    jbe .m_one
    ; shift above 18: by 10^18, then by the rest
    lea rcx, [rel pow10]
    mov rcx, [rcx + 18*8]
    cmp rdx, rcx
    jae .m_err
    div rcx
    xor edx, edx
    sub r9, 18
.m_one:
    lea rcx, [rel pow10]
    mov rcx, [rcx + r9*8]
    cmp rdx, rcx
    jae .m_err                          ; quotient would not fit in 64 bits
    div rcx
    jmp .m_check
.m_noshift:
    test rdx, rdx
    jnz .m_err
.m_check:
    test rax, rax
    js .m_err
    test r8, r8
    jns .m_ok
    neg rax
.m_ok:
    xor edx, edx
    ret
.m_err:
    xor eax, eax
    mov edx, 1
    ret

; ARG0 = a (at scale ARG2), ARG1 = b (at scale ARG3), ARG4 = the result's
; scale t, ARG5 = 1 to subtract b. Returns RET0 = a + b (or a - b) at scale
; t, truncated toward zero, the operands aligned and added in 128 bits
; (an 18-digit item and one with decimals); RET1 = 1 if it does not fit.
runtime_num_addw:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r11, ARG0                       ; a
    mov rbx, ARG1                       ; b
    mov r12, ARG2                       ; scale of a
    mov r13, ARG3                       ; scale of b
    mov r14, ARG4                       ; t
    mov r15, ARG5                       ; subtract
    cmp r12, 18
    ja .a_err
    cmp r13, 18
    ja .a_err
    cmp r14, 18
    ja .a_err
    mov r10, r12                        ; s = the larger scale
    cmp r13, r10
    jbe .a_s
    mov r10, r13
.a_s:
    lea rcx, [rel pow10]
    ; A = a * 10^(s - sa), signed 128 bits, in r9:r8
    mov rax, r10
    sub rax, r12
    mov rdx, [rcx + rax*8]
    mov rax, r11
    imul rdx
    mov r8, rax
    mov r9, rdx
    ; B = b * 10^(s - sb)
    mov rax, r10
    sub rax, r13
    mov rdx, [rcx + rax*8]
    mov rax, rbx
    imul rdx
    test r15, r15
    jz .a_add
    neg rax                             ; -B
    adc rdx, 0
    neg rdx
.a_add:
    add r8, rax
    adc r9, rdx                         ; the sum at scale s
    ; |sum|, its sign kept in r11
    mov r11, r9
    test r9, r9
    jns .a_abs
    neg r8
    adc r9, 0
    neg r9
.a_abs:
    mov rax, r8
    mov rdx, r9
    cmp r14, r10
    jae .a_up
    ; down to scale t: divide by 10^(s - t)
    mov rbx, r10
    sub rbx, r14
    mov rbx, [rcx + rbx*8]
    cmp rdx, rbx
    jae .a_err                          ; the quotient would not fit
    div rbx
    jmp .a_fit
.a_up:
    ; up to scale t: multiply by 10^(t - s)
    test rdx, rdx
    jnz .a_err
    mov rbx, r14
    sub rbx, r10
    mul qword [rcx + rbx*8]
    test rdx, rdx
    jnz .a_err
.a_fit:
    test rax, rax
    js .a_err
    test r11, r11
    jns .a_ok
    neg rax
.a_ok:
    xor edx, edx
    jmp .a_ret
.a_err:
    xor eax, eax
    mov edx, 1
.a_ret:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ARG0 = a, ARG1 = b, ARG2 = shift (0..36)
; Returns RET0 = (a * 10^shift) / b truncated toward zero, using a 128-bit
; dividend (192 bits for a shift above 18); RET1 = 1 on division by zero or
; overflow.
runtime_num_div:
    mov r10, ARG0
    mov r11, ARG1
    mov r9, ARG2
    test r11, r11
    jz .v_err
    cmp r9, 36
    ja .v_err
    cmp r9, 18
    ja .v_wide
    mov r8, r10
    xor r8, r11                         ; bit 63 = sign of the result
    mov rax, r10
    test rax, rax
    jns .v_a
    neg rax
.v_a:
    mov rcx, r11
    test rcx, rcx
    jns .v_b
    neg rcx
.v_b:
    lea rdx, [rel pow10]
    mul qword [rdx + r9*8]              ; rdx:rax = |a| * 10^shift
    cmp rdx, rcx
    jae .v_err                          ; quotient would not fit in 64 bits
    div rcx
    test rax, rax
    js .v_err
    test r8, r8
    jns .v_ok
    neg rax
.v_ok:
    xor edx, edx
    ret
.v_err:
    xor eax, eax
    mov edx, 1
    ret
    ; |a| * 10^18 * 10^(shift - 18): three 64-bit limbs, divided by |b|
.v_wide:
    push rbx
    mov r8, r10
    xor r8, r11                         ; bit 63 = sign of the result
    mov rax, r10
    test rax, rax
    jns .w_a
    neg rax
.w_a:
    mov rcx, r11
    test rcx, rcx
    jns .w_b
    neg rcx
.w_b:
    lea rbx, [rel pow10]
    mul qword [rbx + 18*8]              ; rdx:rax = |a| * 10^18
    mov r10, rdx                        ; hi
    sub r9, 18
    mov r9, [rbx + r9*8]                ; m = 10^(shift - 18)
    mul r9                              ; lo * m
    mov r11, rax                        ; n0
    mov rbx, rdx                        ; carry into n1
    mov rax, r10
    mul r9                              ; hi * m: rdx:rax
    add rax, rbx
    adc rdx, 0                          ; n2:n1 = rdx:rax
    cmp rdx, rcx
    jae .w_err                          ; quotient of 128 bits or more
    div rcx                             ; q1 (must be 0), remainder in rdx
    test rax, rax
    jnz .w_err
    mov rax, r11
    div rcx                             ; q0
    test rax, rax
    js .w_err
    test r8, r8
    jns .w_ok
    neg rax
.w_ok:
    xor edx, edx
    pop rbx
    ret
.w_err:
    xor eax, eax
    mov edx, 1
    pop rbx
    ret

; Exponentiation with an exponent known only at run time.
; ARG0 = base, ARG1 = base scale, ARG2 = exponent, ARG3 = exponent scale,
; ARG4 = result scale W (0..18).
; Returns RET0 = base ** exponent at scale W; RET1 = 1 on a size error: 0 **
; n with n <= 0, a negative base with an exponent that is not an integer,
; or overflow.
runtime_num_pow:
    push rbx
    push r12
    push r13
    push r14
    push r15                            ; rsp is 16-byte aligned here
    mov r12, ARG0
    mov r13, ARG1
    mov r14, ARG2
    mov r15, ARG3
    mov rbx, ARG4

    ; The exponent as an integer
    test r15, r15
    jz .p_exp_int
    js .p_exp_up
    cmp r15, 18
    ja .p_err
    lea rcx, [rel pow10]
    mov rcx, [rcx + r15*8]
    mov rax, r14
    cqo
    idiv rcx
    test rdx, rdx
    jnz .p_frac                         ; an exponent with a fraction
    mov r14, rax
    jmp .p_exp_int
.p_exp_up:
    mov ARG0, r14
    mov ARG1, r15
    neg ARG1
    xor ARG2, ARG2
    call runtime_num_rescale
    test dl, dl
    jnz .p_err
    mov r14, rax
.p_exp_int:
    ; The base at scale W
    mov ARG0, r12
    mov ARG1, rbx
    sub ARG1, r13
    xor ARG2, ARG2
    call runtime_num_rescale
    test dl, dl
    jnz .p_err
    mov r12, rax
    test r12, r12
    jnz .p_nonzero
    test r14, r14
    jle .p_err                          ; 0 ** 0 and 0 ** negative
    xor eax, eax
    jmp .p_done
.p_nonzero:
    xor r15d, r15d                      ; r15 = 1 for a negative exponent
    test r14, r14
    jns .p_square
    neg r14
    mov r15d, 1
.p_square:
    lea rcx, [rel pow10]
    mov r13, [rcx + rbx*8]              ; result = 1 at scale W
.p_loop:
    test r14, r14
    jz .p_invert
    test r14, 1
    jz .p_next
    mov ARG0, r13
    mov ARG1, r12
    mov ARG2, rbx
    call runtime_num_mul
    test dl, dl
    jnz .p_err
    mov r13, rax
.p_next:
    shr r14, 1
    jz .p_invert
    mov ARG0, r12
    mov ARG1, r12
    mov ARG2, rbx
    call runtime_num_mul
    test dl, dl
    jnz .p_err
    mov r12, rax
    jmp .p_loop
.p_invert:
    mov rax, r13
    test r15, r15
    jz .p_done
    lea rcx, [rel pow10]
    mov ARG0, [rcx + rbx*8]             ; 1 / result: (10^W * 10^W) / result
    mov ARG1, r13
    mov ARG2, rbx
    call runtime_num_div
    test dl, dl
    jnz .p_err
.p_done:
    xor edx, edx
    jmp .p_ret
.p_frac:
    ; base ** exponent = 2 ** (exponent * log2 base), base > 0 (in x87
    ; extended precision), at scale W, truncated
    test r12, r12
    jz .p_frac_zero
    js .p_err
    cmp r13, 18
    ja .p_err
    sub rsp, 16
    lea rcx, [rel pow10]
    mov [rsp], r14
    fild qword [rsp]
    fild qword [rcx + r15*8]
    fdivp                               ; the exponent
    mov [rsp], r12
    fild qword [rsp]
    fild qword [rcx + r13*8]
    fdivp                               ; the base
    fyl2x                               ; exponent * log2(base)
    fld st0
    frndint
    fstp qword [rsp + 8]                ; n
    fld qword [rsp + 8]
    fsubp                               ; the fraction
    f2xm1
    fld1
    faddp                               ; 2 ** fraction
    fld qword [rsp + 8]
    fxch st1
    fscale                              ; 2 ** fraction * 2 ** n
    fstp st1
    fild qword [rcx + rbx*8]
    fmulp                               ; at scale W
    fisttp qword [rsp]
    mov rax, [rsp]
    add rsp, 16
    mov rcx, 0x8000000000000000         ; the integer indefinite: out of range
    cmp rax, rcx
    je .p_err
    jmp .p_done
.p_frac_zero:
    test r14, r14
    jle .p_err                          ; 0 ** 0 and 0 ** negative
    xor eax, eax
    jmp .p_done
.p_err:
    xor eax, eax
    mov edx, 1
.p_ret:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ARG0 = buffer, ARG1 = number of digits, ARG2 = value
; Writes |value| as that many digits, zero padded on the left (the low-order
; digits when it has more).
runtime_num_to_digits:
    mov r10, ARG0
    mov rcx, ARG1
    mov rax, ARG2
    test rax, rax
    jns .t_loop
    neg rax
.t_loop:
    test rcx, rcx
    jz .t_done
    dec rcx
    xor edx, edx
    mov r8d, 10
    div r8
    add dl, '0'
    mov [r10 + rcx], dl
    jmp .t_loop
.t_done:
    ret

section .rodata
align 8
cobrt_pow10:
pow10:
    dq 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000
    dq 1000000000, 10000000000, 100000000000, 1000000000000
    dq 10000000000000, 100000000000000, 1000000000000000
    dq 10000000000000000, 100000000000000000, 1000000000000000000
