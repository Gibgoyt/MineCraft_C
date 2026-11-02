/* See README.md. The Owner tier of the kernels in asm/: the one place with memory to lend them. */
#include "simd/index.h"

#include "log/index.h"
#include "simd/bitmask/index.h"
#include "simd/blend/index.h"
#include "simd/copy/index.h"
#include "simd/noise/index.h"
#include "simd/reference/index.h"
#include "simd/vec_batch/index.h"

#include <string.h>

enum : uint64_t {
    /* The smallest region there is. Only the pages the self-check touches cost anything. */
    SIMD_SCRATCH_BYTES = HEAP_REGION_ALIGNMENT,
    /* Long enough to pass through every path of every kernel: bitmask_popcount_batch's first widening takes 992 bytes. */
    SIMD_CHECK_BYTES = 8192u,
    SIMD_CHECK_WORDS = SIMD_CHECK_BYTES / sizeof(uint64_t),
    SIMD_CHECK_PIXELS = SIMD_CHECK_BYTES / sizeof(uint32_t),
    SIMD_CHECK_STATES = SIMD_CHECK_BYTES / sizeof(uint16_t),
    SIMD_CHECK_ALIGNMENT = 64u,
    SIMD_CHECK_ROWS = 8u,
    SIMD_CHECK_RANDOM_SEED = 0x9E3779B97F4A7C15ull,
    SIMD_CHECK_MISALIGNMENTS = 8u,
    SIMD_CHECK_SHORT_BY = 3u, /* leaves an odd remainder, so that each kernel's tail runs too */
    SIMD_CHECK_FLOATS = SIMD_CHECK_BYTES / sizeof(float32_t),
    SIMD_CHECK_NOISE_POINTS = SIMD_CHECK_FLOATS / 3u,
    SIMD_CHECK_VEC3S = SIMD_CHECK_BYTES / sizeof(vec3_t),
    SIMD_CHECK_VEC4S = SIMD_CHECK_BYTES / sizeof(vec4_t),
    SIMD_CHECK_MAT4_FLOATS = 16u,
    SIMD_CHECK_NOISE_SEED = 0x51D7A3C9u,
};

/* What the checks run on: two inputs, and two outputs to compare. */
typedef struct simd_check_t {
    uint8_t *borrowed_a;
    uint8_t *borrowed_b;
    uint8_t *borrowed_got;
    uint8_t *borrowed_want;
} simd_check_t;
static_assert(sizeof(simd_check_t) == 32);

/* Marsaglia's xorshift64: anything will do that comes out the same on every run. */
static void simd_check_fill(uint8_t *bytes, uint64_t seed) {
    uint64_t state = seed;
    for (uint64_t i = 0; i < SIMD_CHECK_WORDS; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        memcpy(bytes + i * sizeof(uint64_t), &state, sizeof(uint64_t));
    }
}

static bool simd_check_same(const simd_check_t *check, const char *kernel) {
    if (memcmp(check->borrowed_got, check->borrowed_want, SIMD_CHECK_BYTES) != 0) {
        LOG_ERROR("simd: %s differs from its scalar reference", kernel);
        return false;
    }
    return true;
}

/* Both outputs start the same, so anything a kernel should have left alone has to stay equal too. */
static void simd_check_reset_outputs(const simd_check_t *check) {
    memcpy(check->borrowed_got, check->borrowed_b, SIMD_CHECK_BYTES);
    memcpy(check->borrowed_want, check->borrowed_b, SIMD_CHECK_BYTES);
}

static bool simd_check_copy(const simd_check_t *check) {
    simd_check_reset_outputs(check);
    /* A dst off its alignment, and a length that ends off it too, to take every head and tail step. */
    for (uint64_t offset = 0; offset < SIMD_CHECK_MISALIGNMENTS; offset++) {
        const uint64_t bytes = SIMD_CHECK_BYTES / 2u - offset * 5u;
        copy_stream_batch(check->borrowed_got + offset, check->borrowed_a, bytes);
        simd_reference_copy_stream(check->borrowed_want + offset, check->borrowed_a, bytes);
        if (!simd_check_same(check, "copy_stream_batch")) {
            return false;
        }
    }
    simd_check_reset_outputs(check);
    const uint64_t pitch = SIMD_CHECK_BYTES / SIMD_CHECK_ROWS;
    copy_stream_rows_batch(check->borrowed_got, pitch, check->borrowed_a, pitch, pitch - 32u, SIMD_CHECK_ROWS);
    simd_reference_copy_stream_rows(check->borrowed_want, pitch, check->borrowed_a, pitch, pitch - 32u,
                                    SIMD_CHECK_ROWS);
    return simd_check_same(check, "copy_stream_rows_batch");
}

typedef void (*simd_blend_t)(uint32_t *dst, const uint32_t *src, uint64_t count);

/* Random alpha takes the arithmetic path and, at the end, the masked one. Then a source of runs of fully transparent and
 * fully opaque pixels, for the two paths that skip the arithmetic. That second pass rewrites the source. */
