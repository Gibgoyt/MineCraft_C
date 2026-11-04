/* ctest run_terrain_density. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "simd/reference/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/column/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16); /* the plan's gate says 16 */
static_assert(MC_THREADS_WORKERS <= 64); /* the worker mask below */

enum : uint32_t {
    DENSITY_LAYERS =
        751u, /* -250 to 500, both included: all terrain makes, so the pinned digests say a taller world moved nothing */
    DENSITY_WORKERS_MANY = 16u,
    DENSITY_CHUNK = 16u,
    DENSITY_CHUNK_VOXELS = DENSITY_CHUNK * DENSITY_CHUNK * DENSITY_LAYERS,
    DENSITY_MAP_SIDE = 6u,
    DENSITY_CHUNKS = DENSITY_MAP_SIDE * DENSITY_MAP_SIDE,
    DENSITY_GUARD = 4u,
    /* Columns far apart, for what is said of the whole world. */
    DENSITY_WIDE_SIDE = 512u,
    DENSITY_WIDE_COLUMNS = DENSITY_WIDE_SIDE * DENSITY_WIDE_SIDE,
    DENSITY_WIDE_SEEDS = 8u,
    /* Columns tested through their whole height, for each seed. */
    DENSITY_TALL_SIDE = 48u,
    DENSITY_TALL_COLUMNS = DENSITY_TALL_SIDE * DENSITY_TALL_SIDE,
    DENSITY_TALL_SEEDS = 4u,
};
static const int32_t DENSITY_WIDE_STEP = 193;
static const int32_t DENSITY_TALL_STEP = 1531;
static const int32_t DENSITY_MAP_MIN = -3;
static const int64_t DENSITY_WAIT_NS = 60 * TIME_NS_PER_SECOND;
static const uint64_t DENSITY_WORLD = 0x1234567890ABCDEFull;
static const uint64_t DENSITY_SEEDS[DENSITY_WIDE_SEEDS] = {
    0x1234567890ABCDEFull, 1ull, 2ull, 0xFFFFFFFFFFFFFFFFull, 0x4D494E4543524146ull, 42ull, 1000003ull, 0ull,
};

/* Pinned: see README.md. */
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
static const uint64_t DENSITY_DIGEST_PIN = 0x2E475FECA46EAE66ull;

static uint64_t density_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static terrain_density_box_t density_tall_box(int32_t x, int32_t y, uint32_t across) {
    return (terrain_density_box_t){
        .origin_x = x,
        .origin_y = y,
        .origin_z = MC_WORLD_Z_MIN,
        .count_x = across,
        .count_y = across,
        .count_z = DENSITY_LAYERS,
    };
}

static uint64_t density_voxel(const terrain_density_box_t *box, uint32_t ix, uint32_t iy, uint32_t iz) {
    return (uint64_t)ix + (uint64_t)box->count_x * ((uint64_t)iy + (uint64_t)box->count_y * iz);
}

/* ---- The plan's numbers ------------------------------------------------------------------------------------------- */

TEST(the_limits_are_the_ones_the_plan_and_the_yaml_give) {
    CHECK_EQ_I64(MC_WORLD_Z_MIN, -250);
    CHECK_EQ_I64(MC_WORLD_Z_SEA, 0);
    CHECK_EQ_I64(MC_WORLD_Z_MAX, 1250);
    CHECK_EQ_I64(MC_WORLD_Z_MAX - MC_WORLD_Z_MIN + 1, 1501);
    CHECK_EQ_I64(COLUMN_Z_GAMEPLAY_MIN, MC_WORLD_Z_MIN);
    CHECK_EQ_I64(COLUMN_Z_GAMEPLAY_MAX, MC_WORLD_Z_MAX);
    /* The limits in the header, added up from the README's table, not read from the header's sums. */
    CHECK_EQ_I64(TERRAIN_DENSITY_HEIGHT_MAX, 64 + 320 + 36 + 34);
    CHECK_EQ_I64(TERRAIN_DENSITY_HEIGHT_MIN, -110 - 6);
    CHECK_EQ_I64(TERRAIN_DENSITY_SOFTNESS_MAX, 4 + 26);
    CHECK_EQ_I64(TERRAIN_DENSITY_ALWAYS_SOLID_TOP, -155);
    CHECK_EQ_I64(TERRAIN_DENSITY_NEVER_SOLID_FROM, 492);
}

/* ---- Splines ------------------------------------------------------------------------------------------------------ */

/* The knots again, typed from the README, one row per spline. */
typedef struct density_expected_knot_t {
    float64_t x; /* 0 */
    float64_t y; /* 8 */
} density_expected_knot_t;
static_assert(sizeof(density_expected_knot_t) == 16);

static void density_check_spline(test_context_t *t, const terrain_density_spline_t *spline,
                                 const density_expected_knot_t *knots, uint32_t count) {
    CHECK_EQ_U64(spline->count, count);
    for (uint32_t i = 0u; i < count; i++) {
        CHECK(terrain_density_spline_at(spline, knots[i].x) == knots[i].y);
    }
    CHECK(terrain_density_spline_at(spline, -2.0f64) == knots[0].y);
    CHECK(terrain_density_spline_at(spline, 2.0f64) == knots[count - 1u].y);
    /* Each piece stays between its knots and only moves one way, however it curves. */
    for (uint32_t i = 0u; i + 1u < count; i++) {
        const float64_t rising = knots[i + 1u].y >= knots[i].y ? 1.0f64 : -1.0f64;
        float64_t last = knots[i].y;
        for (uint32_t step = 1u; step < 256u; step++) {
            const float64_t x = knots[i].x + (knots[i + 1u].x - knots[i].x) * (float64_t)step / 256.0f64;
            const float64_t y = terrain_density_spline_at(spline, x);
            CHECK(rising * (y - last) >= 0.0f64);
            CHECK(y >= fmin(knots[i].y, knots[i + 1u].y) && y <= fmax(knots[i].y, knots[i + 1u].y));
            last = y;
        }
    }
    /* No corner: the slope arriving at a knot is the slope leaving it. */
    for (uint32_t i = 1u; i + 1u < count; i++) {
        const float64_t reach = 1.0e-6f64;
        const float64_t left =
            (terrain_density_spline_at(spline, knots[i].x) - terrain_density_spline_at(spline, knots[i].x - reach)) /
            reach;
        const float64_t right =
            (terrain_density_spline_at(spline, knots[i].x + reach) - terrain_density_spline_at(spline, knots[i].x)) /
            reach;
        const float64_t scale = fmax(1.0f64, fmax(fabs(left), fabs(right)));
        CHECK(fabs(left - right) / scale < 1.0e-3f64);
    }
}

#define DENSITY_CHECK_SPLINE(field, ...)                                                                               \
    do {                                                                                                               \
        static const density_expected_knot_t knots[] = {__VA_ARGS__};                                                  \
        density_check_spline(t, &density->field, knots, (uint32_t)(sizeof(knots) / sizeof(knots[0])));                 \
    } while (false)

