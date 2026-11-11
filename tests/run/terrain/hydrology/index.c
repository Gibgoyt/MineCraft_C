/* ctest run_terrain_hydrology. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "terrain/rng/index.h"
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
    HYDRO_LAYERS =
        1501u, /* -250 to 1250, both included: all terrain makes, as plan 003 phase 8 stage 4's ranges use the height */
    HYDRO_WORKERS_MANY = 16u,
    HYDRO_CHUNK = 16u,
    HYDRO_CHUNK_VOXELS = HYDRO_CHUNK * HYDRO_CHUNK * HYDRO_LAYERS,
    HYDRO_MAP_SIDE = 6u,
    HYDRO_CHUNKS = HYDRO_MAP_SIDE * HYDRO_MAP_SIDE,
    HYDRO_GUARD = 4u,
    /* The map a test reads the world's shape from: every 4 blocks. */
    HYDRO_MAP_COLUMNS_SIDE = 512u,
    HYDRO_MAP_COLUMNS = HYDRO_MAP_COLUMNS_SIDE * HYDRO_MAP_COLUMNS_SIDE,
    HYDRO_MAP_STEP = 4u,
    HYDRO_SEEDS = 4u,
    /* Places of each kind that are tested voxel by voxel. */
    HYDRO_PLACES_PER_KIND = 4u,
};
static const int64_t HYDRO_WAIT_NS = 60 * TIME_NS_PER_SECOND;
static const uint64_t HYDRO_WORLD = 0x1234567890ABCDEFull;
static const uint64_t HYDRO_SEED_LIST[HYDRO_SEEDS] = {0x1234567890ABCDEFull, 1ull, 0x4D494E4543524146ull, 42ull};
static const int32_t HYDRO_MAP_MIN = -3;

/* Pinned: see README.md. */
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
/* Plan 003 phase 8 stage 4 raised the land and made its ranges, valleys and wetlands. */
/* Plan 003 phase 2 added four blocks. The land is the same, as its issue 2 shows: the numbers of the blocks after them in the alphabet moved. */
/* Plan 003 phase 3 added seven blocks. The land is the same, as its issue 3 shows: the numbers of the blocks after them in the alphabet moved. */
/* Plan 004 phase 8 added snow_layer. The land is the same, as its issue 19 shows: the numbers of the blocks after it in the alphabet moved. */
/* Plan 005 phase 12 made the four torches on walls one wall_torch. The land is the same, as the digest over the numbers before it
 * showed: the numbers of the blocks after them in the alphabet moved. */
/* Plan 005 phase 2 added three doors. The land is the same, as the digest over the numbers before them showed: the numbers of the
 * blocks after them in the alphabet moved. */
/* Plan 005 phase 11 added chromium_ore and deepslate_chromium_ore. The land is the same, as the digest over the numbers before
 * them showed: the numbers of the blocks after them in the alphabet moved. */
static const uint64_t HYDRO_DIGEST_PIN = 0x8579B6C6CD735924ull;
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
/* Plan 003 phase 8 stage 4 raised the land and made its ranges, valleys and wetlands. */
static const uint64_t HYDRO_COLUMNS_PIN = 0xECA6C70A31BC9567ull;
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
/* Plan 003 phase 8 stage 4 raised the land and made its ranges, valleys and wetlands. */
/* Plan 003 phase 8 stage 5 brought spaghetti closer together and dried out most aquifer cells. */
/* Plan 003 phase 2 added four blocks. The land is the same, as its issue 2 shows: the numbers of the blocks after them in the alphabet moved. */
/* Plan 003 phase 3 added seven blocks. The land is the same, as its issue 3 shows: the numbers of the blocks after them in the alphabet moved. */
/* Plan 004 phase 8 added snow_layer. The land is the same, as its issue 19 shows: the numbers of the blocks after it in the alphabet moved. */
/* Plan 005 phase 12 made the four torches on walls one wall_torch. The land is the same, as the digest over the numbers before it
 * showed: the numbers of the blocks after them in the alphabet moved. */
/* Plan 005 phase 2 added three doors. The land is the same, as the digest over the numbers before them showed: the numbers of the
 * blocks after them in the alphabet moved. */
/* Plan 005 phase 11 added chromium_ore and deepslate_chromium_ore. The land is the same, as the digest over the numbers before
 * them showed: the numbers of the blocks after them in the alphabet moved. */
static const uint64_t HYDRO_FLOOD_PIN = 0x807D0BBA1F2B2B1Dull;

static uint64_t hydro_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static terrain_density_box_t hydro_tall_box(int32_t x, int32_t y, uint32_t across) {
    return (terrain_density_box_t){
        .origin_x = x,
        .origin_y = y,
        .origin_z = MC_WORLD_Z_MIN,
        .count_x = across,
        .count_y = across,
        .count_z = HYDRO_LAYERS,
    };
}

static uint64_t hydro_voxel(const terrain_density_box_t *box, uint32_t ix, uint32_t iy, uint32_t iz) {
    return (uint64_t)ix + (uint64_t)box->count_x * ((uint64_t)iy + (uint64_t)box->count_y * iz);
}

static bool hydro_is_ground(uint16_t block) {
    return block != BLOCK_ID_AIR && block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA;
}

/* ---- The plan's numbers ------------------------------------------------------------------------------------------- */

TEST(the_limits_are_the_ones_the_readme_gives) {
    CHECK_EQ_I64(MC_WORLD_Z_SEA, 0);
    CHECK_EQ_I64(MC_WORLD_Z_MIN, -250);
    CHECK_EQ_I64(TERRAIN_HYDROLOGY_LAVA_TOP, -230);
    CHECK_EQ_I64(TERRAIN_HYDROLOGY_AQUIFER_LAVA_TOP, -160);
    CHECK_EQ_I64(TERRAIN_HYDROLOGY_SURFACE_BAND, 12);
    CHECK_EQ_I64(TERRAIN_HYDROLOGY_AQUIFER_CELL, 32);
    CHECK_EQ_I64(TERRAIN_HYDROLOGY_BED_SHALLOWEST, -3);
    CHECK_EQ_I64(TERRAIN_HYDROLOGY_BED_DEEPEST, -7);
    CHECK_EQ_I64(TERRAIN_HYDROLOGY_SAND_TOP_MAX, 5);
    /* The sum that keeps the sea from lava: the lowest the near-surface rule can reach. */
    CHECK(-116 - 30 - 12 > TERRAIN_HYDROLOGY_AQUIFER_LAVA_TOP + 1);
}

/* ---- A column, by hand -------------------------------------------------------------------------------------------- */

/* The README's formula, typed out again. */
static terrain_hydrology_column_t hydro_expected_column(const terrain_hydrology_inputs_t *in) {
    const float32_t wide = (in->width + 1.0f32) * 0.5f32;
    const float32_t near = fabsf(in->weirdness);
    float32_t reach = 1.0f32;
    if (in->continentalness >= 0.15f32) {
        reach = 0.0f32;
    } else if (in->continentalness > -0.3f32) {
        const float32_t u = (0.15f32 - in->continentalness) / (0.15f32 - -0.3f32);
        reach = u * u * (3.0f32 - 2.0f32 * u);
    }
    float32_t high_up = 1.0f32;
    if (in->height >= 120.0f32) {
        high_up = 0.0f32;
    } else if (in->height > 45.0f32) {
        const float32_t u = (120.0f32 - in->height) / (120.0f32 - 45.0f32);
        high_up = u * u * (3.0f32 - 2.0f32 * u);
    }
    const float32_t depth = reach * high_up;
    const float32_t bank = 0.12f32 * reach;
    const float32_t inner = (0.03f32 + 0.04f32 * wide) * reach;
    const float32_t outer = inner + bank;
    float32_t river = 0.0f32;
    if (near <= inner) {
        river = depth;
    } else if (near < outer) {
        const float32_t s = (outer - near) / bank;
        river = depth * s * s * (3.0f32 - 2.0f32 * s);
    }
    const float32_t top = in->height + in->softness + 9.0f32;
    const float32_t bed = -3.0f32 - 4.0f32 * wide;
    const float32_t carve_top = river > 0.0f32 ? bed + (top - bed) * (1.0f32 - river) * (1.0f32 - river) : top;
    const float32_t ground = fminf(carve_top, in->height);

    terrain_hydrology_kind_t kind = TERRAIN_HYDROLOGY_KIND_LAND;
    const float32_t sand_top = 2.0f32 + 1.5f32 * (in->shore + 1.0f32);
    if (in->height >= 0.0f32 && ground <= 0.0f32) {
        kind = TERRAIN_HYDROLOGY_KIND_RIVER;
    } else if (river < 0.5f32 && in->continentalness <= -0.24f32 && ground >= -6.0f32 && ground <= sand_top + 1.0f32) {
        kind = in->steepness >= 1.5f32 ? TERRAIN_HYDROLOGY_KIND_STONY_SHORE : TERRAIN_HYDROLOGY_KIND_BEACH;
    } else if (ground < 0.0f32) {
        if (in->continentalness > -0.45f32) {
            kind = TERRAIN_HYDROLOGY_KIND_LAKE;
        } else {
            kind = ground < -60.0f32 ? TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN : TERRAIN_HYDROLOGY_KIND_OCEAN;
        }
    }
    return (terrain_hydrology_column_t){
        .height = in->height,
        .softness = in->softness,
        .ground = ground,
        .carve_top = carve_top,
        .river = river,
        .shore = in->shore,
        .kind = kind,
    };
}

