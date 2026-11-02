; Constants of asm/src/bitmask/index.asm. See its README.md.
%ifndef MC_ASM_BITMASK_INDEX_INC
%define MC_ASM_BITMASK_INDEX_INC

%include "conventions/index.inc"

%define BITMASK_WORD_BYTES 8
%define BITMASK_WORD_BITS 64
%define BITMASK_WORDS_PER_VECTOR 4

; How many vectors of per-byte counts may add up before they have to be widened. A byte counts at most 8 bits per vector, and
; 31 * 8 = 248 still fits in the byte that holds the sum.
%define BITMASK_POPCOUNT_ROUNDS 31

; The body of one word-wise kernel. rdi = dst, rsi = a, rdx = b, rcx = count (uint64_t *, uint64_t *, uint64_t *, uint64_t).
; %1 is the instruction, and %2 and %3 are the numbers of the registers it reads, where a is register 0 and b register 1.
%macro BITMASK_BINARY_BODY 3
.next:
    cmp     rcx, BITMASK_WORDS_PER_VECTOR
    jb      .tail
    vmovdqu ymm0, [rsi]
    vmovdqu ymm1, [rdx]
    %1      ymm0, ymm%2, ymm%3
    vmovdqu [rdi], ymm0
    add     rsi, YMM_BYTES
    add     rdx, YMM_BYTES
    add     rdi, YMM_BYTES
    sub     rcx, BITMASK_WORDS_PER_VECTOR
    jmp     .next
.tail:
    test    rcx, rcx
    jz      .done
    vmovq   xmm0, [rsi]
    vmovq   xmm1, [rdx]
    %1      xmm0, xmm%2, xmm%3
    vmovq   [rdi], xmm0
    add     rsi, BITMASK_WORD_BYTES
    add     rdx, BITMASK_WORD_BYTES
    add     rdi, BITMASK_WORD_BYTES
    dec     rcx
    jmp     .tail
.done:
%endmacro

%endif