TEST(splines_go_through_their_knots_and_never_past_them) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    DENSITY_CHECK_SPLINE(continental, {-1.0f64, -110.0f64}, {-0.88f64, -90.0f64}, {-0.77f64, -64.0f64},
                         {-0.62f64, -36.0f64}, {-0.52f64, -10.0f64}, {-0.47f64, 3.0f64}, {-0.35f64, 10.0f64},
                         {-0.07f64, 24.0f64}, {0.43f64, 44.0f64}, {1.0f64, 64.0f64});
    DENSITY_CHECK_SPLINE(inland, {-1.0f64, 0.0f64}, {-0.52f64, 0.0f64}, {-0.35f64, 0.1f64}, {-0.07f64, 0.6f64},
                         {0.21f64, 1.0f64}, {1.0f64, 1.0f64});
    DENSITY_CHECK_SPLINE(relief, {-1.0f64, 1.0f64}, {-0.5f64, 0.9f64}, {-0.2f64, 0.55f64}, {0.1f64, 0.2f64},
                         {0.4f64, 0.05f64}, {0.7f64, 0.0f64}, {1.0f64, 0.0f64});
    DENSITY_CHECK_SPLINE(peak, {-1.0f64, 0.0f64}, {-0.5f64, 0.029023f64}, {0.0f64, 0.111111f64}, {0.25f64, 0.197688f64},
                         {0.5f64, 0.343292f64}, {0.75f64, 0.588169f64}, {1.0f64, 1.0f64});
    DENSITY_CHECK_SPLINE(valley, {-1.0f64, 0.3f64}, {-0.6f64, 0.65f64}, {-0.2f64, 1.0f64}, {1.0f64, 1.0f64});
    DENSITY_CHECK_SPLINE(hill_land, {-1.0f64, 0.0f64}, {-0.47f64, 0.0f64}, {-0.22f64, 1.0f64}, {1.0f64, 1.0f64});
    DENSITY_CHECK_SPLINE(hill_sea, {-1.0f64, 1.0f64}, {-0.6f64, 1.0f64}, {-0.53f64, 0.0f64}, {1.0f64, 0.0f64});
    DENSITY_CHECK_SPLINE(hill_relief, {-1.0f64, 1.0f64}, {0.0f64, 0.5f64}, {0.6f64, 0.12f64}, {1.0f64, 0.0f64});
    terrain_deinit(&terrain);
}

/* The README's formula, typed out again. */
static terrain_density_profile_t density_expected_profile(const terrain_density_t *density,
                                                          const terrain_density_inputs_t *in) {
    const float64_t c = (float64_t)in->continentalness;
    const float64_t e = (float64_t)in->erosion;
    const float64_t r = (float64_t)in->ridges;
    float64_t base = terrain_density_spline_at(&density->continental, c);
    if (base > 0.0f64) {
        base *= terrain_density_spline_at(&density->valley, r);
    }
    const float64_t high =
        terrain_density_spline_at(&density->inland, c) * terrain_density_spline_at(&density->relief, e);
    const float64_t mountain = high * terrain_density_spline_at(&density->peak, r);
    const float64_t hill = terrain_density_spline_at(&density->hill_land, c) *
                               (6.0f64 + 30.0f64 * terrain_density_spline_at(&density->hill_relief, e)) +
                           terrain_density_spline_at(&density->hill_sea, c) * 6.0f64;
    return (terrain_density_profile_t){
        .height = (float32_t)(base + 320.0f64 * mountain + hill * (float64_t)in->hills +
                              34.0f64 * mountain * (float64_t)in->jagged),
        .softness = (float32_t)(4.0f64 + 26.0f64 * high),
    };
}

/* Every corner of what the noise can give, and a grid through the middle: there is no climate that can reach past a limit. */
TEST(no_climate_at_all_can_reach_past_the_height_limits) {
    enum : uint32_t { STEPS = 16u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t lowest = 1.0e9f32;
    float32_t highest = -1.0e9f32;
    float32_t softest = 1.0e9f32;
    float32_t hardest = -1.0e9f32;
    for (uint32_t ic = 0u; ic <= STEPS; ic++) {
        for (uint32_t ie = 0u; ie <= STEPS; ie++) {
            for (uint32_t ir = 0u; ir <= STEPS; ir++) {
                for (uint32_t noise = 0u; noise < 9u; noise++) {
                    const terrain_density_inputs_t inputs = {
                        .continentalness = (float32_t)ic / (float32_t)STEPS * 2.0f32 - 1.0f32,
                        .erosion = (float32_t)ie / (float32_t)STEPS * 2.0f32 - 1.0f32,
                        .ridges = (float32_t)ir / (float32_t)STEPS * 2.0f32 - 1.0f32,
                        .hills = (float32_t)(noise % 3u) - 1.0f32,
                        .jagged = (float32_t)(noise / 3u) - 1.0f32,
                    };
                    const terrain_density_profile_t profile = terrain_density_profile_from(density, &inputs);
                    const terrain_density_profile_t expected = density_expected_profile(density, &inputs);
                    CHECK(memcmp(&profile, &expected, sizeof(profile)) == 0);
                    lowest = fminf(lowest, profile.height);
                    highest = fmaxf(highest, profile.height);
                    softest = fminf(softest, profile.softness);
                    hardest = fmaxf(hardest, profile.softness);
                }
            }
        }
    }
    printf("over the grid: height %.1f to %.1f, softness %.1f to %.1f\n", (libc_double_t)lowest, (libc_double_t)highest,
           (libc_double_t)softest, (libc_double_t)hardest);
    /* The grid holds both ends of every input, so each limit is reached and is not only an upper bound on paper. */
    CHECK(lowest == (float32_t)TERRAIN_DENSITY_HEIGHT_MIN);
    CHECK(highest == (float32_t)TERRAIN_DENSITY_HEIGHT_MAX);
    CHECK(softest == (float32_t)TERRAIN_DENSITY_SOFTNESS_MIN);
    CHECK(hardest == (float32_t)TERRAIN_DENSITY_SOFTNESS_MAX);
    terrain_deinit(&terrain);
}

/* ---- The range limits, on real columns ----------------------------------------------------------------------------- */

/* Whole columns, from below the storage's bottom to above its top, far apart, on several seeds. Every claim the README makes
 * about what is at which z. */
TEST(every_column_has_bedrock_at_the_bottom_stone_above_it_and_no_ground_near_the_top) {
    const int32_t below = COLUMN_Z_STORAGE_MIN;
    const uint32_t layers = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN);
    uint16_t *const blocks = test_alloc(t, (uint64_t)layers * sizeof(uint16_t));
    int32_t lowest_open = INT32_MAX;
    int32_t highest_solid = INT32_MIN;
    int32_t highest_water = INT32_MIN;
    uint64_t columns = 0u;
    uint64_t sea_columns = 0u;
    uint64_t dry_columns = 0u;

    for (uint32_t seed = 0u; seed < DENSITY_TALL_SEEDS; seed++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, DENSITY_SEEDS[seed]));
        for (uint32_t c = 0u; c < DENSITY_TALL_COLUMNS; c++) {
            const int32_t half = (int32_t)(DENSITY_TALL_SIDE / 2u) * DENSITY_TALL_STEP;
            const terrain_density_box_t box = {
                .origin_x = -half + (int32_t)(c % DENSITY_TALL_SIDE) * DENSITY_TALL_STEP,
                .origin_y = -half + (int32_t)(c / DENSITY_TALL_SIDE) * DENSITY_TALL_STEP,
                .origin_z = below,
                .count_x = 1u,
                .count_y = 1u,
                .count_z = layers,
            };
            terrain_density_fill_box(terrain.borrowed_density, &box, blocks);
            columns++;
            int32_t column_top_water = INT32_MIN;
            bool holds_water = false;
            for (uint32_t i = 0u; i < layers; i++) {
                const int32_t z = below + (int32_t)i;
                const uint16_t block = blocks[i];
                if (z < MC_WORLD_Z_MIN || z > MC_WORLD_Z_MAX) {
                    CHECK_EQ_U64(block, BLOCK_ID_AIR);
                } else if (z == MC_WORLD_Z_MIN) {
                    CHECK_EQ_U64(block, BLOCK_ID_BEDROCK);
                } else {
                    CHECK(block != BLOCK_ID_BEDROCK);
                }
                if (z > MC_WORLD_Z_MIN && z <= TERRAIN_DENSITY_ALWAYS_SOLID_TOP) {
                    CHECK_EQ_U64(block, BLOCK_ID_STONE);
                }
                if (z >= TERRAIN_DENSITY_NEVER_SOLID_FROM) {
                    CHECK_EQ_U64(block, BLOCK_ID_AIR);
                }
                if (z > MC_WORLD_Z_SEA) {
                    CHECK(block != BLOCK_ID_WATER);
                } else if (z >= MC_WORLD_Z_MIN) {
                    CHECK(block != BLOCK_ID_AIR);
                }
                if (block == BLOCK_ID_STONE) {
                    highest_solid = z > highest_solid ? z : highest_solid;
                } else if (block == BLOCK_ID_WATER) {
                    highest_water = z > highest_water ? z : highest_water;
                    column_top_water = z > column_top_water ? z : column_top_water;
                    holds_water = true;
                    lowest_open = z < lowest_open ? z : lowest_open;
                } else if (block == BLOCK_ID_AIR && z > MC_WORLD_Z_MIN && z <= MC_WORLD_Z_MAX) {
                    lowest_open = z < lowest_open ? z : lowest_open;
                }
            }
            /* A column of open sea reaches the sea's surface: water's top block is at z = 0. Where z = 0 is stone, any water
             * lies under an overhang at the shore, which is still water for lack of a better idea until Stage 10.4. */
            const uint16_t at_sea = blocks[(uint32_t)(MC_WORLD_Z_SEA - below)];
            if (at_sea == BLOCK_ID_WATER) {
                sea_columns++;
                CHECK_EQ_I64(column_top_water, MC_WORLD_Z_SEA);
                CHECK_EQ_U64(blocks[(uint32_t)(MC_WORLD_Z_SEA + 1 - below)], BLOCK_ID_AIR);
            } else if (!holds_water) {
                dry_columns++;
            }
        }
        terrain_deinit(&terrain);
    }
    printf("%" PRIu64 " columns, %" PRIu64 " reach the sea at z = 0, %" PRIu64 " hold no water; highest stone %" PRId32
           ", highest water %" PRId32 ", lowest open %" PRId32 "\n",
           columns, sea_columns, dry_columns, highest_solid, highest_water, lowest_open);
    CHECK_EQ_I64(highest_water, 0);
    CHECK(highest_solid < TERRAIN_DENSITY_NEVER_SOLID_FROM);
    CHECK(highest_solid > 100);
    CHECK(lowest_open > TERRAIN_DENSITY_ALWAYS_SOLID_TOP);
    CHECK(sea_columns > columns / 5u);
    CHECK(dry_columns > columns / 5u);
}

