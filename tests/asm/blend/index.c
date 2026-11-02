/* ctest asm_blend. See README.md. */
#include "asm/support/index.h"

#include "simd/blend/index.h"
#include "simd/reference/index.h"

#include <string.h>

enum : uint64_t {
    VALUES = 256u,
    TABLE_PIXELS = VALUES * VALUES,
    CANARY_PIXELS = 16u,
    MAX_SHORT_PIXELS = 80u,
    MIXED_PIXELS = 4096u,
};
enum : uint32_t { CANARY = 0xDEADBEEFu };

typedef void (*blend_t)(uint32_t *dst, const uint32_t *src, uint64_t count);

/* x / 255 to nearest, by plain division, as the definition the reference's shift trick has to meet. */
static uint32_t by_division(uint32_t x) {
    return (2u * x + 255u) / 510u;
}

static uint32_t pixel(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

TEST(reference_over_gives_the_known_answers) {
    uint32_t dst = pixel(0x7Fu, 0u, 100u, 255u);
    const uint32_t src = pixel(128u, 255u, 100u, 0u);
    simd_reference_blend_over(&dst, &src, 1u);
    /* 255 * 128 / 255 = 128; 100 stays 100; 255 * 127 / 255 = 127. The top byte stays. */
    CHECK_EQ_U64(dst, pixel(0x7Fu, 128u, 100u, 127u));
}

TEST(reference_premul_gives_the_known_answers) {
    uint32_t dst = pixel(255u, 200u, 100u, 0u);
    const uint32_t src = pixel(128u, 128u, 0u, 200u);
    simd_reference_blend_premul(&dst, &src, 1u);
    /* Alpha: 128 + 127. Red: 128 + 200 * 127 / 255 = 128 + 100. Green: 0 + 50. Blue: 200 + 0. */
    CHECK_EQ_U64(dst, pixel(255u, 228u, 50u, 200u));
    /* The sum stops at 255. */
    dst = pixel(255u, 255u, 255u, 255u);
    const uint32_t bright = pixel(128u, 255u, 255u, 255u);
    simd_reference_blend_premul(&dst, &bright, 1u);
    CHECK_EQ_U64(dst, pixel(255u, 255u, 255u, 255u));
}

/* Every source, destination and alpha, on all three colour channels. 2^24 cases: one table per alpha. */
TEST(over_is_right_for_every_source_destination_and_alpha) {
    uint32_t *src = test_alloc(t, TABLE_PIXELS * sizeof(uint32_t));
    uint32_t *got = test_alloc(t, TABLE_PIXELS * sizeof(uint32_t));
    uint32_t *want = test_alloc(t, TABLE_PIXELS * sizeof(uint32_t));
    for (uint32_t alpha = 0; alpha < VALUES; alpha++) {
        for (uint32_t s = 0; s < VALUES; s++) {
            for (uint32_t d = 0; d < VALUES; d++) {
                const uint64_t i = s * VALUES + d;
                src[i] = pixel(alpha, s, 255u - s, s ^ d);
                got[i] = pixel(s, d, 255u - d, d);
                want[i] = (got[i] & 0xFF000000u) | (by_division(s * alpha + d * (255u - alpha)) << 16) |
                          (by_division((255u - s) * alpha + (255u - d) * (255u - alpha)) << 8) |
                          by_division((s ^ d) * alpha + d * (255u - alpha));
            }
        }
        blend_over_batch(got, src, TABLE_PIXELS);
        if (!CHECK_MEM_EQ(got, want, TABLE_PIXELS * sizeof(uint32_t))) {
            return;
        }
        /* The reference has to meet the same definition. */
        for (uint64_t i = 0; i < TABLE_PIXELS; i++) {
            got[i] = pixel((uint32_t)(i >> 8), (uint32_t)i & 255u, 255u - ((uint32_t)i & 255u), (uint32_t)i & 255u);
        }
        simd_reference_blend_over(got, src, TABLE_PIXELS);
        if (!CHECK_MEM_EQ(got, want, TABLE_PIXELS * sizeof(uint32_t))) {
            return;
        }
    }
}

TEST(premul_is_right_for_every_destination_and_alpha_and_a_spread_of_sources) {
    uint32_t *src = test_alloc(t, TABLE_PIXELS * sizeof(uint32_t));
    uint32_t *got = test_alloc(t, TABLE_PIXELS * sizeof(uint32_t));
    uint32_t *want = test_alloc(t, TABLE_PIXELS * sizeof(uint32_t));
    for (uint32_t s = 0; s < VALUES; s += 5u) {
        for (uint32_t alpha = 0; alpha < VALUES; alpha++) {
            for (uint32_t d = 0; d < VALUES; d++) {
                const uint64_t i = alpha * VALUES + d;
                src[i] = pixel(alpha, s, 255u - s, s);
                got[i] = pixel(d, d, d, d);
                const uint32_t scaled = by_division(d * (255u - alpha));
                const uint32_t red = s + scaled;
                const uint32_t green = 255u - s + scaled;
                const uint32_t alpha_sum = alpha + scaled;
                want[i] = ((alpha_sum < 255u ? alpha_sum : 255u) << 24) | ((red < 255u ? red : 255u) << 16) |
                          ((green < 255u ? green : 255u) << 8) | (red < 255u ? red : 255u);
            }
        }
        blend_premul_batch(got, src, TABLE_PIXELS);
        if (!CHECK_MEM_EQ(got, want, TABLE_PIXELS * sizeof(uint32_t))) {
            return;
        }
    }
}

/* Runs of 8 and more in each state, so all the kernels' paths meet each other at every offset. */
static void fill_mixed(uint64_t *state, uint32_t *src, uint32_t *dst, uint64_t count) {
    uint64_t i = 0;
    while (i < count) {
        const uint64_t kind = test_random(state) % 4u;
        const uint64_t run = 1u + test_random(state) % 40u;
        for (uint64_t j = 0; j < run && i < count; j++, i++) {
            uint32_t value = (uint32_t)test_random(state);
            if (kind == 0u) {
                value &= 0x00FFFFFFu; /* transparent, with colour left in it */
            } else if (kind == 1u) {
                value |= 0xFF000000u;
            } else if (kind == 2u) {
                value = 0u;
            }
            src[i] = value;
            dst[i] = (uint32_t)test_random(state);
        }
    }
}

static void check_against_reference(test_context_t *t, blend_t kernel, blend_t reference) {
    uint64_t random_state = 0xB1E2Du;
    uint32_t *src = test_alloc(t, (MIXED_PIXELS + CANARY_PIXELS) * sizeof(uint32_t));
    uint32_t *got = test_alloc(t, (MIXED_PIXELS + CANARY_PIXELS) * sizeof(uint32_t));
    uint32_t *want = test_alloc(t, (MIXED_PIXELS + CANARY_PIXELS) * sizeof(uint32_t));
    for (uint32_t round = 0; round < 200u; round++) {
        const uint64_t count = test_random(&random_state) % MIXED_PIXELS;
        fill_mixed(&random_state, src, got, MIXED_PIXELS);
        memcpy(want, got, MIXED_PIXELS * sizeof(uint32_t));
        kernel(got, src, count);
        reference(want, src, count);
        if (!CHECK_MEM_EQ(got, want, MIXED_PIXELS * sizeof(uint32_t))) {
            return;
        }
    }
}

TEST(over_matches_its_reference_through_every_path) {
    check_against_reference(t, blend_over_batch, simd_reference_blend_over);
}

TEST(premul_matches_its_reference_through_every_path) {
    check_against_reference(t, blend_premul_batch, simd_reference_blend_premul);
}

/* A pixel past count stays as it was, and neither buffer is read past its end: both sit against a page that faults. */
static void check_short_runs(test_context_t *t, blend_t kernel, blend_t reference) {
    guard_t src_guard;
    guard_t dst_guard;
    REQUIRE(guard_init(&src_guard));
    REQUIRE(guard_init(&dst_guard));
    uint64_t random_state = 0xF00Du;
    uint32_t src_values[MAX_SHORT_PIXELS];
    uint32_t dst_values[MAX_SHORT_PIXELS + CANARY_PIXELS];
    uint32_t want[MAX_SHORT_PIXELS + CANARY_PIXELS];
    for (uint64_t count = 0; count <= MAX_SHORT_PIXELS; count++) {
        const uint64_t bytes = count * sizeof(uint32_t);
        fill_mixed(&random_state, src_values, dst_values, MAX_SHORT_PIXELS);
        memcpy(want, dst_values, bytes);
        reference(want, src_values, count);

        uint32_t *src_high = (uint32_t *)(void *)(src_guard.borrowed_high - bytes);
        uint32_t *dst_high = (uint32_t *)(void *)(dst_guard.borrowed_high - bytes);
        memcpy(src_high, src_values, bytes);
        memcpy(dst_high, dst_values, bytes);
        kernel(dst_high, src_high, count);
        CHECK_MEM_EQ(dst_high, want, bytes);

        uint32_t *src_low = (uint32_t *)(void *)src_guard.borrowed_low;
        uint32_t *dst_low = (uint32_t *)(void *)dst_guard.borrowed_low;
        memcpy(src_low, src_values, bytes);
        memcpy(dst_low, dst_values, bytes);
        for (uint64_t i = 0; i < CANARY_PIXELS; i++) {
            dst_low[count + i] = CANARY;
        }
        kernel(dst_low, src_low, count);
        CHECK_MEM_EQ(dst_low, want, bytes);
        for (uint64_t i = 0; i < CANARY_PIXELS; i++) {
            CHECK_EQ_U64(dst_low[count + i], CANARY);
        }
    }
    guard_deinit(&dst_guard);
    guard_deinit(&src_guard);
}

TEST(over_short_runs_stay_inside_their_buffers) {
    check_short_runs(t, blend_over_batch, simd_reference_blend_over);
}

TEST(premul_short_runs_stay_inside_their_buffers) {
    check_short_runs(t, blend_premul_batch, simd_reference_blend_premul);
}

/* Neither kernel may care where a pixel starts within a vector. */
TEST(unaligned_pointers_give_the_same_answer) {
    uint64_t random_state = 0xCAFEu;
    uint8_t *src_bytes = test_alloc(t, MIXED_PIXELS * sizeof(uint32_t) + 64u);
    uint8_t *got_bytes = test_alloc(t, MIXED_PIXELS * sizeof(uint32_t) + 64u);
    uint32_t *src_values = test_alloc(t, MIXED_PIXELS * sizeof(uint32_t));
    uint32_t *dst_values = test_alloc(t, MIXED_PIXELS * sizeof(uint32_t));
    uint32_t *want = test_alloc(t, MIXED_PIXELS * sizeof(uint32_t));
    fill_mixed(&random_state, src_values, dst_values, MIXED_PIXELS);
    memcpy(want, dst_values, MIXED_PIXELS * sizeof(uint32_t));
    simd_reference_blend_over(want, src_values, MIXED_PIXELS);
    for (uint64_t offset = 0; offset < 32u; offset += 4u) {
        memcpy(src_bytes + offset, src_values, MIXED_PIXELS * sizeof(uint32_t));
        memcpy(got_bytes + 32u - offset, dst_values, MIXED_PIXELS * sizeof(uint32_t));
        blend_over_batch((uint32_t *)(void *)(got_bytes + 32u - offset), (uint32_t *)(void *)(src_bytes + offset),
                         MIXED_PIXELS);
        CHECK_MEM_EQ(got_bytes + 32u - offset, want, MIXED_PIXELS * sizeof(uint32_t));
    }
}

TEST_MAIN()