TEST(a_column_is_worked_out_as_the_readme_says) {
    static const float32_t heights[] = {-116.0f32, -60.5f32, -7.0f32, -0.5f32, 0.0f32,   1.0f32,
                                        6.0f32,    40.0f32,  45.0f32, 80.0f32, 120.0f32, 1190.0f32};
    static const float32_t softness[] = {4.0f32, 26.0f32};
    static const float32_t continents[] = {-1.0f32,  -0.46f32, -0.45f32,  -0.44f32, -0.3f32,
                                           -0.24f32, -0.23f32, -0.075f32, 0.15f32,  1.0f32};
    static const float32_t weird[] = {-1.0f32, -0.1f32, -0.03f32, 0.0f32, 0.02f32,
                                      0.05f32, 0.09f32, 0.15f32,  0.2f32, 1.0f32};
    static const float32_t noise[] = {-1.0f32, -0.3f32, 0.0f32, 0.5f32, 1.0f32};
    static const float32_t steep[] = {0.0f32, 1.5f32, 9.0f32};
    uint64_t seen[TERRAIN_HYDROLOGY_KIND_COUNT] = {0u};
#define HYDRO_EACH(array, index) for (uint32_t index = 0u; index < sizeof(array) / sizeof(array[0]); index++)
    HYDRO_EACH(heights, a) {
        HYDRO_EACH(softness, b) {
            HYDRO_EACH(continents, c) {
                HYDRO_EACH(weird, d) {
                    HYDRO_EACH(noise, e) {
                        HYDRO_EACH(noise, f) {
                            HYDRO_EACH(steep, g) {
                                const terrain_hydrology_inputs_t inputs = {
                                    .height = heights[a],
                                    .softness = softness[b],
                                    .continentalness = continents[c],
                                    .weirdness = weird[d],
                                    .width = noise[e],
                                    .shore = noise[f],
                                    .steepness = steep[g],
                                };
                                const terrain_hydrology_column_t got = terrain_hydrology_column_from(&inputs);
                                const terrain_hydrology_column_t want = hydro_expected_column(&inputs);
                                CHECK(memcmp(&got, &want, sizeof(got)) == 0);
                                seen[got.kind]++;
                                /* It only ever cuts, and never below the deepest bed. */
                                CHECK(got.ground <= inputs.height);
                                CHECK(got.ground >= inputs.height || got.ground >= -7.0f32);
                                CHECK(got.river >= 0.0f32 && got.river <= 1.0f32);
                            }
                        }
                    }
                }
            }
        }
    }
#undef HYDRO_EACH
    /* The grid reached every kind, so each branch above was compared. */
    for (uint32_t kind = 0u; kind < TERRAIN_HYDROLOGY_KIND_COUNT; kind++) {
        CHECK(seen[kind] > 0u);
    }

    /* A few written out in numbers, so the formula itself is not all that is being trusted. */
    terrain_hydrology_inputs_t in = {
        .height = 20.0f32, .softness = 4.0f32, .continentalness = -0.3f32, .width = -1.0f32};
    terrain_hydrology_column_t column = terrain_hydrology_column_from(&in);
    CHECK(column.river == 1.0f32);
    CHECK(column.carve_top == -3.0f32);
    CHECK(column.ground == -3.0f32);
    CHECK_EQ_U64(column.kind, TERRAIN_HYDROLOGY_KIND_RIVER);
    in.width = 1.0f32;
    column = terrain_hydrology_column_from(&in);
    CHECK(column.carve_top == -7.0f32);
    /* Half way up the bank of a narrow river: a quarter of the way from the bed to the top. */
    in.width = -1.0f32;
    in.weirdness = 0.09f32;
    column = terrain_hydrology_column_from(&in);
    CHECK(fabsf(column.river - 0.5f32) < 1.0e-5f32);
    CHECK(fabsf(column.carve_top - (-3.0f32 + 36.0f32 * 0.25f32)) < 1.0e-3f32);
    CHECK(column.ground == column.carve_top);
    /* Past the bank nothing is cut, and the top is above all the ground there could be. */
    in.weirdness = 0.15f32;
    column = terrain_hydrology_column_from(&in);
    CHECK(column.river == 0.0f32);
    CHECK(column.carve_top == 33.0f32);
    CHECK(column.ground == 20.0f32);
    /* A river does nothing to what is already lower than its bed. */
    in.weirdness = 0.0f32;
    in.height = -40.0f32;
    in.continentalness = -0.5f32;
    column = terrain_hydrology_column_from(&in);
    CHECK(column.ground == -40.0f32);
    CHECK_EQ_U64(column.kind, TERRAIN_HYDROLOGY_KIND_OCEAN);
    /* Half way inland a river is half a river: half its width, and a gully that does not reach the water. */
    in.height = 20.0f32;
    in.continentalness = -0.075f32;
    column = terrain_hydrology_column_from(&in);
    CHECK(fabsf(column.river - 0.5f32) < 1.0e-5f32);
    CHECK(fabsf(column.carve_top - (-3.0f32 + 36.0f32 * 0.25f32)) < 1.0e-3f32);
    CHECK_EQ_U64(column.kind, TERRAIN_HYDROLOGY_KIND_LAND);
    in.weirdness = 0.09f32;
    column = terrain_hydrology_column_from(&in);
    CHECK(column.river == 0.0f32);
    /* Far inland there are none, on the very line where one would run. */
    in.weirdness = 0.0f32;
    in.continentalness = 0.15f32;
    column = terrain_hydrology_column_from(&in);
    CHECK(column.river == 0.0f32);
    CHECK(column.ground == 20.0f32);
    CHECK_EQ_U64(column.kind, TERRAIN_HYDROLOGY_KIND_LAND);
}

/* Each field's limit is meant to be about twice what it measures. Prints what it finds. */
TEST(raw_noise_measures_what_the_limits_were_chosen_for) {
    enum : uint32_t { LOTS = 1u << 20 };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    float32_t *const raw = test_alloc(t, LOTS * sizeof(float32_t));
    float64_t *const xs = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const zs = test_alloc(t, LOTS * sizeof(float64_t));
    const terrain_noise_field_t *const fields[TERRAIN_HYDROLOGY_NOISE_FIELDS] = {
        hydrology->borrowed_width, hydrology->borrowed_shore, hydrology->borrowed_flood, hydrology->borrowed_level,
        hydrology->borrowed_lava};
    static const float64_t limits[TERRAIN_HYDROLOGY_NOISE_FIELDS] = {0.42f64, 0.42f64, 0.42f64, 0.42f64, 0.54f64};
    static const char *const names[TERRAIN_HYDROLOGY_NOISE_FIELDS] = {"width", "shore", "flood", "level", "lava"};
    for (uint32_t i = 0u; i < LOTS; i++) {
        xs[i] = (float64_t)((int32_t)(i % 1024u) * 83 - 42000);
        ys[i] = (float64_t)((int32_t)(i / 1024u) * 89 - 45000);
        zs[i] = (float64_t)((int32_t)(i % 751u) * 31 - 11000);
    }
    for (uint32_t f = 0u; f < TERRAIN_HYDROLOGY_NOISE_FIELDS; f++) {
        terrain_noise_sample_points(fields[f], xs, ys, zs, LOTS, raw);
        float64_t squares = 0.0f64;
        for (uint32_t i = 0u; i < LOTS; i++) {
            squares += (float64_t)raw[i] * (float64_t)raw[i];
        }
        const float64_t deviation = sqrt(squares / (float64_t)LOTS);
        printf("%-6s raw standard deviation %.4f; limit is %.2f of them\n", names[f], (libc_double_t)deviation,
               (libc_double_t)(limits[f] / deviation));
        CHECK(limits[f] > 1.5f64 * deviation && limits[f] < 2.5f64 * deviation);
    }
    terrain_deinit(&terrain);
}

/* ---- Columns on real ground --------------------------------------------------------------------------------------- */

TEST(columns_do_not_depend_on_the_area_asked_for) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    enum : uint32_t { SIDE = 37u };
    terrain_hydrology_column_t *const all = test_alloc(t, SIDE * SIDE * sizeof(terrain_hydrology_column_t));
    const terrain_fields_area_t area = {
        .origin_x = -19, .origin_y = 1003, .step_x = 1, .step_y = 1, .count_x = SIDE, .count_y = SIDE};
    terrain_hydrology_columns_area(hydrology, &area, all);
    for (uint32_t iy = 0u; iy < SIDE; iy += 5u) {
        for (uint32_t ix = 0u; ix < SIDE; ix += 3u) {
            const terrain_hydrology_column_t one =
                terrain_hydrology_column_at(hydrology, area.origin_x + (int32_t)ix, area.origin_y + (int32_t)iy);
            /* Only a cell's steepness is shared between all the columns of a cell, and it comes from the lattice. So every
             * byte can be asked to match, one by itself against all of them together. */
            CHECK(memcmp(&one, &all[ix + SIDE * iy], sizeof(one)) == 0);
        }
    }
    /* And steps wider than a tile. */
    enum : uint32_t { WIDE = 9u };
    terrain_hydrology_column_t wide[WIDE * WIDE];
    const terrain_fields_area_t apart = {
        .origin_x = -19, .origin_y = 1003, .step_x = 4, .step_y = 3, .count_x = WIDE, .count_y = WIDE};
    terrain_hydrology_columns_area(hydrology, &apart, wide);
    for (uint32_t iy = 0u; iy < WIDE; iy++) {
        for (uint32_t ix = 0u; ix < WIDE; ix++) {
            CHECK(memcmp(&wide[ix + WIDE * iy], &all[ix * 4u + SIDE * (iy * 3u)], sizeof(wide[0])) == 0);
        }
    }
    terrain_deinit(&terrain);
}

