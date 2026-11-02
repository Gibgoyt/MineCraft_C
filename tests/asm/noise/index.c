/* ctest asm_noise. See README.md. */
#include "asm/support/index.h"

#include "simd/noise/index.h"
#include "simd/reference/index.h"
#include "types/index.h"

#include <math.h>
#include <string.h>

enum : uint64_t {
    MAX_SHORT_POINTS = 80u,
    CANARY_POINTS = 16u,
    MANY_POINTS = 20000u,
    STAT_POINTS = 1u << 18,
    UNALIGNED_POINTS = 1024u,
};
enum : uint32_t { CANARY = 0xDEADBEEFu, SEED = 0x51D7A3C9u };

/* One set of test points, in test memory. */
typedef struct points_t {
    float32_t *xs;
    float32_t *ys;
    float32_t *zs;
    float32_t *got;
    float32_t *want;
} points_t;
static_assert(sizeof(points_t) == 40);

static points_t points_alloc(test_context_t *t, uint64_t count) {
    return (points_t){
        .xs = test_alloc(t, count * sizeof(float32_t)),
        .ys = test_alloc(t, count * sizeof(float32_t)),
        .zs = test_alloc(t, count * sizeof(float32_t)),
        .got = test_alloc(t, count * sizeof(float32_t)),
        .want = test_alloc(t, count * sizeof(float32_t)),
    };
}

/* A float32_t from -scale to scale. */
static float32_t random_coordinate(uint64_t *state, float32_t scale) {
    const float32_t unit = (float32_t)(test_random(state) >> 40) / 16777216.0f32;
    return (unit * 2.0f32 - 1.0f32) * scale;
}

static void fill_points(points_t points, uint64_t *state, uint64_t count, float32_t scale) {
    for (uint64_t i = 0; i < count; i++) {
        points.xs[i] = random_coordinate(state, scale);
        points.ys[i] = random_coordinate(state, scale);
        points.zs[i] = random_coordinate(state, scale);
    }
}

