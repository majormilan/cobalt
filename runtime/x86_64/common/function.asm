; runtime/x86_64/common/function.asm
; Intrinsic functions (runtime/ABI.md section 13).
;
; runtime_function(ARG0 = arguments, ARG1 = their number, ARG2 = function,
; ARG3 = scale of a numeric result, ARG4 = result, ARG5 = result length)
; An argument is 3 qwords: a numeric one its scale, its value (an integer:
; the number times 10^scale) and 0; an alphanumeric one -1, its address and
; its length. A numeric result is stored at ARG4 as an integer at the given
; scale (bit 8 of ARG2: DECIMAL-POINT IS COMMA, for NUMVAL); an
; alphanumeric one fills the ARG5 bytes at ARG4 (padded with spaces).
; Transcendental functions are computed with the x87 FPU (64-bit mantissa),
; the others with integers.

%include "runtime.inc"

%define F_ACOS          0
%define F_ANNUITY       1
%define F_ASIN          2
%define F_ATAN          3
%define F_CHAR          4
%define F_COS           5
%define F_CURRENT_DATE  6
%define F_DATE_OF_INT   7
%define F_DAY_OF_INT    8
%define F_FACTORIAL     9
%define F_INTEGER       10
%define F_INT_OF_DATE   11
%define F_INT_OF_DAY    12
%define F_INTEGER_PART  13
%define F_LOG           15
%define F_LOG10         16
%define F_LOWER_CASE    17
%define F_MAX           18
%define F_MEAN          19
%define F_MEDIAN        20
%define F_MIDRANGE      21
%define F_MIN           22
%define F_MOD           23
%define F_NUMVAL        24
%define F_NUMVAL_C      25
%define F_ORD           26
%define F_ORD_MAX       27
%define F_ORD_MIN       28
%define F_PRESENT_VALUE 29
%define F_RANDOM        30
%define F_RANGE         31
%define F_REM           32
%define F_REVERSE       33
%define F_SIN           34
%define F_SQRT          35
%define F_STD_DEV       36
%define F_SUM           37
%define F_TAN           38
%define F_UPPER_CASE    39
%define F_VARIANCE      40

%define ARG_SIZE        24

section .text

global runtime_function
extern cobrt_collation
extern cobrt_os_time

; Frame (rbp-based): the arguments, their number, the function, the result
; scale, the result, its length, a scratch qword
%define FA      [rbp - 8]
%define FN      [rbp - 16]
%define FID     [rbp - 24]
%define FSCALE  [rbp - 32]
%define FRES    [rbp - 40]
%define FRLEN   [rbp - 48]
%define FTMP    [rbp - 56]
%define FTMP2   [rbp - 64]

runtime_function:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov FA, ARG0
    mov FN, ARG1
    mov FID, ARG2
    mov FSCALE, ARG3
    mov FRES, ARG4
    mov FRLEN, ARG5
    mov rax, ARG2
    and eax, 0xFF
    cmp eax, F_VARIANCE
    ja .done
    lea rcx, [rel dispatch]
    jmp [rcx + rax*8]
.done:
    lea rsp, [rbp - 104]                ; (MEDIAN moves rsp)
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    leave
    ret

; ── Helpers (rbp: the frame) ────────────────────────────────────────────────

; Address of argument ARG0 in rax
arg_addr:
    mov rax, ARG0
    imul rax, rax, ARG_SIZE
    add rax, FA
    ret

; Push numeric argument ARG0 onto the FPU stack
fld_arg:
    call arg_addr
    fild qword [rax + 8]
    mov rcx, [rax]
    lea rdx, [rel pow10]
    fild qword [rdx + rcx*8]
    fdivp
    ret

; Numeric argument ARG0 as an integer at scale ARG1 (truncated) in rax
arg_at_scale:
    push rbx
    mov rbx, ARG1
    call arg_addr
    mov rcx, [rax]                      ; its scale
    mov rax, [rax + 8]
    lea rdx, [rel pow10]
    cmp rcx, rbx
    je .done
    jb .up
    sub rcx, rbx
    mov r8, [rdx + rcx*8]
    cqo
    idiv r8
    jmp .done