/* The real blocks against the two things they are made of, voxel by voxel. The reference is typed from the README's table of
 * materials and walks a whole column from the top of the world, so it does not share the fill's shortcuts. */
static uint16_t hydro_expected_material(const terrain_hydrology_column_t *c, int32_t z, uint32_t depth, bool wet) {
    switch (c->kind) {
    case TERRAIN_HYDROLOGY_KIND_OCEAN:
    case TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN:
        if (!wet) {
            return BLOCK_ID_STONE;
        }
        if (z < -50) {
            return depth < 2u ? BLOCK_ID_GRAVEL : BLOCK_ID_STONE;
        }
        if (z >= -30 && c->shore >= 0.5f32 && depth < 2u) {
            return BLOCK_ID_CLAY;
        }
        return depth < 3u ? BLOCK_ID_SAND : BLOCK_ID_STONE;
    case TERRAIN_HYDROLOGY_KIND_RIVER:
    case TERRAIN_HYDROLOGY_KIND_LAKE:
        if (!wet) {
            return BLOCK_ID_STONE;
        }
        if (c->shore >= 0.5f32 && depth < 2u) {
            return BLOCK_ID_CLAY;
        }
        if (depth >= 3u) {
            return BLOCK_ID_STONE;
        }
        return c->shore < -0.2f32 ? BLOCK_ID_GRAVEL : BLOCK_ID_SAND;
    case TERRAIN_HYDROLOGY_KIND_BEACH:
        if ((float32_t)z > 2.0f32 + 1.5f32 * (c->shore + 1.0f32)) {
            return BLOCK_ID_STONE;
        }
        if (depth < 4u) {
            return BLOCK_ID_SAND;
        }
        return depth < 6u ? BLOCK_ID_SANDSTONE : BLOCK_ID_STONE;
    case TERRAIN_HYDROLOGY_KIND_STONY_SHORE:
        return wet && depth < 2u ? BLOCK_ID_GRAVEL : BLOCK_ID_STONE;
    default:
        break;
    }
    if (c->river >= 0.05f32 && z <= 2 && depth < 2u) {
        return c->shore < -0.2f32 ? BLOCK_ID_GRAVEL : BLOCK_ID_SAND;
    }
    return BLOCK_ID_STONE;
}

/* Finds up to HYDRO_PLACES_PER_KIND places of each kind in a wide map, and fills a whole chunk round each. */
typedef struct hydro_tally_t {
    uint64_t by_block[BLOCK_COUNT];
    uint64_t voxels;
    uint64_t cut;
    uint64_t floating_sand;
    uint64_t sand_voxels;
} hydro_tally_t;
static_assert(sizeof(hydro_tally_t) == (BLOCK_COUNT + 4u) * sizeof(uint64_t));

static void hydro_check_chunk(test_context_t *t, const terrain_t *terrain, int32_t x, int32_t y, uint16_t *blocks,
                              uint16_t *natural, terrain_hydrology_column_t *columns, hydro_tally_t *tally) {
    const terrain_hydrology_t *const hydrology = terrain->borrowed_hydrology;
    const terrain_density_box_t box = hydro_tall_box(x, y, HYDRO_CHUNK);
    terrain_hydrology_fill_box(hydrology, &box, blocks);
    terrain_density_fill_box(terrain->borrowed_density, &box, natural);
    const terrain_fields_area_t area = {
        .origin_x = x, .origin_y = y, .step_x = 1, .step_y = 1, .count_x = HYDRO_CHUNK, .count_y = HYDRO_CHUNK};
    terrain_hydrology_columns_area(hydrology, &area, columns);

    for (uint32_t iy = 0u; iy < HYDRO_CHUNK; iy++) {
        for (uint32_t ix = 0u; ix < HYDRO_CHUNK; ix++) {
            const terrain_hydrology_column_t *const column = &columns[ix + HYDRO_CHUNK * iy];
            uint32_t depth = 1000u;
            bool wet = false;
            /* From the top of the world: nothing above the box can be ground, so the walk starts with a clear sky. */
            depth = 0u;
            for (uint32_t i = HYDRO_LAYERS; i-- > 0u;) {
                const int32_t z = MC_WORLD_Z_MIN + (int32_t)i;
                const uint64_t at = hydro_voxel(&box, ix, iy, i);
                const uint16_t was = natural[at];
                const uint16_t got = blocks[at];
                uint16_t want = was;
                if (was == BLOCK_ID_STONE && (float32_t)z > column->carve_top) {
                    want = z <= 0 ? BLOCK_ID_WATER : BLOCK_ID_AIR;
                    tally->cut++;
                }
                if (want == BLOCK_ID_STONE && depth < 6u) {
                    want = hydro_expected_material(column, z, depth, wet);
                }
                if (want == BLOCK_ID_AIR || want == BLOCK_ID_WATER) {
                    depth = 0u;
                    wet = want == BLOCK_ID_WATER;
                } else {
                    depth = depth < 6u ? depth + 1u : 6u;
                }
                CHECK_EQ_U64(got, want);
                tally->by_block[got]++;
                tally->voxels++;
                if (z > 0) {
                    CHECK(got != BLOCK_ID_WATER);
                }
                if (got == BLOCK_ID_SAND) {
                    tally->sand_voxels++;
                    CHECK(z <= TERRAIN_HYDROLOGY_SAND_TOP_MAX);
                    if (i > 0u && !hydro_is_ground(natural[hydro_voxel(&box, ix, iy, i - 1u)])) {
                        tally->floating_sand++;
                    }
                }
            }
        }
    }
}

TEST(the_blocks_are_the_density_with_the_rivers_cut_and_the_shore_laid_on) {
    uint16_t *const blocks = test_alloc(t, HYDRO_CHUNK_VOXELS * sizeof(uint16_t));
    uint16_t *const natural = test_alloc(t, HYDRO_CHUNK_VOXELS * sizeof(uint16_t));
    terrain_hydrology_column_t *const columns =
        test_alloc(t, HYDRO_CHUNK * HYDRO_CHUNK * sizeof(terrain_hydrology_column_t));
    enum : uint32_t { SEARCH_SIDE = 256u };
    terrain_hydrology_column_t *const map =
        test_alloc(t, SEARCH_SIDE * SEARCH_SIDE * sizeof(terrain_hydrology_column_t));
    hydro_tally_t tally = {0};
    uint32_t tested[TERRAIN_HYDROLOGY_KIND_COUNT] = {0u};

    for (uint32_t seed = 0u; seed < 2u; seed++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, HYDRO_SEED_LIST[seed]));
        const terrain_fields_area_t search = {.origin_x = -8192,
                                              .origin_y = -8192,
                                              .step_x = 64,
                                              .step_y = 64,
                                              .count_x = SEARCH_SIDE,
                                              .count_y = SEARCH_SIDE};
        terrain_hydrology_columns_area(terrain.borrowed_hydrology, &search, map);
        uint32_t found[TERRAIN_HYDROLOGY_KIND_COUNT] = {0u};
        for (uint32_t i = 0u; i < SEARCH_SIDE * SEARCH_SIDE; i++) {
            const uint32_t pick = (i * 40503u + 17u) % (SEARCH_SIDE * SEARCH_SIDE);
            const terrain_hydrology_kind_t kind = map[pick].kind;
            if (found[kind] >= HYDRO_PLACES_PER_KIND) {
                continue;
            }
            found[kind]++;
            tested[kind]++;
            const int32_t x = search.origin_x + (int32_t)(pick % SEARCH_SIDE) * search.step_x - 8;
            const int32_t y = search.origin_y + (int32_t)(pick / SEARCH_SIDE) * search.step_y - 8;
            hydro_check_chunk(t, &terrain, x, y, blocks, natural, columns, &tally);
        }
        terrain_deinit(&terrain);
    }
    printf("%" PRIu64 " voxels: %" PRIu64 " cut by rivers, %" PRIu64 " water, %" PRIu64 " sand, %" PRIu64
           " sandstone, %" PRIu64 " gravel, %" PRIu64 " clay; %" PRIu64 " sand over something open\n",
           tally.voxels, tally.cut, tally.by_block[BLOCK_ID_WATER], tally.by_block[BLOCK_ID_SAND],
           tally.by_block[BLOCK_ID_SANDSTONE], tally.by_block[BLOCK_ID_GRAVEL], tally.by_block[BLOCK_ID_CLAY],
           tally.floating_sand);
    for (uint32_t kind = 0u; kind < TERRAIN_HYDROLOGY_KIND_COUNT; kind++) {
        CHECK(tested[kind] > 0u);
    }
    CHECK(tally.cut > 0u);
    CHECK(tally.by_block[BLOCK_ID_SAND] > 0u);
    CHECK(tally.by_block[BLOCK_ID_SANDSTONE] > 0u);
    CHECK(tally.by_block[BLOCK_ID_GRAVEL] > 0u);
    CHECK(tally.by_block[BLOCK_ID_CLAY] > 0u);
    CHECK_EQ_U64(tally.by_block[BLOCK_ID_LAVA], 0u); /* nothing has opened the ground that far down */
    /* A thin roof of sand under an overhang can happen; it must stay rare. */
    CHECK(tally.floating_sand * 50u < tally.sand_voxels);
}

/* ---- Rivers, oceans and shores on the map ------------------------------------------------------------------------- */

static bool hydro_is_open_water(terrain_hydrology_kind_t kind) {
    return kind == TERRAIN_HYDROLOGY_KIND_OCEAN || kind == TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN;
}

