default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
copy_batch:
    jmp .body
.exit:
    ret
.body:
    vpxor ymm0, ymm0, ymm0
    vzeroupper
    jmp .exit

copy_loop:
.next:
    vpxor ymm0, ymm0, ymm0
    dec rdx
    jnz .next
    vzeroupper
    ret

copy_scalar:
    jmp .exit
.exit:
    ret
