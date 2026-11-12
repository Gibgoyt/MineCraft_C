/* ctest run_terrain_fields. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "simd/reference/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16); /* the plan's gate says 16 */
static_assert(MC_THREADS_WORKERS <= 64); /* the worker mask below */

enum : uint32_t {
    FIELDS_COUNT = 6u,
    FIELDS_WORKERS_MANY = 16u,
    FIELDS_TILE_SIDE = 16u,
    FIELDS_TILE_COLUMNS = FIELDS_TILE_SIDE * FIELDS_TILE_SIDE,
    FIELDS_MAP_SIDE = 8u,
    FIELDS_TILES = FIELDS_MAP_SIDE * FIELDS_MAP_SIDE,
    /* The spread is a claim about the whole world, so it is measured far apart: 512 columns on a side, 193 blocks apart, is
     * about 48 of the broadest field's cells on a side, and each seed adds as many again. */
    FIELDS_SPREAD_SIDE = 512u,
    FIELDS_SPREAD_POINTS = FIELDS_SPREAD_SIDE * FIELDS_SPREAD_SIDE,
    FIELDS_SPREAD_SEEDS = 8u,
    FIELDS_GUARD = 4u,
};
static const int32_t FIELDS_SPREAD_STEP = 193;
static const int32_t FIELDS_MAP_MIN = -4;
static const int64_t FIELDS_WAIT_NS = 60 * TIME_NS_PER_SECOND;
static const uint64_t FIELDS_WORLD = 0x1234567890ABCDEFull;

/* Pinned: see README.md. */
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
static const uint64_t FIELDS_DIGEST_PIN = 0xB97D2A98FDF4D13Aull;

static const char *const FIELDS_NAMES[FIELDS_COUNT] = {
    "continentalness", "erosion", "ridges", "temperature", "humidity", "weirdness",
};

/* Six float32_t per column, one block after another. A test's memory for all six fields. */
typedef struct fields_block_t {
    float32_t *fields[FIELDS_COUNT];
} fields_block_t;
static_assert(sizeof(fields_block_t) == 48);

static fields_block_t fields_block_alloc(test_context_t *t, uint64_t columns) {
    fields_block_t block;
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        block.fields[f] = test_alloc(t, columns * sizeof(float32_t));
    }
    return block;
}

static terrain_fields_arrays_t fields_block_arrays(const fields_block_t *block, uint64_t at) {
    return (terrain_fields_arrays_t){
        .continentalness = block->fields[0] + at,
        .erosion = block->fields[1] + at,
        .ridges = block->fields[2] + at,
        .temperature = block->fields[3] + at,
        .humidity = block->fields[4] + at,
        .weirdness = block->fields[5] + at,
    };
}

static uint32_t fields_bits_of(float32_t value) {
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* FNV-1a over bytes, which is enough to notice a change. */
static uint64_t fields_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static terrain_fields_area_t fields_square(int32_t x, int32_t y, int32_t step, uint32_t across) {
    return (terrain_fields_area_t){
        .origin_x = x,
        .origin_y = y,
        .step_x = step,
        .step_y = step,
        .count_x = across,
        .count_y = across,
    };
}

/* One seed's values over the wide area the spread is measured on. */
static void fields_sample_wide(uint64_t seed, const fields_block_t *block, uint64_t at, test_context_t *t) {
    terrain_t terrain;
    if (!CHECK(terrain_init(&terrain, seed))) {
        return;
    }
    const int32_t half = (int32_t)(FIELDS_SPREAD_SIDE / 2u) * FIELDS_SPREAD_STEP;
    const terrain_fields_area_t area = fields_square(-half, -half, FIELDS_SPREAD_STEP, FIELDS_SPREAD_SIDE);
    const terrain_fields_arrays_t arrays = fields_block_arrays(block, at);
    terrain_fields_sample_area(terrain.borrowed_fields, &area, &arrays);
    terrain_deinit(&terrain);
}

/* What the README says each value is, written out again from the README and not from the source. */
static float32_t fields_expected_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = fmin(1.0f64, fmax(-1.0f64, unit));
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

/* A field worked out on the scalar kernel: the sum of its octaves at a point. */
static float32_t fields_scalar_field(const terrain_noise_field_t *field, float64_t x, float64_t y) {
    float32_t sum = 0.0f32;
    for (uint32_t o = 0u; o < field->octave_count; o++) {
        const terrain_noise_octave_t *const octave = &field->octaves[o];
        const float32_t cell_x = (float32_t)(x * octave->frequency_x + octave->offset_x);
        const float32_t cell_y = (float32_t)(y * octave->frequency_y + octave->offset_y);
        const float32_t cell_z = (float32_t)octave->offset_z;
        float32_t value = 0.0f32;
        simd_reference_noise3(&value, &cell_x, &cell_y, &cell_z, 1u, octave->seed);
        sum += value * octave->amplitude;
    }
    return sum;
}

TEST(each_value_is_worked_out_as_the_readme_says) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    const terrain_fields_t *const fields = terrain.borrowed_fields;
    enum : uint32_t { ACROSS = 32u, COLUMNS = ACROSS * ACROSS };
    const fields_block_t block = fields_block_alloc(t, COLUMNS);
    const terrain_fields_arrays_t arrays = fields_block_arrays(&block, 0u);
    const terrain_fields_area_t area = fields_square(-5000, 7001, 37, ACROSS);
    terrain_fields_sample_area(fields, &area, &arrays);

    for (uint32_t i = 0u; i < COLUMNS; i++) {
        const float64_t x = (float64_t)(-5000 + (int32_t)(i % ACROSS) * 37);
        const float64_t y = (float64_t)(7001 + (int32_t)(i / ACROSS) * 37);
        const float64_t wx = x + (float64_t)fields_scalar_field(fields->borrowed_warp_x, x, y) * 96.0f64;
        const float64_t wy = y + (float64_t)fields_scalar_field(fields->borrowed_warp_y, x, y) * 96.0f64;
        const float32_t weirdness =
            fields_expected_spread(fields_scalar_field(fields->borrowed_weirdness, wx, wy), 0.35f64);
        const float32_t expected[FIELDS_COUNT] = {
            fields_expected_spread(fields_scalar_field(fields->borrowed_continentalness, wx, wy), 0.259f64),
            fields_expected_spread(fields_scalar_field(fields->borrowed_erosion, wx, wy), 0.31f64),
            1.0f32 - fabsf(3.0f32 * fabsf(weirdness) - 2.0f32),
            fields_expected_spread(fields_scalar_field(fields->borrowed_temperature, wx, wy), 0.35f64),
            fields_expected_spread(fields_scalar_field(fields->borrowed_humidity, wx, wy), 0.35f64),
            weirdness,
        };
        for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
            CHECK_EQ_U64(fields_bits_of(block.fields[f][i]), fields_bits_of(expected[f]));
        }
    }
    terrain_deinit(&terrain);
}

