/* ctest run_terrain_climate_rules. See README.md. */
#include "harness/index.h"

#include "climate/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Ahmed's seed, the world of ~/Pictures/1.png. */
static const uint64_t CR_AHMED_SEED = 2198175988018515243ull;
static const uint64_t CR_SEEDS[8] = {2198175988018515243ull, 1ull, 36ull, 11ull, 0x1234567890ABCDEFull,
                                     0x4D494E4543524146ull, 7ull, 0xC0FFEEull};

enum : uint32_t {
    CR_CHUNK = 16u,
    CR_CHUNK_COLUMNS = CR_CHUNK * CR_CHUNK,
    CR_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    CR_CHUNK_VOXELS = CR_CHUNK_COLUMNS * CR_LAYERS,
};

/* ---- A climate of a seed, built over a square ------------------------------------------------------------------------ */

typedef struct cr_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
    terrain_t *terrain;          /* 24 */
} cr_rig_t;
static_assert(sizeof(cr_rig_t) == 32);

static void *cr_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool cr_start(test_context_t *t, cr_rig_t *rig, uint64_t seed, int32_t x, int32_t y, int32_t half) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = cr_aligned(t, sizeof(job_system_t));
    rig->climate = cr_aligned(t, sizeof(climate_t));
    rig->terrain = cr_aligned(t, sizeof(terrain_t));
    if (!thread_topology_read(rig->topology) || !job_init(rig->job, rig->topology)) {
        return false;
    }
    if (!climate_init(rig->climate) || !terrain_init(rig->terrain, seed) || !climate_open(rig->climate, rig->terrain)) {
        return false;
    }
    climate_prepare_area(rig->climate, rig->job, x - half, y - half, x + half, y + half, nullptr, nullptr);
    return true;
}

static void cr_stop(cr_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    terrain_deinit(rig->terrain);
    job_deinit(rig->job);
}

/* The sample a column job at (x, y) has. */
static void cr_sample(test_context_t *t, const cr_rig_t *rig, int32_t x, int32_t y, terrain_climate_sample_t *out) {
    CHECK(climate_terrain_sample(rig->climate, rig->terrain->seed, (int64_t)x, (int64_t)y, out));
}

static terrain_biome_column_t cr_column(test_context_t *t, const cr_rig_t *rig, int32_t x, int32_t y) {
    terrain_climate_sample_t sample;
    cr_sample(t, rig, x - 8, y - 8, &sample);
    return terrain_biome_column_at(rig->terrain->borrowed_biome, &sample, x, y);
}

/* The whole height of the chunk whose south-west column is (x, y), as a column job makes it. */
static void cr_chunk(test_context_t *t, const cr_rig_t *rig, int32_t x, int32_t y, uint8_t *opened, uint16_t *blocks) {
    terrain_climate_sample_t sample;
    cr_sample(t, rig, x, y, &sample);
    const terrain_density_box_t box = {
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = CR_CHUNK,
        .count_y = CR_CHUNK,
        .count_z = CR_LAYERS,
    };
    terrain_generate_box(rig->terrain, &sample, &box, opened, blocks);
}

static bool cr_is_frozen(uint16_t block) {
    return block == BLOCK_ID_SNOW_BLOCK || block == BLOCK_ID_ICE || block == BLOCK_ID_PACKED_ICE ||
           block == BLOCK_ID_BLUE_ICE;
}

/* What grows, or is not solid ground: bench/world_map's rule. */
static bool cr_is_growth(uint16_t block) {
    if (block == BLOCK_ID_WATER || block == BLOCK_ID_LAVA) {
        return false;
    }
    if (block == BLOCK_ID_AIR || block == BLOCK_ID_CACTUS || block == BLOCK_ID_MELON || block == BLOCK_ID_PUMPKIN) {
        return true;
    }
    const block_def_t *const def = &block_defs[block];
    return (def->flags & BLOCK_FLAG_SOLID) == 0u || (def->flags & BLOCK_FLAG_DECAYS) != 0u ||
           strstr(def->name, "_log") != nullptr;
}

/* The highest voxel of a chunk's column i that is not air. -1 for none. */
static int32_t cr_top(const uint16_t *blocks, uint32_t i) {
    for (uint32_t iz = CR_LAYERS; iz-- > 0u;) {
        if (blocks[i + (uint64_t)CR_CHUNK_COLUMNS * iz] != BLOCK_ID_AIR) {
            return (int32_t)iz;
        }
    }
    return -1;
}

/* The highest voxel of column i that is neither growth nor a snow block laid over the ground: the ground the cold paint
 * read. Returns its index, or -1. */
static int32_t cr_ground(const uint16_t *blocks, uint32_t i) {
    int32_t iz = cr_top(blocks, i);
    while (iz >= 0 && cr_is_growth(blocks[i + (uint64_t)CR_CHUNK_COLUMNS * (uint32_t)iz])) {
        iz--;
    }
    return iz;
}

/* ---- The rules, by hand ---------------------------------------------------------------------------------------------- */

/* Master 2.5's worked example and the band: the edge moves the line by at most 24 blocks either way, and every snow
 * decision is z >= line + w. */
