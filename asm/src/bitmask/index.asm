; Popcount and mask kernels, for building and measuring the voxel bitmasks of a section. A bit array here is an array of
; uint64_t, with element i at bit i % 64 of word i / 64. Prototypes: run/include/simd/bitmask/index.h.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

%include "bitmask/index.inc"

REQUIRE_CPU AVX2
REQUIRE_CPU POPCNT
REQUIRE_CPU BMI2

section .text
global bitmask_popcount_batch
align 32
bitmask_popcount_batch:
    ; rdi = words (uint64_t *), rsi = count. Returns in rax the set bits in all of them.
    vmovdqa ymm7, [bitmask_nibble_counts]
    vpbroadcastd ymm6, [bitmask_dword_low_nibbles]
    vpxor   ymm5, ymm5, ymm5
    vpxor   ymm4, ymm4, ymm4            ; the total so far, in four 64-bit parts
.batch:
    cmp     rsi, BITMASK_WORDS_PER_VECTOR
    jb      .sum
    mov     rcx, rsi
    shr     rcx, 2
    mov     r8d, BITMASK_POPCOUNT_ROUNDS
    cmp     rcx, r8
    cmova   rcx, r8
    lea     r8, [rcx * BITMASK_WORDS_PER_VECTOR]
    sub     rsi, r8
    vpxor   ymm3, ymm3, ymm3            ; this batch's count per byte
.round:
    vmovdqu ymm0, [rdi]
    vpand   ymm1, ymm0, ymm6
    vpsrlw  ymm0, ymm0, 4
    vpand   ymm0, ymm0, ymm6
    vpshufb ymm1, ymm7, ymm1
    vpshufb ymm0, ymm7, ymm0
    vpaddb  ymm3, ymm3, ymm1
    vpaddb  ymm3, ymm3, ymm0
    add     rdi, YMM_BYTES
    dec     rcx
    jnz     .round
    vpsadbw ymm3, ymm3, ymm5
    vpaddq  ymm4, ymm4, ymm3
    jmp     .batch
.sum:
    vextracti128 xmm0, ymm4, 1
    vpaddq  xmm0, xmm0, xmm4
    vmovq   rax, xmm0
    vpextrq rcx, xmm0, 1
    add     rax, rcx
.tail:
    test    rsi, rsi
    jz      .done
    xor     ecx, ecx                    ; popcnt has a false dependency on its output on this CPU
    popcnt  rcx, [rdi]
    add     rax, rcx
    add     rdi, BITMASK_WORD_BYTES
    dec     rsi
    jmp     .tail
.done:
    vzeroupper
    ret

global bitmask_and_batch
align 32
bitmask_and_batch:
    BITMASK_BINARY_BODY vpand, 0, 1
    vzeroupper
    ret

global bitmask_or_batch
align 32
bitmask_or_batch:
    BITMASK_BINARY_BODY vpor, 0, 1
    vzeroupper
    ret

global bitmask_andnot_batch
align 32
bitmask_andnot_batch:
    BITMASK_BINARY_BODY vpandn, 1, 0    ; vpandn inverts the first register it reads, which here is b
    vzeroupper
    ret

global bitmask_from_nonzero_u8_batch
align 32
bitmask_from_nonzero_u8_batch:
    ; rdi = values (uint8_t *), rsi = count, rdx = out (uint64_t *).
    vpxor   ymm5, ymm5, ymm5
.next:
    cmp     rsi, BITMASK_WORD_BITS
    jb      .tail
    vpcmpeqb ymm0, ymm5, [rdi]
    vpcmpeqb ymm1, ymm5, [rdi + YMM_BYTES]
    vpmovmskb eax, ymm0
    vpmovmskb ecx, ymm1
    shl     rcx, 32
    or      rax, rcx
    not     rax
    mov     [rdx], rax
    add     rdi, BITMASK_WORD_BITS
    add     rdx, BITMASK_WORD_BYTES
    sub     rsi, BITMASK_WORD_BITS
    jmp     .next
.tail:
    test    rsi, rsi
    jz      .done
    xor     eax, eax
    xor     ecx, ecx
.bit:
    cmp     byte [rdi + rcx], 0
    je      .bit_next
    bts     rax, rcx
.bit_next:
    inc     rcx
    cmp     rcx, rsi
    jb      .bit
    mov     [rdx], rax
.done:
    vzeroupper
    ret

global bitmask_from_nonzero_u16_batch
align 32
bitmask_from_nonzero_u16_batch:
    ; rdi = values (uint16_t *), rsi = count, rdx = out (uint64_t *). "Not equal to 0" is the shared code's other answer.
    mov     rcx, rdx
    xor     edx, edx
    mov     r9, -1
    jmp     bitmask_from_u16_shared

global bitmask_from_equal_u16_batch
align 32
bitmask_from_equal_u16_batch:
    ; rdi = values (uint16_t *), rsi = count, rdx = value (uint16_t), rcx = out (uint64_t *).
    xor     r9d, r9d
    jmp     bitmask_from_u16_shared

align 32
bitmask_from_u16_shared:
    ; rdi = values, rsi = count, dx = the value to look for, rcx = out, r9 = what to xor each result word with.
    vmovd   xmm5, edx
    vpbroadcastw ymm5, xmm5
.next:
    cmp     rsi, BITMASK_WORD_BITS
    jb      .tail
    vpcmpeqw ymm0, ymm5, [rdi]
    vpcmpeqw ymm1, ymm5, [rdi + YMM_BYTES]
    vpcmpeqw ymm2, ymm5, [rdi + 2 * YMM_BYTES]
    vpcmpeqw ymm3, ymm5, [rdi + 3 * YMM_BYTES]
    vpacksswb ymm0, ymm0, ymm1          ; packs within each 128-bit half, which the vpermq puts back in order
    vpacksswb ymm2, ymm2, ymm3
    vpermq  ymm0, ymm0, 0xD8
    vpermq  ymm2, ymm2, 0xD8
    vpmovmskb eax, ymm0
    vpmovmskb r8d, ymm2
    shl     r8, 32
    or      rax, r8
    xor     rax, r9
    mov     [rcx], rax
    add     rdi, 4 * YMM_BYTES
    add     rcx, BITMASK_WORD_BYTES
    sub     rsi, BITMASK_WORD_BITS
    jmp     .next
.tail:
    test    rsi, rsi
    jz      .done
    xor     eax, eax
    xor     r8d, r8d
.bit:
    cmp     [rdi + r8 * 2], dx
    jne     .bit_next
    bts     rax, r8
.bit_next:
    inc     r8
    cmp     r8, rsi
    jb      .bit
    xor     rax, r9
    bzhi    rax, rax, rsi               ; the xor set the bits past count too, and those must stay clear
    mov     [rcx], rax
.done:
    vzeroupper
    ret

; Constants, in .text so that reaching them takes no relocation (see asm/include/conventions/).
align 32
bitmask_nibble_counts:
    ; How many bits are set in each value of a nibble, which vpshufb looks up 32 at a time.
    times 2 db 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
bitmask_dword_low_nibbles:
    dd      0x0F0F0F0F
