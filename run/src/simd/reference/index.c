/* See README.md. Plain loops: the build's -fno-tree-vectorize keeps these scalar, which is the point of them. */
#include "simd/reference/index.h"

#include "math/mat4/index.h"

#include <math.h>
#include <stdbit.h>

enum : uint32_t {
    CHANNEL_MAX = 255u,
    CHANNEL_BITS = 8u,
    CHANNEL_COUNT = 4u,
    COLOUR_CHANNELS = 3u,
    ALPHA_SHIFT = 24u,
    WORD_BITS = 64u,
};

/* What asm/include/noise/index.inc holds as well. See reference_noise3_one. */
enum : uint32_t {
    NOISE_HASH_X = 0x8DA6B343u,
    NOISE_HASH_Y = 0xD8163841u,
    NOISE_HASH_Z = 0xCB1AB31Fu,
    NOISE_HASH_MIX_1 = 0x2C1B3C6Du,
    NOISE_HASH_MIX_2 = 0x297A2D39u,
    NOISE_HASH_SHIFT_1 = 15u,
    NOISE_HASH_SHIFT_2 = 12u,
    NOISE_BUCKET_SHIFT = 28u,
    NOISE_CELL_OUT_OF_RANGE = 0x80000000u,
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

/* The whole-number cell, as a wrapping integer. What vcvttps2dq does, spelled out, since C leaves a cast of a value out of
 * range undefined. */
static uint32_t reference_noise_cell(float32_t floored) {
    if (floored >= -2147483648.0f32 && floored < 2147483648.0f32) {
        return (uint32_t)(int32_t)floored;
    }
    return NOISE_CELL_OUT_OF_RANGE;
}

/* Perlin's improved fade, 6t^5 - 15t^4 + 10t^3. Each fmaf is a vfmadd in the kernel. */
static float32_t reference_noise_fade(float32_t t) {
    return (t * t * t) * fmaf(fmaf(t, 6.0f32, -15.0f32), t, 10.0f32);
}

static float32_t reference_noise_lerp(float32_t t, float32_t a, float32_t b) {
    return fmaf(t, b - a, a);
}

/* The dot product of a corner's gradient with its distance vector. The hash's top 4 bits pick the gradient: Perlin's
 * improved set, the twelve edges of a cube and four of them again. terms is the seed and the three axis terms, xored. */
static float32_t reference_noise_corner(uint32_t terms, float32_t x, float32_t y, float32_t z) {
    const uint32_t first_round = (terms ^ (terms >> NOISE_HASH_SHIFT_1)) * NOISE_HASH_MIX_1;
    const uint32_t hash = (first_round ^ (first_round >> NOISE_HASH_SHIFT_2)) * NOISE_HASH_MIX_2;
    const uint32_t bucket = hash >> NOISE_BUCKET_SHIFT;
    const float32_t first = bucket < 8u ? x : y;
    const float32_t second = bucket < 4u ? y : ((bucket == 12u || bucket == 14u) ? x : z);
    return ((bucket & 1u) != 0u ? -first : first) + ((bucket & 2u) != 0u ? -second : second);
}

static float32_t reference_noise3_one(float32_t x, float32_t y, float32_t z, uint32_t seed) {
    const float32_t floor_x = floorf(x);
    const float32_t floor_y = floorf(y);
    const float32_t floor_z = floorf(z);
    const float32_t fx = x - floor_x;
    const float32_t fy = y - floor_y;
    const float32_t fz = z - floor_z;
    const float32_t gx = fx - 1.0f32;
    const float32_t gy = fy - 1.0f32;
    const float32_t gz = fz - 1.0f32;
    const float32_t u = reference_noise_fade(fx);
    const float32_t v = reference_noise_fade(fy);
    const float32_t w = reference_noise_fade(fz);

    /* Each axis's term at both ends of the cell. Unsigned, because cell + 1 and the products are meant to wrap. */
    const uint32_t x0 = reference_noise_cell(floor_x) * NOISE_HASH_X;
    const uint32_t x1 = x0 + NOISE_HASH_X;
    const uint32_t y0 = reference_noise_cell(floor_y) * NOISE_HASH_Y;
    const uint32_t y1 = y0 + NOISE_HASH_Y;
    const uint32_t z0 = reference_noise_cell(floor_z) * NOISE_HASH_Z;
    const uint32_t z1 = z0 + NOISE_HASH_Z;
    const uint32_t yz00 = y0 ^ z0 ^ seed;
    const uint32_t yz10 = y1 ^ z0 ^ seed;
    const uint32_t yz01 = y0 ^ z1 ^ seed;
    const uint32_t yz11 = y1 ^ z1 ^ seed;

    const float32_t l00 = reference_noise_lerp(u, reference_noise_corner(x0 ^ yz00, fx, fy, fz),
                                               reference_noise_corner(x1 ^ yz00, gx, fy, fz));
    const float32_t l10 = reference_noise_lerp(u, reference_noise_corner(x0 ^ yz10, fx, gy, fz),
                                               reference_noise_corner(x1 ^ yz10, gx, gy, fz));
    const float32_t l01 = reference_noise_lerp(u, reference_noise_corner(x0 ^ yz01, fx, fy, gz),
                                               reference_noise_corner(x1 ^ yz01, gx, fy, gz));
    const float32_t l11 = reference_noise_lerp(u, reference_noise_corner(x0 ^ yz11, fx, gy, gz),
                                               reference_noise_corner(x1 ^ yz11, gx, gy, gz));
    return reference_noise_lerp(w, reference_noise_lerp(v, l00, l10), reference_noise_lerp(v, l01, l11));
}

void simd_reference_noise3(float32_t *out, const float32_t *xs, const float32_t *ys, const float32_t *zs,
                           uint64_t count, uint32_t seed) {
    for (uint64_t i = 0; i < count; i++) {
        out[i] = reference_noise3_one(xs[i], ys[i], zs[i], seed);
    }
}

void simd_reference_vec_batch_transform_points(vec3_t *out, const vec3_t *in, uint64_t count, const mat4_t *m) {
    for (uint64_t i = 0; i < count; i++) {
        out[i] = mat4_transform_point(m, in[i]);
    }
}

void simd_reference_vec_batch_transform_directions(vec3_t *out, const vec3_t *in, uint64_t count, const mat4_t *m) {
    for (uint64_t i = 0; i < count; i++) {
        out[i] = mat4_transform_direction(m, in[i]);
    }
}

void simd_reference_vec_batch_mul_vec4(vec4_t *out, const vec4_t *in, uint64_t count, const mat4_t *m) {
    for (uint64_t i = 0; i < count; i++) {
        out[i] = mat4_mul_vec4(m, in[i]);
    }
}