.up:
    sub rbx, rcx
    imul rax, [rdx + rbx*8]
.done:
    pop rbx
    ret

; Store the FPU top (popped) as the result at the result scale (rounded)
fstore_result:
    mov rcx, FSCALE
    lea rdx, [rel pow10]
    fild qword [rdx + rcx*8]
    fmulp
    mov rax, FRES
    fistp qword [rax]
    ret

; Store rax (an integer at scale ARG0) as the result at the result scale
istore_result:
    mov rcx, FSCALE
    lea rdx, [rel pow10]
    cmp rcx, ARG0
    je .store
    jb .down
    sub rcx, ARG0
    imul rax, [rdx + rcx*8]
    jmp .store
.down:
    mov r8, ARG0
    sub r8, rcx
    mov r8, [rdx + r8*8]
    cqo
    idiv r8
.store:
    mov rcx, FRES
    mov [rcx], rax
    ret

; st0 = st0 ^ st1 (st0 > 0), popping one: 2^(st1 * log2(st0))
fpow:
    fyl2x                               ; y * log2(x)
    fld st0
    frndint
    fstp qword FTMP2                    ; n
    fld qword FTMP2
    fsubp                               ; fraction
    f2xm1
    fld1
    faddp                               ; 2^fraction
    fld qword FTMP2
    fxch st1
    fscale                              ; 2^fraction * 2^n
    fstp st1
    ret

; ── Functions ───────────────────────────────────────────────────────────────

f_sqrt:
    xor ARG0, ARG0
    call fld_arg
    fsqrt
    jmp store_float
f_log:
    fldln2
    jmp f_log_common
f_log10:
    fldlg2
f_log_common:
    xor ARG0, ARG0
    call fld_arg
    fyl2x
    jmp store_float
f_sin:
    xor ARG0, ARG0
    call fld_arg
    fsin
    jmp store_float
f_cos:
    xor ARG0, ARG0
    call fld_arg
    fcos
    jmp store_float
f_tan:
    xor ARG0, ARG0
    call fld_arg
    fptan
    fstp st0                            ; the 1.0 fptan pushes
    jmp store_float
f_atan:
    xor ARG0, ARG0
    call fld_arg
    fld1
    fpatan
    jmp store_float
f_asin:
f_acos:
    xor ARG0, ARG0
    call fld_arg                        ; x
    xor ARG0, ARG0
    call fld_arg                        ; x, x
    fld st0
    fmulp                               ; x * x, x
    fld1
    fsubrp                              ; 1 - x * x, x
    fsqrt                               ; s, x
    mov rax, FID
    and eax, 0xFF
    cmp eax, F_ACOS
    jne .asin
    fxch st1                            ; x, s: atan(s / x)
.asin:
    fpatan                              ; asin: atan(x / s)
    jmp store_float

; ANNUITY(r, n): r / (1 - (1 + r) ^ -n), or 1 / n when r is 0
f_annuity:
    xor ARG0, ARG0
    call arg_addr
    cmp qword [rax + 8], 0
    jne .rate
    fld1
    mov ARG0, 1
    call fld_arg
    fdivp
    jmp store_float
.rate:
    mov ARG0, 1
    call fld_arg
    fchs                                ; -n
    xor ARG0, ARG0
    call fld_arg
    fld1
    faddp                               ; 1 + r, -n
    call fpow                           ; (1 + r) ^ -n
    fld1
    fsubrp                              ; 1 - that
    xor ARG0, ARG0
    call fld_arg
    fdivrp                              ; r / (1 - ...)
    jmp store_float

; PRESENT-VALUE(r, a1, ...): the sum of ak / (1 + r) ^ k
f_present_value:
    fldz                                ; sum
    mov r12d, 1