TEST(rivers_hold_water_and_reach_the_sea_and_the_coast_has_beaches) {
    const uint64_t cells = HYDRO_MAP_COLUMNS;
    terrain_hydrology_column_t *const map = test_alloc(t, cells * sizeof(terrain_hydrology_column_t));
    uint32_t *const stack = test_alloc(t, cells * sizeof(uint32_t));
    uint8_t *const reached = test_alloc(t, cells);
    uint64_t totals[TERRAIN_HYDROLOGY_KIND_COUNT] = {0u};
    uint64_t rivers = 0u;
    uint64_t rivers_reaching = 0u;
    uint64_t runs = 0u;
    uint64_t run_blocks = 0u;
    uint64_t coast_edges = 0u;
    uint64_t bare_coast_edges = 0u;

    for (uint32_t seed = 0u; seed < HYDRO_SEEDS; seed++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, HYDRO_SEED_LIST[seed]));
        const terrain_fields_area_t area = {
            .origin_x = -1024 - (int32_t)seed * 8192,
            .origin_y = -1024,
            .step_x = (int32_t)HYDRO_MAP_STEP,
            .step_y = (int32_t)HYDRO_MAP_STEP,
            .count_x = HYDRO_MAP_COLUMNS_SIDE,
            .count_y = HYDRO_MAP_COLUMNS_SIDE,
        };
        terrain_hydrology_columns_area(terrain.borrowed_hydrology, &area, map);

        /* Everything that has water at the top, from the sea and from the map's edge, where a river may simply carry on. */
        memset(reached, 0, cells);
        uint64_t held = 0u;
        for (uint32_t i = 0u; i < cells; i++) {
            totals[map[i].kind]++;
            const uint32_t ix = i % HYDRO_MAP_COLUMNS_SIDE;
            const uint32_t iy = i / HYDRO_MAP_COLUMNS_SIDE;
            const bool edge =
                ix == 0u || iy == 0u || ix == HYDRO_MAP_COLUMNS_SIDE - 1u || iy == HYDRO_MAP_COLUMNS_SIDE - 1u;
            if (hydro_is_open_water(map[i].kind) || (edge && map[i].kind == TERRAIN_HYDROLOGY_KIND_RIVER)) {
                reached[i] = 1u;
                stack[held++] = i;
            }
        }
        while (held > 0u) {
            const uint32_t i = stack[--held];
            const uint32_t ix = i % HYDRO_MAP_COLUMNS_SIDE;
            const uint32_t iy = i / HYDRO_MAP_COLUMNS_SIDE;
            const uint32_t next[4] = {ix > 0u ? i - 1u : i, ix + 1u < HYDRO_MAP_COLUMNS_SIDE ? i + 1u : i,
                                      iy > 0u ? i - HYDRO_MAP_COLUMNS_SIDE : i,
                                      iy + 1u < HYDRO_MAP_COLUMNS_SIDE ? i + HYDRO_MAP_COLUMNS_SIDE : i};
            for (uint32_t n = 0u; n < 4u; n++) {
                if (reached[next[n]] == 0u && map[next[n]].ground <= 0.0f32) {
                    reached[next[n]] = 1u;
                    stack[held++] = next[n];
                }
            }
        }
        for (uint32_t i = 0u; i < cells; i++) {
            if (map[i].kind == TERRAIN_HYDROLOGY_KIND_RIVER) {
                rivers++;
                rivers_reaching += reached[i];
            }
        }
        /* How wide a river is, along the rows. */
        for (uint32_t iy = 0u; iy < HYDRO_MAP_COLUMNS_SIDE; iy += 8u) {
            uint32_t run = 0u;
            for (uint32_t ix = 0u; ix < HYDRO_MAP_COLUMNS_SIDE; ix++) {
                if (map[ix + HYDRO_MAP_COLUMNS_SIDE * iy].kind == TERRAIN_HYDROLOGY_KIND_RIVER) {
                    run++;
                } else if (run > 0u) {
                    runs++;
                    run_blocks += (uint64_t)run * HYDRO_MAP_STEP;
                    run = 0u;
                }
            }
        }
        /* Where the sea meets what is not water: it should be meeting a shore. */
        for (uint32_t iy = 0u; iy + 1u < HYDRO_MAP_COLUMNS_SIDE; iy++) {
            for (uint32_t ix = 0u; ix + 1u < HYDRO_MAP_COLUMNS_SIDE; ix++) {
                const uint32_t i = ix + HYDRO_MAP_COLUMNS_SIDE * iy;
                const terrain_hydrology_kind_t here = map[i].kind;
                const terrain_hydrology_kind_t right = map[i + 1u].kind;
                const terrain_hydrology_kind_t up = map[i + HYDRO_MAP_COLUMNS_SIDE].kind;
                const bool sea_here = hydro_is_open_water(here);
                if (sea_here != hydro_is_open_water(right)) {
                    coast_edges++;
                    const terrain_hydrology_kind_t other = sea_here ? right : here;
                    bare_coast_edges += other == TERRAIN_HYDROLOGY_KIND_LAND ? 1u : 0u;
                }
                if (sea_here != hydro_is_open_water(up)) {
                    coast_edges++;
                    const terrain_hydrology_kind_t other = sea_here ? up : here;
                    bare_coast_edges += other == TERRAIN_HYDROLOGY_KIND_LAND ? 1u : 0u;
                }
            }
        }
        terrain_deinit(&terrain);
    }

    const uint64_t all = cells * HYDRO_SEEDS;
    static const char *const names[TERRAIN_HYDROLOGY_KIND_COUNT] = {"land", "ocean", "deep ocean", "river",
                                                                    "lake", "beach", "stony shore"};
    for (uint32_t kind = 0u; kind < TERRAIN_HYDROLOGY_KIND_COUNT; kind++) {
        printf("%-12s %6.2f%%\n", names[kind], (libc_double_t)((float64_t)(totals[kind] * 100u) / (float64_t)all));
        CHECK(totals[kind] > 0u);
    }
    const float64_t mean_width = (float64_t)run_blocks / (float64_t)(runs > 0u ? runs : 1u);
    printf("rivers: %" PRIu64 " columns, %" PRIu64 " joined to the sea or leaving the map; %" PRIu64
           " crossings, mean %.1f blocks across\n",
           rivers, rivers_reaching, runs, (libc_double_t)mean_width);
    printf("coast: %" PRIu64 " edges, %" PRIu64 " straight onto land\n", coast_edges, bare_coast_edges);
    CHECK(totals[TERRAIN_HYDROLOGY_KIND_RIVER] * 100u > all);
    CHECK(totals[TERRAIN_HYDROLOGY_KIND_RIVER] * 100u < all * 15u);
    CHECK(rivers_reaching * 100u >= rivers * 90u);
    CHECK(mean_width > 6.0f64 && mean_width < 60.0f64);
    CHECK(totals[TERRAIN_HYDROLOGY_KIND_OCEAN] * 10u > all);
    CHECK(totals[TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN] * 10u > all);
    CHECK(totals[TERRAIN_HYDROLOGY_KIND_BEACH] * 200u > all);
    CHECK(bare_coast_edges * 10u < coast_edges);
}

/* The rule for water at the top, on every kind of ground. */
TEST(water_tops_out_at_the_sea_everywhere_and_rivers_are_water_to_their_bed) {
    enum : uint32_t { PLACES = 4096u, LAYERS = 80u };
    uint64_t rivers = 0u;
    uint64_t oceans = 0u;
    for (uint32_t seed = 0u; seed < 2u; seed++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, HYDRO_SEED_LIST[seed]));
        const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
        uint16_t blocks[LAYERS];
        for (uint32_t p = 0u; p < PLACES; p++) {
            /* Lines of columns close together, so that some of them cross a river. */
            const int32_t x = (int32_t)(p % 64u) * 13 - 416 + (int32_t)(p / 64u) * 2039;
            const int32_t y = (int32_t)(p / 64u) * 1021 - 32000;
            const terrain_hydrology_column_t column = terrain_hydrology_column_at(hydrology, x, y);
            const terrain_density_box_t box = {
                .origin_x = x, .origin_y = y, .origin_z = -40, .count_x = 1u, .count_y = 1u, .count_z = LAYERS};
            terrain_hydrology_fill_box(hydrology, &box, blocks);
            for (uint32_t i = 0u; i < LAYERS; i++) {
                const int32_t z = -40 + (int32_t)i;
                if (z > 0) {
                    CHECK(blocks[i] != BLOCK_ID_WATER);
                }
                if (z <= 0 && !hydro_is_ground(blocks[i])) {
                    CHECK_EQ_U64(blocks[i], BLOCK_ID_WATER);
                }
                if (column.kind == TERRAIN_HYDROLOGY_KIND_RIVER && (float32_t)z > column.carve_top) {
                    CHECK_EQ_U64(blocks[i], z <= 0 ? BLOCK_ID_WATER : BLOCK_ID_AIR);
                }
            }
            rivers += column.kind == TERRAIN_HYDROLOGY_KIND_RIVER ? 1u : 0u;
            oceans += hydro_is_open_water(column.kind) ? 1u : 0u;
        }
        terrain_deinit(&terrain);
    }
    printf("%" PRIu64 " river columns and %" PRIu64 " ocean columns looked at\n", rivers, oceans);
    CHECK(rivers > 50u);
    CHECK(oceans > 500u);
}

/* ---- Aquifers and lava -------------------------------------------------------------------------------------------- */

