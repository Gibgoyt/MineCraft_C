/* See README.md. Plain loops: the build's -fno-tree-vectorize keeps these scalar, which is the point of them. */
#include "simd/reference/index.h"

#include <stdbit.h>

enum : uint32_t {
    CHANNEL_MAX = 255u,
    CHANNEL_BITS = 8u,
    CHANNEL_COUNT = 4u,
    COLOUR_CHANNELS = 3u,
    ALPHA_SHIFT = 24u,
    WORD_BITS = 64u,
};

void simd_reference_copy_stream(uint8_t *dst, const uint8_t *src, uint64_t bytes) {
    for (uint64_t i = 0; i < bytes; i++) {
        dst[i] = src[i];
    }
}

void simd_reference_copy_stream_rows(uint8_t *dst, uint64_t dst_pitch, const uint8_t *src, uint64_t src_pitch,
                                     uint64_t row_bytes, uint64_t rows) {
    for (uint64_t row = 0; row < rows; row++) {
        simd_reference_copy_stream(dst + row * dst_pitch, src + row * src_pitch, row_bytes);
    }
}

/* x / 255, rounded to nearest, exactly, for x up to 255 * 255. */
static uint32_t reference_div255(uint32_t x) {
    const uint32_t rounded = x + 128u;
    return (rounded + (rounded >> CHANNEL_BITS)) >> CHANNEL_BITS;
}

void simd_reference_blend_over(uint32_t *dst, const uint32_t *src, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        const uint32_t s = src[i];
        const uint32_t d = dst[i];
        const uint32_t alpha = s >> ALPHA_SHIFT;
        uint32_t out = d & ((uint32_t)CHANNEL_MAX << ALPHA_SHIFT);
        for (uint32_t c = 0; c < COLOUR_CHANNELS; c++) {
            const uint32_t shift = c * CHANNEL_BITS;
            const uint32_t s_channel = (s >> shift) & CHANNEL_MAX;
            const uint32_t d_channel = (d >> shift) & CHANNEL_MAX;
            out |= reference_div255(s_channel * alpha + d_channel * (CHANNEL_MAX - alpha)) << shift;
        }
        dst[i] = out;
    }
}

void simd_reference_blend_premul(uint32_t *dst, const uint32_t *src, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        const uint32_t s = src[i];
        const uint32_t d = dst[i];
        const uint32_t alpha = s >> ALPHA_SHIFT;
        uint32_t out = 0;
        for (uint32_t c = 0; c < CHANNEL_COUNT; c++) {
            const uint32_t shift = c * CHANNEL_BITS;
            const uint32_t s_channel = (s >> shift) & CHANNEL_MAX;
            const uint32_t d_channel = (d >> shift) & CHANNEL_MAX;
            const uint32_t sum = s_channel + reference_div255(d_channel * (CHANNEL_MAX - alpha));
            out |= (sum < CHANNEL_MAX ? sum : (uint32_t)CHANNEL_MAX) << shift;
        }
        dst[i] = out;
    }
}

uint64_t simd_reference_bitmask_popcount(const uint64_t *words, uint64_t count) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < count; i++) {
        total += stdc_count_ones(words[i]);
    }
    return total;
}

void simd_reference_bitmask_and(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        dst[i] = a[i] & b[i];
    }
}

void simd_reference_bitmask_or(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        dst[i] = a[i] | b[i];
    }
}

void simd_reference_bitmask_andnot(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        dst[i] = a[i] & ~b[i];
    }
}

void simd_reference_bitmask_from_nonzero_u8(const uint8_t *values, uint64_t count, uint64_t *out) {
    for (uint64_t i = 0; i < count; i += WORD_BITS) {
        uint64_t word = 0;
        for (uint64_t bit = 0; bit < WORD_BITS && i + bit < count; bit++) {
            word |= (uint64_t)(values[i + bit] != 0u) << bit;
        }
        out[i / WORD_BITS] = word;
    }
}

void simd_reference_bitmask_from_nonzero_u16(const uint16_t *values, uint64_t count, uint64_t *out) {
    for (uint64_t i = 0; i < count; i += WORD_BITS) {
        uint64_t word = 0;
        for (uint64_t bit = 0; bit < WORD_BITS && i + bit < count; bit++) {
            word |= (uint64_t)(values[i + bit] != 0u) << bit;
        }
        out[i / WORD_BITS] = word;
    }
}

void simd_reference_bitmask_from_equal_u16(const uint16_t *values, uint64_t count, uint16_t value, uint64_t *out) {
    for (uint64_t i = 0; i < count; i += WORD_BITS) {
        uint64_t word = 0;
        for (uint64_t bit = 0; bit < WORD_BITS && i + bit < count; bit++) {
            word |= (uint64_t)(values[i + bit] == value) << bit;
        }
        out[i / WORD_BITS] = word;
    }
}