.term:
    cmp r12, FN
    jae store_float
    mov FTMP, r12
    fild qword FTMP                     ; k
    fchs
    xor ARG0, ARG0
    call fld_arg
    fld1
    faddp
    call fpow                           ; (1 + r) ^ -k, sum
    mov ARG0, r12
    call fld_arg
    fmulp
    faddp
    inc r12
    jmp .term

store_float:
    call fstore_result
    jmp runtime_function.done

; MEAN, VARIANCE, STANDARD-DEVIATION
f_mean:
    call fld_mean
    jmp store_float
f_variance:
f_std_dev:
    call fld_mean
    fstp qword FTMP2                    ; the mean
    fldz
    xor r12d, r12d
.sum:
    cmp r12, FN
    jae .avg
    mov ARG0, r12
    call fld_arg
    fld qword FTMP2
    fsubp
    fld st0
    fmulp
    faddp
    inc r12
    jmp .sum
.avg:
    mov rax, FN
    mov FTMP, rax
    fild qword FTMP
    fdivp
    mov rax, FID
    and eax, 0xFF
    cmp eax, F_STD_DEV
    jne store_float
    fsqrt
    jmp store_float

; The mean of the arguments onto the FPU stack
fld_mean:
    push r12
    sub rsp, 8
    fldz
    xor r12d, r12d
.add:
    cmp r12, FN
    jae .div
    mov ARG0, r12
    call fld_arg
    faddp
    inc r12
    jmp .add
.div:
    mov rax, FN
    mov FTMP, rax
    fild qword FTMP
    fdivp
    add rsp, 8
    pop r12
    ret

; MAX, MIN, ORD-MAX, ORD-MIN, RANGE, MIDRANGE, SUM: integers at the result
; scale (alphanumeric arguments: MAX and MIN of the strings)
f_max:
f_min:
f_ord_max:
f_ord_min:
f_range:
f_midrange:
f_sum:
    xor ARG0, ARG0
    call arg_addr
    cmp qword [rax], -1
    je f_alnum_max_min
    mov r15, FSCALE                     ; the scale of the comparison
    mov rax, FID
    and eax, 0xFF
    cmp eax, F_ORD_MAX
    je .ord
    cmp eax, F_ORD_MIN
    jne .values
.ord:
    ; positions: compare at the largest argument scale (18 at most)
    xor r15d, r15d
    xor ecx, ecx
.scale:
    cmp rcx, FN
    jae .values
    mov rax, rcx
    imul rax, rax, ARG_SIZE
    add rax, FA
    cmp [rax], r15
    jbe .next_scale
    mov r15, [rax]
.next_scale:
    inc rcx
    jmp .scale
.values:
    xor ARG0, ARG0
    mov ARG1, r15
    call arg_at_scale
    mov r12, rax                        ; max
    mov r13, rax                        ; min
    mov r14, rax                        ; sum
    mov qword FTMP, 1                   ; position of max
    mov qword FTMP2, 1                  ; position of min
    mov ebx, 1
.each:
    cmp rbx, FN
    jae .result
    mov ARG0, rbx
    mov ARG1, r15
    call arg_at_scale
    add r14, rax
    cmp rax, r12
    jle .not_max
    mov r12, rax
    lea rcx, [rbx + 1]
    mov FTMP, rcx
.not_max:
    cmp rax, r13
    jge .not_min
    mov r13, rax
    lea rcx, [rbx + 1]
    mov FTMP2, rcx
.not_min:
    inc rbx
    jmp .each
.result:
    mov rcx, FID
    and ecx, 0xFF
    mov rax, r12
    cmp ecx, F_MAX
    je .store
    mov rax, r13
    cmp ecx, F_MIN
    je .store
    mov rax, r14
    cmp ecx, F_SUM
    je .store
    mov rax, r12
    sub rax, r13
    cmp ecx, F_RANGE
    je .store
    mov rax, r12
    add rax, r13
    cqo
    mov rcx, 2
    idiv rcx
    mov rcx, FID
    and ecx, 0xFF
    cmp ecx, F_MIDRANGE
    je .store
    ; ORD-MAX / ORD-MIN: integer positions
    mov rax, FTMP
    cmp ecx, F_ORD_MAX
    je .position
    mov rax, FTMP2