/* The hard edges: one voxel, by itself, just either side of every limit, at extreme columns too. */
TEST(one_voxel_asked_alone_obeys_the_limits) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    static const int32_t places[] = {0, -1, 1, 15, 16, -16, 1000003, -1000003, 8388607, -8388608};
    for (uint32_t a = 0u; a < sizeof(places) / sizeof(places[0]); a++) {
        for (uint32_t b = 0u; b < sizeof(places) / sizeof(places[0]); b++) {
            const int32_t x = places[a];
            const int32_t y = places[b];
            CHECK_EQ_U64(terrain_density_block_at(density, x, y, MC_WORLD_Z_MIN - 1), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_density_block_at(density, x, y, MC_WORLD_Z_MIN), BLOCK_ID_BEDROCK);
            CHECK_EQ_U64(terrain_density_block_at(density, x, y, MC_WORLD_Z_MIN + 1), BLOCK_ID_STONE);
            CHECK_EQ_U64(terrain_density_block_at(density, x, y, MC_WORLD_Z_MAX), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_density_block_at(density, x, y, MC_WORLD_Z_MAX + 1), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_density_block_at(density, x, y, INT32_MAX), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_density_block_at(density, x, y, INT32_MIN), BLOCK_ID_AIR);
            const uint16_t at_sea = terrain_density_block_at(density, x, y, MC_WORLD_Z_SEA);
            const uint16_t above_sea = terrain_density_block_at(density, x, y, MC_WORLD_Z_SEA + 1);
            CHECK(at_sea == BLOCK_ID_STONE || at_sea == BLOCK_ID_WATER);
            CHECK(above_sea == BLOCK_ID_STONE || above_sea == BLOCK_ID_AIR);
            /* The number has no limits in it; only the block does. */
            CHECK(terrain_density_at(density, x, y, MC_WORLD_Z_MIN - 1) > 0.0f32);
            CHECK(terrain_density_at(density, x, y, MC_WORLD_Z_MAX + 1) < 0.0f32);
        }
    }
    /* The very ends of a box with the most columns a test can stand. Nothing wraps. */
    const terrain_density_box_t corner = {
        .origin_x = INT32_MAX - 1,
        .origin_y = INT32_MIN,
        .origin_z = -2,
        .count_x = 2u,
        .count_y = 2u,
        .count_z = 4u,
    };
    float32_t values[16];
    uint16_t blocks[16];
    terrain_density_sample_box(density, &corner, values);
    terrain_density_fill_box(density, &corner, blocks);
    for (uint32_t i = 0u; i < 16u; i++) {
        CHECK(isfinite(values[i]));
        CHECK_EQ_U64(blocks[i], values[i] > 0.0f32 ? BLOCK_ID_STONE : (i < 12u ? BLOCK_ID_WATER : BLOCK_ID_AIR));
    }
    terrain_deinit(&terrain);
}

