/* See run/src/simd/reference/README.md. Each function here does in scalar C exactly what the NASM kernel of the same name
 * does, with the same arguments, so a kernel's meaning is written down somewhere readable. */
#ifndef MC_RUN_SIMD_REFERENCE_INDEX_H
#define MC_RUN_SIMD_REFERENCE_INDEX_H

#include "math/mat4/index.h"
#include "math/vec3/index.h"
#include "math/vec4/index.h"
#include "types/index.h"

#include <stdint.h>

void simd_reference_copy_stream(uint8_t *dst, const uint8_t *src, uint64_t bytes);
void simd_reference_copy_stream_rows(uint8_t *dst, uint64_t dst_pitch, const uint8_t *src, uint64_t src_pitch,
                                     uint64_t row_bytes, uint64_t rows);

void simd_reference_blend_over(uint32_t *dst, const uint32_t *src, uint64_t count);
void simd_reference_blend_premul(uint32_t *dst, const uint32_t *src, uint64_t count);

uint64_t simd_reference_bitmask_popcount(const uint64_t *words, uint64_t count);
void simd_reference_bitmask_and(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);
void simd_reference_bitmask_or(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);
void simd_reference_bitmask_andnot(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);
void simd_reference_bitmask_from_nonzero_u8(const uint8_t *values, uint64_t count, uint64_t *out);
void simd_reference_bitmask_from_nonzero_u16(const uint16_t *values, uint64_t count, uint64_t *out);
void simd_reference_bitmask_from_equal_u16(const uint16_t *values, uint64_t count, uint16_t value, uint64_t *out);

void simd_reference_noise3(float32_t *out, const float32_t *xs, const float32_t *ys, const float32_t *zs,
                           uint64_t count, uint32_t seed);

void simd_reference_vec_batch_transform_points(vec3_t *out, const vec3_t *in, uint64_t count, const mat4_t *m);
void simd_reference_vec_batch_transform_directions(vec3_t *out, const vec3_t *in, uint64_t count, const mat4_t *m);
void simd_reference_vec_batch_mul_vec4(vec4_t *out, const vec4_t *in, uint64_t count, const mat4_t *m);

#endif
