; Batch sum in the form plans/CODING_STANDARD.md section 7 requires.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global mc_hello_simd_sum_batch
align 32
mc_hello_simd_sum_batch:
    ; rdi = values (uint32_t *, need not be aligned), rsi = count. Returns the 64-bit sum in rax.
    vpxor   ymm0, ymm0, ymm0
    xor     ecx, ecx
    mov     rdx, rsi
    and     rdx, -4                     ; elements the wide loop can take
.wide:
    cmp     rcx, rdx
    jae     .wide_done
    vpmovzxdq ymm1, [rdi + rcx * 4]     ; four uint32_t into four uint64_t, so the sum cannot overflow
    vpaddq  ymm0, ymm0, ymm1
    add     rcx, 4
    jmp     .wide
.wide_done:
    vextracti128 xmm1, ymm0, 1
    vpaddq  xmm0, xmm0, xmm1
    vpsrldq xmm1, xmm0, 8
    vpaddq  xmm0, xmm0, xmm1
    vmovq   rax, xmm0
.tail:
    cmp     rcx, rsi
    jae     .done
    mov     edx, [rdi + rcx * 4]
    add     rax, rdx
    inc     rcx
    jmp     .tail
.done:
    vzeroupper
    ret
