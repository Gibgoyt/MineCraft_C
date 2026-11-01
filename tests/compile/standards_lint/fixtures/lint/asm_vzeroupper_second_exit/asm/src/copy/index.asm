default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
copy_batch:
    vpxor ymm0, ymm0, ymm0
    vzeroupper
    ret
.other:
    vpxor ymm1, ymm1, ymm1
    ret