static uint32_t bits_of(float32_t value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float32_t one_kernel(float32_t x, float32_t y, float32_t z, uint32_t seed) {
    float32_t out = 0.0f32;
    noise3_batch8(&out, &x, &y, &z, 1u, seed);
    return out;
}

static float32_t one_reference(float32_t x, float32_t y, float32_t z, uint32_t seed) {
    float32_t out = 0.0f32;
    simd_reference_noise3(&out, &x, &y, &z, 1u, seed);
    return out;
}

typedef struct known_value_t {
    float32_t x;
    float32_t y;
    float32_t z;
    uint32_t seed;
    uint32_t bits;
} known_value_t;
static_assert(sizeof(known_value_t) == 20);

/* Every world depends on these. A change that moves one has changed every world, so it must be on purpose. A separate
 * program with exact rational arithmetic gave the same bits when these were pinned: see the stage's Results. */
TEST(known_values_never_change) {
    static const known_value_t known[] = {
        {0.5f32, 0.5f32, 0.5f32, 0u, 0x3EC00000u},
        {0.25f32, 0.5f32, 0.75f32, 0u, 0xBE108380u},
        {-0.25f32, 12.5f32, -3.75f32, 1u, 0xBC87D600u},
        {1000.125f32, -2000.25f32, 3000.375f32, 0x51D7A3C9u, 0x3DC1DA48u},
        {-30000.75f32, 64.0625f32, 8191.5f32, 0xFFFFFFFFu, 0xBE96C60Du},
        {3.5f32, 3.5f32, 3.5f32, 0x80000000u, 0x00000000u},
    };
    for (uint64_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        CHECK_EQ_U64(bits_of(one_reference(known[i].x, known[i].y, known[i].z, known[i].seed)), known[i].bits);
        CHECK_EQ_U64(bits_of(one_kernel(known[i].x, known[i].y, known[i].z, known[i].seed)), known[i].bits);
    }
}

TEST(matches_its_reference_bit_for_bit_at_every_scale) {
    static const float32_t scales[] = {
        0.5f32, 2.0f32, 1000.0f32, 30000000.0f32, 2147483648.0f32, 1.0e30f32,
    };
    uint64_t state = 0xA11CEu;
    points_t points = points_alloc(t, MANY_POINTS);
    for (uint64_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++) {
        for (uint32_t round = 0; round < 8u; round++) {
            fill_points(points, &state, MANY_POINTS, scales[s]);
            const uint32_t seed = (uint32_t)test_random(&state);
            noise3_batch8(points.got, points.xs, points.ys, points.zs, MANY_POINTS, seed);
            simd_reference_noise3(points.want, points.xs, points.ys, points.zs, MANY_POINTS, seed);
            if (!CHECK_MEM_EQ(points.got, points.want, MANY_POINTS * sizeof(float32_t))) {
                return;
            }
        }
    }
}

/* Exactly where the cell, the fraction and the float-to-int conversion change behaviour. */
TEST(matches_its_reference_on_the_edges) {
    static const float32_t edges[] = {
        0.0f32,           -0.0f32,         1.0f32,           -1.0f32,        0.5f32,
        -0.5f32,          1.0e-30f32,      -1.0e-30f32,      1.0e-45f32,     -1.0e-45f32,
        0.99999994f32,    -0.99999994f32,  16777216.0f32,    -16777216.0f32, 2147483520.0f32,
        -2147483648.0f32, 2147483648.0f32, -2147483904.0f32, 3.0e38f32,      -3.0e38f32,
    };
    enum : uint64_t { EDGES = sizeof(edges) / sizeof(edges[0]), EDGE_POINTS = EDGES * EDGES * EDGES };
    points_t points = points_alloc(t, EDGE_POINTS);
    uint64_t i = 0;
    for (uint64_t a = 0; a < EDGES; a++) {
        for (uint64_t b = 0; b < EDGES; b++) {
            for (uint64_t c = 0; c < EDGES; c++) {
                points.xs[i] = edges[a];
                points.ys[i] = edges[b];
                points.zs[i] = edges[c];
                i++;
            }
        }
    }
    noise3_batch8(points.got, points.xs, points.ys, points.zs, EDGE_POINTS, SEED);
    simd_reference_noise3(points.want, points.xs, points.ys, points.zs, EDGE_POINTS, SEED);
    CHECK_MEM_EQ(points.got, points.want, EDGE_POINTS * sizeof(float32_t));
}

/* A point past count stays as it was, and no array is read past its end: each sits against a page that faults. */
TEST(short_runs_stay_inside_their_buffers) {
    guard_t guards[4];
    for (uint32_t i = 0; i < 4u; i++) {
        REQUIRE(guard_init(&guards[i]));
    }
    uint64_t state = 0xF00Du;
    float32_t xs[MAX_SHORT_POINTS];
    float32_t ys[MAX_SHORT_POINTS];
    float32_t zs[MAX_SHORT_POINTS];
    float32_t want[MAX_SHORT_POINTS];
    const points_t values = {.xs = xs, .ys = ys, .zs = zs, .got = nullptr, .want = want};
    for (uint64_t count = 0; count <= MAX_SHORT_POINTS; count++) {
        const uint64_t bytes = count * sizeof(float32_t);
        fill_points(values, &state, MAX_SHORT_POINTS, 100.0f32);
        simd_reference_noise3(want, xs, ys, zs, count, SEED);

        float32_t *high[4];
        for (uint32_t i = 0; i < 4u; i++) {
            high[i] = (float32_t *)(void *)(guards[i].borrowed_high - bytes);
        }
        memcpy(high[0], xs, bytes);
        memcpy(high[1], ys, bytes);
        memcpy(high[2], zs, bytes);
        noise3_batch8(high[3], high[0], high[1], high[2], count, SEED);
        CHECK_MEM_EQ(high[3], want, bytes);

        float32_t *low[4];
        for (uint32_t i = 0; i < 4u; i++) {
            low[i] = (float32_t *)(void *)guards[i].borrowed_low;
        }
        memcpy(low[0], xs, bytes);
        memcpy(low[1], ys, bytes);
        memcpy(low[2], zs, bytes);
        uint32_t *canary = (uint32_t *)(void *)(low[3] + count);
        for (uint64_t i = 0; i < CANARY_POINTS; i++) {
            canary[i] = CANARY;
        }
        noise3_batch8(low[3], low[0], low[1], low[2], count, SEED);
        CHECK_MEM_EQ(low[3], want, bytes);
        for (uint64_t i = 0; i < CANARY_POINTS; i++) {
            CHECK_EQ_U64(canary[i], CANARY);
        }
    }
    for (uint32_t i = 0; i < 4u; i++) {
        guard_deinit(&guards[i]);
    }
}

TEST(no_pointer_needs_to_be_aligned) {
    uint64_t state = 0xCAFEu;
    points_t points = points_alloc(t, UNALIGNED_POINTS);
    fill_points(points, &state, UNALIGNED_POINTS, 50.0f32);
    simd_reference_noise3(points.want, points.xs, points.ys, points.zs, UNALIGNED_POINTS, SEED);
    const uint64_t bytes = UNALIGNED_POINTS * sizeof(float32_t);
    uint8_t *xs = test_alloc(t, bytes + 64u);
    uint8_t *ys = test_alloc(t, bytes + 64u);
    uint8_t *zs = test_alloc(t, bytes + 64u);
    uint8_t *out = test_alloc(t, bytes + 64u);
    for (uint64_t offset = 0; offset < 32u; offset += 4u) {
        memcpy(xs + offset, points.xs, bytes);
        memcpy(ys + 28u - offset, points.ys, bytes);
        memcpy(zs + offset / 2u * 2u + 4u, points.zs, bytes);
        noise3_batch8((float32_t *)(void *)(out + offset), (float32_t *)(void *)(xs + offset),
                      (float32_t *)(void *)(ys + 28u - offset), (float32_t *)(void *)(zs + offset / 2u * 2u + 4u),
                      UNALIGNED_POINTS, SEED);
        CHECK_MEM_EQ(out + offset, points.want, bytes);
    }
}

TEST(out_may_be_an_input) {
    uint64_t state = 0xBEEFu;
    points_t points = points_alloc(t, UNALIGNED_POINTS + 3u);
    const uint64_t count = UNALIGNED_POINTS + 3u;
    fill_points(points, &state, count, 50.0f32);
    simd_reference_noise3(points.want, points.xs, points.ys, points.zs, count, SEED);
    noise3_batch8(points.xs, points.xs, points.ys, points.zs, count, SEED);
    CHECK_MEM_EQ(points.xs, points.want, count * sizeof(float32_t));
}

/* Which lane a point lands in, and how long the array is, may not matter. */
TEST(a_point_gives_the_same_bits_in_any_batch) {
    uint64_t state = 0xD00Du;
    enum : uint64_t { COUNT = 67u };
    points_t points = points_alloc(t, COUNT);
    fill_points(points, &state, COUNT, 200.0f32);
    noise3_batch8(points.got, points.xs, points.ys, points.zs, COUNT, SEED);
    for (uint64_t i = 0; i < COUNT; i++) {
        CHECK_EQ_U64(bits_of(one_kernel(points.xs[i], points.ys[i], points.zs[i], SEED)), bits_of(points.got[i]));
    }
}

TEST(zero_on_every_whole_number_point) {
    enum : uint64_t { COUNT = 1000u };
    points_t points = points_alloc(t, COUNT);
    for (uint64_t i = 0; i < COUNT; i++) {
        points.xs[i] = (float32_t)((int32_t)(i * 37u % 2001u) - 1000);
        points.ys[i] = (float32_t)((int32_t)(i * 91u % 2001u) - 1000);
        points.zs[i] = (float32_t)((int32_t)(i * 53u % 2001u) - 1000);
    }
    noise3_batch8(points.got, points.xs, points.ys, points.zs, COUNT, SEED);
    for (uint64_t i = 0; i < COUNT; i++) {
        CHECK(points.got[i] == 0.0f32);
    }
}

/* A cell's corner gradients are about sqrt(2) long and its halfway point about 0.87 from a corner, so 1 is the top. */
TEST(stays_in_range_and_uses_the_range) {
    uint64_t state = 0x7E57u;
    points_t points = points_alloc(t, STAT_POINTS);
    fill_points(points, &state, STAT_POINTS, 4000.0f32);
    noise3_batch8(points.got, points.xs, points.ys, points.zs, STAT_POINTS, SEED);
    float32_t lowest = 0.0f32;
    float32_t highest = 0.0f32;
    for (uint64_t i = 0; i < STAT_POINTS; i++) {
        lowest = points.got[i] < lowest ? points.got[i] : lowest;
        highest = points.got[i] > highest ? points.got[i] : highest;
    }
    CHECK(lowest >= -1.1f32);
    CHECK(highest <= 1.1f32);
    CHECK(lowest <= -0.6f32);
    CHECK(highest >= 0.6f32);
}

/* A short step gives a small change everywhere, including across a cell's boundary and through zero. */
TEST(has_no_seam_between_cells) {
    enum : uint64_t { COUNT = 50000u };
    const float32_t step = 1.0f32 / 1024.0f32;
    uint64_t state = 0x5EA3u;
    points_t points = points_alloc(t, COUNT);
    fill_points(points, &state, COUNT, 3.0f32);
    float32_t *stepped = test_alloc(t, COUNT * sizeof(float32_t));
    noise3_batch8(points.got, points.xs, points.ys, points.zs, COUNT, SEED);
    for (uint64_t i = 0; i < COUNT; i++) {
        /* Half the points are pushed to just below a whole number, so the step crosses it. */
        const float32_t x = (i & 1u) != 0u ? points.xs[i] : floorf(points.xs[i]) - step / 2.0f32;
        points.xs[i] = x;
        points.want[i] = x + step;
    }
    noise3_batch8(points.got, points.xs, points.ys, points.zs, COUNT, SEED);
    noise3_batch8(stepped, points.want, points.ys, points.zs, COUNT, SEED);
    for (uint64_t i = 0; i < COUNT; i++) {
        const float32_t change = fabsf(stepped[i] - points.got[i]);
        if (!CHECK(change <= 0.01f32)) {
            return;
        }
    }
}

/* Pearson correlation of a and b. Both have about zero mean, which the caller checks. */
static float64_t correlation(const float32_t *a, const float32_t *b, uint64_t count) {
    float64_t ab = 0.0f64;
    float64_t aa = 0.0f64;
    float64_t bb = 0.0f64;
    for (uint64_t i = 0; i < count; i++) {
        ab += (float64_t)a[i] * (float64_t)b[i];
        aa += (float64_t)a[i] * (float64_t)a[i];
        bb += (float64_t)b[i] * (float64_t)b[i];
    }
    return ab / sqrt(aa * bb);
}

/* Cell centres, one sample per cell. Neighbours share corners, so they do correlate a little by construction, but only
 * through the gradients. Anything over this limit means the hash has a pattern in it. */
TEST(the_hash_has_no_pattern_a_correlation_could_see) {
    enum : uint64_t { SIDE = 64u, COUNT = SIDE * SIDE * SIDE };
    points_t points = points_alloc(t, COUNT);
    float32_t *other = test_alloc(t, COUNT * sizeof(float32_t));
    float32_t *moved = test_alloc(t, COUNT * sizeof(float32_t));
    for (uint64_t i = 0; i < COUNT; i++) {
        points.xs[i] = (float32_t)((i % SIDE)) - 32.0f32 + 0.5f32;
        points.ys[i] = (float32_t)((i / SIDE) % SIDE) - 32.0f32 + 0.5f32;
        points.zs[i] = (float32_t)(i / SIDE / SIDE) - 32.0f32 + 0.5f32;
    }
    noise3_batch8(points.got, points.xs, points.ys, points.zs, COUNT, SEED);
    float64_t mean = 0.0f64;
    for (uint64_t i = 0; i < COUNT; i++) {
        mean += (float64_t)points.got[i];
    }
    mean /= (float64_t)COUNT;
    CHECK(fabs(mean) < 0.01f64);

    /* Against another seed, nearly nothing. Seeds next to each other are what a caller is likely to use. */
    static const uint32_t other_seeds[] = {
        SEED + 1u, SEED + 256u, SEED ^ 0x10000u, SEED ^ 0x80000000u, 0u, 0xFFFFFFFFu,
    };
    for (uint64_t s = 0; s < sizeof(other_seeds) / sizeof(other_seeds[0]); s++) {
        noise3_batch8(other, points.xs, points.ys, points.zs, COUNT, other_seeds[s]);
        CHECK(fabs(correlation(points.got, other, COUNT)) < 0.02f64);
    }

    /* The same seed with its axes swapped has to be a different field too, or x and y would look alike. Where x equals y
     * the swap changes nothing, and that is 1 cell in 64, so these start at about 0.016. */
    noise3_batch8(other, points.ys, points.xs, points.zs, COUNT, SEED);
    CHECK(fabs(correlation(points.got, other, COUNT)) < 0.05f64);
    noise3_batch8(other, points.zs, points.ys, points.xs, COUNT, SEED);
    CHECK(fabs(correlation(points.got, other, COUNT)) < 0.05f64);

    /* Against itself far away, on each axis: nothing repeats at these distances. */
    static const float32_t distances[] = {16.0f32, 256.0f32, 4096.0f32, 65536.0f32, 1048576.0f32};
    for (uint64_t d = 0; d < sizeof(distances) / sizeof(distances[0]); d++) {
        for (uint64_t i = 0; i < COUNT; i++) {
            moved[i] = points.xs[i] + distances[d];
        }
        noise3_batch8(other, moved, points.ys, points.zs, COUNT, SEED);
        CHECK(fabs(correlation(points.got, other, COUNT)) < 0.02f64);
        for (uint64_t i = 0; i < COUNT; i++) {
            moved[i] = points.ys[i] + distances[d];
        }
        noise3_batch8(other, points.xs, moved, points.zs, COUNT, SEED);
        CHECK(fabs(correlation(points.got, other, COUNT)) < 0.02f64);
        for (uint64_t i = 0; i < COUNT; i++) {
            moved[i] = points.zs[i] + distances[d];
        }
        noise3_batch8(other, points.xs, points.ys, moved, COUNT, SEED);
        CHECK(fabs(correlation(points.got, other, COUNT)) < 0.02f64);
    }
}

TEST(a_point_that_is_not_finite_gives_a_nan_and_leaves_its_neighbours_alone) {
    enum : uint64_t { COUNT = 11u };
    const float32_t bad[] = {NAN, INFINITY, -INFINITY};
    uint64_t state = 0x0BADu;
    points_t points = points_alloc(t, COUNT);
    for (uint64_t b = 0; b < sizeof(bad) / sizeof(bad[0]); b++) {
        for (uint64_t lane = 0; lane < COUNT; lane++) {
            for (uint32_t axis = 0; axis < 3u; axis++) {
                fill_points(points, &state, COUNT, 100.0f32);
                float32_t *arrays[3] = {points.xs, points.ys, points.zs};
                arrays[axis][lane] = bad[b];
                noise3_batch8(points.got, points.xs, points.ys, points.zs, COUNT, SEED);
                simd_reference_noise3(points.want, points.xs, points.ys, points.zs, COUNT, SEED);
                for (uint64_t i = 0; i < COUNT; i++) {
                    if (i == lane) {
                        CHECK(isnan(points.got[i]));
                        CHECK(isnan(points.want[i]));
                    } else {
                        CHECK_EQ_U64(bits_of(points.got[i]), bits_of(points.want[i]));
                    }
                }
            }
        }
    }
}

TEST_MAIN()
