/* Prototypes of asm/src/noise/index.asm. See its README.md. Scalar reference: simd/reference/index.h. */
#ifndef MC_RUN_SIMD_NOISE_INDEX_H
#define MC_RUN_SIMD_NOISE_INDEX_H

#include "types/index.h"

#include <stdint.h>

/* Gradient noise at each point (xs[i], ys[i], zs[i]), for count points, 8 at a time. Roughly from -1 to 1. It is 0 at
 * every whole-number point, and the same seed and point give the same bits on every run, for any count and any offset
 * into the arrays.
 *
 * No pointer needs more than float32_t alignment, and nothing past count is touched. out may be the same memory as an
 * input, but may not otherwise overlap one. Points must be finite: from anything else out is a NaN. A coordinate too large
 * for an int32_t still gives a repeatable result, though not a useful one, since a float32_t that large has no fraction. */
void noise3_batch8(float32_t *out, const float32_t *xs, const float32_t *ys, const float32_t *zs, uint64_t count,
                   uint32_t seed);

#endif