.position:
    xor ARG0, ARG0
    call istore_result
    jmp runtime_function.done
.store:
    mov ARG0, r15
    call istore_result
    jmp runtime_function.done

; MAX / MIN of alphanumeric arguments: the result is the greatest / least
; (in the program collating sequence), padded with spaces
f_alnum_max_min:
    xor r12d, r12d                      ; the best argument
    mov ebx, 1
.each:
    cmp rbx, FN
    jae .copy
    mov ARG0, r12
    mov ARG1, rbx
    call compare_alnum_args             ; eax <0 / 0 / >0: arg r12 against arg rbx
    mov rcx, FID
    and ecx, 0xFF
    cmp ecx, F_MIN
    je .min
    cmp ecx, F_ORD_MIN
    je .min
    test eax, eax
    jge .next
    mov r12, rbx
    jmp .next
.min:
    test eax, eax
    jle .next
    mov r12, rbx
.next:
    inc rbx
    jmp .each
.copy:
    mov rcx, FID
    and ecx, 0xFF
    cmp ecx, F_ORD_MAX
    je .position
    cmp ecx, F_ORD_MIN
    jne .string
.position:
    lea rax, [r12 + 1]
    xor ARG0, ARG0
    call istore_result
    jmp runtime_function.done
.string:
    mov ARG0, r12
    call arg_addr
    mov rsi, [rax + 8]
    mov rcx, [rax + 16]
    jmp copy_result

; Compare alphanumeric arguments ARG0 and ARG1 (the shorter padded with
; spaces) in the program collating sequence: eax < 0, 0, > 0
compare_alnum_args:
    push r12
    push r13
    push r14
    call arg_addr
    mov r12, rax
    mov ARG0, ARG1
    call arg_addr
    mov r13, rax
    xor r14d, r14d                      ; position
.loop:
    mov eax, ' '
    cmp r14, [r12 + 16]
    jae .a_pad
    mov rcx, [r12 + 8]
    movzx eax, byte [rcx + r14]
.a_pad:
    mov edx, ' '
    cmp r14, [r13 + 16]
    jae .b_pad
    mov rcx, [r13 + 8]
    movzx edx, byte [rcx + r14]
.b_pad:
    mov rcx, [r12 + 16]
    cmp rcx, [r13 + 16]
    jae .longer
    mov rcx, [r13 + 16]
.longer:
    cmp r14, rcx
    jae .equal
    mov r8, [rel cobrt_collation]
    test r8, r8
    jz .native
    movzx eax, byte [r8 + rax]
    movzx edx, byte [r8 + rdx]
.native:
    sub eax, edx
    jnz .done
    inc r14
    jmp .loop
.equal:
    xor eax, eax
.done:
    pop r14
    pop r13
    pop r12
    ret

; MEDIAN: the middle value (the mean of the two middle ones), at the
; result scale; the values are sorted in a buffer on the stack
f_median:
    mov rax, FN
    lea rax, [rax*8 + 15]
    and rax, -16
    sub rsp, rax
    mov r14, rsp                        ; values
    xor ebx, ebx
.load:
    cmp rbx, FN
    jae .sort
    mov ARG0, rbx
    mov ARG1, FSCALE
    call arg_at_scale
    mov [r14 + rbx*8], rax
    inc rbx
    jmp .load
.sort:
    ; insertion sort
    mov ebx, 1
.outer:
    cmp rbx, FN
    jae .sorted
    mov rax, [r14 + rbx*8]
    mov rcx, rbx
.inner:
    test rcx, rcx
    jz .place
    mov rdx, [r14 + rcx*8 - 8]
    cmp rdx, rax
    jle .place
    mov [r14 + rcx*8], rdx
    dec rcx
    jmp .inner
.place:
    mov [r14 + rcx*8], rax
    inc rbx
    jmp .outer
