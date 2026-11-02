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

; ---- The early guard. Everything from here on stays inside the x86-64 baseline: no VEX, no BMI, no YMM. ----------------
;
; main and all the C code are compiled with -march=<cpu.arch_flag>, so on a CPU older than that flag they die of SIGILL
; before they can print anything. This runs from .preinit_array, before any of them. The bits are CPU_FEATURE_* in
; run/include/main/cpu_check/index.h, and the words of a raw record are what cpu_check_decode passes.

%include "machine/index.inc"

%define RAW_MAX_LEAF        0
%define RAW_MAX_EXTENDED    4
%define RAW_LEAF1_ECX       8
%define RAW_LEAF7_EBX       12
%define RAW_EXTENDED1_ECX   16
%define RAW_XCR0_LOW        20
%define RAW_BYTES           24

%define FEATURE_COUNT       10
%define EXIT_CPU_MISMATCH   3           ; MAIN_EXIT_CPU_MISMATCH
%define SYS_WRITE           1
%define SYS_EXIT_GROUP      231

; Sets feature bit %3 of eax if bit %2 of %1 is set.
%macro FEATURE_IF 3
    bt      %1, %2
    jnc     %%absent
    or      eax, 1 << %3
%%absent:
%endmacro

global cpuid_decode_batch
align 32
cpuid_decode_batch:
    ; rdi = raw (uint32_t *, 6 per record: max leaf, max extended leaf, leaf 1 ecx, leaf 7 ebx, extended leaf 1 ecx, low
    ; half of XCR0), rsi = count, rdx = out (uint32_t *, 1 per record). No pointer needs more than uint32_t alignment.
.next:
    test    rsi, rsi
    jz      .done
    mov     r8d, [rdi + RAW_LEAF1_ECX]
    mov     r9d, [rdi + RAW_LEAF7_EBX]
    mov     r10d, [rdi + RAW_EXTENDED1_ECX]
    mov     r11d, [rdi + RAW_XCR0_LOW]
    ; A leaf past the maximum returns another leaf's data, not an error.
    cmp     dword [rdi + RAW_MAX_LEAF], 1
    jae     .have_leaf1
    xor     r8d, r8d
.have_leaf1:
    cmp     dword [rdi + RAW_MAX_LEAF], 7
    jae     .have_leaf7
    xor     r9d, r9d
.have_leaf7:
    cmp     dword [rdi + RAW_MAX_EXTENDED], 0x80000001
    jae     .have_extended1
    xor     r10d, r10d
.have_extended1:
    xor     eax, eax
    FEATURE_IF r8d, 20, 0               ; sse42
    FEATURE_IF r8d, 23, 6               ; popcnt
    FEATURE_IF r9d, 3, 4                ; bmi1
    FEATURE_IF r9d, 8, 5                ; bmi2
    FEATURE_IF r10d, 5, 7               ; lzcnt
    ; XCR0 means nothing unless the OS turned XSAVE on, and AVX on an OS that does not save YMM is not usable.
    bt      r8d, 27
    jnc     .store
    not     r11d                        ; now a set bit is state the OS does not save
    test    r11d, 0x06
    jnz     .store
    FEATURE_IF r8d, 28, 1               ; avx
    FEATURE_IF r9d, 5, 2                ; avx2
    FEATURE_IF r8d, 12, 3               ; fma3
    FEATURE_IF r8d, 29, 8               ; f16c
    test    r11d, 0xE0
    jnz     .store
    FEATURE_IF r9d, 16, 9               ; avx512
.store:
    mov     [rdx], eax
    add     rdx, 4
    add     rdi, RAW_BYTES
    dec     rsi
    jmp     .next
.done:
    ret

global cpuid_guard_batch
align 32
cpuid_guard_batch:
    ; rdi = required (uint32_t *), rsi = count. Returns if this CPU has every feature in every mask. If not, writes why to
    ; stderr and ends the process with exit status 3, using nothing but system calls.
    push    rbx
    push    r12
    push    r13
    push    r14
    sub     rsp, 40
    xor     r12d, r12d
.gather:
    test    rsi, rsi
    jz      .read
    or      r12d, [rdi]
    add     rdi, 4
    dec     rsi
    jmp     .gather
