/* Prototypes of asm/src/blend/index.asm. See its README.md. Scalar references: simd/reference/index.h.
 *
 * A pixel is a uint32_t of 0xAARRGGBB. Neither pointer needs more than uint32_t alignment. The two runs must be the same
 * memory or not overlap at all, and the kernels never touch a pixel past count. */
#ifndef MC_RUN_SIMD_BLEND_INDEX_H
#define MC_RUN_SIMD_BLEND_INDEX_H

#include <stdint.h>

/* Straight alpha over the destination. For each pixel's red, green and blue:
 *     dst = (src * alpha + dst * (255 - alpha)) / 255, rounded to nearest.
 * The destination's top byte is left as it was, since a scanout frame's is unused. */
void blend_over_batch(uint32_t *dst, const uint32_t *src, uint64_t count);

/* A premultiplied source over the destination. For all four bytes of each pixel:
 *     dst = src + dst * (255 - alpha) / 255, with the division rounded to nearest and the sum stopping at 255.
 * The result is premultiplied too, so a layer can be built up and then blended onto the frame. */
void blend_premul_batch(uint32_t *dst, const uint32_t *src, uint64_t count);

#endif