.sorted:
    mov rcx, FN
    mov rax, rcx
    shr rax, 1
    test rcx, 1
    jnz .odd
    mov rdx, [r14 + rax*8 - 8]
    add rdx, [r14 + rax*8]
    mov rax, rdx
    cqo
    mov rcx, 2
    idiv rcx
    jmp .store
.odd:
    mov rax, [r14 + rax*8]
.store:
    mov ARG0, FSCALE
    call istore_result
    jmp runtime_function.done

; INTEGER: the greatest integer not above the argument; INTEGER-PART: its
; integer part
f_integer:
f_integer_part:
    xor ARG0, ARG0
    call arg_addr
    mov rcx, [rax]
    mov rax, [rax + 8]
    lea rdx, [rel pow10]
    mov r8, [rdx + rcx*8]
    cqo
    idiv r8
    mov rcx, FID
    and ecx, 0xFF
    cmp ecx, F_INTEGER
    jne .store
    test rdx, rdx
    jns .store
    jz .store
    dec rax                             ; negative with a fraction: below
.store:
    xor ARG0, ARG0
    call istore_result
    jmp runtime_function.done

; MOD(a, b): a - b * INTEGER(a / b), of integers
f_mod:
    xor ARG0, ARG0
    xor ARG1, ARG1
    call arg_at_scale
    mov r12, rax
    mov ARG0, 1
    xor ARG1, ARG1
    call arg_at_scale
    mov r13, rax
    xor eax, eax
    test r13, r13
    jz .store
    mov rax, r12
    cqo
    idiv r13
    mov rax, rdx                        ; the remainder takes the sign of b
    test rdx, rdx
    jz .store
    mov rcx, rdx
    xor rcx, r13
    jns .store
    add rax, r13
.store:
    xor ARG0, ARG0
    call istore_result
    jmp runtime_function.done

; REM(a, b): a - b * INTEGER-PART(a / b), at the result scale
f_rem:
    xor ARG0, ARG0
    mov ARG1, FSCALE
    call arg_at_scale
    mov r12, rax
    mov ARG0, 1
    mov ARG1, FSCALE
    call arg_at_scale
    mov r13, rax
    xor edx, edx
    test r13, r13
    jz .store
    mov rax, r12
    cqo
    idiv r13
.store:
    mov rax, rdx
    mov ARG0, FSCALE
    call istore_result
    jmp runtime_function.done

; FACTORIAL(n), n from 0 to 20
f_factorial:
    xor ARG0, ARG0
    xor ARG1, ARG1
    call arg_at_scale
    mov rcx, rax
    mov eax, 1
.mul:
    cmp rcx, 1
    jle .store
    imul rax, rcx
    dec rcx
    jmp .mul
.store:
    xor ARG0, ARG0
    call istore_result
    jmp runtime_function.done

; INTEGER-OF-DATE(yyyymmdd), INTEGER-OF-DAY(yyyyddd): days from
; 1600-12-31 (1601-01-01 is day 1)
f_int_of_date:
    xor ARG0, ARG0
    xor ARG1, ARG1
    call arg_at_scale
    mov rcx, 10000
    cqo
    idiv rcx                            ; rax = year, rdx = mmdd
    mov r12, rax
    mov rax, rdx
    xor edx, edx
    mov rcx, 100
    div rcx                             ; rax = month, rdx = day
    mov r13, rdx
    mov r14, rax
    ; day of the year: days of the months before, + 1 in a leap year after February
    lea rcx, [rel month_days]
    mov rax, [rcx + r14*8 - 8]
    add r13, rax
    cmp r14, 2
    jbe .days
    mov ARG0, r12
    call is_leap
    add r13, rax
.days:
    mov rax, r12
    mov rcx, r13
    call days_before_year
    add rax, r13
    jmp store_int0
f_int_of_day:
    xor ARG0, ARG0
    xor ARG1, ARG1
    call arg_at_scale
    mov rcx, 1000
    cqo
    idiv rcx                            ; rax = year, rdx = day of the year
    mov r13, rdx
    call days_before_year
    add rax, r13
    jmp store_int0

