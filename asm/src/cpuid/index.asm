; The CPUID and XGETBV instructions, for run/src/main/cpu_check/. They are not SIMD, but inline asm is banned in C, so
; they live here. Arrays and not single values, per plans/CODING_STANDARD.md section 7. Prototypes:
; run/include/simd/cpuid/index.h. No YMM is used, and this must stay true: this runs before the CPU has been checked.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global cpuid_read_batch
align 32
cpuid_read_batch:
    ; rdi = leaves (uint32_t *), rsi = subleaves (uint32_t *), rdx = count, rcx = out (uint32_t *, 4 per query: eax,
    ; ebx, ecx, edx). No pointer needs more than uint32_t alignment.
    push    rbx                         ; cpuid writes it, and the ABI says it is ours to keep
    mov     r8, rdx
    mov     r9, rcx
    xor     r10d, r10d
.next:
    cmp     r10, r8
    jae     .done
    mov     eax, [rdi + r10 * 4]
    mov     ecx, [rsi + r10 * 4]
    cpuid
    mov     [r9], eax
    mov     [r9 + 4], ebx
    mov     [r9 + 8], ecx
    mov     [r9 + 12], edx
    add     r9, 16
    inc     r10
    jmp     .next
.done:
    pop     rbx
    ret

global cpuid_xgetbv_batch
align 32
cpuid_xgetbv_batch:
    ; rdi = registers (uint32_t *), rsi = count, rdx = out (uint64_t *, 1 per query). Faults unless CPUID says OSXSAVE.
    mov     r8, rdx
    xor     r9d, r9d
.next:
    cmp     r9, rsi
    jae     .done
    mov     ecx, [rdi + r9 * 4]
    xgetbv
    shl     rdx, 32
    or      rax, rdx
    mov     [r8 + r9 * 8], rax
    inc     r9
    jmp     .next
.done:
    ret