TEST(every_field_keeps_to_its_range_wherever_it_is_read) {
    enum : uint32_t { ACROSS = 256u, COLUMNS = ACROSS * ACROSS };
    const fields_block_t block = fields_block_alloc(t, COLUMNS);
    const terrain_fields_arrays_t arrays = fields_block_arrays(&block, 0u);
    /* Near the origin, far out, and at the ends of an int32_t, where a float32_t has no fraction left. */
    const int32_t origins[][2] = {
        {-128, -128}, {1000000, -1000000}, {INT32_MIN, INT32_MIN}, {INT32_MAX - 255, INT32_MAX - 255}, {INT32_MIN, 0},
    };
    for (uint64_t seed = 0u; seed < 4u; seed++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, seed * 0x9E3779B97F4A7C15ull));
        for (uint32_t o = 0u; o < sizeof(origins) / sizeof(origins[0]); o++) {
            const terrain_fields_area_t area = fields_square(origins[o][0], origins[o][1], 1, ACROSS);
            terrain_fields_sample_area(terrain.borrowed_fields, &area, &arrays);
            for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
                uint32_t outside = 0u;
                for (uint32_t i = 0u; i < COLUMNS; i++) {
                    /* Written so that NaN counts as outside. */
                    outside += block.fields[f][i] >= -1.0f32 && block.fields[f][i] <= 1.0f32 ? 0u : 1u;
                }
                CHECK_EQ_U64(outside, 0u);
            }
        }
        terrain_deinit(&terrain);
    }
}

/* The promise the README makes to Stages 10.3 and 10.6: how much of the world is below a value, field by field, measured
 * over many seeds, and the whole range really used. Ridges has its own answer, since it is weirdness folded. */
TEST(each_field_is_spread_out_as_the_readme_promises) {
    const uint64_t points = (uint64_t)FIELDS_SPREAD_POINTS * FIELDS_SPREAD_SEEDS;
    const fields_block_t block = fields_block_alloc(t, points);
    for (uint32_t s = 0u; s < FIELDS_SPREAD_SEEDS; s++) {
        fields_sample_wide(FIELDS_WORLD + s * 0x51ED270B7Cull, &block, (uint64_t)s * FIELDS_SPREAD_POINTS, t);
    }
    static const float32_t cuts[] = {-0.75f32, -0.5f32, -0.25f32, 0.0f32, 0.25f32, 0.5f32, 0.75f32};
    enum : uint32_t { CUTS = sizeof(cuts) / sizeof(cuts[0]) };
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        uint64_t below[CUTS] = {0u};
        uint64_t at_the_ends = 0u;
        float64_t total = 0.0f64;
        float32_t lowest = 2.0f32;
        float32_t highest = -2.0f32;
        for (uint64_t i = 0u; i < points; i++) {
            const float32_t value = block.fields[f][i];
            for (uint32_t c = 0u; c < CUTS; c++) {
                below[c] += value < cuts[c] ? 1u : 0u;
            }
            at_the_ends += value == -1.0f32 || value == 1.0f32 ? 1u : 0u;
            total += (float64_t)value;
            lowest = value < lowest ? value : lowest;
            highest = value > highest ? value : highest;
        }
        const float64_t ends_share = (float64_t)at_the_ends / (float64_t)points;
        printf("%-15s from %.4f to %.4f, mean %.4f, exactly at an end %.4f, below:", FIELDS_NAMES[f],
               (libc_double_t)lowest, (libc_double_t)highest, (libc_double_t)(total / (float64_t)points),
               (libc_double_t)ends_share);
        for (uint32_t c = 0u; c < CUTS; c++) {
            const float64_t share = (float64_t)below[c] / (float64_t)points;
            printf(" %.3f", (libc_double_t)share);
            const float64_t cut = (float64_t)cuts[c];
            float64_t expected = (cut + 1.0f64) / 2.0f64;
            if (f == 2u) {
                /* Two thirds of weirdness's size climbs -1 to 1, and the last third falls 1 to 0. */
                expected = expected * 2.0f64 / 3.0f64 + (cut > 0.0f64 ? cut / 3.0f64 : 0.0f64);
            }
            CHECK_NEAR(share, expected, f == 2u ? 0.06f64 : 0.02f64);
        }
        printf("\n");
        CHECK(lowest <= -0.98f32);
        CHECK(highest >= 0.98f32);
        /* The ends must stay rare, or a biome's edge at -1 or 1 would be a large flat place. */
        CHECK(ends_share < 0.05f64);
        if (f != 2u) {
            CHECK_NEAR(total / (float64_t)points, 0.0f64, 0.03f64);
        }
    }
}