; Days of the years from 1601 to rax - 1 in rax
days_before_year:
    lea rcx, [rax - 1601]
    mov rax, rcx
    imul rax, rax, 365
    mov r8, rcx
    shr r8, 2
    add rax, r8                         ; + every 4th
    push rax
    mov rax, rcx
    xor edx, edx
    mov r8, 100
    div r8
    mov r9, rax
    pop rax
    sub rax, r9                         ; - every 100th
    push rax
    mov rax, rcx
    xor edx, edx
    mov r8, 400
    div r8
    mov r9, rax
    pop rax
    add rax, r9                         ; + every 400th
    ret

; 1 if year ARG0 is a leap year, else 0 (in rax)
is_leap:
    mov rax, ARG0
    xor edx, edx
    mov rcx, 400
    div rcx
    test rdx, rdx
    jz .yes
    mov rax, ARG0
    xor edx, edx
    mov rcx, 100
    div rcx
    test rdx, rdx
    jz .no
    test ARG0, 3
    jz .yes
.no:
    xor eax, eax
    ret
.yes:
    mov eax, 1
    ret

; DATE-OF-INTEGER(n): yyyymmdd; DAY-OF-INTEGER(n): yyyyddd
f_date_of_int:
f_day_of_int:
    xor ARG0, ARG0
    xor ARG1, ARG1
    call arg_at_scale
    mov rcx, FID
    and ecx, 0xFF
    xor ARG1, ARG1
    cmp ecx, F_DAY_OF_INT
    jne .date
    mov ARG1, 1
.date:
    mov ARG0, rax
    call days_to_date
    jmp store_int0

; Day ARG0 (1601-01-01 is day 1) as yyyymmdd (ARG1 = 0) or yyyyddd (ARG1 =
; 1) in rax
days_to_date:
    push r12
    push r13
    push r14
    push r15
    push rbx
    mov r13, ARG0                       ; days left
    mov rbx, ARG1
    mov r12d, 1601                      ; year
.year:
    mov ARG0, r12
    call is_leap
    lea rcx, [rax + 365]
    cmp r13, rcx
    jle .in_year
    sub r13, rcx
    inc r12
    jmp .year
.in_year:
    test rbx, rbx
    jz .month
    mov rax, r12
    imul rax, rax, 1000
    add rax, r13
    jmp .done
.month:
    mov ARG0, r12
    call is_leap
    mov r15, rax
    mov r14d, 12
.find:
    lea rcx, [rel month_days]
    mov rax, [rcx + r14*8 - 8]
    cmp r14, 2
    jbe .no_leap
    add rax, r15
.no_leap:
    cmp r13, rax
    jg .found
    dec r14
    jmp .find
.found:
    sub r13, rax                        ; the day of the month
    mov rax, r12
    imul rax, rax, 10000
    imul rcx, r14, 100
    add rax, rcx
    add rax, r13
.done:
    pop rbx
    pop r15
    pop r14
    pop r13
    pop r12
    ret

store_int0:
    xor ARG0, ARG0
    call istore_result
    jmp runtime_function.done

; ORD(c): the ordinal position of the character in the collating sequence
f_ord:
    xor ARG0, ARG0
    call arg_addr
    mov rax, [rax + 8]
    movzx eax, byte [rax]
    mov rcx, [rel cobrt_collation]
    test rcx, rcx
    jz .native
    movzx eax, byte [rcx + rax]
.native:
    inc eax
    jmp store_int0

; CHAR(n): the character at ordinal position n
f_char:
    xor ARG0, ARG0
    xor ARG1, ARG1
    call arg_at_scale
    dec rax
    and eax, 0xFF
    mov rcx, [rel cobrt_collation]
    test rcx, rcx
    jz .put
    ; the character whose weight is n - 1
    xor edx, edx
.find:
    cmp byte [rcx + rdx], al
    je .found
    inc edx
    cmp edx, 256
    jb .find
    xor edx, edx
.found:
    mov eax, edx
.put:
    mov FTMP, rax
    lea rsi, FTMP
    mov ecx, 1
    jmp copy_result

