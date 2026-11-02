; Layout and shared arithmetic of asm/src/vec_batch/index.asm. See its README.md. Struct offsets come from the generated
; struct_offsets/index.inc, never from a number typed here.
%ifndef MC_ASM_VEC_BATCH_INDEX_INC
%define MC_ASM_VEC_BATCH_INDEX_INC

%include "conventions/index.inc"
%include "struct_offsets/index.inc"

%define VEC_BATCH_VEC3_LANES 8
%define VEC_BATCH_VEC4_LANES 2
%define VEC_BATCH_FLOAT_BYTES 4
%define VEC_BATCH_MAT4_ROWS 4

; The three-register unpacking below moves 16 bytes at a time, three floats and one of the next, so it assumes the floats
; are side by side in this order. A vec3_t that changed would stop the build here.
%if VEC3_T_X != 0 || VEC3_T_Y != VEC_BATCH_FLOAT_BYTES || VEC3_T_Z != 2 * VEC_BATCH_FLOAT_BYTES || VEC3_T_SIZEOF != 3 * VEC_BATCH_FLOAT_BYTES
    %fatal vec_batch assumes a vec3_t of three packed floats
%endif
%if VEC4_T_X != 0 || VEC4_T_Y != VEC_BATCH_FLOAT_BYTES || VEC4_T_Z != 2 * VEC_BATCH_FLOAT_BYTES || VEC4_T_W != 3 * VEC_BATCH_FLOAT_BYTES || VEC4_T_SIZEOF != XMM_BYTES
    %fatal vec_batch assumes a vec4_t of four packed floats
%endif

; The byte offset of one element of a mat4_t: column-major, so m[column * 4 + row].
%define VEC_BATCH_M(column, row) (MAT4_T_M + ((column) * VEC_BATCH_MAT4_ROWS + (row)) * VEC_BATCH_FLOAT_BYTES)

; imm8 for vshufps: the first two come from the first source and the last two from the second. Written high to low, as in
; the instruction's encoding.
%define VEC_BATCH_SHUFFLE(d, c, b, a) (((d) << 6) | ((c) << 4) | ((b) << 2) | (a))

; Registers that hold the matrix through a whole kernel: column, then row. Only three rows matter for a vec3_t.
%define VEC_BATCH_M00 ymm4
%define VEC_BATCH_M01 ymm5
%define VEC_BATCH_M02 ymm6
%define VEC_BATCH_M10 ymm7
%define VEC_BATCH_M11 ymm8
%define VEC_BATCH_M12 ymm9
%define VEC_BATCH_M20 ymm10
%define VEC_BATCH_M21 ymm11
%define VEC_BATCH_M22 ymm12

; rcx = the matrix. Fills the registers above.
%macro VEC_BATCH_LOAD_3X3 0
    vbroadcastss VEC_BATCH_M00, [rcx + VEC_BATCH_M(0, 0)]
    vbroadcastss VEC_BATCH_M01, [rcx + VEC_BATCH_M(0, 1)]
    vbroadcastss VEC_BATCH_M02, [rcx + VEC_BATCH_M(0, 2)]
    vbroadcastss VEC_BATCH_M10, [rcx + VEC_BATCH_M(1, 0)]
    vbroadcastss VEC_BATCH_M11, [rcx + VEC_BATCH_M(1, 1)]
    vbroadcastss VEC_BATCH_M12, [rcx + VEC_BATCH_M(1, 2)]
    vbroadcastss VEC_BATCH_M20, [rcx + VEC_BATCH_M(2, 0)]
    vbroadcastss VEC_BATCH_M21, [rcx + VEC_BATCH_M(2, 1)]
    vbroadcastss VEC_BATCH_M22, [rcx + VEC_BATCH_M(2, 2)]
%endmacro

; rsi = 8 vec3_t. Leaves x in ymm0, y in ymm1 and z in ymm2. Reads exactly those 96 bytes. Uses ymm13 and ymm14.
%macro VEC_BATCH_LOAD_8 0
    vmovups xmm0, [rsi]                                         ; x0 y0 z0 x1
    vinsertf128 ymm0, ymm0, [rsi + 4 * VEC3_T_SIZEOF], 1        ; x4 y4 z4 x5
    vmovups xmm1, [rsi + 1 * VEC3_T_SIZEOF + VEC3_T_Y]          ; y1 z1 x2 y2
    vinsertf128 ymm1, ymm1, [rsi + 5 * VEC3_T_SIZEOF + VEC3_T_Y], 1
    vmovups xmm2, [rsi + 2 * VEC3_T_SIZEOF + VEC3_T_Z]          ; z2 x3 y3 z3
    vinsertf128 ymm2, ymm2, [rsi + 6 * VEC3_T_SIZEOF + VEC3_T_Z], 1
    vshufps ymm13, ymm1, ymm2, VEC_BATCH_SHUFFLE(2, 1, 3, 2)    ; x2 y2 x3 y3
    vshufps ymm14, ymm0, ymm1, VEC_BATCH_SHUFFLE(1, 0, 2, 1)    ; y0 z0 y1 z1
    vshufps ymm0, ymm0, ymm13, VEC_BATCH_SHUFFLE(2, 0, 3, 0)    ; x0 x1 x2 x3
    vshufps ymm1, ymm14, ymm13, VEC_BATCH_SHUFFLE(3, 1, 2, 0)   ; y0 y1 y2 y3
    vshufps ymm2, ymm14, ymm2, VEC_BATCH_SHUFFLE(3, 0, 3, 1)    ; z0 z1 z2 z3