/* What the blocks are worth to the world: a real column stores them, and hands the limits back the same way. */
TEST(a_real_column_stores_what_the_density_fills_it_with) {
    enum : uint32_t { POOL_COLUMNS = 8u };
    uint8_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
    world_t *const world = (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    REQUIRE(world_init(world, (world_limits_t){64ull * 1024ull * 1024ull, POOL_COLUMNS}));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;

    column_t column;
    column_init(&column, -2, 5);
    const int32_t origin_x = -2 * (int32_t)COLUMN_SIZE;
    const int32_t origin_y = 5 * (int32_t)COLUMN_SIZE;
    const terrain_density_box_t box = density_tall_box(origin_x, origin_y, COLUMN_SIZE);
    uint16_t *const blocks = test_alloc(t, terrain_density_box_voxels(&box) * sizeof(uint16_t));
    terrain_density_fill_box(density, &box, blocks);
    for (uint32_t iz = 0u; iz < DENSITY_LAYERS; iz++) {
        for (uint32_t iy = 0u; iy < COLUMN_SIZE; iy++) {
            for (uint32_t ix = 0u; ix < COLUMN_SIZE; ix++) {
                const column_status_t status =
                    column_block_set(&column, &world->section_pool, ix, iy, MC_WORLD_Z_MIN + (int32_t)iz,
                                     blocks[density_voxel(&box, ix, iy, iz)]);
                CHECK_EQ_U64(status, COLUMN_STATUS_OK);
            }
        }
    }
    for (uint32_t iy = 0u; iy < COLUMN_SIZE; iy += 5u) {
        for (uint32_t ix = 0u; ix < COLUMN_SIZE; ix += 5u) {
            uint16_t held = 0u;
            CHECK(column_block_get(&column, ix, iy, MC_WORLD_Z_MIN, &held));
            CHECK_EQ_U64(held, BLOCK_ID_BEDROCK);
            CHECK(column_block_get(&column, ix, iy, MC_WORLD_Z_MAX, &held));
            CHECK_EQ_U64(held, BLOCK_ID_AIR);
            CHECK(!column_block_get(&column, ix, iy, MC_WORLD_Z_MIN - 1, &held));
            CHECK(column_block_get(&column, ix, iy, 17, &held));
            CHECK_EQ_U64(held, terrain_density_block_at(density, origin_x + (int32_t)ix, origin_y + (int32_t)iy, 17));
        }
    }
    column_deinit(&column, &world->section_pool);
    terrain_deinit(&terrain);
    world_deinit(world);
}

/* ---- Each value worked out by hand -------------------------------------------------------------------------------- */

static float32_t density_expected_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = fmin(1.0f64, fmax(-1.0f64, unit));
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

/* A field worked out on the scalar kernel: the sum of its octaves at a point. */
static float32_t density_scalar_field(const terrain_noise_field_t *field, float64_t x, float64_t y, float64_t z) {
    float32_t sum = 0.0f32;
    for (uint32_t o = 0u; o < field->octave_count; o++) {
        const terrain_noise_octave_t *const octave = &field->octaves[o];
        const float32_t cell_x = (float32_t)(x * octave->frequency_x + octave->offset_x);
        const float32_t cell_y = (float32_t)(y * octave->frequency_y + octave->offset_y);
        const float32_t cell_z = (float32_t)(z * octave->frequency_z + octave->offset_z);
        float32_t value = 0.0f32;
        simd_reference_noise3(&value, &cell_x, &cell_y, &cell_z, 1u, octave->seed);
        sum += value * octave->amplitude;
    }
    return sum;
}

/* The density at a lattice point, from the README and the scalar kernel. */
static float32_t density_expected_corner(const terrain_t *terrain, int32_t x, int32_t y, int32_t z) {
    const terrain_density_t *const density = terrain->borrowed_density;
    terrain_climate_point_t climate;
    terrain_climate_at(terrain->borrowed_climate, x, y, &climate);
    const terrain_density_inputs_t inputs = {
        .continentalness = climate.continentalness,
        .erosion = climate.erosion,
        .ridges = climate.ridges,
        .hills = density_expected_spread(density_scalar_field(density->borrowed_hills, x, y, 0.0f64), 0.34f64),
        .jagged = density_expected_spread(density_scalar_field(density->borrowed_jagged, x, y, 0.0f64), 0.39f64),
    };
    const terrain_density_profile_t profile = density_expected_profile(density, &inputs);
    const float32_t depth = (profile.height - (float32_t)z) / profile.softness;
    const float32_t size = fabsf(depth);
    if (size >= 2.0f32) {
        return depth;
    }
    const float32_t fade = size <= 1.0f32 ? 1.0f32 : 2.0f32 - size;
    return depth + density_expected_spread(density_scalar_field(density->borrowed_detail, x, y, z), 0.35f64) * fade;
}

static float32_t density_lerp(float32_t a, float32_t b, float32_t t) {
    return a + t * (b - a);
}

TEST(each_value_is_worked_out_as_the_readme_says) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;

    /* Find places of every kind: a shore, a plain, the sea's floor, the highest ground around. Each is near where something
     * changes, so the noise has work to do. */
    enum : uint32_t { SEARCH_SIDE = 256u, SEARCH_COLUMNS = SEARCH_SIDE * SEARCH_SIDE, PLACES = 96u };
    float32_t *const heights = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    float32_t *const softness = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    const terrain_climate_area_t search = {
        .origin_x = -32768,
        .origin_y = -32768,
        .step_x = 256,
        .step_y = 256,
        .count_x = SEARCH_SIDE,
        .count_y = SEARCH_SIDE,
    };
    terrain_density_profile_area(density, &search, heights, softness);
    uint64_t compared = 0u;
    uint64_t in_band = 0u;
    for (uint32_t p = 0u; p < PLACES; p++) {
        const uint32_t pick = (p * 40503u + 17u) * 65599u % SEARCH_COLUMNS;
        const int32_t x = search.origin_x + (int32_t)(pick % SEARCH_SIDE) * search.step_x + (int32_t)(p % 7u);
        const int32_t y = search.origin_y + (int32_t)(pick / SEARCH_SIDE) * search.step_y + (int32_t)(p % 5u);
        /* Through the height where the ground is, in steps that land on all of a cell's fractions. */
        const int32_t around = (int32_t)heights[pick];
        for (int32_t dz = -20; dz <= 20; dz += 3) {
            const int32_t z = around + dz;
            const int32_t x0 = (int32_t)floor((float64_t)x / 4.0f64) * 4;
            const int32_t y0 = (int32_t)floor((float64_t)y / 4.0f64) * 4;
            const int32_t z0 = (int32_t)floor((float64_t)(z + 250) / 8.0f64) * 8 - 250;
            const float32_t tx = (float32_t)(x - x0) / 4.0f32;
            const float32_t ty = (float32_t)(y - y0) / 4.0f32;
            const float32_t tz = (float32_t)(z - z0) / 8.0f32;
            float32_t corners[2][2];
            for (uint32_t b = 0u; b < 2u; b++) {
                for (uint32_t a = 0u; a < 2u; a++) {
                    const int32_t cx = x0 + (int32_t)a * 4;
                    const int32_t cy = y0 + (int32_t)b * 4;
                    corners[b][a] = density_lerp(density_expected_corner(&terrain, cx, cy, z0),
                                                 density_expected_corner(&terrain, cx, cy, z0 + 8), tz);
                }
            }
            const float32_t expected = density_lerp(density_lerp(corners[0][0], corners[0][1], tx),
                                                    density_lerp(corners[1][0], corners[1][1], tx), ty);
            const float32_t actual = terrain_density_at(density, x, y, z);
            CHECK(fabsf(actual - expected) < 1.0e-5f32 * fmaxf(1.0f32, fabsf(expected)));
            compared++;
            in_band += fabsf(expected) < 1.0f32 ? 1u : 0u;
        }
    }
    printf("%" PRIu64 " voxels worked out by hand, %" PRIu64 " of them near the ground\n", compared, in_band);
    CHECK(in_band > compared / 8u);
    terrain_deinit(&terrain);
}

/* Each field's limit is meant to be about twice what it measures, so that spread-out noise is well used and rarely at an end.
 * Prints what it finds, so that a change of shape can be judged by its numbers. */
TEST(raw_noise_measures_what_the_limits_were_chosen_for) {
    enum : uint32_t { LOTS = 1u << 20 };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t *const raw = test_alloc(t, LOTS * sizeof(float32_t));
    float64_t *const xs = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const zs = test_alloc(t, LOTS * sizeof(float64_t));
    const terrain_noise_field_t *const fields[TERRAIN_DENSITY_NOISE_FIELDS] = {
        density->borrowed_hills, density->borrowed_jagged, density->borrowed_detail};
    static const float64_t limits[TERRAIN_DENSITY_NOISE_FIELDS] = {0.34f64, 0.39f64, 0.35f64};
    static const char *const names[TERRAIN_DENSITY_NOISE_FIELDS] = {"hills", "jagged", "detail"};
    for (uint32_t i = 0u; i < LOTS; i++) {
        xs[i] = (float64_t)((int32_t)(i % 1024u) * 83 - 42000);
        ys[i] = (float64_t)((int32_t)(i / 1024u) * 89 - 45000);
        zs[i] = (float64_t)((int32_t)(i % 751u) * 31 - 11000);
    }
    for (uint32_t f = 0u; f < TERRAIN_DENSITY_NOISE_FIELDS; f++) {
        terrain_noise_sample_points(fields[f], xs, ys, zs, LOTS, raw);
        float64_t squares = 0.0f64;
        uint64_t past = 0u;
        for (uint32_t i = 0u; i < LOTS; i++) {
            squares += (float64_t)raw[i] * (float64_t)raw[i];
            past += fabs((float64_t)raw[i]) >= limits[f] ? 1u : 0u;
        }
        const float64_t deviation = sqrt(squares / (float64_t)LOTS);
        const float64_t share = (float64_t)past / (float64_t)LOTS;
        printf("%-7s raw standard deviation %.4f; limit is %.2f of them; %.2f%% past it\n", names[f],
               (libc_double_t)deviation, (libc_double_t)(limits[f] / deviation), (libc_double_t)(share * 100.0f64));
        CHECK(limits[f] > 1.5f64 * deviation && limits[f] < 2.5f64 * deviation);
        CHECK(share < 0.08f64);
    }
    terrain_deinit(&terrain);
}