; RANDOM [(seed)]: the next of a linear congruential sequence, 0 <= r < 1
f_random:
    cmp qword FN, 0
    je .next
    xor ARG0, ARG0
    xor ARG1, ARG1
    call arg_at_scale
    mov rcx, 0x9E3779B97F4A7C15
    xor rax, rcx
    mov [rel random_state], rax
    mov byte [rel random_seeded], 1
.next:
    cmp byte [rel random_seeded], 0
    jne .step
    mov rax, 0x2545F4914F6CDD1D
    mov [rel random_state], rax
    mov byte [rel random_seeded], 1
.step:
    mov rax, [rel random_state]
    mov rcx, 6364136223846793005
    imul rax, rcx
    mov rcx, 1442695040888963407
    add rax, rcx
    mov [rel random_state], rax
    shr rax, 11                         ; 53 bits
    mov FTMP, rax
    fild qword FTMP
    mov rax, 0x20000000000000           ; 2^53
    mov FTMP, rax
    fild qword FTMP
    fdivp
    ; at the result scale, truncated: below 1 always
    mov rcx, FSCALE
    lea rdx, [rel pow10]
    fild qword [rdx + rcx*8]
    fmulp
    mov rax, FRES
    fisttp qword [rax]
    jmp runtime_function.done

; UPPER-CASE, LOWER-CASE, REVERSE of an alphanumeric argument
f_upper_case:
f_lower_case:
f_reverse:
    xor ARG0, ARG0
    call arg_addr
    mov rsi, [rax + 8]
    mov rcx, [rax + 16]
    mov rdi, FRES
    mov rdx, FRLEN
    cmp rcx, rdx
    jbe .fits
    mov rcx, rdx
.fits:
    mov r8, FID
    and r8d, 0xFF
    xor r9d, r9d
.each:
    cmp r9, rcx
    jae .pad
    cmp r8d, F_REVERSE
    jne .case
    mov r10, rcx
    sub r10, r9
    movzx eax, byte [rsi + r10 - 1]
    jmp .put
.case:
    movzx eax, byte [rsi + r9]
    cmp r8d, F_UPPER_CASE
    jne .lower
    lea r10d, [rax - 'a']
    cmp r10d, 25
    ja .put
    sub eax, 32
    jmp .put
.lower:
    lea r10d, [rax - 'A']
    cmp r10d, 25
    ja .put
    add eax, 32
.put:
    mov [rdi + r9], al
    inc r9
    jmp .each
.pad:
    cmp r9, rdx
    jae runtime_function.done
    mov byte [rdi + r9], ' '
    inc r9
    jmp .pad

; Copy rcx bytes from rsi to the result, padded with spaces
copy_result:
    mov rdi, FRES
    mov rdx, FRLEN
    cmp rcx, rdx
    jbe .fits
    mov rcx, rdx
.fits:
    sub rdx, rcx
    rep movsb
    mov rcx, rdx
    mov al, ' '
    rep stosb
    jmp runtime_function.done

; CURRENT-DATE: YYYYMMDDHHMMSShh, then 00000 (the offset from UTC is not
; known)
f_current_date:
    sub rsp, 16
    mov ARG0, rsp
    call cobrt_os_time
    mov rax, [rsp]                      ; seconds since 1970-01-01
    mov r15, [rsp + 8]                  ; nanoseconds
    add rsp, 16
    xor edx, edx
    mov rcx, 86400
    div rcx
    mov r14, rdx                        ; seconds of the day
    add rax, 134775                     ; the day of 1970-01-01
    mov ARG0, rax
    xor ARG1, ARG1
    call days_to_date
    mov r13, FRES
    mov rdi, r13
    mov ecx, 8
    call put_digits
    mov rax, r14                        ; hhmmss
    xor edx, edx
    mov rcx, 3600
    div rcx
    imul r8, rax, 10000
    mov rax, rdx
    xor edx, edx
    mov rcx, 60
    div rcx
    imul rax, rax, 100
    add rax, rdx
    add rax, r8
    lea rdi, [r13 + 8]
    mov ecx, 6
    call put_digits
    mov rax, r15                        ; hundredths
    xor edx, edx
    mov rcx, 10000000
    div rcx
    lea rdi, [r13 + 14]
    mov ecx, 2
    call put_digits
    mov dword [r13 + 16], 0x30303030    ; offset from UTC not known: 00000
    mov byte [r13 + 20], '0'
    jmp runtime_function.done