typedef struct hydro_fluid_tally_t {
    uint64_t air;
    uint64_t water;
    uint64_t lava;
    uint64_t stone;
    uint64_t perched;
    uint64_t lava_above_the_sheet;
} hydro_fluid_tally_t;
static_assert(sizeof(hydro_fluid_tally_t) == 48);

/* A whole box opened, so that nothing but the flood decides. What it may never do is put fluid where it could flow: beside air,
 * over air, or water and lava touching. */
static void hydro_check_flood(test_context_t *t, const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                              uint8_t *opened, uint16_t *blocks, hydro_fluid_tally_t *tally) {
    const uint64_t voxels = terrain_density_box_voxels(box);
    memset(opened, 1, voxels);
    terrain_hydrology_flood_box(hydrology, box, opened, blocks);
    for (uint32_t iz = 0u; iz < box->count_z; iz++) {
        const int32_t z = box->origin_z + (int32_t)iz;
        for (uint32_t iy = 0u; iy < box->count_y; iy++) {
            for (uint32_t ix = 0u; ix < box->count_x; ix++) {
                const uint16_t block = blocks[hydro_voxel(box, ix, iy, iz)];
                CHECK(block == BLOCK_ID_AIR || block == BLOCK_ID_WATER || block == BLOCK_ID_LAVA ||
                      block == BLOCK_ID_STONE);
                tally->air += block == BLOCK_ID_AIR ? 1u : 0u;
                tally->water += block == BLOCK_ID_WATER ? 1u : 0u;
                tally->lava += block == BLOCK_ID_LAVA ? 1u : 0u;
                tally->stone += block == BLOCK_ID_STONE ? 1u : 0u;
                if (z <= TERRAIN_HYDROLOGY_LAVA_TOP) {
                    CHECK_EQ_U64(block, BLOCK_ID_LAVA);
                }
                if (block == BLOCK_ID_LAVA && z > TERRAIN_HYDROLOGY_LAVA_TOP) {
                    tally->lava_above_the_sheet++;
                    CHECK(z <= TERRAIN_HYDROLOGY_AQUIFER_LAVA_TOP);
                }
                if (block == BLOCK_ID_WATER && z > 0) {
                    tally->perched++;
                }
                if (block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA) {
                    continue;
                }
                const uint16_t other_fluid = block == BLOCK_ID_WATER ? BLOCK_ID_LAVA : BLOCK_ID_WATER;
                if (ix + 1u < box->count_x) {
                    const uint16_t next = blocks[hydro_voxel(box, ix + 1u, iy, iz)];
                    CHECK(next != BLOCK_ID_AIR && next != other_fluid);
                }
                if (ix > 0u) {
                    const uint16_t next = blocks[hydro_voxel(box, ix - 1u, iy, iz)];
                    CHECK(next != BLOCK_ID_AIR && next != other_fluid);
                }
                if (iy + 1u < box->count_y) {
                    const uint16_t next = blocks[hydro_voxel(box, ix, iy + 1u, iz)];
                    CHECK(next != BLOCK_ID_AIR && next != other_fluid);
                }
                if (iy > 0u) {
                    const uint16_t next = blocks[hydro_voxel(box, ix, iy - 1u, iz)];
                    CHECK(next != BLOCK_ID_AIR && next != other_fluid);
                }
                if (iz > 0u) {
                    const uint16_t next = blocks[hydro_voxel(box, ix, iy, iz - 1u)];
                    CHECK(next != BLOCK_ID_AIR && next != other_fluid);
                }
                if (iz + 1u < box->count_z) {
                    CHECK(blocks[hydro_voxel(box, ix, iy, iz + 1u)] != other_fluid);
                }
            }
        }
    }
}

TEST(aquifers_never_leave_a_fluid_able_to_flow_and_lava_lies_under_everything) {
    enum : uint32_t { ACROSS = 72u, TALL = 360u };
    const terrain_density_box_t model = {
        .origin_z = MC_WORLD_Z_MIN + 1, .count_x = ACROSS, .count_y = ACROSS, .count_z = TALL};
    const uint64_t voxels = terrain_density_box_voxels(&model);
    uint8_t *const opened = test_alloc(t, voxels);
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    hydro_fluid_tally_t tally = {0};
    for (uint32_t seed = 0u; seed < HYDRO_SEEDS; seed++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, HYDRO_SEED_LIST[seed]));
        for (uint32_t p = 0u; p < 6u; p++) {
            terrain_density_box_t box = model;
            box.origin_x = (int32_t)p * 2311 - 4000 + (int32_t)seed * 17;
            box.origin_y = (int32_t)p * -1733 + 900;
            hydro_check_flood(t, terrain.borrowed_hydrology, &box, opened, blocks, &tally);
        }
        terrain_deinit(&terrain);
    }
    const uint64_t all = tally.air + tally.water + tally.lava + tally.stone;
    printf("flooded %" PRIu64 " voxels: %.1f%% air, %.1f%% water, %.1f%% lava, %.1f%% stone; %" PRIu64
           " lava in aquifers, %" PRIu64 " water above the sea\n",
           all, (libc_double_t)((float64_t)tally.air * 100.0f64 / (float64_t)all),
           (libc_double_t)((float64_t)tally.water * 100.0f64 / (float64_t)all),
           (libc_double_t)((float64_t)tally.lava * 100.0f64 / (float64_t)all),
           (libc_double_t)((float64_t)tally.stone * 100.0f64 / (float64_t)all), tally.lava_above_the_sheet,
           tally.perched);
    CHECK(tally.air > all / 4u);
    CHECK(tally.water > all / 50u);
    CHECK(tally.lava_above_the_sheet > 0u);
    CHECK(tally.stone > 0u);
    CHECK(tally.stone < all / 5u);
}

/* The README's aquifer, typed out again, with no shortcut: every site in 5 cells each way, and each cell's numbers at its site.
 * Looking further than the code's 27 cells is what checks that those 27 are enough. */
static float32_t hydro_expected_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = fmin(1.0f64, fmax(-1.0f64, unit));
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

static float32_t hydro_field_at(const terrain_noise_field_t *field, int64_t x, int64_t y, int64_t z) {
    const float64_t xs = (float64_t)x;
    const float64_t ys = (float64_t)y;
    const float64_t zs = (float64_t)z;
    float32_t value = 0.0f32;
    terrain_noise_sample_points(field, &xs, &ys, &zs, 1u, &value);
    return value;
}

enum : uint32_t { HYDRO_SITES = 125u };
static const int64_t HYDRO_NO_LEVEL = INT64_MIN;

typedef struct hydro_site_t {
    int64_t x;      /* 0 */
    int64_t y;      /* 8 */
    int64_t z;      /* 16 */
    int64_t level;  /* 24 */
    uint16_t fluid; /* 32 */
    uint8_t _pad0[6];
} hydro_site_t;
static_assert(sizeof(hydro_site_t) == 40);

static uint16_t hydro_expected_aquifer(const terrain_hydrology_t *hydrology, int64_t x, int64_t y, int64_t z) {
    hydro_site_t sites[HYDRO_SITES];
    const int64_t cell_x = x >> 5;
    const int64_t cell_y = y >> 5;
    const int64_t cell_z = z >> 5;
    uint32_t nearest = 0u;
    int64_t nearest_square = INT64_MAX;
    int64_t squares[HYDRO_SITES];
    uint32_t at = 0u;
    for (int64_t dz = -2; dz <= 2; dz++) {
        for (int64_t dy = -2; dy <= 2; dy++) {
            for (int64_t dx = -2; dx <= 2; dx++) {
                terrain_rng_t rng = terrain_rng_start(terrain_rng_hash3(
                    hydrology->aquifer_seed, (int32_t)(cell_x + dx), (int32_t)(cell_y + dy), (int32_t)(cell_z + dz)));
                hydro_site_t *const site = &sites[at];
                site->x = (cell_x + dx) * 32 + 10 + (int64_t)terrain_rng_next_below(&rng, 13u);
                site->y = (cell_y + dy) * 32 + 10 + (int64_t)terrain_rng_next_below(&rng, 13u);
                site->z = (cell_z + dz) * 32 + 10 + (int64_t)terrain_rng_next_below(&rng, 13u);
                site->level = HYDRO_NO_LEVEL;
                site->fluid = BLOCK_ID_AIR;
                const float32_t flood = hydro_expected_spread(
                    hydro_field_at(hydrology->borrowed_flood, site->x, site->y, site->z), 0.42f64);
                if (flood >= 0.65f32) {
                    const float32_t spread = hydro_expected_spread(
                        hydro_field_at(hydrology->borrowed_level, site->x, site->y, site->z), 0.42f64);
                    const float32_t lava = hydro_expected_spread(
                        hydro_field_at(hydrology->borrowed_lava, site->x, site->y, site->z), 0.54f64);
                    const int64_t band = (int64_t)floor((float64_t)site->z / 40.0f64);
                    site->level = band * 40 + 20 + 3 * (int64_t)(int32_t)(spread * 4.0f32);
                    site->fluid = site->level <= -160 && lava >= 0.4f32 ? BLOCK_ID_LAVA : BLOCK_ID_WATER;
                }
                const int64_t ax = site->x - x;
                const int64_t ay = site->y - y;
                const int64_t az = site->z - z;
                squares[at] = ax * ax + ay * ay + az * az;
                if (squares[at] < nearest_square) {
                    nearest_square = squares[at];
                    nearest = at;
                }
                at++;
            }
        }
    }
    const hydro_site_t *const own = &sites[nearest];
    const bool own_holds = own->level >= z;
    for (uint32_t i = 0u; i < HYDRO_SITES; i++) {
        const hydro_site_t *const other = &sites[i];
        if (i == nearest || (other->level == own->level && other->fluid == own->fluid)) {
            continue;
        }
        if (!own_holds && other->level < z) {
            continue;
        }
        /* The plane halfway between the sites, 2 blocks each way. */
        const int64_t ax = other->x - own->x;
        const int64_t ay = other->y - own->y;
        const int64_t az = other->z - own->z;
        const float64_t apart = sqrt((float64_t)(ax * ax + ay * ay + az * az));
        const float64_t from_plane = (float64_t)(squares[i] - nearest_square) / (2.0f64 * apart);
        if (from_plane <= 2.0f64) {
            return BLOCK_ID_STONE;
        }
    }
    if (!own_holds) {
        return BLOCK_ID_AIR;
    }
    return z == -229 && own->fluid == BLOCK_ID_WATER ? BLOCK_ID_STONE : own->fluid;
}

