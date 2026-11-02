; Streaming copy, for writing a finished frame to scanout memory. Prototypes: run/include/simd/copy/index.h.
;
; The destination is written with non-temporal stores, so it does not pull scanout memory into the cache, and a write-combined
; destination receives whole lines. Bytes before the first 32-byte boundary of the destination, and after the last, take
; ordinary stores. The two kinds never touch the same byte: on the same address, an ordinary store may be seen before an
; earlier non-temporal one, which is why this does not use the usual trick of one overlapping unaligned store at each end.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

%include "copy/index.inc"

REQUIRE_CPU AVX

section .text
global copy_stream_batch
align 32
copy_stream_batch:
    ; rdi = dst, rsi = src, rdx = bytes. One row of copy_stream_rows_batch.
    mov     r8, rdx
    mov     rdx, rsi
    xor     esi, esi
    xor     ecx, ecx
    mov     r9d, 1
    jmp     copy_stream_rows_batch

global copy_stream_rows_batch
align 32
copy_stream_rows_batch:
    ; rdi = dst, rsi = dst_pitch, rdx = src, rcx = src_pitch, r8 = row_bytes, r9 = rows.
    push    rbx
    test    r9, r9
    jz      .done
.row:
    mov     r10, rdi                    ; where this row is up to, in dst
    mov     r11, rdx                    ; and in src
    mov     rax, r8                     ; bytes of it left
    cmp     rax, YMM_BYTES
    jb      .tail
    ; Bring dst up to a 32-byte boundary. Each step leaves the next bit of the address clear.
    test    r10b, 1
    jz      .head_2
    movzx   ebx, byte [r11]
    mov     [r10], bl
    inc     r10
    inc     r11
    dec     rax
.head_2:
    test    r10b, 2
    jz      .head_4
    movzx   ebx, word [r11]
    mov     [r10], bx
    add     r10, 2
    add     r11, 2
    sub     rax, 2
.head_4:
    test    r10b, 4
    jz      .head_8
    mov     ebx, [r11]
    mov     [r10], ebx
    add     r10, 4
    add     r11, 4
    sub     rax, 4
.head_8:
    test    r10b, 8
    jz      .head_16
    mov     rbx, [r11]
    mov     [r10], rbx
    add     r10, 8
    add     r11, 8
    sub     rax, 8
.head_16:
    test    r10b, 16
    jz      .blocks
    vmovdqu xmm0, [r11]
    vmovdqu [r10], xmm0
    add     r10, XMM_BYTES
    add     r11, XMM_BYTES
    sub     rax, XMM_BYTES
.blocks:
    mov     rbx, rax
    shr     rbx, COPY_BLOCK_SHIFT
    jz      .vectors
.block:
    vmovdqu ymm0, [r11]
    vmovdqu ymm1, [r11 + YMM_BYTES]
    vmovdqu ymm2, [r11 + 2 * YMM_BYTES]
    vmovdqu ymm3, [r11 + 3 * YMM_BYTES]
    vmovntdq [r10], ymm0
    vmovntdq [r10 + YMM_BYTES], ymm1
    vmovntdq [r10 + 2 * YMM_BYTES], ymm2
    vmovntdq [r10 + 3 * YMM_BYTES], ymm3
    add     r10, COPY_BLOCK_BYTES
    add     r11, COPY_BLOCK_BYTES
    dec     rbx
    jnz     .block
    and     eax, COPY_BLOCK_BYTES - 1
.vectors:
    cmp     rax, YMM_BYTES
    jb      .tail
    vmovdqu ymm0, [r11]
    vmovntdq [r10], ymm0
    add     r10, YMM_BYTES
    add     r11, YMM_BYTES
    sub     rax, YMM_BYTES
    jmp     .vectors
.tail:
    ; Under 32 bytes left, so the low five bits of rax say exactly which of these run.
    test    al, 16
    jz      .tail_8
    vmovdqu xmm0, [r11]
    vmovdqu [r10], xmm0
    add     r10, XMM_BYTES
    add     r11, XMM_BYTES
.tail_8:
    test    al, 8
    jz      .tail_4
    mov     rbx, [r11]
    mov     [r10], rbx
    add     r10, 8
    add     r11, 8
.tail_4:
    test    al, 4
    jz      .tail_2
    mov     ebx, [r11]
    mov     [r10], ebx
    add     r10, 4
    add     r11, 4
.tail_2:
    test    al, 2
    jz      .tail_1
    movzx   ebx, word [r11]
    mov     [r10], bx
    add     r10, 2
    add     r11, 2
.tail_1:
    test    al, 1
    jz      .row_done
    movzx   ebx, byte [r11]
    mov     [r10], bl
.row_done:
    add     rdi, rsi
    add     rdx, rcx
    dec     r9
    jnz     .row
.done:
    sfence                              ; everything above is visible before the caller flips to this frame
    pop     rbx
    vzeroupper
    ret