TEST(the_ragged_edge_is_within_twenty_four_blocks) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CR_AHMED_SEED));
    const terrain_surface_t *const surface = terrain.borrowed_surface;
    int32_t lowest = 0, highest = 0;
    uint64_t state = 0x5EEDull;
    for (uint32_t n = 0u; n < 100000u; n++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const int32_t x = (int32_t)(state >> 40u) - (1 << 23);
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const int32_t y = (int32_t)(state >> 40u) - (1 << 23);
        const int32_t w = terrain_surface_snow_wander(surface->snow_seed, x, y);
        CHECK(w >= -TERRAIN_SURFACE_SNOW_WANDER_BLOCKS && w <= TERRAIN_SURFACE_SNOW_WANDER_BLOCKS);
        lowest = w < lowest ? w : lowest;
        highest = w > highest ? w : highest;
        CHECK_EQ_I64(terrain_surface_snow_from(surface, BIOME_FAMILY_TUNDRA, 700, x, y), 700 + w);
        CHECK_EQ_I64(terrain_surface_snow_from(surface, BIOME_FAMILY_ICE, 700, x, y), INT32_MIN);
        CHECK_EQ_I64(terrain_surface_snow_from(surface, BIOME_FAMILY_BOREAL, TERRAIN_CLIMATE_NO_SNOW_LINE, x, y),
                     INT32_MAX);
        /* The wander is the same wherever it is asked from. */
        if (n % 1000u == 0u) {
            CHECK_EQ_I64(terrain_surface_snow_wander(surface->snow_seed, x, y), w);
        }
    }
    printf("wander over 100000 columns: %" PRId32 " to %" PRId32 " blocks\n", lowest, highest);
    CHECK(lowest <= -18 && highest >= 18); /* the band is used, not just its middle */

    /* The worked example: line 700. Top 690 with the line moved to 688 is snow; top 705 with it at 718 is not. */
    const biome_def_t *const plains = &biome_defs[BIOME_ID_SNOWY_PLAINS];
    terrain_surface_inputs_t in = {.z = 690, .band_z = 690, .depth = 0u, .block = BLOCK_ID_STONE, .snow_from = 688};
    CHECK_EQ_U64(terrain_surface_block_from(plains, surface->bands, &in), BLOCK_ID_SNOW_BLOCK);
    in = (terrain_surface_inputs_t){.z = 705, .band_z = 705, .depth = 0u, .block = BLOCK_ID_STONE, .snow_from = 718};
    CHECK_EQ_U64(terrain_surface_block_from(plains, surface->bands, &in), BLOCK_ID_GRASS_BLOCK);
    /* Snowy slopes: its filler of snow is dirt below the line; a frozen peak's packed ice is stone. */
    const biome_def_t *const slopes = &biome_defs[BIOME_ID_SNOWY_SLOPES];
    in = (terrain_surface_inputs_t){.z = 400, .band_z = 400, .depth = 1u, .block = BLOCK_ID_STONE, .snow_from = 500};
    CHECK_EQ_U64(terrain_surface_block_from(slopes, surface->bands, &in), BLOCK_ID_DIRT);
    in.snow_from = 400;
    CHECK_EQ_U64(terrain_surface_block_from(slopes, surface->bands, &in), BLOCK_ID_SNOW_BLOCK);
    const biome_def_t *const frozen = &biome_defs[BIOME_ID_FROZEN_PEAKS];
    in = (terrain_surface_inputs_t){.z = 900, .band_z = 900, .depth = 1u, .block = BLOCK_ID_STONE, .snow_from = 1000};
    CHECK_EQ_U64(terrain_surface_block_from(frozen, surface->bands, &in), BLOCK_ID_STONE);
    /* The high rule: windswept hills' snow from 400 waits for the snowline, and gives the grass under it. */
    const biome_def_t *const hills = &biome_defs[BIOME_ID_WINDSWEPT_HILLS];
    in = (terrain_surface_inputs_t){.z = 450, .band_z = 450, .depth = 0u, .block = BLOCK_ID_STONE, .snow_from = 600};
    CHECK_EQ_U64(terrain_surface_block_from(hills, surface->bands, &in), BLOCK_ID_GRASS_BLOCK);
    in.snow_from = 300;
    CHECK_EQ_U64(terrain_surface_block_from(hills, surface->bands, &in), BLOCK_ID_SNOW_BLOCK);
    in.z = 350;
    CHECK_EQ_U64(terrain_surface_block_from(hills, surface->bands, &in), BLOCK_ID_GRASS_BLOCK); /* under its from */
    terrain_deinit(&terrain);
}

/* Master 2.5: -1.5 degrees in the coldest month at sea level; a lake 60 blocks up is 0.72 colder, -2.22: ice. At sea level,
 * none. Below sea level on land the basin heat counts. */