/* ---- The shape of the world ---------------------------------------------------------------------------------------- */

/* Not the look of it, which is for Ahmed's biome tour: only that the height splines are doing what they are there to do. */
TEST(the_world_has_seas_plains_and_mountains_in_believable_shares) {
    float32_t *const heights = test_alloc(t, (uint64_t)DENSITY_WIDE_COLUMNS * sizeof(float32_t));
    float32_t *const softness = test_alloc(t, (uint64_t)DENSITY_WIDE_COLUMNS * sizeof(float32_t));
    uint64_t sea = 0u;
    uint64_t deep = 0u;
    uint64_t low_land = 0u;
    uint64_t high = 0u;
    uint64_t very_high = 0u;
    float32_t lowest = 1.0e9f32;
    float32_t highest = -1.0e9f32;
    for (uint32_t s = 0u; s < DENSITY_WIDE_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, DENSITY_SEEDS[s]));
        const int32_t half = (int32_t)(DENSITY_WIDE_SIDE / 2u) * DENSITY_WIDE_STEP;
        const terrain_climate_area_t area = {
            .origin_x = -half,
            .origin_y = -half,
            .step_x = DENSITY_WIDE_STEP,
            .step_y = DENSITY_WIDE_STEP,
            .count_x = DENSITY_WIDE_SIDE,
            .count_y = DENSITY_WIDE_SIDE,
        };
        terrain_density_profile_area(terrain.borrowed_density, &area, heights, softness);
        for (uint32_t i = 0u; i < DENSITY_WIDE_COLUMNS; i++) {
            const float32_t h = heights[i];
            CHECK(h >= (float32_t)TERRAIN_DENSITY_HEIGHT_MIN && h <= (float32_t)TERRAIN_DENSITY_HEIGHT_MAX);
            CHECK(softness[i] >= (float32_t)TERRAIN_DENSITY_SOFTNESS_MIN &&
                  softness[i] <= (float32_t)TERRAIN_DENSITY_SOFTNESS_MAX);
            sea += h < 0.0f32 ? 1u : 0u;
            deep += h < -60.0f32 ? 1u : 0u;
            low_land += (h >= 0.0f32 && h < 60.0f32) ? 1u : 0u;
            high += h >= 150.0f32 ? 1u : 0u;
            very_high += h >= 300.0f32 ? 1u : 0u;
            lowest = fminf(lowest, h);
            highest = fmaxf(highest, h);
        }
        terrain_deinit(&terrain);
    }
    const float64_t all = (float64_t)DENSITY_WIDE_COLUMNS * DENSITY_WIDE_SEEDS;
    printf("under the sea %.3f, of it deep %.3f; low land %.3f; above 150 %.4f; above 300 %.5f; height %.1f to %.1f\n",
           (libc_double_t)((float64_t)sea / all), (libc_double_t)((float64_t)deep / all),
           (libc_double_t)((float64_t)low_land / all), (libc_double_t)((float64_t)high / all),
           (libc_double_t)((float64_t)very_high / all), (libc_double_t)lowest, (libc_double_t)highest);
    CHECK((float64_t)sea / all > 0.2f64 && (float64_t)sea / all < 0.35f64);
    CHECK((float64_t)deep / all > 0.05f64);
    CHECK((float64_t)low_land / all > 0.2f64);
    CHECK((float64_t)high / all > 0.01f64);
    CHECK((float64_t)very_high / all > 0.0001f64);
    CHECK(highest > 350.0f32);
    CHECK(lowest < -90.0f32);
}

/* What is above and what is below a column's ground: stone, then open. Whether it ever goes open and back is what 3D is for. */
static uint32_t density_count_overhangs(const uint16_t *blocks, const terrain_density_box_t *box, uint32_t *columns) {
    uint32_t overhangs = 0u;
    for (uint32_t iy = 0u; iy < box->count_y; iy++) {
        for (uint32_t ix = 0u; ix < box->count_x; ix++) {
            bool seen_open = false;
            bool over = false;
            for (uint32_t iz = 1u; iz < box->count_z; iz++) {
                const bool stone = blocks[density_voxel(box, ix, iy, iz)] == BLOCK_ID_STONE;
                over = over || (seen_open && stone);
                seen_open = seen_open || !stone;
            }
            overhangs += over ? 1u : 0u;
            (*columns)++;
        }
    }
    return overhangs;
}

