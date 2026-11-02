; 8-wide 3D gradient noise. Prototype: run/include/simd/noise/index.h. simd_reference_noise3 is this, one lane at a time, and
; the two agree bit for bit, because terrain has to come out the same whichever one computed it. That is why the multiply
; and add pairs here are FMA, the reference calls fmaf, and nothing here is reordered.
;
; A hash picks each corner's gradient, and there is no table: a lookup would need a gather for each of 8 corners, and the
; Downfall microcode makes gathers very slow on this CPU. The last few lanes go through masked moves, which do not touch,
; or fault on, what the mask leaves out.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

%include "noise/index.inc"

REQUIRE_CPU AVX2
REQUIRE_CPU FMA3

section .text
global noise3_batch8
align 32
noise3_batch8:
    ; rdi = out, rsi = xs, rdx = ys, rcx = zs (float32_t *), r8 = count, r9d = seed.
    push    rbp
    mov     rbp, rsp
    and     rsp, -YMM_BYTES
    sub     rsp, NOISE_FRAME_BYTES
    vmovd   xmm0, r9d
    vpbroadcastd ymm0, xmm0
    vmovaps [NOISE_SEED], ymm0
    vmovaps NOISE_REG_SIGN, [noise_sign]
    vmovaps NOISE_REG_FOUR, [noise_four]
    vmovaps NOISE_REG_13, [noise_thirteen]
    vmovaps NOISE_REG_12, [noise_twelve]
    vmovaps NOISE_REG_MIX_1, [noise_mix_1]
.next:
    cmp     r8, NOISE_LANES
    jb      .tail_load
    vmovups ymm0, [rsi]
    vmovups ymm1, [rdx]
    vmovups ymm2, [rcx]
.compute:
    ; Each cell's low corner, and how far along the cell each lane is.
    vroundps ymm3, ymm0, NOISE_ROUND_FLOOR
    vroundps ymm4, ymm1, NOISE_ROUND_FLOOR
    vroundps ymm5, ymm2, NOISE_ROUND_FLOOR
    vsubps  ymm0, ymm0, ymm3
    vsubps  ymm1, ymm1, ymm4
    vsubps  ymm2, ymm2, ymm5
    vmovaps [NOISE_FX0], ymm0
    vmovaps [NOISE_FY0], ymm1
    vmovaps [NOISE_FZ0], ymm2
    vmovaps ymm6, [noise_one]
    vsubps  ymm7, ymm0, ymm6
    vmovaps [NOISE_FX1], ymm7
    vsubps  ymm7, ymm1, ymm6
    vmovaps [NOISE_FY1], ymm7
    vsubps  ymm7, ymm2, ymm6
    vmovaps [NOISE_FZ1], ymm7

    ; Out-of-range lanes convert to 0x80000000, which simd_reference_noise3 spells out.
    vcvttps2dq ymm3, ymm3
    vcvttps2dq ymm4, ymm4
    vcvttps2dq ymm5, ymm5

    NOISE_FADE ymm0, ymm6, ymm7
    vmovaps [NOISE_FADE_X], ymm6
    NOISE_FADE ymm1, ymm6, ymm7
    vmovaps [NOISE_FADE_Y], ymm6
    NOISE_FADE ymm2, ymm6, ymm7
    vmovaps [NOISE_FADE_Z], ymm6

    ; Each axis's hash term at both ends of the cell. (i + 1) * k is i * k + k, so that takes no second multiply.
    vpmulld ymm9, ymm3, [noise_hash_x]
    vpaddd  ymm10, ymm9, [noise_hash_x]
    vpmulld ymm4, ymm4, [noise_hash_y]
    vpaddd  ymm6, ymm4, [noise_hash_y]
    vpmulld ymm5, ymm5, [noise_hash_z]
    vpaddd  ymm7, ymm5, [noise_hash_z]
    vmovaps ymm8, [NOISE_SEED]
    vpxor   ymm0, ymm4, ymm5
    vpxor   ymm0, ymm0, ymm8
    vmovaps [NOISE_YZ00], ymm0
    vpxor   ymm0, ymm6, ymm5
    vpxor   ymm0, ymm0, ymm8
    vmovaps [NOISE_YZ10], ymm0
    vpxor   ymm0, ymm4, ymm7
    vpxor   ymm0, ymm0, ymm8
    vmovaps [NOISE_YZ01], ymm0
    vpxor   ymm0, ymm6, ymm7
    vpxor   ymm0, ymm0, ymm8
    vmovaps [NOISE_YZ11], ymm0

    ; The eight corners, interpolated along x, then y, then z. lerp(t, a, b) is a + t * (b - a).
    NOISE_CORNER ymm7, ymm9, NOISE_YZ00, NOISE_FX0, NOISE_FY0, NOISE_FZ0
    NOISE_CORNER ymm8, ymm10, NOISE_YZ00, NOISE_FX1, NOISE_FY0, NOISE_FZ0
    vsubps  ymm8, ymm8, ymm7
    vfmadd231ps ymm7, ymm8, [NOISE_FADE_X]
    vmovaps [NOISE_LERP_A], ymm7

    NOISE_CORNER ymm7, ymm9, NOISE_YZ10, NOISE_FX0, NOISE_FY1, NOISE_FZ0
    NOISE_CORNER ymm8, ymm10, NOISE_YZ10, NOISE_FX1, NOISE_FY1, NOISE_FZ0
    vsubps  ymm8, ymm8, ymm7
    vfmadd231ps ymm7, ymm8, [NOISE_FADE_X]
    vmovaps ymm6, [NOISE_LERP_A]
    vsubps  ymm7, ymm7, ymm6
    vfmadd231ps ymm6, ymm7, [NOISE_FADE_Y]
    vmovaps [NOISE_LERP_B], ymm6

    NOISE_CORNER ymm7, ymm9, NOISE_YZ01, NOISE_FX0, NOISE_FY0, NOISE_FZ1
    NOISE_CORNER ymm8, ymm10, NOISE_YZ01, NOISE_FX1, NOISE_FY0, NOISE_FZ1
    vsubps  ymm8, ymm8, ymm7
    vfmadd231ps ymm7, ymm8, [NOISE_FADE_X]
    vmovaps [NOISE_LERP_A], ymm7

    NOISE_CORNER ymm7, ymm9, NOISE_YZ11, NOISE_FX0, NOISE_FY1, NOISE_FZ1
    NOISE_CORNER ymm8, ymm10, NOISE_YZ11, NOISE_FX1, NOISE_FY1, NOISE_FZ1
    vsubps  ymm8, ymm8, ymm7
    vfmadd231ps ymm7, ymm8, [NOISE_FADE_X]
    vmovaps ymm6, [NOISE_LERP_A]
    vsubps  ymm7, ymm7, ymm6
    vfmadd231ps ymm6, ymm7, [NOISE_FADE_Y]

    vmovaps ymm0, [NOISE_LERP_B]
    vsubps  ymm6, ymm6, ymm0
    vfmadd231ps ymm0, ymm6, [NOISE_FADE_Z]

    cmp     r8, NOISE_LANES
    jb      .tail_store
    vmovups [rdi], ymm0
    add     rsi, YMM_BYTES
    add     rdx, YMM_BYTES
    add     rcx, YMM_BYTES
    add     rdi, YMM_BYTES
    sub     r8, NOISE_LANES
    jmp     .next