TEST(ice_follows_the_coldest_month) {
    CHECK(terrain_surface_freezes(-150, 60));
    CHECK(!terrain_surface_freezes(-150, 0));
    CHECK(!terrain_surface_freezes(-200, 0));
    CHECK(terrain_surface_freezes(-201, 0));
    CHECK(!terrain_surface_freezes(-210, -10)); /* -210 + 12 + 25 */
    CHECK(terrain_surface_freezes(-1000, 0));   /* "permanent" ice, the same block */

    /* Through the far field's cold column, on a hand-built sample where every cell is about -1.47 in its coldest month
     * (mean 1.5, swing 3): Tundra, not Ice. */
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CR_AHMED_SEED));
    terrain_climate_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    terrain_climate_sample_origin(0, 0, &sample.origin_x, &sample.origin_y);
    sample.warp_off = 1u;
    for (uint32_t j = 0u; j < (uint32_t)TERRAIN_CLIMATE_SIDE; j++) {
        for (uint32_t i = 0u; i < (uint32_t)TERRAIN_CLIMATE_SIDE; i++) {
            sample.cells[j][i] = (terrain_climate_cell_t){.t_mean_cc = 150,
                                                          .t_swing_cc = 300,
                                                          .rain_mm_year = 500,
                                                          .snow_line_z = 50,
                                                          .dry_month_mm = 40,
                                                          .climate_class = TERRAIN_CLIMATE_CLASS_TUNDRA};
        }
    }
    const terrain_biome_column_t own = terrain_biome_column_at(terrain.borrowed_biome, &sample, 8, 8);
    CHECK(own.t_cold_cc >= -148 && own.t_cold_cc <= -145); /* 150 - 300 x 0.98862 */
    CHECK_EQ_I64(own.snow_line_z, 50);
    enum : uint32_t { COUNT = 200u };
    uint16_t column[COUNT];
    /* Water whose top is at z 60, from z -100: freezes. */
    for (uint32_t i = 0u; i < COUNT; i++) {
        column[i] = i <= 160u ? BLOCK_ID_WATER : BLOCK_ID_AIR;
    }
    terrain_feature_cold_column(terrain.borrowed_feature, &sample, 8, 8, -100, COUNT, column);
    CHECK_EQ_U64(column[160], BLOCK_ID_ICE);
    /* The same with its top at sea level: open. */
    for (uint32_t i = 0u; i < COUNT; i++) {
        column[i] = i <= 100u ? BLOCK_ID_WATER : BLOCK_ID_AIR;
    }
    terrain_feature_cold_column(terrain.borrowed_feature, &sample, 8, 8, -100, COUNT, column);
    CHECK_EQ_U64(column[100], BLOCK_ID_WATER);
    /* Dry ground at z 90, over the line of 50 and its 24: a snow block on it. At z 0, under 50 - 24: none. */
    for (uint32_t i = 0u; i < COUNT; i++) {
        column[i] = i <= 190u ? BLOCK_ID_GRASS_BLOCK : BLOCK_ID_AIR;
    }
    terrain_feature_cold_column(terrain.borrowed_feature, &sample, 8, 8, -100, COUNT, column);
    CHECK_EQ_U64(column[191], BLOCK_ID_SNOW_BLOCK);
    for (uint32_t i = 0u; i < COUNT; i++) {
        column[i] = i <= 100u ? BLOCK_ID_GRASS_BLOCK : BLOCK_ID_AIR;
    }
    terrain_feature_cold_column(terrain.borrowed_feature, &sample, 8, 8, -100, COUNT, column);
    CHECK_EQ_U64(column[101], BLOCK_ID_AIR);
    terrain_deinit(&terrain);
}

/* Master 2.5: forest's rain_mm is 500 to 1800, a middle of 1150. 1.8 times it is held to 1.5; 0.4 times it is 0.4. */
TEST(tree_rates_scale_with_rain) {
    const biome_def_t *const forest = &biome_defs[BIOME_ID_FOREST];
    REQUIRE((forest->limits & BIOME_LIMIT_RAIN) != 0u);
    const int32_t mid = (forest->rain_low_mm + forest->rain_high_mm) / 2;
    CHECK_EQ_I64(mid, 1150);
    CHECK_EQ_U64(terrain_feature_rain_factor(BIOME_ID_FOREST, mid * 18 / 10), 1500u);
    CHECK_EQ_U64(terrain_feature_rain_factor(BIOME_ID_FOREST, mid * 4 / 10), 400u);
    CHECK_EQ_U64(terrain_feature_rain_factor(BIOME_ID_FOREST, mid), 1000u);
    CHECK_EQ_U64(terrain_feature_rain_factor(BIOME_ID_FOREST, 0), 0u);
    CHECK_EQ_U64(terrain_feature_rain_factor(BIOME_ID_FOREST, 100000), 1500u);
    const float32_t oak = biome_features[BIOME_ID_FOREST][BIOME_FEATURE_TREE_OAK];
    CHECK(terrain_feature_rate_scaled(oak, 1500u) == oak * 1.5f32);
    CHECK(terrain_feature_rate_scaled(oak, 400u) == oak * 400.0f32 / 1000.0f32);
    /* A biome with no rain_mm keeps its rates. */
    REQUIRE((biome_defs[BIOME_ID_OCEAN].limits & BIOME_LIMIT_RAIN) == 0u);
    CHECK_EQ_U64(terrain_feature_rain_factor(BIOME_ID_OCEAN, 5000), 1000u);
}

/* ---- Sand and beaches by family -------------------------------------------------------------------------------------- */

