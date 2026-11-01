default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
copy_batch:
    pxor mm0, mm0
    emms
    ret