/* What the limits in the source were chosen from: each field's raw noise, measured as widely as the spread is, and the raw
 * value at each of the shares the spread makes promises about. The check is that nobody changes a shape and leaves its limit
 * behind. */
TEST(raw_noise_measures_what_the_limits_were_chosen_for) {
    enum : uint32_t { LOTS = FIELDS_SPREAD_SEEDS * FIELDS_SPREAD_POINTS };
    float64_t *const xs = test_alloc(t, FIELDS_SPREAD_POINTS * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, FIELDS_SPREAD_POINTS * sizeof(float64_t));
    float64_t *const zs = test_alloc(t, FIELDS_SPREAD_POINTS * sizeof(float64_t));
    float32_t *const raw = test_alloc(t, FIELDS_SPREAD_POINTS * sizeof(float32_t));
    const int32_t half = (int32_t)(FIELDS_SPREAD_SIDE / 2u) * FIELDS_SPREAD_STEP;
    for (uint32_t i = 0u; i < FIELDS_SPREAD_POINTS; i++) {
        xs[i] = (float64_t)(-half + (int32_t)(i % FIELDS_SPREAD_SIDE) * FIELDS_SPREAD_STEP);
        ys[i] = (float64_t)(-half + (int32_t)(i / FIELDS_SPREAD_SIDE) * FIELDS_SPREAD_STEP);
    }
    static const float64_t limits[TERRAIN_FIELDS_NOISE_FIELDS - 2u] = {0.259f64, 0.31f64, 0.35f64, 0.35f64,
                                                                       0.35f64,  0.35f64, 0.35f64};
    static const char *const names[TERRAIN_FIELDS_NOISE_FIELDS] = {
        "continentalness", "erosion", "temperature", "humidity", "weirdness", "variety", "wetness", "warp_x", "warp_y",
    };
    /* Over -1 to 1, fine enough to read a quantile to three places. */
    enum : uint32_t { BINS = 4096u };
    uint32_t *const counts = test_alloc(t, TERRAIN_FIELDS_NOISE_FIELDS * BINS * sizeof(uint32_t));
    float64_t squares[TERRAIN_FIELDS_NOISE_FIELDS] = {0.0f64};
    for (uint32_t s = 0u; s < FIELDS_SPREAD_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, FIELDS_WORLD + s * 0x51ED270B7Cull));
        const terrain_fields_t *const c = terrain.borrowed_fields;
        const terrain_noise_field_t *const fields[TERRAIN_FIELDS_NOISE_FIELDS] = {
            c->borrowed_continentalness, c->borrowed_erosion,   c->borrowed_temperature,
            c->borrowed_humidity,        c->borrowed_weirdness, c->borrowed_variety,
            c->borrowed_wetness,         c->borrowed_warp_x,    c->borrowed_warp_y,
        };
        for (uint32_t f = 0u; f < TERRAIN_FIELDS_NOISE_FIELDS; f++) {
            terrain_noise_sample_points(fields[f], xs, ys, zs, FIELDS_SPREAD_POINTS, raw);
            for (uint32_t i = 0u; i < FIELDS_SPREAD_POINTS; i++) {
                squares[f] += (float64_t)raw[i] * (float64_t)raw[i];
                const float64_t bin = ((float64_t)raw[i] + 1.0f64) * 0.5f64 * (float64_t)BINS;
                counts[f * BINS + (uint32_t)fmin(fmax(bin, 0.0f64), (float64_t)(BINS - 1u))]++;
            }
        }
        terrain_deinit(&terrain);
    }
    static const float64_t shares[] = {0.03f64, 0.125f64, 0.25f64, 0.375f64, 0.625f64, 0.75f64, 0.875f64, 0.97f64};
    for (uint32_t f = 0u; f < TERRAIN_FIELDS_NOISE_FIELDS; f++) {
        const float64_t deviation = sqrt(squares[f] / (float64_t)LOTS);
        printf("%-15s raw standard deviation %.4f; raw at a share:", names[f], (libc_double_t)deviation);
        uint64_t running = 0u;
        uint32_t next = 0u;
        for (uint32_t b = 0u; b < BINS && next < sizeof(shares) / sizeof(shares[0]); b++) {
            running += counts[f * BINS + b];
            if ((float64_t)running >= shares[next] * (float64_t)LOTS) {
                printf(" %.3f", (libc_double_t)((float64_t)(b + 1u) / (float64_t)BINS * 2.0f64 - 1.0f64));
                next++;
            }
        }
        printf("\n");
        if (f < TERRAIN_FIELDS_NOISE_FIELDS - 2u) {
            printf("%-15s limit is %.2f of them\n", names[f], (libc_double_t)(limits[f] / deviation));
            CHECK(limits[f] > 1.5f64 * deviation && limits[f] < 2.5f64 * deviation);
        }
    }
}