static bool cr_family_in(uint8_t family, uint32_t families) {
    return (families & (1u << family)) != 0u;
}

/* Every land biome whose top is desert sand is a Desert or Steppe member, and snowy_beach an Ice or Tundra one, in the table
 * and over 8 seeds from pole to pole on the fallback climate. */
TEST(sand_only_in_desert_and_steppe_and_snowy_beach_only_in_ice_and_tundra) {
    const uint32_t dry = (1u << BIOME_FAMILY_DESERT) | (1u << BIOME_FAMILY_STEPPE);
    const uint32_t cold = (1u << BIOME_FAMILY_ICE) | (1u << BIOME_FAMILY_TUNDRA);
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        const biome_def_t *const def = &biome_defs[b];
        if (def->where == BIOME_WHERE_LAND && def->top == BLOCK_ID_SAND) {
            CHECK_EQ_U64(def->families & ~dry, 0u);
        }
    }
    CHECK_EQ_U64(biome_defs[BIOME_ID_SNOWY_BEACH].families & ~cold, 0u);

    enum : uint32_t { ACROSS = 64u, ROWS = 160u };
    terrain_biome_column_t *const columns = test_alloc(t, ACROSS * sizeof(terrain_biome_column_t));
    uint64_t sand = 0u, beaches = 0u, snowy = 0u, land = 0u;
    for (uint32_t s = 0u; s < 8u; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, CR_SEEDS[s]));
        for (uint32_t row = 0u; row < ROWS; row++) {
            const terrain_fields_area_t area = {
                .origin_x = -2048,
                .origin_y = (int32_t)((float64_t)row * CLIMATE_POLE_Y / (float64_t)ROWS),
                .step_x = 64,
                .step_y = 1,
                .count_x = ACROSS,
                .count_y = 1u,
            };
            terrain_biome_columns_area(terrain.borrowed_biome, nullptr, &area, columns);
            for (uint32_t i = 0u; i < ACROSS; i++) {
                const biome_def_t *const def = &biome_defs[columns[i].biome];
                land += def->where == BIOME_WHERE_LAND ? 1u : 0u;
                if (def->where == BIOME_WHERE_LAND && def->top == BLOCK_ID_SAND) {
                    sand++;
                    CHECK(cr_family_in(columns[i].family, dry));
                }
                if (def->where == BIOME_WHERE_BEACH) {
                    beaches++;
                    if (columns[i].biome == BIOME_ID_SNOWY_BEACH) {
                        snowy++;
                        CHECK(cr_family_in(columns[i].family, cold));
                    } else {
                        CHECK(!cr_family_in(columns[i].family, cold));
                    }
                }
            }
        }
        terrain_deinit(&terrain);
    }
    printf("8 seeds: %" PRIu64 " land columns, %" PRIu64 " of desert sand; %" PRIu64 " beach, %" PRIu64 " snowy\n", land,
           sand, beaches, snowy);
    CHECK(sand > 0u && snowy > 0u);
}

/* ---- On the real climate --------------------------------------------------------------------------------------------- */

/* Counts the frozen voxels of the 3 by 3 chunks round (x, y). */
static uint64_t cr_frozen_round(test_context_t *t, const cr_rig_t *rig, int32_t x, int32_t y, uint8_t *opened,
                                uint16_t *blocks) {
    uint64_t frozen = 0u;
    for (int32_t dy = -1; dy <= 1; dy++) {
        for (int32_t dx = -1; dx <= 1; dx++) {
            cr_chunk(t, rig, (x & ~15) + dx * (int32_t)CR_CHUNK, (y & ~15) + dy * (int32_t)CR_CHUNK, opened, blocks);
            for (uint64_t v = 0u; v < CR_CHUNK_VOXELS; v++) {
                frozen += cr_is_frozen(blocks[v]) ? 1u : 0u;
            }
        }
    }
    return frozen;
}

/* Master 5: with the climate on, a seed's 16 km round (0, 0) has no snow block and no generated ice at any height. Every
 * climate cell there is checked first, which is the whole proof: a cell with no snow line under the world's top + 24,
 * whose warmest month at the top is 0 degrees or more and whose coldest is -2 or more, gives no column read from it snow, a
 * the Ice family or ice, and a blend of such cells is such a cell. Then the columns every 64 blocks, read as the stages read
 * them, and the whole of the chunks round the highest ground, (0, 0) and the screenshot's place. */
