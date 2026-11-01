default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
%include "copy/index.inc"
copy_batch:
    ; rdi = out
    vpxor ymm0, ymm0, ymm0
    int 3
    vzeroupper
    pop rbx
    ret
.scalar:
    xor eax, eax
    ret
