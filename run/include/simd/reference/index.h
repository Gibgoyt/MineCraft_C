/* See run/src/simd/reference/README.md. Each function here does in scalar C exactly what the NASM kernel of the same name
 * does, with the same arguments, so a kernel's meaning is written down somewhere readable. */
#ifndef MC_RUN_SIMD_REFERENCE_INDEX_H
#define MC_RUN_SIMD_REFERENCE_INDEX_H

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

#endif