/* Weirdness and ridges are one field and its fold; the rest must not be each other in disguise. */
TEST(fields_are_unrelated_and_ridges_is_folded_weirdness) {
    const fields_block_t block = fields_block_alloc(t, FIELDS_SPREAD_POINTS);
    fields_sample_wide(FIELDS_WORLD, &block, 0u, t);
    for (uint32_t i = 0u; i < FIELDS_SPREAD_POINTS; i++) {
        const float32_t expected = 1.0f32 - fabsf(3.0f32 * fabsf(block.fields[5][i]) - 2.0f32);
        if (!CHECK_EQ_U64(fields_bits_of(block.fields[2][i]), fields_bits_of(expected))) {
            break;
        }
    }
    float64_t means[FIELDS_COUNT] = {0.0f64};
    float64_t squares[FIELDS_COUNT] = {0.0f64};
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        for (uint32_t i = 0u; i < FIELDS_SPREAD_POINTS; i++) {
            means[f] += (float64_t)block.fields[f][i];
        }
        means[f] /= (float64_t)FIELDS_SPREAD_POINTS;
        for (uint32_t i = 0u; i < FIELDS_SPREAD_POINTS; i++) {
            const float64_t gap = (float64_t)block.fields[f][i] - means[f];
            squares[f] += gap * gap;
        }
    }
    for (uint32_t a = 0u; a < FIELDS_COUNT; a++) {
        for (uint32_t b = a + 1u; b < FIELDS_COUNT; b++) {
            float64_t both = 0.0f64;
            for (uint32_t i = 0u; i < FIELDS_SPREAD_POINTS; i++) {
                both += ((float64_t)block.fields[a][i] - means[a]) * ((float64_t)block.fields[b][i] - means[b]);
            }
            const float64_t correlation = both / sqrt(squares[a] * squares[b]);
            printf("%s and %s: %.3f\n", FIELDS_NAMES[a], FIELDS_NAMES[b], (libc_double_t)correlation);
            CHECK(fabs(correlation) < 0.1f64);
        }
    }
}

/* Plan 006 phase 03: variety and wetness, which biomes read in place of temperature and humidity. They spread as the others
 * do, are unrelated to each other and to the two they replace, a point is the area's bits, and asking for them moves no
 * other field's bits. */
TEST(variety_and_wetness_spread_like_the_others_and_are_their_own) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    const uint64_t points = FIELDS_SPREAD_POINTS;
    float32_t *const variety = test_alloc(t, points * sizeof(float32_t));
    float32_t *const wetness = test_alloc(t, points * sizeof(float32_t));
    float32_t *const temperature = test_alloc(t, points * sizeof(float32_t));
    float32_t *const humidity = test_alloc(t, points * sizeof(float32_t));
    float32_t *const humidity_alone = test_alloc(t, points * sizeof(float32_t));
    const int32_t half = (int32_t)(FIELDS_SPREAD_SIDE / 2u) * FIELDS_SPREAD_STEP;
    const terrain_fields_area_t area = fields_square(-half, -half, FIELDS_SPREAD_STEP, FIELDS_SPREAD_SIDE);
    const terrain_fields_arrays_t arrays = {
        .temperature = temperature, .humidity = humidity, .variety = variety, .wetness = wetness};
    terrain_fields_sample_area(terrain.borrowed_fields, &area, &arrays);
    const terrain_fields_arrays_t alone = {.humidity = humidity_alone};
    terrain_fields_sample_area(terrain.borrowed_fields, &area, &alone);
    CHECK(memcmp(humidity, humidity_alone, points * sizeof(float32_t)) == 0);

    const float32_t *const fields[4] = {variety, wetness, temperature, humidity};
    static const char *const names[4] = {"variety", "wetness", "temperature", "humidity"};
    float64_t means[4] = {0.0f64};
    float64_t squares[4] = {0.0f64};
    static const float32_t cuts[] = {-0.5f32, 0.0f32, 0.5f32};
    for (uint32_t f = 0u; f < 4u; f++) {
        uint64_t below[3] = {0u};
        for (uint64_t i = 0u; i < points; i++) {
            means[f] += (float64_t)fields[f][i];
            for (uint32_t c = 0u; c < 3u; c++) {
                below[c] += fields[f][i] < cuts[c] ? 1u : 0u;
            }
            CHECK(fields[f][i] >= -1.0f32 && fields[f][i] <= 1.0f32);
        }
        means[f] /= (float64_t)points;
        for (uint32_t c = 0u; c < 3u && f < 2u; c++) {
            const float64_t share = (float64_t)below[c] / (float64_t)points;
            printf("%s below %.2f: %.3f\n", names[f], (libc_double_t)cuts[c], (libc_double_t)share);
            CHECK_NEAR(share, ((float64_t)cuts[c] + 1.0f64) / 2.0f64, 0.04f64);
        }
        for (uint64_t i = 0u; i < points; i++) {
            const float64_t gap = (float64_t)fields[f][i] - means[f];
            squares[f] += gap * gap;
        }
    }
    for (uint32_t a = 0u; a < 2u; a++) {
        for (uint32_t b = a + 1u; b < 4u; b++) {
            float64_t both = 0.0f64;
            for (uint64_t i = 0u; i < points; i++) {
                both += ((float64_t)fields[a][i] - means[a]) * ((float64_t)fields[b][i] - means[b]);
            }
            const float64_t correlation = both / sqrt(squares[a] * squares[b]);
            printf("%s and %s: %.3f\n", names[a], names[b], (libc_double_t)correlation);
            CHECK(fabs(correlation) < 0.1f64);
        }
    }
    for (uint32_t i = 0u; i < 64u; i++) {
        const uint64_t at = (uint64_t)i * 4099u % points;
        terrain_fields_point_t point;
        terrain_fields_at(terrain.borrowed_fields, area.origin_x + (int32_t)(at % FIELDS_SPREAD_SIDE) * area.step_x,
                          area.origin_y + (int32_t)(at / FIELDS_SPREAD_SIDE) * area.step_y, &point);
        CHECK_EQ_U64(fields_bits_of(point.variety), fields_bits_of(variety[at]));
        CHECK_EQ_U64(fields_bits_of(point.wetness), fields_bits_of(wetness[at]));
    }
    terrain_deinit(&terrain);
}