%endmacro

; ymm13 = x, ymm15 = y, ymm3 = z. Writes 8 vec3_t to rdi: exactly 96 bytes. Uses ymm0-ymm2.
%macro VEC_BATCH_STORE_8 0
    vshufps ymm0, ymm13, ymm15, VEC_BATCH_SHUFFLE(2, 0, 2, 0)   ; x0 x2 y0 y2
    vshufps ymm1, ymm15, ymm3, VEC_BATCH_SHUFFLE(3, 1, 3, 1)    ; y1 y3 z1 z3
    vshufps ymm2, ymm3, ymm13, VEC_BATCH_SHUFFLE(3, 1, 2, 0)    ; z0 z2 x1 x3
    vshufps ymm13, ymm0, ymm2, VEC_BATCH_SHUFFLE(2, 0, 2, 0)    ; x0 y0 z0 x1
    vshufps ymm15, ymm1, ymm0, VEC_BATCH_SHUFFLE(3, 1, 2, 0)    ; y1 z1 x2 y2
    vshufps ymm3, ymm2, ymm1, VEC_BATCH_SHUFFLE(3, 1, 3, 1)     ; z2 x3 y3 z3
    vmovups [rdi], xmm13
    vmovups [rdi + 1 * VEC3_T_SIZEOF + VEC3_T_Y], xmm15
    vmovups [rdi + 2 * VEC3_T_SIZEOF + VEC3_T_Z], xmm3
    vextractf128 [rdi + 4 * VEC3_T_SIZEOF], ymm13, 1
    vextractf128 [rdi + 5 * VEC3_T_SIZEOF + VEC3_T_Y], ymm15, 1
    vextractf128 [rdi + 6 * VEC3_T_SIZEOF + VEC3_T_Z], ymm3, 1
%endmacro

; All three rows of the 3x3 part on 8 lanes, from x in ymm0, y in ymm1 and z in ymm2, left in ymm13, ymm15 and ymm3. Each is
; m0 * x + m1 * y + m2 * z, added left to right as mat4_transform_point does, so that the answer is the same to the bit.
; Uses ymm14.
%macro VEC_BATCH_ROWS_8 0
    vmulps  ymm13, VEC_BATCH_M00, ymm0
    vmulps  ymm14, VEC_BATCH_M10, ymm1
    vaddps  ymm13, ymm13, ymm14
    vmulps  ymm14, VEC_BATCH_M20, ymm2
    vaddps  ymm13, ymm13, ymm14
    vmulps  ymm15, VEC_BATCH_M01, ymm0
    vmulps  ymm14, VEC_BATCH_M11, ymm1
    vaddps  ymm15, ymm15, ymm14
    vmulps  ymm14, VEC_BATCH_M21, ymm2
    vaddps  ymm15, ymm15, ymm14
    vmulps  ymm3, VEC_BATCH_M02, ymm0
    vmulps  ymm14, VEC_BATCH_M12, ymm1
    vaddps  ymm3, ymm3, ymm14
    vmulps  ymm14, VEC_BATCH_M22, ymm2
    vaddps  ymm3, ymm3, ymm14
%endmacro

; The same for one vec3_t at rsi, on the low lane of the same registers.
%macro VEC_BATCH_ROWS_1 0
    vmovss  xmm0, [rsi + VEC3_T_X]
    vmovss  xmm1, [rsi + VEC3_T_Y]
    vmovss  xmm2, [rsi + VEC3_T_Z]
    vmulss  xmm13, xmm4, xmm0
    vmulss  xmm14, xmm7, xmm1
    vaddss  xmm13, xmm13, xmm14
    vmulss  xmm14, xmm10, xmm2
    vaddss  xmm13, xmm13, xmm14
    vmulss  xmm15, xmm5, xmm0
    vmulss  xmm14, xmm8, xmm1
    vaddss  xmm15, xmm15, xmm14
    vmulss  xmm14, xmm11, xmm2
    vaddss  xmm15, xmm15, xmm14
    vmulss  xmm3, xmm6, xmm0
    vmulss  xmm14, xmm9, xmm1
    vaddss  xmm3, xmm3, xmm14
    vmulss  xmm14, xmm12, xmm2
    vaddss  xmm3, xmm3, xmm14
%endmacro

; Writes that one result to the vec3_t at rdi.
%macro VEC_BATCH_STORE_1 0
    vmovss  [rdi + VEC3_T_X], xmm13
    vmovss  [rdi + VEC3_T_Y], xmm15
    vmovss  [rdi + VEC3_T_Z], xmm3
%endmacro

%endif