; rax as ecx decimal digits at rdi
put_digits:
    mov r8, rcx
.loop:
    test r8, r8
    jz .done
    xor edx, edx
    mov r9, 10
    div r9
    add dl, '0'
    mov [rdi + r8 - 1], dl
    dec r8
    jmp .loop
.done:
    ret

; NUMVAL(s), NUMVAL-C(s [, currency]): the number a string shows:
; [spaces] [+ | -] [spaces] digits [. digits] [spaces] [+ | - | CR | DB]
; [spaces], NUMVAL-C also with the currency sign ($, or the second
; argument) after the leading sign
f_numval:
f_numval_c:
    xor ARG0, ARG0
    call arg_addr
    mov r12, [rax + 8]                  ; string
    mov r13, [rax + 16]                 ; length
    xor r14d, r14d                      ; value
    xor r15d, r15d                      ; negative
    xor ebx, ebx                        ; position
    mov qword FTMP, -1                  ; digits after the point (-1: none yet)
    mov rax, FID
    mov edx, '.'
    test eax, 0x100
    jz .point
    mov edx, ','
.point:
    mov FTMP2, rdx
.scan:
    cmp rbx, r13
    jae .end
    movzx eax, byte [r12 + rbx]
    inc rbx
    cmp al, ' '
    je .scan
    cmp al, '-'
    je .minus
    cmp al, '+'
    je .scan
    cmp al, '$'
    je .scan
    cmp eax, FTMP2
    je .dot
    lea ecx, [rax - '0']
    cmp ecx, 9
    ja .letters
    cmp qword FTMP, -1
    je .digit
    mov rcx, FTMP
    cmp rcx, FSCALE
    jae .scan                           ; beyond the result scale: dropped
    inc qword FTMP
.digit:
    imul r14, r14, 10
    sub eax, '0'
    add r14, rax
    jmp .scan
.dot:
    mov qword FTMP, 0
    jmp .scan
.minus:
    mov r15d, 1
    jmp .scan
.letters:
    or al, 0x20
    cmp al, 'c'                         ; CR
    je .credit
    cmp al, 'd'                         ; DB
    je .credit
    jmp .scan                           ; other characters (a currency string)
.credit:
    mov r15d, 1
    inc rbx
    jmp .scan
.end:
    mov rcx, FTMP
    cmp rcx, -1
    jne .have
    xor ecx, ecx
.have:
    test r15d, r15d
    jz .positive
    neg r14
.positive:
    mov rax, r14
    mov ARG0, rcx
    call istore_result
    jmp runtime_function.done

section .rodata
align 8
dispatch:
    dq f_acos, f_annuity, f_asin, f_atan, f_char, f_cos, f_current_date, f_date_of_int
    dq f_day_of_int, f_factorial, f_integer, f_int_of_date, f_int_of_day, f_integer_part
    dq runtime_function.done, f_log, f_log10, f_lower_case, f_max, f_mean, f_median
    dq f_midrange, f_min, f_mod, f_numval, f_numval_c, f_ord, f_ord_max, f_ord_min
    dq f_present_value, f_random, f_range, f_rem, f_reverse, f_sin, f_sqrt, f_std_dev
    dq f_sum, f_tan, f_upper_case, f_variance
pow10:
    dq 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000
    dq 10000000000, 100000000000, 1000000000000, 10000000000000, 100000000000000
    dq 1000000000000000, 10000000000000000, 100000000000000000, 1000000000000000000
; days of the year before the first of each month (not a leap year)
month_days:
    dq 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334

section .bss
random_state: resq 1
random_seeded: resb 1