TEST(the_equator_has_no_snow_and_no_ice) {
    enum : int32_t { HALF = 8192 };
    cr_rig_t rig;
    REQUIRE(cr_start(t, &rig, CR_AHMED_SEED, 0, 0, HALF + 512));
    const int32_t top = MC_WORLD_Z_MAX;

    uint64_t cells = 0u;
    int32_t coldest_cc = INT32_MAX, lowest_line = INT32_MAX, warmest_low_cc = INT32_MAX;
    terrain_climate_sample_t sample;
    for (int32_t y = -HALF; y < HALF; y += 256) {
        for (int32_t x = -HALF; x < HALF; x += 256) {
            cr_sample(t, &rig, x, y, &sample);
            for (uint32_t j = 0u; j < (uint32_t)TERRAIN_CLIMATE_SIDE; j++) {
                for (uint32_t i = 0u; i < (uint32_t)TERRAIN_CLIMATE_SIDE; i++) {
                    const int64_t cx = sample.origin_x + 64 * (int64_t)i + 32;
                    const int64_t cy = sample.origin_y + 64 * (int64_t)j + 32;
                    if (cx < -HALF || cx >= HALF || cy < -HALF || cy >= HALF) {
                        continue;
                    }
                    terrain_climate_sample_t one = sample;
                    one.warp_off = 1u;
                    const terrain_climate_point_t p = terrain_climate_nearest_at(&one, cx, cy, top);
                    cells++;
                    coldest_cc = p.t_cold_cc < coldest_cc ? p.t_cold_cc : coldest_cc;
                    lowest_line = sample.cells[j][i].snow_line_z < lowest_line ? sample.cells[j][i].snow_line_z
                                                                               : lowest_line;
                    CHECK(sample.cells[j][i].snow_line_z > top + TERRAIN_SURFACE_SNOW_WANDER_BLOCKS);
                    CHECK(p.t_warm_cc >= 0); /* not Ice, where snow lies at any height */
                    warmest_low_cc = p.t_warm_cc < warmest_low_cc ? p.t_warm_cc : warmest_low_cc;
                    CHECK(p.t_cold_cc >= TERRAIN_SURFACE_ICE_BELOW_CC);
                }
            }
        }
    }
    printf("16 km round (0, 0): %" PRIu64 " cell reads; at z %" PRId32 " the coldest month %.2f and the warmest %.2f "
           "degrees at the coldest cell; lowest snow line %" PRId32 "\n",
           cells, top, (libc_double_t)((float64_t)coldest_cc / 100.0f64),
           (libc_double_t)((float64_t)warmest_low_cc / 100.0f64), lowest_line);

    uint64_t columns = 0u, land = 0u, tundra = 0u;
    int32_t highest_x = 0, highest_y = 0;
    float32_t highest = -1000.0f32;
    for (int32_t y = -HALF; y <= HALF; y += 64) {
        for (int32_t x = -HALF; x <= HALF; x += 64) {
            const terrain_biome_column_t c = cr_column(t, &rig, x, y);
            columns++;
            land += biome_defs[c.biome].where == BIOME_WHERE_LAND ? 1u : 0u;
            CHECK(c.family != BIOME_FAMILY_ICE);
            tundra += c.family == BIOME_FAMILY_TUNDRA ? 1u : 0u;
            CHECK(terrain_surface_snow_from(rig.terrain->borrowed_surface, c.family, c.snow_line_z, x, y) > top);
            CHECK(!terrain_surface_freezes(c.t_cold_cc, top));
            if (c.ground > highest) {
                highest = c.ground;
                highest_x = x;
                highest_y = y;
            }
        }
    }
    printf("%" PRIu64 " columns every 64 blocks, %" PRIu64 " land, %" PRIu64 " Tundra; highest ground %.0f at (%" PRId32
           ", %" PRId32 ")\n",
           columns, land, tundra, (libc_double_t)highest, highest_x, highest_y);

    uint8_t *const opened = test_alloc(t, CR_CHUNK_VOXELS);
    uint16_t *const blocks = test_alloc(t, CR_CHUNK_VOXELS * sizeof(uint16_t));
    const int32_t places[3][2] = {{highest_x, highest_y}, {0, 0}, {-68, 149}};
    for (uint32_t p = 0u; p < 3u; p++) {
        const uint64_t frozen = cr_frozen_round(t, &rig, places[p][0], places[p][1], opened, blocks);
        printf("3 by 3 chunks round (%" PRId32 ", %" PRId32 "): %" PRIu64 " frozen voxels\n", places[p][0],
               places[p][1], frozen);
        CHECK_EQ_U64(frozen, 0u);
    }
    cr_stop(&rig);
}

/* Ahmed's 1.png: x -68, y 149, on snow at about z 443 then. On his seed the column is sea; whatever it is, its top is not
 * snow or ice. */
TEST(the_screenshot_place_is_not_snow) {
    cr_rig_t rig;
    REQUIRE(cr_start(t, &rig, CR_AHMED_SEED, -68, 149, 1024));
    uint8_t *const opened = test_alloc(t, CR_CHUNK_VOXELS);
    uint16_t *const blocks = test_alloc(t, CR_CHUNK_VOXELS * sizeof(uint16_t));
    cr_chunk(t, &rig, -68 & ~15, 149 & ~15, opened, blocks);
    const uint32_t i = (uint32_t)(-68 & 15) + CR_CHUNK * (uint32_t)(149 & 15);
    const int32_t iz = cr_top(blocks, i);
    REQUIRE(iz >= 0);
    const uint16_t block = blocks[i + (uint64_t)CR_CHUNK_COLUMNS * (uint32_t)iz];
    printf("x -68, y 149: %s at z %" PRId32 "\n", block_defs[block].name, iz + COLUMN_Z_STORAGE_MIN);
    CHECK(!cr_is_frozen(block));
    cr_stop(&rig);
}