/* No seam at a batch's end, and no jump anywhere: a slope this steep at one column is a cliff in the world. */
TEST(fields_change_slowly_from_column_to_column) {
    enum : uint32_t { ACROSS = 1024u, COLUMNS = ACROSS * ACROSS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    const fields_block_t block = fields_block_alloc(t, COLUMNS);
    const terrain_fields_arrays_t arrays = fields_block_arrays(&block, 0u);
    const terrain_fields_area_t area = fields_square(-512, -512, 1, ACROSS);
    terrain_fields_sample_area(terrain.borrowed_fields, &area, &arrays);
    /* Ridges moves three times as fast as weirdness, and weirdness has the shortest wavelength. The numbers are up to
     * about twice what was measured on this seed, which the test prints. */
    static const float32_t limits[FIELDS_COUNT] = {0.02f32, 0.02f32, 0.15f32, 0.006f32, 0.012f32, 0.05f32};
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        const float32_t *const values = block.fields[f];
        float32_t steepest = 0.0f32;
        for (uint32_t i = 0u; i < COLUMNS; i++) {
            if (i % ACROSS != ACROSS - 1u) {
                steepest = fmaxf(steepest, fabsf(values[i + 1u] - values[i]));
            }
            if (i + ACROSS < COLUMNS) {
                steepest = fmaxf(steepest, fabsf(values[i + ACROSS] - values[i]));
            }
        }
        printf("%-15s steepest step between neighbours %.5f\n", FIELDS_NAMES[f], (libc_double_t)steepest);
        CHECK(steepest < limits[f]);
        CHECK(steepest > 0.0f32);
    }
    terrain_deinit(&terrain);
}

/* Every way of asking must give the same bits: an area, its rows backwards, lists cut near the batch sizes, one column at a
 * time, and a field left out. */
TEST(any_split_of_the_work_gives_the_same_bits) {
    enum : uint32_t { ACROSS = 96u, COLUMNS = ACROSS * ACROSS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    const terrain_fields_t *const fields = terrain.borrowed_fields;
    const fields_block_t whole = fields_block_alloc(t, COLUMNS);
    const fields_block_t part = fields_block_alloc(t, COLUMNS);
    const terrain_fields_area_t area = fields_square(-48, -48, 1, ACROSS);
    const terrain_fields_arrays_t whole_arrays = fields_block_arrays(&whole, 0u);
    const terrain_fields_arrays_t part_arrays = fields_block_arrays(&part, 0u);
    CHECK_EQ_U64(terrain_fields_area_columns(&area), COLUMNS);
    terrain_fields_sample_area(fields, &area, &whole_arrays);

    for (uint32_t row = ACROSS; row-- > 0u;) {
        terrain_fields_area_t one_row = area;
        one_row.origin_y = area.origin_y + (int32_t)row;
        one_row.count_y = 1u;
        const terrain_fields_arrays_t at_row = fields_block_arrays(&part, (uint64_t)row * ACROSS);
        terrain_fields_sample_area(fields, &one_row, &at_row);
    }
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        CHECK_MEM_EQ(whole.fields[f], part.fields[f], COLUMNS * sizeof(float32_t));
    }

    float64_t *const xs = test_alloc(t, COLUMNS * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, COLUMNS * sizeof(float64_t));
    for (uint32_t i = 0u; i < COLUMNS; i++) {
        xs[i] = (float64_t)(area.origin_x + (int32_t)(i % ACROSS));
        ys[i] = (float64_t)(area.origin_y + (int32_t)(i / ACROSS));
    }
    const uint32_t lengths[] = {1u, 7u, 8u, 9u, 127u, 128u, 129u, 255u, 256u, 257u, 1000u};
    for (uint32_t which = 0u; which < sizeof(lengths) / sizeof(lengths[0]); which++) {
        for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
            memset(part.fields[f], 0, COLUMNS * sizeof(float32_t));
        }
        for (uint32_t done = 0u; done < COLUMNS;) {
            const uint32_t length = COLUMNS - done < lengths[which] ? COLUMNS - done : lengths[which];
            const terrain_fields_arrays_t at = fields_block_arrays(&part, done);
            terrain_fields_sample_points(fields, xs + done, ys + done, length, &at);
            done += length;
        }
        for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
            CHECK_MEM_EQ(whole.fields[f], part.fields[f], COLUMNS * sizeof(float32_t));
        }
    }

    for (uint32_t i = 0u; i < COLUMNS; i += 97u) {
        terrain_fields_point_t point;
        terrain_fields_at(fields, (int32_t)xs[i], (int32_t)ys[i], &point);
        CHECK_EQ_U64(fields_bits_of(point.continentalness), fields_bits_of(whole.fields[0][i]));
        CHECK_EQ_U64(fields_bits_of(point.erosion), fields_bits_of(whole.fields[1][i]));
        CHECK_EQ_U64(fields_bits_of(point.ridges), fields_bits_of(whole.fields[2][i]));
        CHECK_EQ_U64(fields_bits_of(point.temperature), fields_bits_of(whole.fields[3][i]));
        CHECK_EQ_U64(fields_bits_of(point.humidity), fields_bits_of(whole.fields[4][i]));
        CHECK_EQ_U64(fields_bits_of(point.weirdness), fields_bits_of(whole.fields[5][i]));
    }

    /* Each field asked for alone, and each left out alone, with ridges having to read weirdness for itself. */
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        float32_t *lone[FIELDS_COUNT] = {nullptr};
        lone[f] = part.fields[f];
        memset(part.fields[f], 0, COLUMNS * sizeof(float32_t));
        const terrain_fields_arrays_t alone = {
            .continentalness = lone[0],
            .erosion = lone[1],
            .ridges = lone[2],
            .temperature = lone[3],
            .humidity = lone[4],
            .weirdness = lone[5],
        };
        terrain_fields_sample_area(fields, &area, &alone);
        CHECK_MEM_EQ(whole.fields[f], part.fields[f], COLUMNS * sizeof(float32_t));
    }

    /* An area of nothing writes nothing. */
    const terrain_fields_area_t nothing = fields_square(0, 0, 1, 0u);
    part.fields[0][0] = 7.0f32;
    terrain_fields_sample_area(fields, &nothing, &part_arrays);
    CHECK(part.fields[0][0] == 7.0f32);
    CHECK_EQ_U64(terrain_fields_area_columns(&nothing), 0u);
    terrain_deinit(&terrain);
}

