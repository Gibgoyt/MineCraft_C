; Constants and the shared arithmetic of asm/src/blend/index.asm. See its README.md.
%ifndef MC_ASM_BLEND_INDEX_INC
%define MC_ASM_BLEND_INDEX_INC

%include "conventions/index.inc"

%define BLEND_PIXEL_BYTES 4
%define BLEND_PIXELS_PER_VECTOR 8

; Registers that hold the same thing through a whole kernel.
%define BLEND_BYTE_255 ymm9             ; every bit set; only the premultiplied kernel loads it
%define BLEND_TAIL_MASK ymm10
%define BLEND_ALPHA_BYTES ymm11         ; 0xFF000000 in each pixel
%define BLEND_ROUND ymm12               ; 128 in each 16-bit word
%define BLEND_WORD_255 ymm13
%define BLEND_ZERO ymm14
%define BLEND_ALPHA_SHUFFLE ymm15       ; copies each pixel's alpha where its kernel wants it

; %1 = 16-bit words holding x, %2 = a scratch register. Leaves x / 255, rounded to nearest, exactly, for x up to 255 * 255.
; The largest value on the way is 65407, so no word overflows.
%macro BLEND_DIV255 2
    vpaddw  %1, %1, BLEND_ROUND
    vpsrlw  %2, %1, 8
    vpaddw  %1, %1, %2
    vpsrlw  %1, %1, 8
%endmacro

; Loads BLEND_TAIL_MASK so that its first rdx dwords are set, for rdx below 8. Uses rax and rcx.
%macro BLEND_LOAD_TAIL_MASK 0
    mov     eax, BLEND_PIXELS_PER_VECTOR
    sub     rax, rdx
    lea     rcx, [blend_tail_masks]
    vmovdqu BLEND_TAIL_MASK, [rcx + rax * BLEND_PIXEL_BYTES]
%endmacro

; ymm0 = src, ymm1 = dst, BLEND_ALPHA_SHUFFLE gives each channel's weight. Leaves in ymm0, for each byte,
; (src * weight + dst * (255 - weight)) / 255. A weight of 0 keeps the dst byte, which is what keeps its top byte.
%macro BLEND_OVER_PIXELS 0
    vpshufb ymm2, ymm0, BLEND_ALPHA_SHUFFLE
    vpunpcklbw ymm3, ymm0, BLEND_ZERO
    vpunpckhbw ymm4, ymm0, BLEND_ZERO
    vpunpcklbw ymm5, ymm1, BLEND_ZERO
    vpunpckhbw ymm6, ymm1, BLEND_ZERO
    vpunpcklbw ymm7, ymm2, BLEND_ZERO
    vpunpckhbw ymm2, ymm2, BLEND_ZERO
    vpmullw ymm3, ymm3, ymm7
    vpmullw ymm4, ymm4, ymm2
    vpxor   ymm7, ymm7, BLEND_WORD_255
    vpxor   ymm2, ymm2, BLEND_WORD_255
    vpmullw ymm5, ymm5, ymm7
    vpmullw ymm6, ymm6, ymm2
    vpaddw  ymm3, ymm3, ymm5
    vpaddw  ymm4, ymm4, ymm6
    BLEND_DIV255 ymm3, ymm5
    BLEND_DIV255 ymm4, ymm6
    vpackuswb ymm0, ymm3, ymm4
%endmacro

; ymm0 = src, ymm1 = dst, BLEND_ALPHA_SHUFFLE gives alpha in all four bytes of each pixel. Leaves in ymm0, for each byte,
; src + dst * (255 - alpha) / 255, stopping at 255.
%macro BLEND_PREMUL_PIXELS 0
    vpshufb ymm2, ymm0, BLEND_ALPHA_SHUFFLE
    vpxor   ymm2, ymm2, BLEND_BYTE_255
    vpunpcklbw ymm5, ymm1, BLEND_ZERO
    vpunpckhbw ymm6, ymm1, BLEND_ZERO
    vpunpcklbw ymm7, ymm2, BLEND_ZERO
    vpunpckhbw ymm2, ymm2, BLEND_ZERO
    vpmullw ymm5, ymm5, ymm7
    vpmullw ymm6, ymm6, ymm2
    BLEND_DIV255 ymm5, ymm7
    BLEND_DIV255 ymm6, ymm2
    vpackuswb ymm5, ymm5, ymm6
    vpaddusb ymm0, ymm0, ymm5
%endmacro

%endif