/* What a chunk's columns show against their snowline: counts, and failures checked. */
typedef struct cr_tally_t {
    uint64_t columns;      /* 0 */
    uint64_t over;         /* 8: dry ground at or over line + 24 */
    uint64_t snowy_over;   /* 16: of those, under snow */
    uint64_t under;        /* 24: snow voxels looked at that are not in the Ice family */
    uint64_t ice_family;   /* 32 */
    uint64_t below_failed; /* 40: snow voxels under line - 24 */
    uint64_t bare_failed;  /* 48: bare ground over line + 24 */
    uint64_t snow;         /* 56: snow voxels */
} cr_tally_t;
static_assert(sizeof(cr_tally_t) == 64);

/* One chunk against the rule: no snow block under its column's line - 24 (but in the Ice family), and dry ground at or over
 * the line + 24 lies under snow unless something grew over it. */
static void cr_tally_chunk(test_context_t *t, const cr_rig_t *rig, int32_t x0, int32_t y0, const uint16_t *blocks,
                           cr_tally_t *tally) {
    terrain_climate_sample_t sample;
    cr_sample(t, rig, x0, y0, &sample);
    for (uint32_t i = 0u; i < CR_CHUNK_COLUMNS; i++) {
        const int32_t x = x0 + (int32_t)(i % CR_CHUNK);
        const int32_t y = y0 + (int32_t)(i / CR_CHUNK);
        const terrain_biome_column_t c = terrain_biome_column_at(rig->terrain->borrowed_biome, &sample, x, y);
        const int32_t line = (int32_t)c.snow_line_z;
        tally->columns++;
        const bool ice = c.family == BIOME_FAMILY_ICE;
        tally->ice_family += ice ? 1u : 0u;
        for (uint32_t iz = 0u; iz < CR_LAYERS; iz++) {
            if (blocks[i + (uint64_t)CR_CHUNK_COLUMNS * iz] != BLOCK_ID_SNOW_BLOCK) {
                continue;
            }
            tally->snow++;
            if (ice) {
                continue;
            }
            tally->under++;
            const int32_t z = (int32_t)iz + COLUMN_Z_STORAGE_MIN;
            if (z < line - TERRAIN_SURFACE_SNOW_WANDER_BLOCKS) {
                if (tally->below_failed < 5u) {
                    printf("  snow at (%" PRId32 ", %" PRId32 ", %" PRId32 "), line %" PRId32 "\n", x, y, z, line);
                }
                tally->below_failed++;
            }
        }
        const int32_t g = cr_ground(blocks, i);
        if (g < 0) {
            continue;
        }
        const uint16_t ground = blocks[i + (uint64_t)CR_CHUNK_COLUMNS * (uint32_t)g];
        const int32_t z = g + COLUMN_Z_STORAGE_MIN;
        const bool must = ice || (line < TERRAIN_CLIMATE_NO_SNOW_LINE && z >= line + TERRAIN_SURFACE_SNOW_WANDER_BLOCKS);
        if (ground == BLOCK_ID_WATER || ground == BLOCK_ID_LAVA || !must) {
            continue;
        }
        tally->over++;
        if (cr_is_frozen(ground)) {
            tally->snowy_over++; /* a snow top of the surface's, or a snow block laid over the ground */
            continue;
        }
        /* Bare dry ground over the line: the voxel over it must be snow, or what grew there first. */
        const uint16_t over = g + 1 < (int32_t)CR_LAYERS ? blocks[i + (uint64_t)CR_CHUNK_COLUMNS * (uint32_t)(g + 1)]
                                                         : (uint16_t)BLOCK_ID_AIR;
        if (over == BLOCK_ID_AIR) {
            if (tally->bare_failed < 5u) {
                printf("  bare %s at (%" PRId32 ", %" PRId32 ", %" PRId32 "), line %" PRId32 ", family %s\n",
                       block_defs[ground].name, x, y, z, line, biome_family_names[c.family]);
            }
            tally->bare_failed++;
        }
    }
}

/* Master 5: at 75 degrees every column above the sea is Ice or Tundra, and every dry top at or over its snow line + 24 lies
 * under snow. */
TEST(pole_land_is_cold_and_snowy_above_the_line) {
    const int32_t y = (int32_t)(CLIMATE_POLE_Y * 75.0f64 / 90.0f64);
    cr_rig_t rig;
    REQUIRE(cr_start(t, &rig, CR_AHMED_SEED, 0, y, 2048));
    uint32_t land = 0u, cold = 0u;
    for (int32_t dy = -1024; dy < 1024; dy += 32) {
        for (int32_t dx = -1024; dx < 1024; dx += 32) {
            const terrain_biome_column_t c = cr_column(t, &rig, dx, y + dy);
            if (c.ground < (float32_t)MC_WORLD_Z_SEA) {
                continue;
            }
            land++;
            cold += c.family == BIOME_FAMILY_ICE || c.family == BIOME_FAMILY_TUNDRA ? 1u : 0u;
        }
    }
    printf("75 N: %u columns above the sea, %u in Ice or Tundra\n", land, cold);
    CHECK(land > 0u);
    CHECK_EQ_U64(cold, land);

    uint8_t *const opened = test_alloc(t, CR_CHUNK_VOXELS);
    uint16_t *const blocks = test_alloc(t, CR_CHUNK_VOXELS * sizeof(uint16_t));
    cr_tally_t tally = {0};
    for (int32_t dy = -768; dy <= 768; dy += 512) {
        for (int32_t dx = -768; dx <= 768; dx += 512) {
            const int32_t x0 = dx & ~15, y0 = (y + dy) & ~15;
            cr_chunk(t, &rig, x0, y0, opened, blocks);
            cr_tally_chunk(t, &rig, x0, y0, blocks, &tally);
        }
    }
    printf("16 chunks: %" PRIu64 " columns (%" PRIu64 " Ice), %" PRIu64 " dry tops over the line, %" PRIu64
           " under snow; %" PRIu64 " snow voxels\n",
           tally.columns, tally.ice_family, tally.over, tally.snowy_over, tally.snow);
    CHECK_EQ_U64(tally.bare_failed, 0u);
    CHECK_EQ_U64(tally.below_failed, 0u);
    CHECK(tally.snowy_over > 0u);
    cr_stop(&rig);
}

