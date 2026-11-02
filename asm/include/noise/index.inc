; Constants and the corner step of asm/src/noise/index.asm. See its README.md. Each number here must match
; run/src/simd/reference/index.c, whose gradient and hash are the same arithmetic written out in C.
%ifndef MC_ASM_NOISE_INDEX_INC
%define MC_ASM_NOISE_INDEX_INC

%include "conventions/index.inc"

%define NOISE_LANES 8
%define NOISE_LANE_BYTES 4

; vroundps: round down, without raising the inexact flag.
%define NOISE_ROUND_FLOOR 9

; How a lattice cell becomes a hash: see simd_reference_noise3.
%define NOISE_HASH_X 0x8DA6B343
%define NOISE_HASH_Y 0xD8163841
%define NOISE_HASH_Z 0xCB1AB31F
%define NOISE_HASH_MIX_1 0x2C1B3C6D
%define NOISE_HASH_MIX_2 0x297A2D39
%define NOISE_HASH_SHIFT_1 15
%define NOISE_HASH_SHIFT_2 12
%define NOISE_BUCKET_SHIFT 28       ; the top 4 bits pick the gradient
%define NOISE_BUCKET_NEGATE_U 3     ; shifts bucket bit 0 (bit 28 of the hash) into the sign bit
%define NOISE_BUCKET_NEGATE_V 2     ; shifts bucket bit 1 (bit 29) into the sign bit

; Registers that hold the same thing through a whole kernel.
%define NOISE_REG_SIGN ymm11        ; 0x80000000
%define NOISE_REG_FOUR ymm12        ; 4: bucket below this takes y as its second axis
%define NOISE_REG_13 ymm13          ; bucket & 13 == 12 is bucket 12 or 14, which take x as their second axis
%define NOISE_REG_12 ymm14
%define NOISE_REG_MIX_1 ymm15

; The stack frame, one vector to a slot, from an rsp aligned to 32.
%define NOISE_SLOT(n) (rsp + (n) * YMM_BYTES)
%define NOISE_FX0 NOISE_SLOT(0)     ; distance from the cell's low corner on x, then the same from the high corner
%define NOISE_FX1 NOISE_SLOT(1)
%define NOISE_FY0 NOISE_SLOT(2)
%define NOISE_FY1 NOISE_SLOT(3)
%define NOISE_FZ0 NOISE_SLOT(4)
%define NOISE_FZ1 NOISE_SLOT(5)
%define NOISE_FADE_X NOISE_SLOT(6)
%define NOISE_FADE_Y NOISE_SLOT(7)
%define NOISE_FADE_Z NOISE_SLOT(8)
%define NOISE_YZ00 NOISE_SLOT(9)    ; the seed and the y and z hash terms of each y, z corner: y bit, then z bit
%define NOISE_YZ10 NOISE_SLOT(10)
%define NOISE_YZ01 NOISE_SLOT(11)
%define NOISE_YZ11 NOISE_SLOT(12)
%define NOISE_LERP_A NOISE_SLOT(13) ; a finished interpolation waiting for its partner
%define NOISE_LERP_B NOISE_SLOT(14)
%define NOISE_SEED NOISE_SLOT(15)
%define NOISE_TAIL_MASK NOISE_SLOT(16)
%define NOISE_FRAME_BYTES (17 * YMM_BYTES)

; %1 = t, a register it leaves alone. %2 = a free register, left holding the fade 6t^5 - 15t^4 + 10t^3 as
; (t * t * t) * (((t * 6 - 15) * t) + 10). %3 = a free register.
%macro NOISE_FADE 3
    vmovaps %2, [noise_six]
    vfmadd213ps %2, %1, [noise_minus_15]
    vfmadd213ps %2, %1, [noise_ten]
    vmulps  %3, %1, %1
    vmulps  %3, %3, %1
    vmulps  %2, %2, %3
%endmacro

; %1 = the register for the result, not ymm0-ymm5. %2 = the x hash term. %3 = the y and z terms and the seed, a slot.
; %4, %5, %6 = the distances on x, y and z, slots. Uses ymm0-ymm5.
;
; One corner's gradient dotted with its distance vector. Perlin's improved set: the twelve edges of a cube and four of them
; again. bucket < 8 picks x as the first axis and otherwise y, and that test is the hash's own sign bit. The second axis
; is y below 4, x for 12 and 14, and otherwise z. The bucket's two low bits negate each.
%macro NOISE_CORNER 6
    vpxor   ymm0, %2, [%3]
    vpsrld  ymm1, ymm0, NOISE_HASH_SHIFT_1
    vpxor   ymm0, ymm0, ymm1
    vpmulld ymm0, ymm0, NOISE_REG_MIX_1
    vpsrld  ymm1, ymm0, NOISE_HASH_SHIFT_2
    vpxor   ymm0, ymm0, ymm1
    vpmulld ymm0, ymm0, [noise_mix_2]
    vmovaps ymm1, [%4]
    vmovaps ymm2, [%5]
    vmovaps ymm3, [%6]
    vpsrld  ymm4, ymm0, NOISE_BUCKET_SHIFT
    vpcmpgtd ymm5, NOISE_REG_FOUR, ymm4
    vpand   ymm4, ymm4, NOISE_REG_13
    vpcmpeqd ymm4, ymm4, NOISE_REG_12
    vblendvps ymm3, ymm3, ymm1, ymm4    ; the second axis: z, or x for 12 and 14
    vblendvps ymm3, ymm3, ymm2, ymm5    ; or y below 4
    vblendvps ymm1, ymm1, ymm2, ymm0    ; the first axis: x, or y from 8 up
    vpslld  ymm4, ymm0, NOISE_BUCKET_NEGATE_U
    vpand   ymm4, ymm4, NOISE_REG_SIGN
    vpxor   ymm1, ymm1, ymm4
    vpslld  ymm4, ymm0, NOISE_BUCKET_NEGATE_V
    vpand   ymm4, ymm4, NOISE_REG_SIGN
    vpxor   ymm3, ymm3, ymm4
    vaddps  %1, ymm1, ymm3
%endmacro

%endif