static bool simd_check_blend(const simd_check_t *check, simd_blend_t kernel, simd_blend_t reference, const char *name) {
    uint32_t *src = (uint32_t *)(void *)check->borrowed_a;
    uint32_t *got = (uint32_t *)(void *)check->borrowed_got;
    uint32_t *want = (uint32_t *)(void *)check->borrowed_want;
    simd_check_fill(check->borrowed_a, SIMD_CHECK_RANDOM_SEED);
    simd_check_reset_outputs(check);
    kernel(got, src, SIMD_CHECK_PIXELS - SIMD_CHECK_SHORT_BY);
    reference(want, src, SIMD_CHECK_PIXELS - SIMD_CHECK_SHORT_BY);
    if (!simd_check_same(check, name)) {
        return false;
    }
    for (uint64_t i = 0; i < SIMD_CHECK_PIXELS; i++) {
        src[i] = (i / 16u) % 2u == 0u ? 0u : (src[i] | 0xFF000000u);
    }
    simd_check_reset_outputs(check);
    kernel(got, src, SIMD_CHECK_PIXELS);
    reference(want, src, SIMD_CHECK_PIXELS);
    return simd_check_same(check, name);
}

typedef void (*simd_binary_t)(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);

static bool simd_check_binary(const simd_check_t *check, simd_binary_t kernel, simd_binary_t reference,
                              const char *name) {
    const uint64_t *a = (const uint64_t *)(const void *)check->borrowed_a;
    const uint64_t *b = (const uint64_t *)(const void *)check->borrowed_b;
    simd_check_reset_outputs(check);
    kernel((uint64_t *)(void *)check->borrowed_got, a, b, SIMD_CHECK_WORDS - SIMD_CHECK_SHORT_BY);
    reference((uint64_t *)(void *)check->borrowed_want, a, b, SIMD_CHECK_WORDS - SIMD_CHECK_SHORT_BY);
    return simd_check_same(check, name);
}

static bool simd_check_bitmask(const simd_check_t *check) {
    simd_check_fill(check->borrowed_a, SIMD_CHECK_RANDOM_SEED);
    const uint64_t *words = (const uint64_t *)(const void *)check->borrowed_a;
    if (bitmask_popcount_batch(words, SIMD_CHECK_WORDS - SIMD_CHECK_SHORT_BY) !=
        simd_reference_bitmask_popcount(words, SIMD_CHECK_WORDS - SIMD_CHECK_SHORT_BY)) {
        LOG_ERROR("simd: bitmask_popcount_batch differs from its scalar reference");
        return false;
    }
    if (!simd_check_binary(check, bitmask_and_batch, simd_reference_bitmask_and, "bitmask_and_batch") ||
        !simd_check_binary(check, bitmask_or_batch, simd_reference_bitmask_or, "bitmask_or_batch") ||
        !simd_check_binary(check, bitmask_andnot_batch, simd_reference_bitmask_andnot, "bitmask_andnot_batch")) {
        return false;
    }

    /* Inputs with zeros in them, which random bytes hardly have. */
    uint8_t *values = check->borrowed_a;
    for (uint64_t i = 0; i < SIMD_CHECK_BYTES; i++) {
        values[i] &= 0x03u;
    }
    uint64_t *got = (uint64_t *)(void *)check->borrowed_got;
    uint64_t *want = (uint64_t *)(void *)check->borrowed_want;
    const uint16_t *states = (const uint16_t *)(const void *)values;
    const uint64_t state_count = SIMD_CHECK_STATES - SIMD_CHECK_SHORT_BY;
    const uint64_t byte_count = SIMD_CHECK_BYTES - SIMD_CHECK_SHORT_BY;

    simd_check_reset_outputs(check);
    bitmask_from_nonzero_u8_batch(values, byte_count, got);
    simd_reference_bitmask_from_nonzero_u8(values, byte_count, want);
    if (!simd_check_same(check, "bitmask_from_nonzero_u8_batch")) {
        return false;
    }
    simd_check_reset_outputs(check);
    bitmask_from_nonzero_u16_batch(states, state_count, got);
    simd_reference_bitmask_from_nonzero_u16(states, state_count, want);
    if (!simd_check_same(check, "bitmask_from_nonzero_u16_batch")) {
        return false;
    }
    simd_check_reset_outputs(check);
    bitmask_from_equal_u16_batch(states, state_count, 0x0101u, got);
    simd_reference_bitmask_from_equal_u16(states, state_count, 0x0101u, want);
    return simd_check_same(check, "bitmask_from_equal_u16_batch");
}

/* Finite values with a fraction, on both sides of zero: what the arithmetic kernels are for. Random bytes would mostly be
 * huge, and sometimes not numbers at all. */
static void simd_check_fill_floats(const simd_check_t *check) {
    float32_t *values = (float32_t *)(void *)check->borrowed_a;
    uint64_t state = SIMD_CHECK_RANDOM_SEED;
    for (uint64_t i = 0; i < SIMD_CHECK_FLOATS; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        values[i] = (float32_t)(int32_t)(state >> 44) * 0.001f32 - 500.0f32;
    }
}