/* Where a mountain stands over its own snow line, from 45 degrees up: the column every 16 blocks with the most ground over
 * the line. */
typedef struct cr_peak_t {
    int32_t x;        /* 0 */
    int32_t y;        /* 4 */
    int32_t over;     /* 8: ground less the line */
    int32_t line;     /* 12 */
    float32_t lat;    /* 16 */
    uint8_t found;    /* 20 */
    uint8_t _pad0[3]; /* 21 */
} cr_peak_t;
static_assert(sizeof(cr_peak_t) == 24);

static cr_peak_t cr_find_peak(test_context_t *t, const cr_rig_t *rig, int32_t y_middle, float32_t lat) {
    cr_peak_t peak = {.over = INT32_MIN, .lat = lat};
    for (int32_t dy = -1536; dy <= 1536; dy += 16) {
        for (int32_t dx = -1536; dx <= 1536; dx += 16) {
            const terrain_biome_column_t c = cr_column(t, rig, dx, y_middle + dy);
            if (c.family == BIOME_FAMILY_ICE || c.snow_line_z >= TERRAIN_CLIMATE_NO_SNOW_LINE ||
                biome_defs[c.biome].where != BIOME_WHERE_LAND) {
                continue;
            }
            const int32_t over = (int32_t)c.ground - (int32_t)c.snow_line_z;
            if (over > peak.over) {
                peak = (cr_peak_t){.x = dx, .y = y_middle + dy, .over = over, .line = c.snow_line_z, .lat = lat};
            }
        }
    }
    peak.found = peak.over >= 2 * TERRAIN_SURFACE_SNOW_WANDER_BLOCKS ? 1u : 0u;
    return peak;
}

/* Master 5: a mountain at 45 degrees (or the first latitude from there up that has one over its line) has snow only at or
 * over each column's line - 24, apart from the Ice family's, and snow over its line + 24. */
TEST(a_45_degree_mountain_has_snow_only_above_its_line) {
    const float32_t lats[4] = {45.0f32, 50.0f32, 55.0f32, 60.0f32};
    cr_peak_t peak = {0};
    cr_rig_t rig;
    bool open = false;
    for (uint32_t l = 0u; l < 4u && peak.found == 0u; l++) {
        if (open) {
            cr_stop(&rig);
        }
        const int32_t y = (int32_t)(CLIMATE_POLE_Y * (float64_t)lats[l] / 90.0f64);
        REQUIRE(cr_start(t, &rig, CR_AHMED_SEED, 0, y, 2048));
        open = true;
        peak = cr_find_peak(t, &rig, y, lats[l]);
        printf("%.0f N: highest over its line by %" PRId32 " at (%" PRId32 ", %" PRId32 "), line %" PRId32 "\n",
               (libc_double_t)lats[l], peak.over, peak.x, peak.y, peak.line);
    }
    REQUIRE(peak.found != 0u);
    uint8_t *const opened = test_alloc(t, CR_CHUNK_VOXELS);
    uint16_t *const blocks = test_alloc(t, CR_CHUNK_VOXELS * sizeof(uint16_t));
    cr_tally_t tally = {0};
    for (int32_t dy = -3; dy <= 3; dy++) {
        for (int32_t dx = -3; dx <= 3; dx++) {
            const int32_t x0 = (peak.x & ~15) + dx * (int32_t)CR_CHUNK;
            const int32_t y0 = (peak.y & ~15) + dy * (int32_t)CR_CHUNK;
            cr_chunk(t, &rig, x0, y0, opened, blocks);
            cr_tally_chunk(t, &rig, x0, y0, blocks, &tally);
        }
    }
    printf("7 by 7 chunks round the peak at %.0f N: %" PRIu64 " columns (%" PRIu64 " Ice), %" PRIu64
           " dry tops over line + 24, %" PRIu64 " under snow; %" PRIu64 " snow voxels outside Ice\n",
           (libc_double_t)peak.lat, tally.columns, tally.ice_family, tally.over, tally.snowy_over, tally.under);
    CHECK_EQ_U64(tally.below_failed, 0u);
    CHECK_EQ_U64(tally.bare_failed, 0u);
    CHECK(tally.snowy_over > 0u);
    cr_stop(&rig);
}