TEST(an_aquifer_voxel_is_worked_out_as_the_readme_says) {
    enum : uint32_t { VOXELS = 8000u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    uint64_t counts[BLOCK_COUNT] = {0u};
    terrain_rng_t rng = terrain_rng_start(0x600DF00Dull);
    uint32_t compared = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        const int32_t x = (int32_t)terrain_rng_next_below(&rng, 20000u) - 10000;
        const int32_t y = (int32_t)terrain_rng_next_below(&rng, 20000u) - 10000;
        /* All the height there is, up to where it is always near the ground, and the lowest layers more often. */
        int32_t z = -229 + (int32_t)terrain_rng_next_below(&rng, 240u);
        if (i % 8u == 0u) {
            z = -229 + (int32_t)terrain_rng_next_below(&rng, 3u);
        } else if (i % 2u == 0u) {
            z = -229 + (int32_t)terrain_rng_next_below(&rng, 70u); /* where aquifers can hold lava */
        }
        const terrain_hydrology_column_t column = terrain_hydrology_column_at(hydrology, x, y);
        const terrain_hydrology_column_t beside[4] = {
            terrain_hydrology_column_at(hydrology, x - 1, y), terrain_hydrology_column_at(hydrology, x + 1, y),
            terrain_hydrology_column_at(hydrology, x, y - 1), terrain_hydrology_column_at(hydrology, x, y + 1)};
        /* Well clear of the near-surface rule, here and next door. That has its own test and its own rules. */
        bool clear = (float32_t)z + 1.0f32 < column.ground - column.softness - 12.0f32;
        for (uint32_t n = 0u; n < 4u; n++) {
            clear = clear && (float32_t)z < beside[n].ground - beside[n].softness - 12.0f32;
        }
        if (!clear) {
            continue;
        }
        const uint16_t got = terrain_hydrology_fluid_at(hydrology, x, y, z);
        CHECK_EQ_U64(got, hydro_expected_aquifer(hydrology, x, y, z));
        counts[got]++;
        compared++;
    }
    printf("%" PRIu32 " voxels worked out by hand: %" PRIu64 " air, %" PRIu64 " water, %" PRIu64 " lava, %" PRIu64
           " stone\n",
           compared, counts[BLOCK_ID_AIR], counts[BLOCK_ID_WATER], counts[BLOCK_ID_LAVA], counts[BLOCK_ID_STONE]);
    CHECK(compared > VOXELS / 2u);
    CHECK(counts[BLOCK_ID_AIR] > 0u);
    CHECK(counts[BLOCK_ID_WATER] > 0u);
    CHECK(counts[BLOCK_ID_LAVA] > 0u);
    CHECK(counts[BLOCK_ID_STONE] > 0u);
    terrain_deinit(&terrain);
}

/* Near the ground, the sea decides, and that is what makes a hole in a sea bed or a shore fill to z = 0 and no higher. */
TEST(an_opening_near_the_ground_takes_after_the_sea) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    uint64_t water = 0u;
    uint64_t air = 0u;
    for (int32_t y = -6000; y < 6000; y += 397) {
        for (int32_t x = -6000; x < 6000; x += 397) {
            const terrain_hydrology_column_t column = terrain_hydrology_column_at(hydrology, x, y);
            const float32_t band = column.ground - column.softness - 12.0f32;
            const int32_t z_first = (int32_t)ceilf(band);
            for (int32_t z = z_first; z < z_first + 12; z++) {
                if (z > MC_WORLD_Z_MAX) {
                    break;
                }
                const uint16_t block = terrain_hydrology_fluid_at(hydrology, x, y, z);
                CHECK_EQ_U64(block, z <= 0 ? BLOCK_ID_WATER : BLOCK_ID_AIR);
                water += block == BLOCK_ID_WATER ? 1u : 0u;
                air += block == BLOCK_ID_AIR ? 1u : 0u;
            }
        }
    }
    printf("near the ground: %" PRIu64 " water, %" PRIu64 " air\n", water, air);
    CHECK(water > 100u);
    CHECK(air > 100u);
    terrain_deinit(&terrain);
}

TEST(the_flood_touches_only_what_was_opened) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    enum : uint32_t { ACROSS = 20u, TALL = 30u, VOXELS = ACROSS * ACROSS * TALL };
    /* Over the bottom of the world, and through its top, where only some voxels can change. */
    static const int32_t bottoms[] = {MC_WORLD_Z_MIN - 3, -30, MC_WORLD_Z_MAX - 10};
    for (uint32_t b = 0u; b < sizeof(bottoms) / sizeof(bottoms[0]); b++) {
        const terrain_density_box_t box = {.origin_x = -7,
                                           .origin_y = 40,
                                           .origin_z = bottoms[b],
                                           .count_x = ACROSS,
                                           .count_y = ACROSS,
                                           .count_z = TALL};
        uint8_t *const opened = test_alloc(t, VOXELS);
        uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
        for (uint32_t i = 0u; i < VOXELS; i++) {
            opened[i] = (uint8_t)((i * 2654435761u >> 28) % 3u == 0u ? 1u : 0u);
            blocks[i] = 0x7777u;
        }
        terrain_hydrology_flood_box(hydrology, &box, opened, blocks);
        for (uint32_t iz = 0u; iz < TALL; iz++) {
            const int32_t z = box.origin_z + (int32_t)iz;
            const bool in_world = z > MC_WORLD_Z_MIN && z <= MC_WORLD_Z_MAX;
            for (uint32_t iy = 0u; iy < ACROSS; iy++) {
                for (uint32_t ix = 0u; ix < ACROSS; ix++) {
                    const uint64_t at = hydro_voxel(&box, ix, iy, iz);
                    if (opened[at] != 0u && in_world) {
                        CHECK(blocks[at] != 0x7777u);
                        CHECK_EQ_U64(blocks[at], terrain_hydrology_fluid_at(hydrology, box.origin_x + (int32_t)ix,
                                                                            box.origin_y + (int32_t)iy, z));
                    } else {
                        CHECK_EQ_U64(blocks[at], 0x7777u);
                    }
                }
            }
        }
    }
    terrain_deinit(&terrain);
}

/* ---- Any split of the work, and any worker ------------------------------------------------------------------------- */

/* The fill looks at what is above a box to know what lies on it, and this is what would catch it getting that wrong: the same
 * chunk cut into sections, or into columns, or into lumps that cross tiles, at places with shores and rivers. */
