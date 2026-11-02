/* Prototypes of asm/src/copy/index.asm. See its README.md. Scalar references: simd/reference/index.h. */
#ifndef MC_RUN_SIMD_COPY_INDEX_H
#define MC_RUN_SIMD_COPY_INDEX_H

#include <stdint.h>

/* Copies bytes from src to dst, then fences, so a page flip issued afterwards cannot overtake the data. The two must not
 * overlap. Neither needs any alignment, though a dst on a 32-byte boundary spends nothing on getting to one. */
void copy_stream_batch(uint8_t *dst, const uint8_t *src, uint64_t bytes);

/* The same for a rectangle of rows lines of row_bytes each, stepping by each side's pitch. A pitch wider than row_bytes
 * leaves the bytes between rows alone. There is one fence, at the end, which is why this is not a C loop over the above. */
void copy_stream_rows_batch(uint8_t *dst, uint64_t dst_pitch, const uint8_t *src, uint64_t src_pitch,
                            uint64_t row_bytes, uint64_t rows);

#endif
