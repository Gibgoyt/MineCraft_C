default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
copy_batch:
    jmp .body
.exit:
    ret
.body:
    vpxor ymm0, ymm0, ymm0
    jmp .exit