static bool simd_check_noise(const simd_check_t *check) {
    simd_check_fill_floats(check);
    const float32_t *xs = (const float32_t *)(const void *)check->borrowed_a;
    const float32_t *ys = xs + SIMD_CHECK_NOISE_POINTS;
    const float32_t *zs = ys + SIMD_CHECK_NOISE_POINTS;
    const uint64_t count = SIMD_CHECK_NOISE_POINTS - SIMD_CHECK_SHORT_BY;
    simd_check_reset_outputs(check);
    noise3_batch8((float32_t *)(void *)check->borrowed_got, xs, ys, zs, count, SIMD_CHECK_NOISE_SEED);
    simd_reference_noise3((float32_t *)(void *)check->borrowed_want, xs, ys, zs, count, SIMD_CHECK_NOISE_SEED);
    return simd_check_same(check, "noise3_batch8");
}

static bool simd_check_vec_batch(const simd_check_t *check) {
    simd_check_fill_floats(check);
    mat4_t m;
    for (uint32_t i = 0; i < SIMD_CHECK_MAT4_FLOATS; i++) {
        m.m[i] = ((float32_t)i - 7.5f32) * 0.25f32; /* no zero and no two alike, so a mixed-up element shows */
    }
    const vec3_t *in3 = (const vec3_t *)(const void *)check->borrowed_a;
    const vec4_t *in4 = (const vec4_t *)(const void *)check->borrowed_a;
    vec3_t *got3 = (vec3_t *)(void *)check->borrowed_got;
    vec3_t *want3 = (vec3_t *)(void *)check->borrowed_want;
    vec4_t *got4 = (vec4_t *)(void *)check->borrowed_got;
    vec4_t *want4 = (vec4_t *)(void *)check->borrowed_want;
    const uint64_t count3 = SIMD_CHECK_VEC3S - SIMD_CHECK_SHORT_BY;
    const uint64_t count4 = SIMD_CHECK_VEC4S - SIMD_CHECK_SHORT_BY;

    simd_check_reset_outputs(check);
    vec_batch_transform_points(got3, in3, count3, &m);
    simd_reference_vec_batch_transform_points(want3, in3, count3, &m);
    if (!simd_check_same(check, "vec_batch_transform_points")) {
        return false;
    }
    simd_check_reset_outputs(check);
    vec_batch_transform_directions(got3, in3, count3, &m);
    simd_reference_vec_batch_transform_directions(want3, in3, count3, &m);
    if (!simd_check_same(check, "vec_batch_transform_directions")) {
        return false;
    }
    simd_check_reset_outputs(check);
    vec_batch_mul_vec4(got4, in4, count4, &m);
    simd_reference_vec_batch_mul_vec4(want4, in4, count4, &m);
    return simd_check_same(check, "vec_batch_mul_vec4");
}

bool simd_self_check(simd_t *simd) {
    arena_reset(&simd->arena_scratch);
    const simd_check_t check = {
        .borrowed_a = arena_alloc(&simd->arena_scratch, SIMD_CHECK_BYTES, SIMD_CHECK_ALIGNMENT),
        .borrowed_b = arena_alloc(&simd->arena_scratch, SIMD_CHECK_BYTES, SIMD_CHECK_ALIGNMENT),
        .borrowed_got = arena_alloc(&simd->arena_scratch, SIMD_CHECK_BYTES, SIMD_CHECK_ALIGNMENT),
        .borrowed_want = arena_alloc(&simd->arena_scratch, SIMD_CHECK_BYTES, SIMD_CHECK_ALIGNMENT),
    };
    simd_check_fill(check.borrowed_a, SIMD_CHECK_RANDOM_SEED);
    simd_check_fill(check.borrowed_b, ~(uint64_t)SIMD_CHECK_RANDOM_SEED);

    const bool ok = simd_check_copy(&check) &&
                    simd_check_blend(&check, blend_over_batch, simd_reference_blend_over, "blend_over_batch") &&
                    simd_check_blend(&check, blend_premul_batch, simd_reference_blend_premul, "blend_premul_batch") &&
                    simd_check_bitmask(&check) && simd_check_noise(&check) && simd_check_vec_batch(&check);
    arena_reset(&simd->arena_scratch);
    return ok;
}

bool simd_init(simd_t *simd) {
    memset(simd, 0, sizeof(*simd));
    if (!heap_reserve(SIMD_SCRATCH_BYTES, &simd->owned_region)) {
        return false; /* heap_reserve has said why */
    }
    arena_init(&simd->arena_scratch, "simd scratch", simd->owned_region.owned_base, simd->owned_region.bytes);
    if (!simd_self_check(simd)) {
        simd_deinit(simd);
        return false;
    }
    return true;
}

void simd_deinit(simd_t *simd) {
    arena_deinit(&simd->arena_scratch);
    heap_release(&simd->owned_region);
}
