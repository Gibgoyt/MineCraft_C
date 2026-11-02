; What every SIMD kernel shares: plans/CODING_STANDARD.md section 7. The rest of the required form is written out in each
; file on purpose (default rel, .note.GNU-stack, global, align, the label, vzeroupper before ret), because standards_lint
; reads those lines from the source and a macro would hide them from it.
;
; Arguments arrive in rdi, rsi, rdx, rcx, r8, r9, and integer results leave in rax. A kernel may use rax, rcx, rdx, rsi,
; rdi, r8-r11 and every YMM register freely, and must save rbx, rbp and r12-r15 if it touches them. No pointer needs more
; alignment than its element unless the kernel's prototype says otherwise.
;
; A kernel's constant tables go in .text, after its code. From .rodata, each reference is a section-crossing relocation, and
; the build's -w+all -Werror refuses those. In .text no relocation is needed, and nothing ever writes them.
%ifndef MC_ASM_CONVENTIONS_INDEX_INC
%define MC_ASM_CONVENTIONS_INDEX_INC

%include "machine/index.inc"

%define YMM_BYTES 32
%define XMM_BYTES 16

; Stops the build if config/machine.yaml's cpu.simd turns off an instruction set this file uses, instead of building code
; that the CPU check at startup was told not to look for. The argument is the end of the MC_CPU_HAS_ name, such as AVX2.
%macro REQUIRE_CPU 1
    %if MC_CPU_HAS_%1 == 0
        %fatal this kernel needs %1, which cpu.simd in config/machine.yaml turns off
    %endif
%endmacro

%endif