/* Steps other than 1, and steps that run off the end of an int32_t. */
TEST(steps_and_directions_read_the_same_columns) {
    enum : uint32_t { ACROSS = 40u, COLUMNS = ACROSS * ACROSS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    const fields_block_t forward = fields_block_alloc(t, COLUMNS);
    const fields_block_t backward = fields_block_alloc(t, COLUMNS);
    const terrain_fields_arrays_t forward_arrays = fields_block_arrays(&forward, 0u);
    const terrain_fields_arrays_t backward_arrays = fields_block_arrays(&backward, 0u);
    const terrain_fields_area_t up = fields_square(-1000, 300, 25, ACROSS);
    terrain_fields_area_t down = up;
    down.origin_x = up.origin_x + (int32_t)(ACROSS - 1u) * up.step_x;
    down.origin_y = up.origin_y + (int32_t)(ACROSS - 1u) * up.step_y;
    down.step_x = -up.step_x;
    down.step_y = -up.step_y;
    terrain_fields_sample_area(terrain.borrowed_fields, &up, &forward_arrays);
    terrain_fields_sample_area(terrain.borrowed_fields, &down, &backward_arrays);
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        for (uint32_t i = 0u; i < COLUMNS; i++) {
            if (!CHECK_EQ_U64(fields_bits_of(forward.fields[f][i]),
                              fields_bits_of(backward.fields[f][COLUMNS - 1u - i]))) {
                break;
            }
        }
    }
    /* Past the end of an int32_t: it must neither wrap nor be undefined. */
    const terrain_fields_area_t off_the_end = fields_square(INT32_MAX - 10, INT32_MAX - 10, INT32_MAX, 3u);
    terrain_fields_sample_area(terrain.borrowed_fields, &off_the_end, &forward_arrays);
    float64_t x = (float64_t)(INT32_MAX - 10) + 2.0f64 * (float64_t)INT32_MAX;
    float64_t y = x;
    float32_t lone = 0.0f32;
    const terrain_fields_arrays_t just_erosion = {.erosion = &lone};
    terrain_fields_sample_points(terrain.borrowed_fields, &x, &y, 1u, &just_erosion);
    CHECK_EQ_U64(fields_bits_of(lone), fields_bits_of(forward.fields[1][8]));
    terrain_deinit(&terrain);
}

TEST(a_seed_makes_its_own_climate_and_only_its_own) {
    enum : uint32_t { ACROSS = 64u, COLUMNS = ACROSS * ACROSS };
    const fields_block_t first = fields_block_alloc(t, COLUMNS);
    const fields_block_t again = fields_block_alloc(t, COLUMNS);
    const fields_block_t other = fields_block_alloc(t, COLUMNS);
    const terrain_fields_area_t area = fields_square(-32, -32, 8, ACROSS);
    const terrain_fields_arrays_t first_arrays = fields_block_arrays(&first, 0u);
    const terrain_fields_arrays_t again_arrays = fields_block_arrays(&again, 0u);
    const terrain_fields_arrays_t other_arrays = fields_block_arrays(&other, 0u);

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    terrain_fields_sample_area(terrain.borrowed_fields, &area, &first_arrays);
    terrain_deinit(&terrain);

    /* Another terrain with the seed, whose bank is not the same order: what came before a field moves nothing in it. */
    uint8_t *const memory = test_alloc(t, 1u << 20);
    arena_t arena;
    arena_init(&arena, "test", memory, 1u << 20);
    terrain_noise_bank_t bank;
    terrain_noise_bank_init(&bank, &arena, FIELDS_WORLD, TERRAIN_NOISE_FIELDS_MAX);
    const terrain_noise_field_t *extra = nullptr;
    const terrain_noise_shape_t shape = {
        .frequency_x = 1.0f64 / 64.0f64,
        .frequency_y = 1.0f64 / 64.0f64,
        .lacunarity = 2.0f32,
        .persistence = 0.5f32,
        .octaves = 2u,
    };
    CHECK(terrain_noise_bank_add(&bank, "test/before", &shape, &extra));
    terrain_fields_t *fields = nullptr;
    CHECK(terrain_fields_init(&fields, &arena, &bank));
    REQUIRE(fields != nullptr);
    terrain_fields_sample_area(fields, &area, &again_arrays);
    terrain_fields_deinit(fields);
    terrain_noise_bank_deinit(&bank);
    arena_deinit(&arena);

    REQUIRE(terrain_init(&terrain, FIELDS_WORLD + 1u));
    terrain_fields_sample_area(terrain.borrowed_fields, &area, &other_arrays);
    terrain_deinit(&terrain);

    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        CHECK_MEM_EQ(first.fields[f], again.fields[f], COLUMNS * sizeof(float32_t));
        CHECK(memcmp(first.fields[f], other.fields[f], COLUMNS * sizeof(float32_t)) != 0);
    }
}