.tail_load:
    test    r8, r8
    jz      .done
    mov     eax, NOISE_LANES
    sub     rax, r8
    lea     r10, [noise_tail_masks]
    vmovdqu ymm3, [r10 + rax * NOISE_LANE_BYTES]
    vmovaps [NOISE_TAIL_MASK], ymm3
    vmaskmovps ymm0, ymm3, [rsi]
    vmaskmovps ymm1, ymm3, [rdx]
    vmaskmovps ymm2, ymm3, [rcx]
    jmp     .compute
.tail_store:
    vmovaps ymm3, [NOISE_TAIL_MASK]
    vmaskmovps [rdi], ymm3, ymm0
.done:
    vzeroupper
    mov     rsp, rbp
    pop     rbp
    ret

; Constants, in .text so that reaching them takes no relocation (see asm/include/conventions/).
align 32
noise_sign:
    times NOISE_LANES dd 0x80000000
noise_four:
    times NOISE_LANES dd 4
noise_thirteen:
    times NOISE_LANES dd 13
noise_twelve:
    times NOISE_LANES dd 12
noise_mix_1:
    times NOISE_LANES dd NOISE_HASH_MIX_1
noise_mix_2:
    times NOISE_LANES dd NOISE_HASH_MIX_2
noise_hash_x:
    times NOISE_LANES dd NOISE_HASH_X
noise_hash_y:
    times NOISE_LANES dd NOISE_HASH_Y
noise_hash_z:
    times NOISE_LANES dd NOISE_HASH_Z
noise_one:
    times NOISE_LANES dd 1.0
noise_six:
    times NOISE_LANES dd 6.0
noise_ten:
    times NOISE_LANES dd 10.0
noise_minus_15:
    times NOISE_LANES dd -15.0
noise_tail_masks:
    times NOISE_LANES dd -1
    times NOISE_LANES dd 0