/* ---- The far field --------------------------------------------------------------------------------------------------- */

/* The top a far field level too coarse for features makes of column (x, y): hydrology, surface and the cold column. */
static uint16_t cr_far_top(const cr_rig_t *rig, const terrain_climate_sample_t *sample, int32_t x, int32_t y,
                           uint16_t *column, int32_t *out_z) {
    const terrain_density_box_t box = {
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = 1u,
        .count_y = 1u,
        .count_z = CR_LAYERS,
    };
    terrain_hydrology_fill_box(rig->terrain->borrowed_hydrology, &box, column);
    terrain_surface_fill_box(rig->terrain->borrowed_surface, sample, &box, column);
    terrain_feature_cold_column(rig->terrain->borrowed_feature, sample, x, y, COLUMN_Z_STORAGE_MIN, CR_LAYERS, column);
    uint32_t top = CR_LAYERS;
    while (top > 0u && column[top - 1u] == BLOCK_ID_AIR) {
        top--;
    }
    *out_z = (int32_t)top - 1 + COLUMN_Z_STORAGE_MIN;
    return top > 0u ? column[top - 1u] : (uint16_t)BLOCK_ID_AIR;
}

/* 64 columns by both paths, at the pole (snow and ice) and on a 45 degree slope crossing its line: where nothing grew and
 * no lake or cave was cut at the near column's top, the far top is the near one, block and height. */
TEST(the_far_field_agrees_with_near_columns) {
    uint8_t *const opened = test_alloc(t, CR_CHUNK_VOXELS);
    uint16_t *const blocks = test_alloc(t, CR_CHUNK_VOXELS * sizeof(uint16_t));
    uint16_t *const column = test_alloc(t, CR_LAYERS * sizeof(uint16_t));
    const float32_t lats[2] = {75.0f32, 50.0f32};
    uint32_t compared = 0u, snowy = 0u, iced = 0u, differ = 0u;
    for (uint32_t l = 0u; l < 2u; l++) {
        const int32_t y_middle = (int32_t)(CLIMATE_POLE_Y * (float64_t)lats[l] / 90.0f64);
        cr_rig_t rig;
        REQUIRE(cr_start(t, &rig, CR_AHMED_SEED, 0, y_middle, 2048));
        uint32_t here = 0u;
        for (int32_t c = 0; c < 400 && here < 32u; c++) {
            const int32_t x0 = ((c % 20) * 96 - 960) & ~15;
            const int32_t y0 = (y_middle + (c / 20) * 96 - 960) & ~15;
            cr_chunk(t, &rig, x0, y0, opened, blocks);
            terrain_climate_sample_t sample;
            cr_sample(t, &rig, x0, y0, &sample);
            const uint32_t i = 5u + CR_CHUNK * 9u;
            const int32_t near_iz = cr_top(blocks, i);
            const int32_t ground_iz = cr_ground(blocks, i);
            if (near_iz < 0 || ground_iz < 0) {
                continue;
            }
            const uint16_t near = blocks[i + (uint64_t)CR_CHUNK_COLUMNS * (uint32_t)near_iz];
            /* Something grew at the top, a lake lies there, or a cave cut near the top: what the far field leaves out. */
            const terrain_feature_lake_t lake = terrain_feature_chunk_lake(rig.terrain->borrowed_feature, &sample,
                                                                           x0 >> 4, y0 >> 4);
            bool cut = false;
            for (int32_t iz = ground_iz - 4; iz <= ground_iz + 2; iz++) {
                const uint8_t at = iz >= 0 && iz < (int32_t)CR_LAYERS
                                       ? opened[i + (uint64_t)CR_CHUNK_COLUMNS * (uint32_t)iz]
                                       : (uint8_t)0u;
                cut = cut || (at & ~(uint8_t)TERRAIN_CAVE_WANTED_UNCUT) != 0u;
            }
            if (cr_is_growth(near) || near_iz - ground_iz > 1 || terrain_feature_lake_depth(&lake, 5u, 9u) > 0u || cut) {
                continue;
            }
            int32_t far_z = 0;
            const uint16_t far = cr_far_top(&rig, &sample, x0 + 5, y0 + 9, column, &far_z);
            const int32_t near_z = near_iz + COLUMN_Z_STORAGE_MIN;
            if (far != near || far_z != near_z) {
                if (differ < 8u) {
                    printf("  (%" PRId32 ", %" PRId32 "): near %s at %" PRId32 ", far %s at %" PRId32 "\n", x0 + 5,
                           y0 + 9, block_defs[near].name, near_z, block_defs[far].name, far_z);
                }
                differ++;
            }
            compared++;
            here++;
            snowy += near == BLOCK_ID_SNOW_BLOCK ? 1u : 0u;
            iced += near == BLOCK_ID_ICE ? 1u : 0u;
        }
        cr_stop(&rig);
    }
    printf("%u columns by both paths: %u snow tops, %u ice tops, %u differ\n", compared, snowy, iced, differ);
    CHECK_EQ_U64(compared, 64u);
    CHECK(snowy > 0u);
    CHECK_EQ_U64(differ, 0u);
}

TEST_MAIN()