TEST(any_split_of_the_work_gives_the_same_bytes) {
    enum : uint32_t { SIDE = 40u, TALL = 96u, VOXELS = SIDE * SIDE * TALL };
    uint16_t *const whole = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const parts = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, VOXELS);
    uint16_t *const whole_flood = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const parts_flood = test_alloc(t, VOXELS * sizeof(uint16_t));
    enum : uint32_t { MAX_PART = SIDE * SIDE * TALL };
    uint16_t *const part = test_alloc(t, MAX_PART * sizeof(uint16_t));
    uint8_t *const part_opened = test_alloc(t, MAX_PART);
    memset(opened, 1, VOXELS);

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;

    static const uint32_t cuts[][3] = {
        {SIDE, SIDE, 1u}, {1u, 1u, TALL}, {7u, 11u, 13u}, {16u, 16u, 16u}, {3u, 40u, 5u}};
    /* Places along a coast and across a river, found by asking. */
    enum : uint32_t { SEARCH = 512u };
    terrain_hydrology_column_t *const map = test_alloc(t, SEARCH * SEARCH * sizeof(terrain_hydrology_column_t));
    const terrain_fields_area_t search = {
        .origin_x = -4096, .origin_y = -4096, .step_x = 16, .step_y = 16, .count_x = SEARCH, .count_y = SEARCH};
    terrain_hydrology_columns_area(hydrology, &search, map);
    uint32_t places = 0u;
    uint32_t found[TERRAIN_HYDROLOGY_KIND_COUNT] = {0u};
    uint64_t compared = 0u;
    for (uint32_t i = 0u; i < SEARCH * SEARCH && places < 7u; i += 37u) {
        const terrain_hydrology_kind_t kind = map[i].kind;
        if (found[kind] > 0u) {
            continue;
        }
        found[kind]++;
        places++;
        const int32_t x = search.origin_x + (int32_t)(i % SEARCH) * search.step_x - 20;
        const int32_t y = search.origin_y + (int32_t)(i / SEARCH) * search.step_y - 20;
        const int32_t z0 = (int32_t)map[i].ground - 48;
        const terrain_density_box_t box = {
            .origin_x = x, .origin_y = y, .origin_z = z0, .count_x = SIDE, .count_y = SIDE, .count_z = TALL};
        terrain_hydrology_fill_box(hydrology, &box, whole);
        terrain_hydrology_flood_box(hydrology, &box, opened, whole_flood);
        for (uint32_t c = 0u; c < sizeof(cuts) / sizeof(cuts[0]); c++) {
            memset(parts, 0xEE, VOXELS * sizeof(uint16_t));
            memset(parts_flood, 0xEE, VOXELS * sizeof(uint16_t));
            /* Each cut works from the far end, so that no part can have had anything from one that ran before. */
            for (uint32_t pz = TALL; pz > 0u;) {
                const uint32_t kz = pz >= cuts[c][2] ? pz - cuts[c][2] : 0u;
                for (uint32_t py = SIDE; py > 0u;) {
                    const uint32_t ky = py >= cuts[c][1] ? py - cuts[c][1] : 0u;
                    for (uint32_t px = SIDE; px > 0u;) {
                        const uint32_t kx = px >= cuts[c][0] ? px - cuts[c][0] : 0u;
                        const terrain_density_box_t bit = {
                            .origin_x = x + (int32_t)kx,
                            .origin_y = y + (int32_t)ky,
                            .origin_z = z0 + (int32_t)kz,
                            .count_x = px - kx,
                            .count_y = py - ky,
                            .count_z = pz - kz,
                        };
                        const uint64_t bit_voxels = terrain_density_box_voxels(&bit);
                        terrain_hydrology_fill_box(hydrology, &bit, part);
                        for (uint32_t bz = 0u; bz < bit.count_z; bz++) {
                            for (uint32_t by = 0u; by < bit.count_y; by++) {
                                memcpy(&parts[hydro_voxel(&box, kx, ky + by, kz + bz)],
                                       &part[hydro_voxel(&bit, 0u, by, bz)], (uint64_t)bit.count_x * sizeof(uint16_t));
                            }
                        }
                        memset(part_opened, 1, bit_voxels);
                        terrain_hydrology_flood_box(hydrology, &bit, part_opened, part);
                        for (uint32_t bz = 0u; bz < bit.count_z; bz++) {
                            for (uint32_t by = 0u; by < bit.count_y; by++) {
                                memcpy(&parts_flood[hydro_voxel(&box, kx, ky + by, kz + bz)],
                                       &part[hydro_voxel(&bit, 0u, by, bz)], (uint64_t)bit.count_x * sizeof(uint16_t));
                            }
                        }
                        px = kx;
                    }
                    py = ky;
                }
                pz = kz;
            }
            CHECK(memcmp(whole, parts, VOXELS * sizeof(uint16_t)) == 0);
            CHECK(memcmp(whole_flood, parts_flood, VOXELS * sizeof(uint16_t)) == 0);
            compared += VOXELS;
        }
    }
    printf("%" PRIu32 " places, %" PRIu64 " voxels compared whole against cut up\n", places, compared);
    CHECK(places >= 5u);
    /* One voxel, alone. */
    const terrain_density_box_t box = {
        .origin_x = 8, .origin_y = -8, .origin_z = -10, .count_x = 4u, .count_y = 4u, .count_z = 20u};
    uint16_t small[320];
    terrain_hydrology_fill_box(hydrology, &box, small);
    for (uint32_t i = 0u; i < 320u; i++) {
        CHECK_EQ_U64(small[i], terrain_hydrology_block_at(hydrology, 8 + (int32_t)(i % 4u),
                                                          -8 + (int32_t)((i / 4u) % 4u), -10 + (int32_t)(i / 16u)));
    }
    terrain_deinit(&terrain);
}

typedef struct hydro_run_t {
    _Atomic uint32_t arrived;                      /* 0: jobs at the barrier; each takes its number from this */
    uint8_t _pad0[4];                              /* 4 */
    _Atomic uint64_t worker_mask;                  /* 8: who ran a job */
    _Atomic uint32_t barrier_failed;               /* 16 */
    uint32_t barrier;                              /* 20: jobs that must all be running before any does its work */
    const terrain_hydrology_t *borrowed_hydrology; /* 24 */
    uint16_t *borrowed_blocks;                     /* 32: HYDRO_CHUNK_VOXELS per chunk, in map order */
    const uint32_t *borrowed_order;                /* 40: the order chunks are taken in */
} hydro_run_t;
static_assert(sizeof(hydro_run_t) == 48);

static void hydro_job(job_worker_t *worker, void *borrowed_argument) {
    hydro_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t ticket = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    const int64_t deadline = time_now_ns() + HYDRO_WAIT_NS;
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        if (time_now_ns() > deadline) {
            atomic_store_explicit(&run->barrier_failed, 1u, memory_order_relaxed);
            break;
        }
        (void)sched_yield();
    }
    for (uint32_t i = ticket; i < HYDRO_CHUNKS; i += run->barrier) {
        const uint32_t chunk = run->borrowed_order[i];
        const terrain_density_box_t box =
            hydro_tall_box((HYDRO_MAP_MIN + (int32_t)(chunk % HYDRO_MAP_SIDE)) * (int32_t)HYDRO_CHUNK,
                           (HYDRO_MAP_MIN + (int32_t)(chunk / HYDRO_MAP_SIDE)) * (int32_t)HYDRO_CHUNK, HYDRO_CHUNK);
        terrain_hydrology_fill_box(run->borrowed_hydrology, &box,
                                   run->borrowed_blocks + (uint64_t)chunk * HYDRO_CHUNK_VOXELS);
    }
}

static uint64_t hydro_run_all(job_system_t *system, const terrain_hydrology_t *hydrology, uint32_t workers,
                              const uint32_t *order, uint16_t *blocks) {
    hydro_run_t run = {
        .barrier = workers,
        .borrowed_hydrology = hydrology,
        .borrowed_blocks = blocks,
        .borrowed_order = order,
    };
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, hydro_job, &run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    if (atomic_load_explicit(&run.barrier_failed, memory_order_relaxed) != 0u) {
        return 0u;
    }
    return atomic_load_explicit(&run.worker_mask, memory_order_relaxed);
}

static uint32_t hydro_count_bits(uint64_t bits) {
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

/* The gate: the same bytes from 1 worker and 16, whichever order the chunks are taken in. And a pin, against any change of a
 * number, and against the presets disagreeing. */
TEST(one_and_sixteen_workers_give_the_same_bytes) {
    const uint64_t voxels = (uint64_t)HYDRO_CHUNKS * HYDRO_CHUNK_VOXELS;
    const uint64_t bytes = voxels * sizeof(uint16_t);
    uint16_t *const one = test_alloc(t, bytes + 2u * HYDRO_GUARD * sizeof(uint16_t));
    uint16_t *const many = test_alloc(t, bytes);
    uint16_t *const shuffled = test_alloc(t, bytes);
    uint32_t in_order[HYDRO_CHUNKS];
    uint32_t out_of_order[HYDRO_CHUNKS];
    for (uint32_t i = 0u; i < HYDRO_CHUNKS; i++) {
        in_order[i] = i;
        out_of_order[i] = HYDRO_CHUNKS - 1u - i;
    }

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));

    /* Guards around one run, to see that it stays inside what it was given. */
    uint16_t *const inside = one + HYDRO_GUARD;
    for (uint32_t i = 0u; i < HYDRO_GUARD; i++) {
        one[i] = 0xBEEFu;
        inside[voxels + i] = 0xBEEFu;
    }
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    const int64_t started = time_now_ns();
    const uint64_t one_mask = hydro_run_all(&system, hydrology, 1u, in_order, inside);
    const int64_t one_done = time_now_ns();
    const uint64_t many_mask = hydro_run_all(&system, hydrology, HYDRO_WORKERS_MANY, in_order, many);
    const int64_t many_done = time_now_ns();
    const uint64_t shuffled_mask = hydro_run_all(&system, hydrology, HYDRO_WORKERS_MANY, out_of_order, shuffled);
    job_deinit(&system);

    CHECK(one_mask != 0u);
    CHECK_EQ_U64(hydro_count_bits(many_mask), HYDRO_WORKERS_MANY);
    CHECK_EQ_U64(hydro_count_bits(shuffled_mask), HYDRO_WORKERS_MANY);
    CHECK(memcmp(inside, many, bytes) == 0);
    CHECK(memcmp(inside, shuffled, bytes) == 0);
    for (uint32_t i = 0u; i < HYDRO_GUARD; i++) {
        CHECK_EQ_U64(one[i], 0xBEEFu);
        CHECK_EQ_U64(inside[voxels + i], 0xBEEFu);
    }
    const uint64_t digest = hydro_digest(0xCBF29CE484222325ull, inside, bytes);
    printf("%" PRIu32 " chunks: %.1f ms on 1 worker, %.1f ms on 16; digest 0x%016" PRIX64 "\n", (uint32_t)HYDRO_CHUNKS,
           (libc_double_t)((float64_t)(one_done - started) / 1.0e6f64),
           (libc_double_t)((float64_t)(many_done - one_done) / 1.0e6f64), digest);
    CHECK_EQ_U64(digest, HYDRO_DIGEST_PIN);
    terrain_deinit(&terrain);
}

/* The pin above is one patch of ground, which may not have every kind on it. These cover every kind of column, and aquifers at
 * every height. */
