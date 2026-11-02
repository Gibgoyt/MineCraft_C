; Batch transforms of arrays of vectors by one matrix. Prototypes: run/include/simd/vec_batch/index.h. Each agrees bit for bit
; with its scalar function in run/src/math/mat4/, one vector at a time, which is why there is no FMA here: the scalar code has
; none, and a fused multiply-add rounds differently.
;
; A vec3_t array is unpacked 8 at a time into x, y and z registers and packed again. What is left over goes one at a time.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

%include "vec_batch/index.inc"

REQUIRE_CPU AVX

section .text
global vec_batch_transform_points
align 32
vec_batch_transform_points:
    ; rdi = out (vec3_t *), rsi = in (const vec3_t *), rdx = count, rcx = m (const mat4_t *). As mat4_transform_point.
    VEC_BATCH_LOAD_3X3
.next:
    cmp     rdx, VEC_BATCH_VEC3_LANES
    jb      .tail
    VEC_BATCH_LOAD_8
    VEC_BATCH_ROWS_8
    vbroadcastss ymm14, [rcx + VEC_BATCH_M(3, 0)]
    vaddps  ymm13, ymm13, ymm14
    vbroadcastss ymm14, [rcx + VEC_BATCH_M(3, 1)]
    vaddps  ymm15, ymm15, ymm14
    vbroadcastss ymm14, [rcx + VEC_BATCH_M(3, 2)]
    vaddps  ymm3, ymm3, ymm14
    VEC_BATCH_STORE_8
    add     rsi, VEC_BATCH_VEC3_LANES * VEC3_T_SIZEOF
    add     rdi, VEC_BATCH_VEC3_LANES * VEC3_T_SIZEOF
    sub     rdx, VEC_BATCH_VEC3_LANES
    jmp     .next
.tail:
    test    rdx, rdx
    jz      .done
    VEC_BATCH_ROWS_1
    vaddss  xmm13, xmm13, [rcx + VEC_BATCH_M(3, 0)]
    vaddss  xmm15, xmm15, [rcx + VEC_BATCH_M(3, 1)]
    vaddss  xmm3, xmm3, [rcx + VEC_BATCH_M(3, 2)]
    VEC_BATCH_STORE_1
    add     rsi, VEC3_T_SIZEOF
    add     rdi, VEC3_T_SIZEOF
    dec     rdx
    jmp     .tail
.done:
    vzeroupper
    ret

global vec_batch_transform_directions
align 32
vec_batch_transform_directions:
    ; rdi = out (vec3_t *), rsi = in (const vec3_t *), rdx = count, rcx = m (const mat4_t *). As mat4_transform_direction.
    VEC_BATCH_LOAD_3X3
.next:
    cmp     rdx, VEC_BATCH_VEC3_LANES
    jb      .tail
    VEC_BATCH_LOAD_8
    VEC_BATCH_ROWS_8
    VEC_BATCH_STORE_8
    add     rsi, VEC_BATCH_VEC3_LANES * VEC3_T_SIZEOF
    add     rdi, VEC_BATCH_VEC3_LANES * VEC3_T_SIZEOF
    sub     rdx, VEC_BATCH_VEC3_LANES
    jmp     .next
.tail:
    test    rdx, rdx
    jz      .done
    VEC_BATCH_ROWS_1
    VEC_BATCH_STORE_1
    add     rsi, VEC3_T_SIZEOF
    add     rdi, VEC3_T_SIZEOF
    dec     rdx
    jmp     .tail
.done:
    vzeroupper
    ret

global vec_batch_mul_vec4
align 32
vec_batch_mul_vec4:
    ; rdi = out (vec4_t *), rsi = in (const vec4_t *), rdx = count, rcx = m (const mat4_t *). As mat4_mul_vec4.
    vbroadcastf128 ymm4, [rcx + VEC_BATCH_M(0, 0)]
    vbroadcastf128 ymm5, [rcx + VEC_BATCH_M(1, 0)]
    vbroadcastf128 ymm6, [rcx + VEC_BATCH_M(2, 0)]
    vbroadcastf128 ymm7, [rcx + VEC_BATCH_M(3, 0)]
.next:
    cmp     rdx, VEC_BATCH_VEC4_LANES
    jb      .tail
    vmovups ymm0, [rsi]
    vshufps ymm1, ymm0, ymm0, VEC_BATCH_SHUFFLE(0, 0, 0, 0)
    vshufps ymm2, ymm0, ymm0, VEC_BATCH_SHUFFLE(1, 1, 1, 1)
    vshufps ymm3, ymm0, ymm0, VEC_BATCH_SHUFFLE(2, 2, 2, 2)
    vshufps ymm0, ymm0, ymm0, VEC_BATCH_SHUFFLE(3, 3, 3, 3)
    vmulps  ymm1, ymm1, ymm4
    vmulps  ymm2, ymm2, ymm5
    vmulps  ymm3, ymm3, ymm6
    vmulps  ymm0, ymm0, ymm7
    vaddps  ymm1, ymm1, ymm2
    vaddps  ymm1, ymm1, ymm3
    vaddps  ymm1, ymm1, ymm0
    vmovups [rdi], ymm1
    add     rsi, VEC_BATCH_VEC4_LANES * VEC4_T_SIZEOF
    add     rdi, VEC_BATCH_VEC4_LANES * VEC4_T_SIZEOF
    sub     rdx, VEC_BATCH_VEC4_LANES
    jmp     .next
.tail:
    test    rdx, rdx
    jz      .done
    vmovups xmm0, [rsi]
    vshufps xmm1, xmm0, xmm0, VEC_BATCH_SHUFFLE(0, 0, 0, 0)
    vshufps xmm2, xmm0, xmm0, VEC_BATCH_SHUFFLE(1, 1, 1, 1)
    vshufps xmm3, xmm0, xmm0, VEC_BATCH_SHUFFLE(2, 2, 2, 2)
    vshufps xmm0, xmm0, xmm0, VEC_BATCH_SHUFFLE(3, 3, 3, 3)
    vmulps  xmm1, xmm1, xmm4
    vmulps  xmm2, xmm2, xmm5
    vmulps  xmm3, xmm3, xmm6
    vmulps  xmm0, xmm0, xmm7
    vaddps  xmm1, xmm1, xmm2
    vaddps  xmm1, xmm1, xmm3
    vaddps  xmm1, xmm1, xmm0
    vmovups [rdi], xmm1
.done:
    vzeroupper
    ret