/* One run over all the tiles: as tests/run/terrain/seed/ does it, so that every worker of the run really makes some. */
typedef struct fields_run_t {
    _Atomic uint32_t arrived;                /* 0: jobs at the barrier; each takes its number from this */
    uint8_t _pad0[4];                        /* 4 */
    _Atomic uint64_t worker_mask;            /* 8: who ran a job */
    _Atomic uint32_t barrier_failed;         /* 16 */
    uint32_t barrier;                        /* 20: jobs that must all be running before any does its work */
    const terrain_fields_t *borrowed_fields; /* 24 */
    float32_t *borrowed_values;              /* 32: FIELDS_COUNT tiles' worth per tile, in map order */
    const uint32_t *borrowed_order;          /* 40: the order tiles are taken in */
} fields_run_t;
static_assert(sizeof(fields_run_t) == 48);

static void fields_tile_fill(const fields_run_t *run, uint32_t tile) {
    const terrain_fields_area_t area = fields_square(
        (FIELDS_MAP_MIN + (int32_t)(tile % FIELDS_MAP_SIDE)) * (int32_t)FIELDS_TILE_SIDE,
        (FIELDS_MAP_MIN + (int32_t)(tile / FIELDS_MAP_SIDE)) * (int32_t)FIELDS_TILE_SIDE, 1, FIELDS_TILE_SIDE);
    float32_t *const values = run->borrowed_values + (uint64_t)tile * FIELDS_COUNT * FIELDS_TILE_COLUMNS;
    const terrain_fields_arrays_t arrays = {
        .continentalness = values,
        .erosion = values + FIELDS_TILE_COLUMNS,
        .ridges = values + 2u * FIELDS_TILE_COLUMNS,
        .temperature = values + 3u * FIELDS_TILE_COLUMNS,
        .humidity = values + 4u * FIELDS_TILE_COLUMNS,
        .weirdness = values + 5u * FIELDS_TILE_COLUMNS,
    };
    terrain_fields_sample_area(run->borrowed_fields, &area, &arrays);
}

static void fields_job(job_worker_t *worker, void *borrowed_argument) {
    fields_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t ticket = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    const int64_t deadline = time_now_ns() + FIELDS_WAIT_NS;
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        if (time_now_ns() > deadline) {
            atomic_store_explicit(&run->barrier_failed, 1u, memory_order_relaxed);
            break;
        }
        (void)sched_yield();
    }
    for (uint32_t i = ticket; i < FIELDS_TILES; i += run->barrier) {
        fields_tile_fill(run, run->borrowed_order[i]);
    }
}

static uint64_t fields_run_all(job_system_t *system, const terrain_fields_t *fields, uint32_t workers,
                               const uint32_t *order, float32_t *values) {
    fields_run_t run = {
        .barrier = workers,
        .borrowed_fields = fields,
        .borrowed_values = values,
        .borrowed_order = order,
    };
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, fields_job, &run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    if (atomic_load_explicit(&run.barrier_failed, memory_order_relaxed) != 0u) {
        return 0u;
    }
    return atomic_load_explicit(&run.worker_mask, memory_order_relaxed);
}

static uint32_t fields_count_bits(uint64_t bits) {
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

/* The gate: the same bytes from 1 worker and 16, whichever order the tiles are taken in. And a pin, against any change
 * of a shape, a limit, the warp or the kernel, and against the presets disagreeing. */
TEST(one_and_sixteen_workers_give_the_same_bytes) {
    const uint64_t bytes = (uint64_t)FIELDS_TILES * FIELDS_COUNT * FIELDS_TILE_COLUMNS * sizeof(float32_t);
    float32_t *const one = test_alloc(t, bytes + 2u * FIELDS_GUARD * sizeof(float32_t));
    float32_t *const many = test_alloc(t, bytes);
    float32_t *const shuffled = test_alloc(t, bytes);
    uint32_t in_order[FIELDS_TILES];
    uint32_t out_of_order[FIELDS_TILES];
    for (uint32_t i = 0u; i < FIELDS_TILES; i++) {
        in_order[i] = i;
        out_of_order[i] = FIELDS_TILES - 1u - i;
    }

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));

    /* Guards around one run, to see that it stays inside what it was given. */
    float32_t *const inside = one + FIELDS_GUARD;
    for (uint32_t i = 0u; i < FIELDS_GUARD; i++) {
        one[i] = -7.0f32;
        inside[bytes / sizeof(float32_t) + i] = -7.0f32;
    }

    const uint64_t one_mask = fields_run_all(&system, terrain.borrowed_fields, 1u, in_order, inside);
    const uint64_t many_mask = fields_run_all(&system, terrain.borrowed_fields, FIELDS_WORKERS_MANY, in_order, many);
    const uint64_t shuffled_mask =
        fields_run_all(&system, terrain.borrowed_fields, FIELDS_WORKERS_MANY, out_of_order, shuffled);
    job_deinit(&system);

    CHECK_EQ_U64(fields_count_bits(one_mask), 1u);
    CHECK_EQ_U64(fields_count_bits(many_mask), FIELDS_WORKERS_MANY);
    CHECK_EQ_U64(fields_count_bits(shuffled_mask), FIELDS_WORKERS_MANY);
    CHECK_MEM_EQ(inside, many, bytes);
    CHECK_MEM_EQ(inside, shuffled, bytes);
    for (uint32_t i = 0u; i < FIELDS_GUARD; i++) {
        CHECK(one[i] == -7.0f32);
        CHECK(inside[bytes / sizeof(float32_t) + i] == -7.0f32);
    }

    const uint64_t digest = fields_digest(0xCBF29CE484222325ull, inside, bytes);
    printf("digest 0x%016" PRIX64 "\n", digest);
    CHECK_EQ_U64(digest, FIELDS_DIGEST_PIN);
    terrain_deinit(&terrain);
}

