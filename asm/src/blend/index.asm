; HUD alpha blending, on 32-bit pixels: blue, green, red and alpha or unused, in that byte order, the layout DRM calls
; ARGB8888 and XRGB8888. Prototypes: run/include/simd/blend/index.h. The arithmetic is in asm/include/blend/index.inc.
;
; Most of a HUD is fully transparent, and the rest mostly fully opaque, so each vector of 8 first asks whether all of it is
; one or the other. Those paths give exactly what the arithmetic would. The last few pixels go through masked moves, which
; do not touch, or fault on, what the mask leaves out.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

%include "blend/index.inc"

REQUIRE_CPU AVX2

section .text
global blend_over_batch
align 32
blend_over_batch:
    ; rdi = dst (uint32_t *), rsi = src (uint32_t *), rdx = count.
    vmovdqa BLEND_ALPHA_SHUFFLE, [blend_shuffle_over]
    vpbroadcastd BLEND_ALPHA_BYTES, [blend_dword_alpha]
    vpbroadcastd BLEND_WORD_255, [blend_dword_255]
    vpbroadcastd BLEND_ROUND, [blend_dword_round]
    vpxor   BLEND_ZERO, BLEND_ZERO, BLEND_ZERO
.next:
    cmp     rdx, BLEND_PIXELS_PER_VECTOR
    jb      .tail
    vmovdqu ymm0, [rsi]
    vptest  ymm0, BLEND_ALPHA_BYTES     ; ZF: every alpha is 0. CF: every alpha is 255.
    jz      .advance
    jc      .opaque
    vmovdqu ymm1, [rdi]
    BLEND_OVER_PIXELS
    vmovdqu [rdi], ymm0
    jmp     .advance
.opaque:
    vpblendvb ymm0, ymm0, [rdi], BLEND_ALPHA_BYTES ; src's colour under dst's top byte
    vmovdqu [rdi], ymm0
.advance:
    add     rsi, YMM_BYTES
    add     rdi, YMM_BYTES
    sub     rdx, BLEND_PIXELS_PER_VECTOR
    jmp     .next
.tail:
    test    rdx, rdx
    jz      .done
    BLEND_LOAD_TAIL_MASK
    vpmaskmovd ymm0, BLEND_TAIL_MASK, [rsi]
    vpmaskmovd ymm1, BLEND_TAIL_MASK, [rdi]
    BLEND_OVER_PIXELS
    vpmaskmovd [rdi], BLEND_TAIL_MASK, ymm0
.done:
    vzeroupper
    ret

global blend_premul_batch
align 32
blend_premul_batch:
    ; rdi = dst (uint32_t *), rsi = src (uint32_t *), rdx = count.
    vmovdqa BLEND_ALPHA_SHUFFLE, [blend_shuffle_premul]
    vpbroadcastd BLEND_ALPHA_BYTES, [blend_dword_alpha]
    vpbroadcastd BLEND_WORD_255, [blend_dword_255]
    vpbroadcastd BLEND_ROUND, [blend_dword_round]
    vpxor   BLEND_ZERO, BLEND_ZERO, BLEND_ZERO
    vpcmpeqb BLEND_BYTE_255, BLEND_BYTE_255, BLEND_BYTE_255
.next:
    cmp     rdx, BLEND_PIXELS_PER_VECTOR
    jb      .tail
    vmovdqu ymm0, [rsi]
    vptest  ymm0, ymm0                  ; a premultiplied pixel adds its colour, so only all of it being 0 changes nothing
    jz      .advance
    vptest  ymm0, BLEND_ALPHA_BYTES     ; CF: every alpha is 255, so dst counts for nothing
    jc      .store
    vmovdqu ymm1, [rdi]
    BLEND_PREMUL_PIXELS
.store:
    vmovdqu [rdi], ymm0
.advance:
    add     rsi, YMM_BYTES
    add     rdi, YMM_BYTES
    sub     rdx, BLEND_PIXELS_PER_VECTOR
    jmp     .next
.tail:
    test    rdx, rdx
    jz      .done
    BLEND_LOAD_TAIL_MASK
    vpmaskmovd ymm0, BLEND_TAIL_MASK, [rsi]
    vpmaskmovd ymm1, BLEND_TAIL_MASK, [rdi]
    BLEND_PREMUL_PIXELS
    vpmaskmovd [rdi], BLEND_TAIL_MASK, ymm0
.done:
    vzeroupper
    ret

; Constants, in .text so that reaching them takes no relocation (see asm/include/conventions/).
align 32
blend_shuffle_over:
    ; Each pixel's alpha into its three colour bytes. Zero in its own top byte, so that one keeps what dst had.
    times 2 db 3, 3, 3, 0x80, 7, 7, 7, 0x80, 11, 11, 11, 0x80, 15, 15, 15, 0x80
blend_shuffle_premul:
    times 2 db 3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15
blend_tail_masks:
    times BLEND_PIXELS_PER_VECTOR dd -1
    times BLEND_PIXELS_PER_VECTOR dd 0
blend_dword_alpha:
    dd      0xFF000000
blend_dword_255:
    dd      0x00FF00FF
blend_dword_round:
    dd      0x00800080