TEST(the_density_is_really_3d_in_the_mountains_and_plain_on_the_plains) {
    enum : uint32_t { SEARCH_SIDE = 512u, SEARCH_COLUMNS = SEARCH_SIDE * SEARCH_SIDE, ACROSS = 32u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t *const heights = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    float32_t *const softness = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    const terrain_climate_area_t search = {
        .origin_x = -65536,
        .origin_y = -65536,
        .step_x = 256,
        .step_y = 256,
        .count_x = SEARCH_SIDE,
        .count_y = SEARCH_SIDE,
    };
    terrain_density_profile_area(density, &search, heights, softness);

    /* The softest high place, and all of the flattest low places. */
    uint32_t mountain = UINT32_MAX;
    float32_t mountain_height = 0.0f32;
    uint32_t plains[8];
    uint32_t plains_found = 0u;
    for (uint32_t i = 0u; i < SEARCH_COLUMNS; i++) {
        if (softness[i] > 20.0f32 && heights[i] > mountain_height) {
            mountain = i;
            mountain_height = heights[i];
        }
        if (plains_found < 8u && softness[i] < 5.0f32 && heights[i] > 8.0f32 && heights[i] < 30.0f32) {
            plains[plains_found] = i;
            plains_found++;
        }
    }
    REQUIRE(mountain != UINT32_MAX);
    REQUIRE(plains_found == 8u);

    uint16_t *const blocks = test_alloc(t, (uint64_t)ACROSS * ACROSS * DENSITY_LAYERS * sizeof(uint16_t));
    const terrain_density_box_t high_box = density_tall_box(
        search.origin_x + (int32_t)(mountain % SEARCH_SIDE) * search.step_x - (int32_t)(ACROSS / 2u),
        search.origin_y + (int32_t)(mountain / SEARCH_SIDE) * search.step_y - (int32_t)(ACROSS / 2u), ACROSS);
    terrain_density_fill_box(density, &high_box, blocks);
    uint32_t columns = 0u;
    const uint32_t over_high = density_count_overhangs(blocks, &high_box, &columns);
    printf("a mountain of %.0f blocks: %" PRIu32 " of %" PRIu32 " columns have stone over air\n",
           (libc_double_t)mountain_height, over_high, columns);

    /* Overhangs are a matter of luck in any one place, so look through a few of the high places. */
    uint32_t over_any = over_high;
    for (uint32_t i = 0u; i < SEARCH_COLUMNS && over_any == 0u; i += 7u) {
        if (softness[i] > 24.0f32) {
            const terrain_density_box_t box =
                density_tall_box(search.origin_x + (int32_t)(i % SEARCH_SIDE) * search.step_x,
                                 search.origin_y + (int32_t)(i / SEARCH_SIDE) * search.step_y, ACROSS);
            terrain_density_fill_box(density, &box, blocks);
            uint32_t ignored = 0u;
            over_any += density_count_overhangs(blocks, &box, &ignored);
        }
    }
    CHECK(over_any > 0u);

    uint32_t over_plains = 0u;
    uint32_t plain_columns = 0u;
    float32_t plain_steepest = 0.0f32;
    for (uint32_t p = 0u; p < plains_found; p++) {
        const terrain_density_box_t box =
            density_tall_box(search.origin_x + (int32_t)(plains[p] % SEARCH_SIDE) * search.step_x,
                             search.origin_y + (int32_t)(plains[p] / SEARCH_SIDE) * search.step_y, ACROSS);
        terrain_density_fill_box(density, &box, blocks);
        over_plains += density_count_overhangs(blocks, &box, &plain_columns);
        /* And the ground steps gently from one column to the next. */
        int32_t previous = INT32_MIN;
        for (uint32_t ix = 0u; ix < ACROSS; ix++) {
            int32_t top = MC_WORLD_Z_MIN;
            for (uint32_t iz = 0u; iz < DENSITY_LAYERS; iz++) {
                if (blocks[density_voxel(&box, ix, 0u, iz)] == BLOCK_ID_STONE) {
                    top = MC_WORLD_Z_MIN + (int32_t)iz;
                }
            }
            if (previous != INT32_MIN) {
                plain_steepest = fmaxf(plain_steepest, fabsf((float32_t)(top - previous)));
            }
            previous = top;
        }
    }
    printf("plains: %" PRIu32 " of %" PRIu32 " columns have stone over air; steepest step %.0f blocks\n", over_plains,
           plain_columns, (libc_double_t)plain_steepest);
    CHECK_EQ_U64(over_plains, 0u);
    CHECK(plain_steepest <= 3.0f32);
    terrain_deinit(&terrain);
}

/* ---- Any split of the work, and any worker ------------------------------------------------------------------------- */

/* The blocks must be the density's sign, on the fast path too. */
TEST(the_blocks_are_the_numbers_sign_voxel_for_voxel) {
    enum : uint32_t { ACROSS = 40u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    const terrain_density_box_t box = density_tall_box(-1234, 777, ACROSS);
    const uint64_t voxels = terrain_density_box_voxels(&box);
    CHECK_EQ_U64(voxels, (uint64_t)ACROSS * ACROSS * DENSITY_LAYERS);
    float32_t *const values = test_alloc(t, voxels * sizeof(float32_t));
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    terrain_density_sample_box(density, &box, values);
    terrain_density_fill_box(density, &box, blocks);
    uint64_t solid = 0u;
    for (uint64_t i = 0u; i < voxels; i++) {
        const int32_t z = MC_WORLD_Z_MIN + (int32_t)(i / ((uint64_t)ACROSS * ACROSS));
        const uint16_t expected =
            z == MC_WORLD_Z_MIN ? (uint16_t)BLOCK_ID_BEDROCK
                                : (values[i] > 0.0f32 ? (uint16_t)BLOCK_ID_STONE
                                                      : (z <= 0 ? (uint16_t)BLOCK_ID_WATER : (uint16_t)BLOCK_ID_AIR));
        if (!CHECK_EQ_U64(blocks[i], expected)) {
            break;
        }
        solid += blocks[i] == BLOCK_ID_STONE ? 1u : 0u;
    }
    CHECK(solid > 0u && solid < voxels);
    terrain_deinit(&terrain);
}

/* Every way of asking must give the same bits: a box, rows backwards, slices of every size across the tiles' and cells' edges,
 * one voxel at a time. */
TEST(any_split_of_the_work_gives_the_same_bits) {
    enum : uint32_t { ACROSS = 70u, TALL = 90u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    /* Starting off every grid at once, and through where the ground changes hands. */
    const terrain_density_box_t whole = {
        .origin_x = -35,
        .origin_y = 3,
        .origin_z = -45,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    float32_t *const values = test_alloc(t, VOXELS * sizeof(float32_t));
    float32_t *const parts = test_alloc(t, VOXELS * sizeof(float32_t));
    uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const block_parts = test_alloc(t, VOXELS * sizeof(uint16_t));
    terrain_density_sample_box(density, &whole, values);
    terrain_density_fill_box(density, &whole, blocks);

    /* Slices of each thickness, in z and y, from the top down. */
    const uint32_t lengths[] = {1u, 3u, 7u, 8u, 9u, 16u, 33u};
    for (uint32_t which = 0u; which < sizeof(lengths) / sizeof(lengths[0]); which++) {
        memset(parts, 0, VOXELS * sizeof(float32_t));
        memset(block_parts, 0, VOXELS * sizeof(uint16_t));
        for (uint32_t z_end = TALL; z_end > 0u;) {
            const uint32_t length = z_end < lengths[which] ? z_end : lengths[which];
            const uint32_t iz = z_end - length;
            terrain_density_box_t slice = whole;
            slice.origin_z = whole.origin_z + (int32_t)iz;
            slice.count_z = length;
            const uint64_t at = (uint64_t)iz * ACROSS * ACROSS;
            terrain_density_sample_box(density, &slice, parts + at);
            terrain_density_fill_box(density, &slice, block_parts + at);
            z_end = iz;
        }
        CHECK_MEM_EQ(values, parts, VOXELS * sizeof(float32_t));
        CHECK_MEM_EQ(blocks, block_parts, VOXELS * sizeof(uint16_t));

        memset(parts, 0, VOXELS * sizeof(float32_t));
        for (uint32_t iz = 0u; iz < TALL; iz++) {
            for (uint32_t iy = 0u; iy < ACROSS; iy += lengths[which]) {
                const uint32_t length = ACROSS - iy < lengths[which] ? ACROSS - iy : lengths[which];
                const terrain_density_box_t slice = {
                    .origin_x = whole.origin_x,
                    .origin_y = whole.origin_y + (int32_t)iy,
                    .origin_z = whole.origin_z + (int32_t)iz,
                    .count_x = ACROSS,
                    .count_y = length,
                    .count_z = 1u,
                };
                terrain_density_sample_box(density, &slice, parts + ((uint64_t)iz * ACROSS + iy) * ACROSS);
            }
        }
        CHECK_MEM_EQ(values, parts, VOXELS * sizeof(float32_t));
    }

    /* One voxel at a time, every so often: the whole lattice worked out for it, against what a tile made. */
    for (uint32_t i = 0u; i < VOXELS; i += 397u) {
        const int32_t x = whole.origin_x + (int32_t)(i % ACROSS);
        const int32_t y = whole.origin_y + (int32_t)(i / ACROSS % ACROSS);
        const int32_t z = whole.origin_z + (int32_t)(i / (ACROSS * ACROSS));
        const float32_t value = terrain_density_at(density, x, y, z);
        CHECK_MEM_EQ(&value, &values[i], sizeof(value));
        CHECK_EQ_U64(terrain_density_block_at(density, x, y, z), blocks[i]);
    }

    /* Nothing asked for: nothing written, and no crash. */
    const terrain_density_box_t nothing = {.origin_x = 5, .origin_y = 5, .origin_z = 5, .count_x = 0u, .count_y = 3u};
    terrain_density_sample_box(density, &nothing, parts);
    terrain_density_fill_box(density, &nothing, block_parts);
    CHECK_EQ_U64(terrain_density_box_voxels(&nothing), 0u);
    terrain_deinit(&terrain);
}

/* A value from a column's profile must not change with what else was asked for, either. */
TEST(profiles_do_not_depend_on_the_area_asked_for) {
    enum : uint32_t { ACROSS = 97u, COLUMNS = ACROSS * ACROSS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t *const heights = test_alloc(t, COLUMNS * sizeof(float32_t));
    float32_t *const softness = test_alloc(t, COLUMNS * sizeof(float32_t));
    const terrain_climate_area_t area = {
        .origin_x = -48,
        .origin_y = 1000,
        .step_x = 1,
        .step_y = 1,
        .count_x = ACROSS,
        .count_y = ACROSS,
    };
    CHECK_EQ_U64(terrain_climate_area_columns(&area), COLUMNS);
    terrain_density_profile_area(density, &area, heights, softness);
    for (uint32_t i = 0u; i < COLUMNS; i += 13u) {
        const terrain_climate_area_t one = {
            .origin_x = area.origin_x + (int32_t)(i % ACROSS),
            .origin_y = area.origin_y + (int32_t)(i / ACROSS),
            .step_x = 1,
            .step_y = 1,
            .count_x = 1u,
            .count_y = 1u,
        };
        float32_t height = 0.0f32;
        float32_t soft = 0.0f32;
        terrain_density_profile_area(density, &one, &height, &soft);
        CHECK_MEM_EQ(&height, &heights[i], sizeof(height));
        CHECK_MEM_EQ(&soft, &softness[i], sizeof(soft));
    }
    terrain_deinit(&terrain);
}

/* No seam at a tile's edge, nor a cell's: the density moves by a small step from each voxel to the next, everywhere. */
TEST(there_is_no_seam_at_a_tile_or_a_cell) {
    enum : uint32_t { ACROSS = 96u, TALL = 64u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t *const values = test_alloc(t, VOXELS * sizeof(float32_t));
    const terrain_density_box_t box = {
        .origin_x = -48,
        .origin_y = -48,
        .origin_z = -32,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    terrain_density_sample_box(density, &box, values);
    float32_t steepest_on_edge = 0.0f32;
    float32_t steepest_inside = 0.0f32;
    for (uint32_t iz = 0u; iz < TALL; iz++) {
        for (uint32_t iy = 0u; iy < ACROSS; iy++) {
            for (uint32_t ix = 0u; ix + 1u < ACROSS; ix++) {
                const float32_t step =
                    fabsf(values[density_voxel(&box, ix + 1u, iy, iz)] - values[density_voxel(&box, ix, iy, iz)]);
                const int32_t x = box.origin_x + (int32_t)ix + 1;
                if ((x & 31) == 0) {
                    steepest_on_edge = fmaxf(steepest_on_edge, step);
                } else {
                    steepest_inside = fmaxf(steepest_inside, step);
                }
            }
        }
    }
    printf("steepest step across a tile's edge %.5f, inside one %.5f\n", (libc_double_t)steepest_on_edge,
           (libc_double_t)steepest_inside);
    CHECK(steepest_on_edge > 0.0f32);
    CHECK(steepest_on_edge <= steepest_inside * 1.5f32);
    terrain_deinit(&terrain);
}

/* One run over all the chunks: as tests/run/terrain/seed/ does it, so that every worker of the run really makes some. */
typedef struct density_run_t {
    _Atomic uint32_t arrived;                  /* 0: jobs at the barrier; each takes its number from this */
    uint8_t _pad0[4];                          /* 4 */
    _Atomic uint64_t worker_mask;              /* 8: who ran a job */
    _Atomic uint32_t barrier_failed;           /* 16 */
    uint32_t barrier;                          /* 20: jobs that must all be running before any does its work */
    const terrain_density_t *borrowed_density; /* 24 */
    uint16_t *borrowed_blocks;                 /* 32: DENSITY_CHUNK_VOXELS per chunk, in map order */
    const uint32_t *borrowed_order;            /* 40: the order chunks are taken in */
} density_run_t;
static_assert(sizeof(density_run_t) == 48);

static void density_job(job_worker_t *worker, void *borrowed_argument) {
    density_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t ticket = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    const int64_t deadline = time_now_ns() + DENSITY_WAIT_NS;
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        if (time_now_ns() > deadline) {
            atomic_store_explicit(&run->barrier_failed, 1u, memory_order_relaxed);
            break;
        }
        (void)sched_yield();
    }
    for (uint32_t i = ticket; i < DENSITY_CHUNKS; i += run->barrier) {
        const uint32_t chunk = run->borrowed_order[i];
        const terrain_density_box_t box = density_tall_box(
            (DENSITY_MAP_MIN + (int32_t)(chunk % DENSITY_MAP_SIDE)) * (int32_t)DENSITY_CHUNK,
            (DENSITY_MAP_MIN + (int32_t)(chunk / DENSITY_MAP_SIDE)) * (int32_t)DENSITY_CHUNK, DENSITY_CHUNK);
        terrain_density_fill_box(run->borrowed_density, &box,
                                 run->borrowed_blocks + (uint64_t)chunk * DENSITY_CHUNK_VOXELS);
    }
}

static uint64_t density_run_all(job_system_t *system, const terrain_density_t *density, uint32_t workers,
                                const uint32_t *order, uint16_t *blocks) {
    density_run_t run = {
        .barrier = workers,
        .borrowed_density = density,
        .borrowed_blocks = blocks,
        .borrowed_order = order,
    };
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, density_job, &run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    if (atomic_load_explicit(&run.barrier_failed, memory_order_relaxed) != 0u) {
        return 0u;
    }
    return atomic_load_explicit(&run.worker_mask, memory_order_relaxed);
}

static uint32_t density_count_bits(uint64_t bits) {
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

/* The gate: the same bytes from 1 worker and 16, whichever order the chunks are taken in. And a pin, against any change of a
 * knot, a shape, a limit or the lattice, and against the presets disagreeing. */
TEST(one_and_sixteen_workers_give_the_same_bytes) {
    const uint64_t voxels = (uint64_t)DENSITY_CHUNKS * DENSITY_CHUNK_VOXELS;
    const uint64_t bytes = voxels * sizeof(uint16_t);
    uint16_t *const one = test_alloc(t, bytes + 2u * DENSITY_GUARD * sizeof(uint16_t));
    uint16_t *const many = test_alloc(t, bytes);
    uint16_t *const shuffled = test_alloc(t, bytes);
    uint32_t in_order[DENSITY_CHUNKS];
    uint32_t out_of_order[DENSITY_CHUNKS];
    for (uint32_t i = 0u; i < DENSITY_CHUNKS; i++) {
        in_order[i] = i;
        out_of_order[i] = DENSITY_CHUNKS - 1u - i;
    }

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));

    /* Guards around one run, to see that it stays inside what it was given. */
    uint16_t *const inside = one + DENSITY_GUARD;
    for (uint32_t i = 0u; i < DENSITY_GUARD; i++) {
        one[i] = 0xBEEFu;
        inside[voxels + i] = 0xBEEFu;
    }

    const int64_t started = time_now_ns();
    const uint64_t one_mask = density_run_all(&system, terrain.borrowed_density, 1u, in_order, inside);
    const int64_t one_done = time_now_ns();
    const uint64_t many_mask = density_run_all(&system, terrain.borrowed_density, DENSITY_WORKERS_MANY, in_order, many);
    const int64_t many_done = time_now_ns();
    const uint64_t shuffled_mask =
        density_run_all(&system, terrain.borrowed_density, DENSITY_WORKERS_MANY, out_of_order, shuffled);
    job_deinit(&system);

    printf("%" PRIu32 " chunks of %" PRIu32 " voxels: %.1f ms on 1 worker, %.1f ms on 16\n", (uint32_t)DENSITY_CHUNKS,
           (uint32_t)DENSITY_CHUNK_VOXELS, (libc_double_t)((float64_t)(one_done - started) / 1.0e6f64),
           (libc_double_t)((float64_t)(many_done - one_done) / 1.0e6f64));
    CHECK_EQ_U64(density_count_bits(one_mask), 1u);
    CHECK_EQ_U64(density_count_bits(many_mask), DENSITY_WORKERS_MANY);
    CHECK_EQ_U64(density_count_bits(shuffled_mask), DENSITY_WORKERS_MANY);
    CHECK_MEM_EQ(inside, many, bytes);
    CHECK_MEM_EQ(inside, shuffled, bytes);
    for (uint32_t i = 0u; i < DENSITY_GUARD; i++) {
        CHECK_EQ_U64(one[i], 0xBEEFu);
        CHECK_EQ_U64(inside[voxels + i], 0xBEEFu);
    }

    const uint64_t digest = density_digest(0xCBF29CE484222325ull, inside, bytes);
    printf("digest 0x%016" PRIX64 "\n", digest);
    CHECK_EQ_U64(digest, DENSITY_DIGEST_PIN);
    terrain_deinit(&terrain);
}

TEST(sampling_stays_inside_its_output_and_allocates_nothing) {
    enum : uint32_t { ACROSS = 37u, TALL = 41u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t *const values = test_alloc(t, (VOXELS + 2u * DENSITY_GUARD) * sizeof(float32_t));
    uint16_t *const blocks = test_alloc(t, (VOXELS + 2u * DENSITY_GUARD) * sizeof(uint16_t));
    float32_t *const heights = test_alloc(t, (ACROSS * ACROSS + 2u * DENSITY_GUARD) * sizeof(float32_t));
    for (uint32_t i = 0u; i < VOXELS + 2u * DENSITY_GUARD; i++) {
        values[i] = -7.0f32;
        blocks[i] = 0xBEEFu;
    }
    for (uint32_t i = 0u; i < ACROSS * ACROSS + 2u * DENSITY_GUARD; i++) {
        heights[i] = -7.0f32;
    }
    float32_t *const softness = test_alloc(t, (uint64_t)ACROSS * ACROSS * sizeof(float32_t));
    const terrain_density_box_t box = {
        .origin_x = 3,
        .origin_y = -9,
        .origin_z = -20,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    const terrain_climate_area_t area = {
        .origin_x = 3,
        .origin_y = -9,
        .step_x = 1,
        .step_y = 1,
        .count_x = ACROSS,
        .count_y = ACROSS,
    };

    heap_counters_t before;
    heap_counters_read(&before);
    const uint64_t arena_before = terrain.arena_tables.alloc_count;
    terrain_density_sample_box(density, &box, values + DENSITY_GUARD);
    terrain_density_fill_box(density, &box, blocks + DENSITY_GUARD);
    terrain_density_profile_area(density, &area, heights + DENSITY_GUARD, softness);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(terrain.arena_tables.alloc_count, arena_before);

    for (uint32_t i = 0u; i < DENSITY_GUARD; i++) {
        CHECK(values[i] == -7.0f32);
        CHECK(values[VOXELS + DENSITY_GUARD + i] == -7.0f32);
        CHECK_EQ_U64(blocks[i], 0xBEEFu);
        CHECK_EQ_U64(blocks[VOXELS + DENSITY_GUARD + i], 0xBEEFu);
        CHECK(heights[i] == -7.0f32);
        CHECK(heights[ACROSS * ACROSS + DENSITY_GUARD + i] == -7.0f32);
    }
    terrain_deinit(&terrain);
}

/* ---- One world per seed ------------------------------------------------------------------------------------------- */

TEST(a_seed_gives_its_own_world_and_the_same_one_every_time) {
    enum : uint32_t { ACROSS = 32u, VOXELS = ACROSS * ACROSS * DENSITY_LAYERS };
    const terrain_density_box_t box = density_tall_box(0, 0, ACROSS);
    uint16_t *const first = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const again = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const other = test_alloc(t, VOXELS * sizeof(uint16_t));

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    terrain_density_fill_box(terrain.borrowed_density, &box, first);
    terrain_deinit(&terrain);

    /* Built by hand on a bank with something else in it first: a field comes from its name, not its place. */
    enum : uint64_t { BYTES = 4ull * 1024ull * 1024ull };
    uint8_t *const memory = test_alloc(t, BYTES);
    arena_t arena;
    arena_init(&arena, "test", memory, BYTES);
    terrain_noise_bank_t bank;
    terrain_noise_bank_init(&bank, &arena, DENSITY_WORLD, TERRAIN_NOISE_FIELDS_MAX);
    const terrain_noise_field_t *extra = nullptr;
    const terrain_noise_shape_t shape = {
        .frequency_x = 0.01f64,
        .frequency_y = 0.01f64,
        .lacunarity = 2.0f32,
        .persistence = 0.5f32,
        .octaves = 2u,
    };
    CHECK(terrain_noise_bank_add(&bank, "test/before", &shape, &extra));
    terrain_climate_t *climate = nullptr;
    CHECK(terrain_climate_init(&climate, &arena, &bank));
    REQUIRE(climate != nullptr);
    terrain_density_t *density = nullptr;
    CHECK(terrain_density_init(&density, &arena, &bank, climate));
    REQUIRE(density != nullptr);
    terrain_density_fill_box(density, &box, again);
    terrain_density_deinit(density);
    terrain_climate_deinit(climate);
    terrain_noise_bank_deinit(&bank);
    arena_deinit(&arena);

    REQUIRE(terrain_init(&terrain, DENSITY_WORLD + 1u));
    terrain_density_fill_box(terrain.borrowed_density, &box, other);
    terrain_deinit(&terrain);

    CHECK_MEM_EQ(first, again, VOXELS * sizeof(uint16_t));
    CHECK(memcmp(first, other, VOXELS * sizeof(uint16_t)) != 0);
}

TEST(a_full_bank_is_refused_and_nothing_is_left_behind) {
    enum : uint64_t { BYTES = 1024ull * 1024ull };
    uint8_t *const memory = test_alloc(t, BYTES);
    arena_t arena;
    arena_init(&arena, "test", memory, BYTES);
    terrain_noise_bank_t bank;
    terrain_noise_bank_init(&bank, &arena, DENSITY_WORLD,
                            TERRAIN_CLIMATE_NOISE_FIELDS + TERRAIN_DENSITY_NOISE_FIELDS - 1u);
    terrain_climate_t *climate = nullptr;
    CHECK(terrain_climate_init(&climate, &arena, &bank));
    REQUIRE(climate != nullptr);
    terrain_density_t *density = (terrain_density_t *)(uintptr_t)1u;
    CHECK(!terrain_density_init(&density, &arena, &bank, climate));
    CHECK(density == nullptr);
    terrain_climate_deinit(climate);
    terrain_noise_bank_deinit(&bank);
    arena_deinit(&arena);
}

TEST(terrain_starts_the_density_and_stops_it) {
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, DENSITY_WORLD));
    REQUIRE(terrain.borrowed_density != nullptr);
    const terrain_density_t *const density = terrain.borrowed_density;
    CHECK(density->borrowed_climate == terrain.borrowed_climate);
    const terrain_noise_field_t *const fields[TERRAIN_DENSITY_NOISE_FIELDS] = {
        density->borrowed_hills, density->borrowed_jagged, density->borrowed_detail};
    for (uint32_t f = 0u; f < TERRAIN_DENSITY_NOISE_FIELDS; f++) {
        REQUIRE(fields[f] != nullptr);
        for (uint32_t o = 0u; o < fields[f]->octave_count; o++) {
            /* Only the 3D one may change with height: a column's ground has no height in it. */
            CHECK((fields[f]->octaves[o].frequency_z != 0.0f64) == (f == 2u));
            /* The lattice has to be able to hold what it samples: at least two points to a noise cell. */
            CHECK(fields[f]->octaves[o].frequency_x * (float64_t)TERRAIN_DENSITY_CELL_XY <= 0.5f64);
            CHECK(fields[f]->octaves[o].frequency_y * (float64_t)TERRAIN_DENSITY_CELL_XY <= 0.5f64);
            CHECK(fields[f]->octaves[o].frequency_z * (float64_t)TERRAIN_DENSITY_CELL_Z <= 0.5f64);
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
    CHECK(terrain.borrowed_density == nullptr);
    terrain_deinit(&terrain); /* safe again */
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
}

TEST_MAIN()