TEST(sampling_stays_inside_its_output_and_allocates_nothing) {
    enum : uint32_t { ACROSS = 37u, COLUMNS = ACROSS * ACROSS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FIELDS_WORLD));
    float32_t *const guarded[FIELDS_COUNT] = {
        test_alloc(t, (COLUMNS + 2u * FIELDS_GUARD) * sizeof(float32_t)),
        test_alloc(t, (COLUMNS + 2u * FIELDS_GUARD) * sizeof(float32_t)),
        test_alloc(t, (COLUMNS + 2u * FIELDS_GUARD) * sizeof(float32_t)),
        test_alloc(t, (COLUMNS + 2u * FIELDS_GUARD) * sizeof(float32_t)),
        test_alloc(t, (COLUMNS + 2u * FIELDS_GUARD) * sizeof(float32_t)),
        test_alloc(t, (COLUMNS + 2u * FIELDS_GUARD) * sizeof(float32_t)),
    };
    fields_block_t block;
    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        for (uint32_t i = 0u; i < COLUMNS + 2u * FIELDS_GUARD; i++) {
            guarded[f][i] = -7.0f32;
        }
        block.fields[f] = guarded[f] + FIELDS_GUARD;
    }
    const terrain_fields_arrays_t arrays = fields_block_arrays(&block, 0u);
    const terrain_fields_area_t area = fields_square(3, -9, 1, ACROSS);

    heap_counters_t before;
    heap_counters_read(&before);
    const uint64_t arena_before = terrain.arena_tables.alloc_count;
    terrain_fields_sample_area(terrain.borrowed_fields, &area, &arrays);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(terrain.arena_tables.alloc_count, arena_before);

    for (uint32_t f = 0u; f < FIELDS_COUNT; f++) {
        for (uint32_t i = 0u; i < FIELDS_GUARD; i++) {
            CHECK(guarded[f][i] == -7.0f32);
            CHECK(guarded[f][FIELDS_GUARD + COLUMNS + i] == -7.0f32);
        }
        uint32_t still_guard = 0u;
        for (uint32_t i = 0u; i < COLUMNS; i++) {
            still_guard += block.fields[f][i] == -7.0f32 ? 1u : 0u;
        }
        CHECK_EQ_U64(still_guard, 0u);
    }
    terrain_deinit(&terrain);
}

TEST(a_bank_with_no_room_is_refused_and_nothing_is_left_behind) {
    uint8_t *const memory = test_alloc(t, 1u << 16);
    /* Room for every field but one, then for none at all. */
    const uint32_t capacities[] = {TERRAIN_FIELDS_NOISE_FIELDS - 1u, 0u};
    for (uint32_t c = 0u; c < 2u; c++) {
        arena_t arena;
        arena_init(&arena, "test", memory, 1u << 16);
        terrain_noise_bank_t bank;
        terrain_noise_bank_init(&bank, &arena, FIELDS_WORLD, capacities[c]);
        terrain_fields_t *fields = &(terrain_fields_t){0};
        CHECK(!terrain_fields_init(&fields, &arena, &bank));
        CHECK(fields == nullptr);
        terrain_noise_bank_deinit(&bank);
        arena_deinit(&arena);
    }
}

TEST(init_and_deinit_are_a_pair) {
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 7u));
    REQUIRE(terrain.borrowed_fields != nullptr);
    const terrain_fields_t *const fields = terrain.borrowed_fields;
    CHECK_EQ_U64(terrain.noise_bank.count, TERRAIN_FIELDS_NOISE_FIELDS + TERRAIN_LANDFORM_NOISE_FIELDS +
                                               TERRAIN_DENSITY_NOISE_FIELDS + TERRAIN_HYDROLOGY_NOISE_FIELDS +
                                               TERRAIN_CAVE_NOISE_FIELDS + TERRAIN_SURFACE_NOISE_FIELDS);
    CHECK_STR_EQ(fields->borrowed_continentalness->borrowed_name, "climate/continentalness");
    CHECK_STR_EQ(fields->borrowed_erosion->borrowed_name, "climate/erosion");
    CHECK_STR_EQ(fields->borrowed_temperature->borrowed_name, "climate/temperature");
    CHECK_STR_EQ(fields->borrowed_humidity->borrowed_name, "climate/humidity");
    CHECK_STR_EQ(fields->borrowed_weirdness->borrowed_name, "climate/weirdness");
    CHECK_STR_EQ(fields->borrowed_warp_x->borrowed_name, "climate/warp_x");
    CHECK_STR_EQ(fields->borrowed_warp_y->borrowed_name, "climate/warp_y");
    CHECK_STR_EQ(fields->borrowed_variety->borrowed_name, "climate/variety");
    CHECK_STR_EQ(fields->borrowed_wetness->borrowed_name, "climate/wetness");

    /* Every one of them is 2D: its frequency on z is 0, so there is no height for a column's climate to depend on. */
    const terrain_noise_field_t *const noises[] = {
        fields->borrowed_continentalness, fields->borrowed_erosion,   fields->borrowed_temperature,
        fields->borrowed_humidity,        fields->borrowed_weirdness, fields->borrowed_warp_x,
        fields->borrowed_warp_y,          fields->borrowed_variety,   fields->borrowed_wetness,
    };
    for (uint32_t f = 0u; f < TERRAIN_FIELDS_NOISE_FIELDS; f++) {
        for (uint32_t o = 0u; o < noises[f]->octave_count; o++) {
            CHECK(noises[f]->octaves[o].frequency_z == 0.0f64);
        }
    }

    heap_counters_t during;
    heap_counters_read(&during);
    CHECK_EQ_U64(during.live_bytes, before.live_bytes + TERRAIN_TABLES_BYTES);
    terrain_deinit(&terrain);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK(terrain.borrowed_fields == nullptr);

    /* Safe again, and on a handle that never started. */
    terrain_deinit(&terrain);
    terrain_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    terrain_deinit(&zeroed);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
}

TEST_MAIN()
