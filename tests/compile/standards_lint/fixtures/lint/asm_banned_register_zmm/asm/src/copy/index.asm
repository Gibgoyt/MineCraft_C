default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
copy_batch:
    vpxord zmm0, zmm0, zmm0
    ret
