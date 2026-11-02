/* Prototypes of asm/src/bitmask/index.asm. See its README.md. Scalar references: simd/reference/index.h.
 *
 * A bit array is an array of uint64_t, with element i at bit i % 64 of word i / 64. No pointer needs more than its element's
 * alignment. A kernel never touches memory past what count says. */
#ifndef MC_RUN_SIMD_BITMASK_INDEX_H
#define MC_RUN_SIMD_BITMASK_INDEX_H

#include <stdint.h>

/* The number of bits set in count words. */
uint64_t bitmask_popcount_batch(const uint64_t *words, uint64_t count);

/* dst[i] = a[i] & b[i]. dst may be a or b, or not overlap them at all. The same goes for the next two. */
void bitmask_and_batch(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);

/* dst[i] = a[i] | b[i]. */
void bitmask_or_batch(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);

/* dst[i] = a[i] & ~b[i]. */
void bitmask_andnot_batch(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);

/* Bit i of out is set if values[i] is not 0. Writes (count + 63) / 64 words, and bits past count are clear. */
void bitmask_from_nonzero_u8_batch(const uint8_t *values, uint64_t count, uint64_t *out);

/* The same for 16-bit elements, such as a section's block states. */
void bitmask_from_nonzero_u16_batch(const uint16_t *values, uint64_t count, uint64_t *out);

/* Bit i of out is set if values[i] is value. Writes (count + 63) / 64 words, and bits past count are clear. */
void bitmask_from_equal_u16_batch(const uint16_t *values, uint64_t count, uint16_t value, uint64_t *out);

#endif