.read:
    xor     eax, eax
    xor     ecx, ecx
    cpuid
    mov     [rsp + RAW_MAX_LEAF], eax
    mov     eax, 0x80000000
    xor     ecx, ecx
    cpuid
    mov     [rsp + RAW_MAX_EXTENDED], eax
    mov     eax, 1
    xor     ecx, ecx
    cpuid
    mov     [rsp + RAW_LEAF1_ECX], ecx
    mov     r13d, ecx
    mov     eax, 7
    xor     ecx, ecx
    cpuid
    mov     [rsp + RAW_LEAF7_EBX], ebx
    mov     eax, 0x80000001
    xor     ecx, ecx
    cpuid
    mov     [rsp + RAW_EXTENDED1_ECX], ecx
    mov     dword [rsp + RAW_XCR0_LOW], 0
    bt      r13d, 27                    ; XGETBV faults unless the OS turned XSAVE on
    jnc     .decode
    xor     ecx, ecx
    xgetbv
    mov     [rsp + RAW_XCR0_LOW], eax
.decode:
    mov     rdi, rsp
    mov     esi, 1
    lea     rdx, [rsp + RAW_BYTES]
    call    cpuid_decode_batch
    mov     eax, [rsp + RAW_BYTES]
    not     eax
    and     r12d, eax                   ; what is required and not present
    jnz     .refuse
    add     rsp, 40
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret
.refuse:
    mov     eax, SYS_WRITE
    mov     edi, 2
    lea     rsi, [guard_message_start]
    mov     edx, guard_message_start_bytes
    syscall
    xor     r13d, r13d
    lea     r14, [guard_names]
.name:
    bt      r12d, r13d
    jnc     .name_next
    mov     eax, SYS_WRITE
    mov     edi, 2
    lea     rsi, [r14 + r13 * 8 + 1]
    movzx   edx, byte [r14 + r13 * 8]
    syscall
.name_next:
    inc     r13d
    cmp     r13d, FEATURE_COUNT
    jb      .name
    mov     eax, SYS_WRITE
    mov     edi, 2
    lea     rsi, [guard_message_end]
    mov     edx, guard_message_end_bytes
    syscall
    mov     eax, SYS_EXIT_GROUP
    mov     edi, EXIT_CPU_MISMATCH
    syscall
    hlt                                 ; exit_group does not return

; The dynamic loader calls this before any constructor and before main.
global cpuid_guard_preinit
align 32
cpuid_guard_preinit:
    sub     rsp, 8
    lea     rdi, [cpuid_guard_required]
    mov     esi, 1
    call    cpuid_guard_batch
    add     rsp, 8
    ret

; Constants, in .text so that reaching them takes no relocation.
; config/machine.yaml's cpu.simd. ctest run_types holds this equal to what the C code says the build needs.
global cpuid_guard_required
align 4
cpuid_guard_required:
    dd      MC_CPU_HAS_SSE42 << 0 | MC_CPU_HAS_AVX << 1 | MC_CPU_HAS_AVX2 << 2 | MC_CPU_HAS_FMA3 << 3 | MC_CPU_HAS_BMI1 << 4 | MC_CPU_HAS_BMI2 << 5 | MC_CPU_HAS_POPCNT << 6 | MC_CPU_HAS_LZCNT << 7 | MC_CPU_HAS_F16C << 8 | MC_CPU_HAS_AVX512 << 9

guard_message_start:
    db      "[error] this CPU does not support what this build was compiled to use. Missing:"
guard_message_start_bytes equ $ - guard_message_start
guard_message_end:
    db      ". Build flags: -march=", MC_CPU_ARCH_FLAG, " and cpu.simd in config/machine.yaml. "
    db      "Change them to match this CPU and rebuild.", 10
guard_message_end_bytes equ $ - guard_message_end

; In bit order, 8 bytes each: the length, then the text, so that no entry needs a pointer and a relocation.
align 8
guard_names:
    db      6, " sse42 "
    db      4, " avx   "
    db      5, " avx2  "
    db      5, " fma3  "
    db      5, " bmi1  "
    db      5, " bmi2  "
    db      7, " popcnt"
    db      6, " lzcnt "
    db      5, " f16c  "
    db      7, " avx512"

; An absolute address is what an entry of this array is, and the only such relocation in this file: the linker turns it into
; a relative one in a position-independent program. So that one warning is off for this one line.
section .preinit_array progbits alloc noexec write align=8
[warning push]
[warning -reloc-abs-qword]
    dq      cpuid_guard_preinit
[warning pop]