TEST(columns_and_the_flood_are_pinned) {
    enum : uint32_t { SIDE = 256u, ACROSS = 48u, TALL = 400u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_hydrology_column_t *const map = test_alloc(t, SIDE * SIDE * sizeof(terrain_hydrology_column_t));
    uint8_t *const opened = test_alloc(t, VOXELS);
    uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_fields_area_t area = {
        .origin_x = -8192, .origin_y = -8192, .step_x = 64, .step_y = 64, .count_x = SIDE, .count_y = SIDE};
    terrain_hydrology_columns_area(terrain.borrowed_hydrology, &area, map);
    uint32_t seen[TERRAIN_HYDROLOGY_KIND_COUNT] = {0u};
    for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
        seen[map[i].kind]++;
    }
    for (uint32_t kind = 0u; kind < TERRAIN_HYDROLOGY_KIND_COUNT; kind++) {
        CHECK(seen[kind] > 0u);
    }
    const uint64_t columns_digest = hydro_digest(0xCBF29CE484222325ull, map, SIDE * SIDE * sizeof(map[0]));
    const terrain_density_box_t box = {
        .origin_x = 777, .origin_y = -1234, .origin_z = -249, .count_x = ACROSS, .count_y = ACROSS, .count_z = TALL};
    memset(opened, 1, VOXELS);
    terrain_hydrology_flood_box(terrain.borrowed_hydrology, &box, opened, blocks);
    const uint64_t flood_digest = hydro_digest(0xCBF29CE484222325ull, blocks, VOXELS * sizeof(uint16_t));
    printf("columns digest 0x%016" PRIX64 ", flood digest 0x%016" PRIX64 "\n", columns_digest, flood_digest);
    CHECK_EQ_U64(columns_digest, HYDRO_COLUMNS_PIN);
    CHECK_EQ_U64(flood_digest, HYDRO_FLOOD_PIN);
    terrain_deinit(&terrain);
}

TEST(sampling_stays_inside_its_output_and_allocates_nothing) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    enum : uint32_t { SIDE = 21u, TALL = 64u, VOXELS = SIDE * SIDE * TALL };
    uint16_t *const raw = test_alloc(t, (VOXELS + 2u * HYDRO_GUARD) * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, VOXELS);
    memset(opened, 1, VOXELS);
    uint16_t *const blocks = raw + HYDRO_GUARD;
    for (uint32_t i = 0u; i < HYDRO_GUARD; i++) {
        raw[i] = 0xBEEFu;
        blocks[VOXELS + i] = 0xBEEFu;
    }
    const terrain_density_box_t box = {
        .origin_x = -5, .origin_y = -5, .origin_z = -32, .count_x = SIDE, .count_y = SIDE, .count_z = TALL};
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_hydrology_fill_box(hydrology, &box, blocks);
    terrain_hydrology_flood_box(hydrology, &box, opened, blocks);
    (void)terrain_hydrology_column_at(hydrology, 1, 1);
    /* Empty boxes do nothing at all. */
    const terrain_density_box_t empty = {.origin_x = 0, .count_x = 0u, .count_y = 3u, .count_z = 3u};
    terrain_hydrology_fill_box(hydrology, &empty, blocks + VOXELS);
    terrain_hydrology_flood_box(hydrology, &empty, opened, blocks + VOXELS);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    for (uint32_t i = 0u; i < HYDRO_GUARD; i++) {
        CHECK_EQ_U64(raw[i], 0xBEEFu);
        CHECK_EQ_U64(blocks[VOXELS + i], 0xBEEFu);
    }
    terrain_deinit(&terrain);
}

/* The hard edges: one voxel, by itself, on either side of each limit, far out too. */
TEST(one_voxel_asked_alone_obeys_the_limits) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    static const int32_t places[] = {0, -1, 1, 15, 16, -16, 1000003, -1000003, 8388607, -8388608};
    for (uint32_t a = 0u; a < sizeof(places) / sizeof(places[0]); a++) {
        for (uint32_t b = 0u; b < sizeof(places) / sizeof(places[0]); b++) {
            const int32_t x = places[a];
            const int32_t y = places[b];
            CHECK_EQ_U64(terrain_hydrology_block_at(hydrology, x, y, MC_WORLD_Z_MIN - 1), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_hydrology_block_at(hydrology, x, y, MC_WORLD_Z_MIN), BLOCK_ID_BEDROCK);
            CHECK_EQ_U64(terrain_hydrology_block_at(hydrology, x, y, MC_WORLD_Z_MIN + 1), BLOCK_ID_STONE);
            CHECK_EQ_U64(terrain_hydrology_block_at(hydrology, x, y, MC_WORLD_Z_MAX), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_hydrology_block_at(hydrology, x, y, INT32_MAX), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_hydrology_block_at(hydrology, x, y, INT32_MIN), BLOCK_ID_AIR);
            CHECK_EQ_U64(terrain_hydrology_fluid_at(hydrology, x, y, TERRAIN_HYDROLOGY_LAVA_TOP), BLOCK_ID_LAVA);
            CHECK(terrain_hydrology_fluid_at(hydrology, x, y, TERRAIN_HYDROLOGY_LAVA_TOP + 1) != BLOCK_ID_WATER);
            CHECK_EQ_U64(terrain_hydrology_fluid_at(hydrology, x, y, MC_WORLD_Z_MIN + 1), BLOCK_ID_LAVA);
            /* Nothing makes a whole world's bedrock or sky into anything else. */
            uint16_t held = 0x7777u;
            const uint8_t opened = 1u;
            const terrain_density_box_t bedrock = {
                .origin_x = x, .origin_y = y, .origin_z = MC_WORLD_Z_MIN, .count_x = 1u, .count_y = 1u, .count_z = 1u};
            terrain_hydrology_flood_box(hydrology, &bedrock, &opened, &held);
            CHECK_EQ_U64(held, 0x7777u);
        }
    }
    /* The very ends of what a fill can be given. Nothing wraps. */
    const terrain_density_box_t corner = {
        .origin_x = INT32_MAX - 1, .origin_y = INT32_MIN, .origin_z = -2, .count_x = 2u, .count_y = 2u, .count_z = 4u};
    uint16_t blocks[16];
    terrain_hydrology_fill_box(hydrology, &corner, blocks);
    for (uint32_t i = 0u; i < 16u; i++) {
        CHECK(blocks[i] < BLOCK_COUNT);
    }
    terrain_deinit(&terrain);
}

TEST(a_seed_gives_its_own_world_and_the_same_one_every_time) {
    uint16_t *const first = test_alloc(t, HYDRO_CHUNK_VOXELS * sizeof(uint16_t));
    uint16_t *const again = test_alloc(t, HYDRO_CHUNK_VOXELS * sizeof(uint16_t));
    uint16_t *const other = test_alloc(t, HYDRO_CHUNK_VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = hydro_tall_box(-64, 256, HYDRO_CHUNK);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, first);
    const uint64_t aquifer_seed = terrain.borrowed_hydrology->aquifer_seed;
    terrain_deinit(&terrain);
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, again);
    CHECK_EQ_U64(terrain.borrowed_hydrology->aquifer_seed, aquifer_seed);
    terrain_deinit(&terrain);
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD + 1u));
    terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, other);
    CHECK(terrain.borrowed_hydrology->aquifer_seed != aquifer_seed);
    terrain_deinit(&terrain);
    CHECK(memcmp(first, again, HYDRO_CHUNK_VOXELS * sizeof(uint16_t)) == 0);
    CHECK(memcmp(first, other, HYDRO_CHUNK_VOXELS * sizeof(uint16_t)) != 0);
}

TEST(a_full_bank_is_refused_and_nothing_is_left_behind) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    /* A bank with room for only some of the fields, as one more stage's fields would find it. */
    uint8_t *const memory = test_alloc(t, 1u << 20);
    arena_t arena;
    arena_init(&arena, "hydrology_test", memory, 1u << 20);
    terrain_noise_bank_t bank;
    terrain_noise_bank_init(&bank, &arena, HYDRO_WORLD, TERRAIN_HYDROLOGY_NOISE_FIELDS - 1u);
    terrain_hydrology_t *hydrology = (terrain_hydrology_t *)&bank; /* something that is not nullptr */
    CHECK(!terrain_hydrology_init(&hydrology, &arena, &bank, terrain.borrowed_density, terrain.borrowed_fields));
    CHECK(hydrology == nullptr);
    terrain_noise_bank_deinit(&bank);
    arena_deinit(&arena);
    terrain_deinit(&terrain);
}

TEST(terrain_starts_the_hydrology_and_stops_it) {
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, HYDRO_WORLD));
    REQUIRE(terrain.borrowed_hydrology != nullptr);
    const terrain_hydrology_t *const hydrology = terrain.borrowed_hydrology;
    CHECK(hydrology->borrowed_density == terrain.borrowed_density);
    CHECK(hydrology->borrowed_fields == terrain.borrowed_fields);
    CHECK_EQ_U64(terrain.noise_bank.count, TERRAIN_FIELDS_NOISE_FIELDS + TERRAIN_DENSITY_NOISE_FIELDS +
                                               TERRAIN_HYDROLOGY_NOISE_FIELDS + TERRAIN_CAVE_NOISE_FIELDS +
                                               TERRAIN_SURFACE_NOISE_FIELDS);
    /* The river's fields are 2D, as a column has no height in it, and the aquifers' are 3D. */
    CHECK(hydrology->borrowed_width->octaves[0].frequency_z == 0.0f64);
    CHECK(hydrology->borrowed_shore->octaves[0].frequency_z == 0.0f64);
    CHECK(hydrology->borrowed_flood->octaves[0].frequency_z != 0.0f64);
    CHECK(hydrology->borrowed_level->octaves[0].frequency_z != 0.0f64);
    CHECK(hydrology->borrowed_lava->octaves[0].frequency_z != 0.0f64);
    terrain_deinit(&terrain);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK(terrain.borrowed_hydrology == nullptr);
    terrain_deinit(&terrain); /* safe again */
}

TEST_MAIN()
