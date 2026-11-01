default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
copy_batch:
    jmp .body
.b:
    ret
.a:
    jmp .b
.body:
    vpxor ymm0, ymm0, ymm0
    jmp .a
