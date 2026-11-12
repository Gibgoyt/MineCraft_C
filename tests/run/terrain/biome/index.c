/* ctest run_terrain_biome. See README.md. */
#include "harness/index.h"

#include "climate/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "terrain/rng/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16); /* the plan's gate says 16 */
static_assert(MC_THREADS_WORKERS <= 64); /* the worker mask below */

enum : uint32_t {
    BIO_LAYERS =
        1501u, /* -250 to 1250, both included: all terrain makes, as plan 003 phase 8 stage 4's ranges use the height */
    BIO_CHUNK = 16u,
    BIO_CHUNK_COLUMNS = BIO_CHUNK * BIO_CHUNK,
    BIO_CHUNK_VOXELS = BIO_CHUNK_COLUMNS * BIO_LAYERS,
    BIO_WORKERS_MANY = 16u,
    BIO_MAP_SIDE = 6u,
    BIO_CHUNKS = BIO_MAP_SIDE * BIO_MAP_SIDE,
    BIO_GUARD = 4u,
    /* The search for every biome: this many columns a side, this far apart, about 65 kilometres across. */
    BIO_SEARCH_SIDE = 512u,
    BIO_SEARCH_STEP = 128u,
    BIO_SEARCH_ROWS = 16u,
    BIO_SEEDS = 3u,
    /* What the search tries each column's underground biomes at. */
    BIO_CAVE_LEVELS = 6u,
    /* How far is near the sea: 2 cells of the search, so 256 blocks. */
    BIO_COAST_CELLS = 2u,
};
static const int64_t BIO_WAIT_NS = 60 * TIME_NS_PER_SECOND;
static const uint64_t BIO_WORLD = 0x1234567890ABCDEFull;
static const uint64_t BIO_SEED_LIST[BIO_SEEDS] = {0x1234567890ABCDEFull, 1ull, 0x4D494E4543524146ull};
static const int32_t BIO_CAVE_LEVEL_Z[BIO_CAVE_LEVELS] = {-210, -170, -100, -60, -20, 30};

/* Pinned: see README.md. */
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
/* Plan 003 phase 8 stage 4 raised the land and made its ranges, valleys and wetlands. */
/* Plan 003 phase 8 stage 5 brought spaghetti closer together and dried out most aquifer cells. */
/* Plan 003 phase 8 stage 6 moved the biomes' boxes to the coast and the height the terrain now has. */
/* Plan 003 phase 2 added four blocks. The land is the same, as its issue 2 shows: the numbers of the blocks after them in the alphabet moved. */
/* Plan 003 phase 3 added seven blocks. The land is the same, as its issue 3 shows: the numbers of the blocks after them in the alphabet moved. */
/* Plan 004 phase 8 added snow_layer. The land is the same, as its issue 19 shows: the numbers of the blocks after it in the alphabet moved. */
/* Plan 005 phase 12 made the four torches on walls one wall_torch. The land is the same, as the digest over the numbers before it
 * showed: the numbers of the blocks after them in the alphabet moved. */
/* Plan 005 phase 2 added three doors. The land is the same, as the digest over the numbers before them showed: the numbers of the
 * blocks after them in the alphabet moved. */
/* Plan 005 phase 11 added chromium_ore and deepslate_chromium_ore. The land is the same, as the digest over the numbers before
 * them showed: the numbers of the blocks after them in the alphabet moved. */
/* Plan 005 phase 11 added chromium ore to config/ores/, laid last: the land differs only by its veins in rock, as the same
 * file at a rate of 0 gave the pin before. */
/* Plan 005 phase 6 added twelve station blocks. The land is the same, as the digest over the numbers before them showed: the numbers of
 * the blocks after them in the alphabet moved. */
/* Plan 006 phase 03: biomes follow the climate (the family from the climate at the column's height first, then the box within it), so
 * the biomes, and the surface and features they lay, moved; height, density, hydrology, caves and ores did not (their pins hold). */
static const uint64_t BIO_DIGEST_PIN = 0xB7BC9D34E16EB6CDull;
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
/* Plan 003 phase 8 stage 4 raised the land and made its ranges, valleys and wetlands. */
/* Plan 003 phase 8 stage 6 moved the biomes' boxes to the coast and the height the terrain now has. */
/* Plan 006 phase 03: biomes follow the climate (the family from the climate at the column's height first, then the box within it), so
 * the biomes, and the surface and features they lay, moved; height, density, hydrology, caves and ores did not (their pins hold). */
static const uint64_t BIO_COLUMNS_PIN = 0x7A499FEBA8AD1D5Cull;

static uint64_t bio_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static terrain_density_box_t bio_tall_box(int32_t x, int32_t y, uint32_t across) {
    return (terrain_density_box_t){
        .origin_x = x,
        .origin_y = y,
        .origin_z = MC_WORLD_Z_MIN,
        .count_x = across,
        .count_y = across,
        .count_z = BIO_LAYERS,
    };
}

static bool bio_is_open(uint16_t block) {
    return block == BLOCK_ID_AIR || block == BLOCK_ID_WATER || block == BLOCK_ID_LAVA;
}

/* ---- The table ---------------------------------------------------------------------------------------------------- */

typedef struct bio_listed_t {
    const char *name; /* 0 */
    uint8_t where;    /* 8: biome_where_t */
    uint8_t _pad0[7]; /* 9 */
} bio_listed_t;
static_assert(sizeof(bio_listed_t) == 16);

/* The plan's list, spelled out: plains, sunflower plains, all forests, taigas, snowy biomes, ice spikes, swamp, mangrove,
 * desert, savanna, windswept, badlands, jungles, meadow, cherry grove, grove, slopes, all peaks, shores, rivers, all oceans,
 * mushroom fields, dripstone, lush caves, deep dark. */
static const bio_listed_t BIO_LISTED[] = {
    {.name = "plains", .where = BIOME_WHERE_LAND},
    {.name = "sunflower_plains", .where = BIOME_WHERE_LAND},
    {.name = "forest", .where = BIOME_WHERE_LAND},
    {.name = "flower_forest", .where = BIOME_WHERE_LAND},
    {.name = "birch_forest", .where = BIOME_WHERE_LAND},
    {.name = "old_growth_birch_forest", .where = BIOME_WHERE_LAND},
    {.name = "dark_forest", .where = BIOME_WHERE_LAND},
    {.name = "taiga", .where = BIOME_WHERE_LAND},
    {.name = "old_growth_pine_taiga", .where = BIOME_WHERE_LAND},
    {.name = "old_growth_spruce_taiga", .where = BIOME_WHERE_LAND},
    {.name = "snowy_taiga", .where = BIOME_WHERE_LAND},
    {.name = "snowy_plains", .where = BIOME_WHERE_LAND},
    {.name = "snowy_beach", .where = BIOME_WHERE_BEACH},
    {.name = "ice_spikes", .where = BIOME_WHERE_LAND},
    {.name = "swamp", .where = BIOME_WHERE_LAND},
    {.name = "mangrove_swamp", .where = BIOME_WHERE_LAND},
    {.name = "desert", .where = BIOME_WHERE_LAND},
    {.name = "savanna", .where = BIOME_WHERE_LAND},
    {.name = "savanna_plateau", .where = BIOME_WHERE_LAND},
    {.name = "windswept_hills", .where = BIOME_WHERE_LAND},
    {.name = "windswept_gravelly_hills", .where = BIOME_WHERE_LAND},
    {.name = "windswept_forest", .where = BIOME_WHERE_LAND},
    {.name = "windswept_savanna", .where = BIOME_WHERE_LAND},
    {.name = "badlands", .where = BIOME_WHERE_LAND},
    {.name = "wooded_badlands", .where = BIOME_WHERE_LAND},
    {.name = "eroded_badlands", .where = BIOME_WHERE_LAND},
    {.name = "jungle", .where = BIOME_WHERE_LAND},
    {.name = "sparse_jungle", .where = BIOME_WHERE_LAND},
    {.name = "bamboo_jungle", .where = BIOME_WHERE_LAND},
    {.name = "meadow", .where = BIOME_WHERE_LAND},
    {.name = "cherry_grove", .where = BIOME_WHERE_LAND},
    {.name = "grove", .where = BIOME_WHERE_LAND},
    {.name = "snowy_slopes", .where = BIOME_WHERE_LAND},
    {.name = "jagged_peaks", .where = BIOME_WHERE_LAND},
    {.name = "frozen_peaks", .where = BIOME_WHERE_LAND},
    {.name = "stony_peaks", .where = BIOME_WHERE_LAND},
    {.name = "beach", .where = BIOME_WHERE_BEACH},
    {.name = "stony_shore", .where = BIOME_WHERE_STONY_SHORE},
    {.name = "river", .where = BIOME_WHERE_RIVER},
    {.name = "frozen_river", .where = BIOME_WHERE_RIVER},
    {.name = "ocean", .where = BIOME_WHERE_OCEAN},
    {.name = "warm_ocean", .where = BIOME_WHERE_OCEAN},
    {.name = "lukewarm_ocean", .where = BIOME_WHERE_OCEAN},
    {.name = "cold_ocean", .where = BIOME_WHERE_OCEAN},
    {.name = "frozen_ocean", .where = BIOME_WHERE_OCEAN},
    {.name = "deep_ocean", .where = BIOME_WHERE_DEEP_OCEAN},
    {.name = "deep_lukewarm_ocean", .where = BIOME_WHERE_DEEP_OCEAN},
    {.name = "deep_cold_ocean", .where = BIOME_WHERE_DEEP_OCEAN},
    {.name = "deep_frozen_ocean", .where = BIOME_WHERE_DEEP_OCEAN},
    {.name = "mushroom_fields", .where = BIOME_WHERE_LAND},
    {.name = "dripstone_caves", .where = BIOME_WHERE_UNDERGROUND},
    {.name = "lush_caves", .where = BIOME_WHERE_UNDERGROUND},
    {.name = "deep_dark", .where = BIOME_WHERE_UNDERGROUND},
};
enum : uint32_t {
    BIO_LISTED_COUNT = sizeof(BIO_LISTED) / sizeof(BIO_LISTED[0]),
};

static uint32_t bio_find(const char *name) {
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        if (strcmp(biome_defs[i].name, name) == 0) {
            return i;
        }
    }
    return BIOME_COUNT;
}

TEST(every_biome_the_plan_lists_is_in_the_table) {
    CHECK_EQ_U64(BIOME_COUNT, BIO_LISTED_COUNT);
    for (uint32_t i = 0u; i < BIO_LISTED_COUNT; i++) {
        const uint32_t found = bio_find(BIO_LISTED[i].name);
        REQUIRE(found != BIOME_COUNT);
        CHECK_EQ_U64(biome_defs[found].where, BIO_LISTED[i].where);
    }
    /* Sorted by name, so the ids do not depend on the order of reading. */
    for (uint32_t i = 1u; i < BIOME_COUNT; i++) {
        CHECK(strcmp(biome_defs[i - 1u].name, biome_defs[i].name) < 0);
    }
    CHECK_EQ_U64(BIOME_UNDERGROUND_COUNT, 3u);
    /* Highest priority first. */
    for (uint32_t i = 1u; i < BIOME_UNDERGROUND_COUNT; i++) {
        CHECK(biome_defs[biome_underground[i - 1u]].priority >= biome_defs[biome_underground[i]].priority);
    }
}

/* Some rows typed out from their files, to see that what the yaml says is what the game gets. */
TEST(the_table_says_what_the_files_say) {
    const biome_def_t *const desert = &biome_defs[BIOME_ID_DESERT];
    CHECK_STR_EQ(desert->name, "desert");
    CHECK_EQ_U64(desert->top, BLOCK_ID_SAND);
    CHECK_EQ_U64(desert->filler, BLOCK_ID_SAND);
    CHECK_EQ_U64(desert->under, BLOCK_ID_SANDSTONE);
    CHECK_EQ_U64(desert->under_depth, 4u);
    CHECK(desert->variety.low == -1.0f32 && desert->variety.high == 1.0f32); /* absent means all of it */
    CHECK(desert->wetness.low == -1.0f32 && desert->wetness.high == 0.3f32);
    CHECK_EQ_U64(desert->families, 1u << BIOME_FAMILY_DESERT);
    CHECK_EQ_U64(desert->limits, BIOME_LIMIT_RAIN);
    CHECK_EQ_I64(desert->rain_low_mm, 0);
    CHECK_EQ_I64(desert->rain_high_mm, 400);
    CHECK_EQ_U64(biome_defs[BIOME_ID_STONY_SHORE].families, (1u << BIOME_FAMILY_COUNT) - 1u);
    CHECK_EQ_U64(biome_defs[BIOME_ID_LUSH_CAVES].families, 0u);
    CHECK_STR_EQ(biome_family_names[BIOME_FAMILY_MEDITERRANEAN], "Mediterranean");
    CHECK(desert->erosion.low == -1.0f32 && desert->erosion.high == 1.0f32); /* absent means all of it */
    CHECK(desert->height.low == -1000.0f32 && desert->height.high == 500.0f32);

    const biome_def_t *const badlands = &biome_defs[BIOME_ID_BADLANDS];
    CHECK_EQ_U64(badlands->top, BLOCK_ID_RED_SAND);
    CHECK_EQ_U64(badlands->bands_depth, 14u);
    CHECK_EQ_I64(badlands->bands_from, 40);

    const biome_def_t *const mangrove = &biome_defs[BIOME_ID_MANGROVE_SWAMP];
    CHECK_EQ_U64(mangrove->top, BLOCK_ID_MUD);
    CHECK_EQ_U64(mangrove->wet_top, BLOCK_ID_MUD);
    CHECK_EQ_U64(mangrove->wet_depth, 3u);

    const biome_def_t *const lush = &biome_defs[BIOME_ID_LUSH_CAVES];
    CHECK_EQ_U64(lush->floor, BLOCK_ID_MOSS_BLOCK);
    CHECK_EQ_U64(lush->ceiling, BLOCK_ID_ROOTED_DIRT);
    CHECK_EQ_U64(lush->top, BLOCK_ID_AIR);
    CHECK(lush->height.low == -120.0f32 && lush->height.high == 1250.0f32);

    const biome_def_t *const windswept = &biome_defs[BIOME_ID_WINDSWEPT_HILLS];
    CHECK_EQ_U64(windswept->steep, BLOCK_ID_STONE);
    CHECK(windswept->steep_from == 1.0f32);
    CHECK_EQ_U64(windswept->high, BLOCK_ID_SNOW_BLOCK);
    CHECK_EQ_I64(windswept->high_from, 400);

    const biome_def_t *const taiga = &biome_defs[BIOME_ID_OLD_GROWTH_SPRUCE_TAIGA];
    REQUIRE(taiga->patch_count == 2u);
    CHECK_EQ_U64(biome_patches[taiga->first_patch].block, BLOCK_ID_COARSE_DIRT);
    CHECK(biome_patches[taiga->first_patch].from == 0.1f32 && biome_patches[taiga->first_patch].to == 0.5f32);
    CHECK_EQ_U64(biome_patches[taiga->first_patch + 1u].block, BLOCK_ID_GRASS_BLOCK);

    /* Features are for Stage 10.7, which reads them from here. */
    CHECK(biome_features[BIOME_ID_DESERT][BIOME_FEATURE_CACTUS] == 1.0f32);
    CHECK(biome_features[BIOME_ID_DESERT][BIOME_FEATURE_TREE_OAK] == 0.0f32);
    CHECK(biome_features[BIOME_ID_DARK_FOREST][BIOME_FEATURE_TREE_DARK_OAK] == 14.0f32);
    CHECK(biome_features[BIOME_ID_MANGROVE_SWAMP][BIOME_FEATURE_TREE_MANGROVE] == 8.0f32);
    CHECK(biome_features[BIOME_ID_CHERRY_GROVE][BIOME_FEATURE_TREE_CHERRY] == 3.0f32);
    CHECK(biome_features[BIOME_ID_FROZEN_RIVER][BIOME_FEATURE_ICE] == 1.0f32);
}

/* ---- The choice, by hand ------------------------------------------------------------------------------------------ */

typedef struct bio_hand_t {
    terrain_biome_inputs_t inputs; /* 0 */
    uint8_t expected;              /* 40: biome_id_t */
    uint8_t _pad0[7];              /* 41 */
} bio_hand_t;
static_assert(sizeof(bio_hand_t) == 48);

/* Each worked out from the files: which of the family's boxes hold the point, and which has the highest priority. Plan 006
 * phase 03: the family first, then variety, wetness and the landform. */
static const bio_hand_t BIO_HAND[] = {
#define BIO_CASE(f_, v_, h_, c_, e_, w_, g_, k_, r_, id)                                                               \
    {.inputs = {.variety = (v_),                                                                                       \
                .wetness = (h_),                                                                                       \
                .continentalness = (c_),                                                                               \
                .erosion = (e_),                                                                                       \
                .weirdness = (w_),                                                                                     \
                .ground = (g_),                                                                                        \
                .kind = (k_),                                                                                          \
                .family = BIOME_FAMILY_##f_,                                                                           \
                .rain_mm_year = (r_)},                                                                                 \
     .expected = (id)}
#define LAND TERRAIN_HYDROLOGY_KIND_LAND
    /* Plain land by family and wetness, at 40 blocks, on middling erosion. */
    BIO_CASE(TEMPERATE, 0.0f32, -0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 1000, BIOME_ID_PLAINS),
    BIO_CASE(TEMPERATE, 0.0f32, 0.0f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 1000, BIOME_ID_FOREST),
    BIO_CASE(TEMPERATE, 0.0f32, 0.2f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 1000, BIOME_ID_BIRCH_FOREST),
    BIO_CASE(TEMPERATE, 0.0f32, 0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 1000, BIOME_ID_DARK_FOREST),
    BIO_CASE(BOREAL, 0.0f32, 0.2f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 800, BIOME_ID_TAIGA),
    BIO_CASE(BOREAL, 0.0f32, 0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 800, BIOME_ID_OLD_GROWTH_SPRUCE_TAIGA),
    BIO_CASE(TUNDRA, 0.0f32, 0.0f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 500, BIOME_ID_SNOWY_PLAINS),
    BIO_CASE(TUNDRA, 0.0f32, 0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 500, BIOME_ID_SNOWY_TAIGA),
    BIO_CASE(SAVANNA, 0.0f32, 0.0f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 800, BIOME_ID_SAVANNA),
    BIO_CASE(RAINFOREST, 0.0f32, 0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 2500, BIOME_ID_JUNGLE),
    BIO_CASE(MONSOON, 0.0f32, 0.2f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 2000, BIOME_ID_SPARSE_JUNGLE),
    BIO_CASE(DESERT, 0.0f32, -0.5f32, 0.5f32, 0.6f32, 0.0f32, 20.0f32, LAND, 100, BIOME_ID_DESERT),
    /* The same column in each family: a family never gives another family's biome. */
    BIO_CASE(STEPPE, -0.5f32, -0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 400, BIOME_ID_PLAINS),
    BIO_CASE(STEPPE, 0.5f32, -0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 400, BIOME_ID_SAVANNA),
    BIO_CASE(ICE, 0.0f32, 0.5f32, 0.5f32, 0.0f32, 0.0f32, 40.0f32, LAND, 300, BIOME_ID_SNOWY_PLAINS),
    /* Ahmed's 1.png: high ground in the Rainforest is never snow, whatever the wetness. */
    BIO_CASE(RAINFOREST, 0.0f32, -0.5f32, 0.8f32, -0.5f32, 0.0f32, 400.0f32, LAND, 2500, BIOME_ID_SPARSE_JUNGLE),
    /* Priority: the rare row inside a common one's box. */
    BIO_CASE(TEMPERATE, 0.0f32, -0.5f32, 0.5f32, 0.0f32, 0.5f32, 40.0f32, LAND, 1000, BIOME_ID_SUNFLOWER_PLAINS),
    BIO_CASE(TEMPERATE, 0.0f32, -0.2f32, 0.5f32, 0.0f32, -0.5f32, 40.0f32, LAND, 1000, BIOME_ID_FLOWER_FOREST),
    BIO_CASE(TEMPERATE, 0.0f32, 0.2f32, 0.5f32, 0.0f32, 0.5f32, 40.0f32, LAND, 1000, BIOME_ID_OLD_GROWTH_BIRCH_FOREST),
    BIO_CASE(BOREAL, 0.0f32, 0.2f32, 0.5f32, 0.0f32, 0.5f32, 40.0f32, LAND, 800, BIOME_ID_OLD_GROWTH_PINE_TAIGA),
    BIO_CASE(ICE, 0.0f32, -0.6f32, 0.5f32, 0.0f32, 0.5f32, 40.0f32, LAND, 300, BIOME_ID_ICE_SPIKES),
    BIO_CASE(RAINFOREST, 0.0f32, 0.5f32, 0.5f32, 0.0f32, 0.5f32, 40.0f32, LAND, 2500, BIOME_ID_BAMBOO_JUNGLE),
    /* Swamps are flat, wet and low; tropical ones are mangroves. */
    BIO_CASE(TEMPERATE, 0.0f32, 0.2f32, 0.2f32, 0.7f32, 0.0f32, 10.0f32, LAND, 1200, BIOME_ID_SWAMP),
    BIO_CASE(RAINFOREST, 0.0f32, 0.2f32, 0.2f32, 0.7f32, 0.0f32, 10.0f32, LAND, 2500, BIOME_ID_MANGROVE_SWAMP),
    /* And right down to the sea. */
    BIO_CASE(TEMPERATE, 0.0f32, 0.2f32, -0.4f32, 0.7f32, 0.0f32, 10.0f32, LAND, 1200, BIOME_ID_SWAMP),
    /* A lake is land under water, so it has land's biomes. */
    BIO_CASE(TEMPERATE, 0.0f32, 0.2f32, 0.2f32, 0.7f32, 0.0f32, -3.0f32, TERRAIN_HYDROLOGY_KIND_LAKE, 1200,
             BIOME_ID_SWAMP),
    BIO_CASE(TEMPERATE, 0.0f32, -0.5f32, 0.5f32, 0.0f32, 0.0f32, -3.0f32, TERRAIN_HYDROLOGY_KIND_LAKE, 1000,
             BIOME_ID_PLAINS),
    /* Dry and dissected: the badlands. */
    BIO_CASE(DESERT, 0.0f32, -0.5f32, 0.5f32, -0.3f32, 0.0f32, 60.0f32, LAND, 100, BIOME_ID_BADLANDS),
    BIO_CASE(MEDITERRANEAN, 0.8f32, 0.5f32, 0.5f32, -0.3f32, 0.0f32, 60.0f32, LAND, 500, BIOME_ID_WOODED_BADLANDS),
    BIO_CASE(DESERT, 0.0f32, -0.5f32, 0.5f32, -0.3f32, 0.6f32, 60.0f32, LAND, 100, BIOME_ID_ERODED_BADLANDS),
    /* The windswept hills, by family and variety. */
    BIO_CASE(BOREAL, 0.5f32, 0.0f32, 0.5f32, 0.0f32, 0.9f32, 60.0f32, LAND, 800, BIOME_ID_WINDSWEPT_HILLS),
    BIO_CASE(TUNDRA, 0.0f32, 0.0f32, 0.5f32, 0.0f32, 0.9f32, 60.0f32, LAND, 500, BIOME_ID_WINDSWEPT_GRAVELLY_HILLS),
    BIO_CASE(TEMPERATE, -0.5f32, 0.0f32, 0.5f32, 0.0f32, 0.9f32, 60.0f32, LAND, 1000, BIOME_ID_WINDSWEPT_FOREST),
    BIO_CASE(SAVANNA, 0.0f32, -0.5f32, 0.5f32, 0.0f32, 0.9f32, 60.0f32, LAND, 700, BIOME_ID_WINDSWEPT_SAVANNA),
    /* Height: a warm plateau, and what is on the high ground. */
    BIO_CASE(SAVANNA, 0.0f32, -0.5f32, 0.5f32, 0.0f32, 0.0f32, 200.0f32, LAND, 700, BIOME_ID_SAVANNA_PLATEAU),
    BIO_CASE(TEMPERATE, 0.0f32, -0.5f32, 0.8f32, -0.5f32, 0.0f32, 400.0f32, LAND, 1000, BIOME_ID_MEADOW),
    BIO_CASE(TEMPERATE, 0.0f32, 0.5f32, 0.8f32, -0.5f32, 0.0f32, 400.0f32, LAND, 1000, BIOME_ID_CHERRY_GROVE),
    BIO_CASE(BOREAL, 0.0f32, 0.5f32, 0.8f32, -0.5f32, 0.0f32, 400.0f32, LAND, 800, BIOME_ID_GROVE),
    BIO_CASE(TUNDRA, 0.0f32, -0.5f32, 0.8f32, -0.5f32, 0.0f32, 400.0f32, LAND, 500, BIOME_ID_SNOWY_SLOPES),
    BIO_CASE(TUNDRA, 0.0f32, 0.0f32, 0.8f32, -0.8f32, 0.6f32, 600.0f32, LAND, 500, BIOME_ID_JAGGED_PEAKS),
    BIO_CASE(ICE, 0.0f32, 0.0f32, 0.8f32, -0.8f32, -0.6f32, 600.0f32, LAND, 300, BIOME_ID_FROZEN_PEAKS),
    BIO_CASE(SAVANNA, 0.0f32, 0.0f32, 0.8f32, -0.8f32, 0.6f32, 600.0f32, LAND, 700, BIOME_ID_STONY_PEAKS),
    /* Low woods go on up a range's foot, and bare peaks start only high up. */
    BIO_CASE(TEMPERATE, 0.0f32, 0.0f32, 0.5f32, 0.0f32, 0.0f32, 280.0f32, LAND, 1000, BIOME_ID_FOREST),
    BIO_CASE(TEMPERATE, 0.0f32, 0.0f32, 0.8f32, -0.8f32, 0.0f32, 1200.0f32, LAND, 1000, BIOME_ID_STONY_PEAKS),
    /* The rarest, on the coast. */
    BIO_CASE(TEMPERATE, 0.0f32, 0.0f32, -0.35f32, 0.0f32, -0.7f32, 12.0f32, LAND, 1000, BIOME_ID_MUSHROOM_FIELDS),
    /* What hydrology says the column is decides which rows are looked at, and the family which of them. */
    BIO_CASE(TEMPERATE, 0.0f32, 0.0f32, -0.5f32, 0.0f32, 0.0f32, -30.0f32, TERRAIN_HYDROLOGY_KIND_OCEAN, 1000,
             BIOME_ID_OCEAN),
    BIO_CASE(RAINFOREST, 0.0f32, 0.0f32, -0.5f32, 0.0f32, 0.0f32, -30.0f32, TERRAIN_HYDROLOGY_KIND_OCEAN, 2500,
             BIOME_ID_WARM_OCEAN),
    BIO_CASE(RAINFOREST, 0.8f32, 0.0f32, -0.5f32, 0.0f32, 0.0f32, -30.0f32, TERRAIN_HYDROLOGY_KIND_OCEAN, 2500,
             BIOME_ID_LUKEWARM_OCEAN),
    BIO_CASE(MEDITERRANEAN, 0.5f32, 0.0f32, -0.5f32, 0.0f32, 0.0f32, -30.0f32, TERRAIN_HYDROLOGY_KIND_OCEAN, 500,
             BIOME_ID_LUKEWARM_OCEAN),
    BIO_CASE(BOREAL, 0.0f32, 0.0f32, -0.5f32, 0.0f32, 0.0f32, -30.0f32, TERRAIN_HYDROLOGY_KIND_OCEAN, 800,
             BIOME_ID_COLD_OCEAN),
    BIO_CASE(ICE, 0.0f32, 0.0f32, -0.5f32, 0.0f32, 0.0f32, -30.0f32, TERRAIN_HYDROLOGY_KIND_OCEAN, 300,
             BIOME_ID_FROZEN_OCEAN),
    BIO_CASE(TEMPERATE, 0.0f32, 0.0f32, -0.9f32, 0.0f32, 0.0f32, -90.0f32, TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN, 1000,
             BIOME_ID_DEEP_OCEAN),
    /* There is no deep warm ocean: the deep lukewarm one goes on up. */
    BIO_CASE(RAINFOREST, -0.9f32, 0.0f32, -0.9f32, 0.0f32, 0.0f32, -90.0f32, TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN, 2500,
             BIOME_ID_DEEP_LUKEWARM_OCEAN),
    BIO_CASE(TUNDRA, 0.0f32, 0.0f32, -0.9f32, 0.0f32, 0.0f32, -90.0f32, TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN, 500,
             BIOME_ID_DEEP_COLD_OCEAN),
    BIO_CASE(ICE, 0.0f32, 0.0f32, -0.9f32, 0.0f32, 0.0f32, -90.0f32, TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN, 300,
             BIOME_ID_DEEP_FROZEN_OCEAN),
    BIO_CASE(SAVANNA, 0.0f32, 0.0f32, 0.5f32, 0.0f32, 0.0f32, -4.0f32, TERRAIN_HYDROLOGY_KIND_RIVER, 700,
             BIOME_ID_RIVER),
    BIO_CASE(ICE, 0.0f32, 0.0f32, 0.5f32, 0.0f32, 0.0f32, -4.0f32, TERRAIN_HYDROLOGY_KIND_RIVER, 300,
             BIOME_ID_FROZEN_RIVER),
    BIO_CASE(SAVANNA, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 2.0f32, TERRAIN_HYDROLOGY_KIND_BEACH, 700,
             BIOME_ID_BEACH),
    BIO_CASE(TUNDRA, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 2.0f32, TERRAIN_HYDROLOGY_KIND_BEACH, 500,
             BIOME_ID_SNOWY_BEACH),
    BIO_CASE(ICE, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 2.0f32, TERRAIN_HYDROLOGY_KIND_STONY_SHORE, 300,
             BIOME_ID_STONY_SHORE),
#undef LAND
#undef BIO_CASE
};

TEST(the_choice_is_worked_out_by_hand) {
    const uint32_t cases = sizeof(BIO_HAND) / sizeof(BIO_HAND[0]);
    for (uint32_t i = 0u; i < cases; i++) {
        const biome_id_t got = terrain_biome_choose(&BIO_HAND[i].inputs);
        if (got != BIO_HAND[i].expected) {
            printf("case %" PRIu32 ": chose %s, expected %s\n", i, biome_defs[got].name,
                   biome_defs[BIO_HAND[i].expected].name);
        }
        CHECK_EQ_U64(got, BIO_HAND[i].expected);
    }
}

/* The sum in the README, typed out again, for a point in no box at all. */
TEST(the_distance_is_the_readmes_sum) {
    const terrain_biome_inputs_t inputs = {
        .variety = 0.5f32,
        .wetness = 0.5f32,
        .continentalness = 0.5f32,
        .erosion = 0.5f32,
        .weirdness = 0.5f32,
        .ground = 300.0f32,
        .kind = TERRAIN_HYDROLOGY_KIND_LAND,
        .rain_mm_year = 1500,
        .t_warm_cc = 2000,
    };
    const biome_def_t row = {
        .variety = {-1.0f32, 0.25f32},        /* 0.25 past the top */
        .wetness = {0.0f32, 1.0f32},          /* inside */
        .continentalness = {0.75f32, 1.0f32}, /* 0.25 under the bottom */
        .erosion = {-1.0f32, 1.0f32},         /* inside */
        .weirdness = {0.5f32, 1.0f32},        /* at the end: inside */
        .height = {0.0f32, 44.0f32},          /* 256 blocks past the top, so 2 */
        .limits = BIOME_LIMIT_RAIN | BIOME_LIMIT_T_WARM,
        .rain_low_mm = 0,
        .rain_high_mm = 1000, /* 500 mm past the top: half of 1000 */
        .t_warm_low_cc = 0,
        .t_warm_high_cc = 2500, /* inside */
    };
    const float32_t expected = 0.25f32 * 0.25f32 + 0.25f32 * 0.25f32 + 2.0f32 * 2.0f32 + 0.5f32 * 0.5f32;
    CHECK_NEAR(terrain_biome_distance(&row, &inputs), expected, 1e-6f32);
}

/* Plan 006 phase 03, the master's worked example (2.4): a cell whose sea-level yearly mean is 14 degrees with a swing of 10.
 * At z 80 the warmest month is 23.0 and the coldest 3.0: Temperate; at z 1100, 10.8 and -9.2: Boreal; at z 1250, 9.0:
 * Tundra. The sample's warm and cold are the mean plus and minus the swing times 0.98862 (a month's mean of the wave), so the
 * swing written in is 10 / 0.98862. */
TEST(the_family_follows_the_worked_example) {
    terrain_climate_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.warp_off = 1u;
    for (int32_t j = 0; j < TERRAIN_CLIMATE_SIDE; j++) {
        for (int32_t i = 0; i < TERRAIN_CLIMATE_SIDE; i++) {
            terrain_climate_cell_make(14.0f64, 10.0f64 / 0.98862f64, 1200.0f64, 9999.0f64, 80.0f64,
                                      TERRAIN_CLIMATE_CLASS_TEMPERATE, 0u, &sample.cells[j][i]);
        }
    }
    const int32_t heights[3] = {80, 1100, 1250};
    const biome_family_t expected[3] = {BIOME_FAMILY_TEMPERATE, BIOME_FAMILY_BOREAL, BIOME_FAMILY_TUNDRA};
    for (uint32_t k = 0u; k < 3u; k++) {
        const terrain_climate_point_t point = terrain_climate_at(&sample, 150, 150, heights[k]);
        printf("z %d: warm %.2f, cold %.2f, family %s\n", heights[k],
               (libc_double_t)((float64_t)point.t_warm_cc / 100.0f64),
               (libc_double_t)((float64_t)point.t_cold_cc / 100.0f64),
               biome_family_names[terrain_biome_family(&point)]);
        CHECK_EQ_U64(terrain_biome_family(&point), expected[k]);
    }
    /* A dry summer makes the temperate column Mediterranean, and nothing else. */
    for (int32_t j = 0; j < TERRAIN_CLIMATE_SIDE; j++) {
        for (int32_t i = 0; i < TERRAIN_CLIMATE_SIDE; i++) {
            sample.cells[j][i].flags = TERRAIN_CLIMATE_DRY_SUMMER;
        }
    }
    const terrain_climate_point_t low = terrain_climate_at(&sample, 150, 150, 80);
    CHECK_EQ_U64(terrain_biome_family(&low), BIOME_FAMILY_MEDITERRANEAN);
    const terrain_climate_point_t high = terrain_climate_at(&sample, 150, 150, 1100);
    CHECK_EQ_U64(terrain_biome_family(&high), BIOME_FAMILY_BOREAL);
    /* Every class names its family, and the sea none. */
    CHECK_EQ_U64(terrain_biome_family_of_class(TERRAIN_CLIMATE_CLASS_NONE), BIOME_FAMILY_COUNT);
    CHECK_EQ_U64(terrain_biome_family_of_class(TERRAIN_CLIMATE_CLASS_MONSOON), BIOME_FAMILY_MONSOON);
    CHECK_EQ_U64(terrain_biome_family_of_class(TERRAIN_CLIMATE_CLASS_CONTINENTAL), BIOME_FAMILY_CONTINENTAL);
    /* The heights: water's family at the sea's top, the rest at the ground, rounded down. */
    CHECK_EQ_I64(terrain_biome_climate_z(-80.5f32, TERRAIN_HYDROLOGY_KIND_OCEAN), MC_WORLD_Z_SEA);
    CHECK_EQ_I64(terrain_biome_climate_z(-80.5f32, TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN), MC_WORLD_Z_SEA);
    CHECK_EQ_I64(terrain_biome_climate_z(-3.5f32, TERRAIN_HYDROLOGY_KIND_LAKE), -4);
    CHECK_EQ_I64(terrain_biome_climate_z(412.9f32, TERRAIN_HYDROLOGY_KIND_LAND), 412);
    CHECK(terrain_biome_height(412.9f32) == 412.9f32);
    /* Wetness: the field plus half of the rain's bias, clamped. */
    CHECK(terrain_biome_wetness(0.0f32, 1000) == 0.0f32);
    CHECK(terrain_biome_wetness(0.0f32, 2000) == 0.5f32);
    CHECK(terrain_biome_wetness(0.0f32, 9000) == 0.5f32);
    CHECK(terrain_biome_wetness(0.0f32, 0) == -0.5f32);
    CHECK(terrain_biome_wetness(0.8f32, 3000) == 1.0f32);
}

/* A million random inputs: whatever they are, the biome chosen is a member of the input's family, of the right where. */
TEST(the_choice_never_leaves_the_family) {
    static const uint8_t WHERE_OF_KIND[TERRAIN_HYDROLOGY_KIND_COUNT] = {
        [TERRAIN_HYDROLOGY_KIND_LAND] = BIOME_WHERE_LAND,
        [TERRAIN_HYDROLOGY_KIND_OCEAN] = BIOME_WHERE_OCEAN,
        [TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN] = BIOME_WHERE_DEEP_OCEAN,
        [TERRAIN_HYDROLOGY_KIND_RIVER] = BIOME_WHERE_RIVER,
        [TERRAIN_HYDROLOGY_KIND_LAKE] = BIOME_WHERE_LAND,
        [TERRAIN_HYDROLOGY_KIND_BEACH] = BIOME_WHERE_BEACH,
        [TERRAIN_HYDROLOGY_KIND_STONY_SHORE] = BIOME_WHERE_STONY_SHORE,
    };
    terrain_rng_t rng = terrain_rng_start(0xFA3117ull);
    uint64_t chosen[BIOME_COUNT] = {0u};
    for (uint32_t i = 0u; i < 1000000u; i++) {
        const terrain_biome_inputs_t inputs = {
            .variety = terrain_rng_next_f32(&rng) * 2.0f32 - 1.0f32,
            .wetness = terrain_rng_next_f32(&rng) * 2.0f32 - 1.0f32,
            .continentalness = terrain_rng_next_f32(&rng) * 2.0f32 - 1.0f32,
            .erosion = terrain_rng_next_f32(&rng) * 2.0f32 - 1.0f32,
            .weirdness = terrain_rng_next_f32(&rng) * 2.0f32 - 1.0f32,
            .ground = terrain_rng_next_f32(&rng) * 1500.0f32 - 250.0f32,
            .kind = (terrain_hydrology_kind_t)terrain_rng_next_below(&rng, TERRAIN_HYDROLOGY_KIND_COUNT),
            .family = (uint8_t)terrain_rng_next_below(&rng, BIOME_FAMILY_COUNT),
            .t_warm_cc = (int32_t)terrain_rng_next_below(&rng, 6000u) - 3000,
            .t_cold_cc = (int32_t)terrain_rng_next_below(&rng, 6000u) - 4000,
            .rain_mm_year = (int32_t)terrain_rng_next_below(&rng, 4000u),
        };
        const biome_id_t got = terrain_biome_choose(&inputs);
        chosen[got]++;
        if (!CHECK((biome_defs[got].families & (1u << inputs.family)) != 0u) ||
            !CHECK_EQ_U64(biome_defs[got].where, WHERE_OF_KIND[inputs.kind])) {
            printf("%s chosen in family %s\n", biome_defs[got].name, biome_family_names[inputs.family]);
            break;
        }
    }
    /* And every surface biome can be chosen at all. */
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        if (biome_defs[i].where != BIOME_WHERE_UNDERGROUND && chosen[i] == 0u) {
            printf("%s was never chosen\n", biome_defs[i].name);
        }
        CHECK(biome_defs[i].where == BIOME_WHERE_UNDERGROUND || chosen[i] != 0u);
    }
}

/* A row that is the nearest to nothing could never turn up. Each is at no distance from the middle of its own box, and whatever
 * wins there outranks it or is it. */
TEST(each_row_holds_the_middle_of_its_box_or_gives_way_to_a_rarer_one) {
    static const terrain_hydrology_kind_t KIND_OF_WHERE[BIOME_WHERE_UNDERGROUND] = {
        [BIOME_WHERE_LAND] = TERRAIN_HYDROLOGY_KIND_LAND,
        [BIOME_WHERE_OCEAN] = TERRAIN_HYDROLOGY_KIND_OCEAN,
        [BIOME_WHERE_DEEP_OCEAN] = TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN,
        [BIOME_WHERE_RIVER] = TERRAIN_HYDROLOGY_KIND_RIVER,
        [BIOME_WHERE_BEACH] = TERRAIN_HYDROLOGY_KIND_BEACH,
        [BIOME_WHERE_STONY_SHORE] = TERRAIN_HYDROLOGY_KIND_STONY_SHORE,
    };
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        const biome_def_t *const def = &biome_defs[i];
        if (def->where == BIOME_WHERE_UNDERGROUND) {
            continue;
        }
        /* In each of its families: plan 006 phase 03 chooses only among a family's members. */
        for (uint32_t family = 0u; family < BIOME_FAMILY_COUNT; family++) {
            if ((def->families & (1u << family)) == 0u) {
                continue;
            }
            const bool rain = (def->limits & BIOME_LIMIT_RAIN) != 0u;
            const terrain_biome_inputs_t inputs = {
                .variety = (def->variety.low + def->variety.high) * 0.5f32,
                .wetness = (def->wetness.low + def->wetness.high) * 0.5f32,
                .continentalness = (def->continentalness.low + def->continentalness.high) * 0.5f32,
                .erosion = (def->erosion.low + def->erosion.high) * 0.5f32,
                .weirdness = (def->weirdness.low + def->weirdness.high) * 0.5f32,
                .ground = (def->height.low + def->height.high) * 0.5f32,
                .kind = KIND_OF_WHERE[def->where],
                .family = (uint8_t)family,
                .rain_mm_year = rain ? (def->rain_low_mm + def->rain_high_mm) / 2 : 1000,
            };
            CHECK(terrain_biome_distance(def, &inputs) == 0.0f32);
            const biome_id_t won = terrain_biome_choose(&inputs);
            CHECK(terrain_biome_distance(&biome_defs[won], &inputs) == 0.0f32);
            if (won != i && biome_defs[won].priority <= def->priority) {
                printf("%s gives way to %s in %s, of no higher a priority\n", def->name, biome_defs[won].name,
                       biome_family_names[family]);
            }
            CHECK(won == i || biome_defs[won].priority > def->priority);
        }
    }
}

TEST(a_voxel_is_underground_only_deep_enough_and_at_its_biomes_heights) {
    uint32_t lush_bit = BIOME_UNDERGROUND_COUNT;
    uint32_t dark_bit = BIOME_UNDERGROUND_COUNT;
    for (uint32_t i = 0u; i < BIOME_UNDERGROUND_COUNT; i++) {
        lush_bit = biome_underground[i] == BIOME_ID_LUSH_CAVES ? i : lush_bit;
        dark_bit = biome_underground[i] == BIOME_ID_DEEP_DARK ? i : dark_bit;
    }
    REQUIRE(lush_bit != BIOME_UNDERGROUND_COUNT && dark_bit != BIOME_UNDERGROUND_COUNT);
    const terrain_biome_column_t column = {
        .ground = 50.0f32,
        .biome = BIOME_ID_PLAINS,
        .caves = (uint8_t)((1u << lush_bit) | (1u << dark_bit)),
    };
    /* The roof: 16 under the ground. */
    CHECK_EQ_U64(terrain_biome_of_voxel(&column, 34), BIOME_ID_LUSH_CAVES);
    CHECK_EQ_U64(terrain_biome_of_voxel(&column, 35), BIOME_ID_PLAINS);
    CHECK_EQ_U64(terrain_biome_of_voxel(&column, 200), BIOME_ID_PLAINS);
    /* The lush caves' floor is -120, and the deep dark's top -140. */
    CHECK_EQ_U64(terrain_biome_of_voxel(&column, -120), BIOME_ID_LUSH_CAVES);
    CHECK_EQ_U64(terrain_biome_of_voxel(&column, -121), BIOME_ID_PLAINS);
    CHECK_EQ_U64(terrain_biome_of_voxel(&column, -140), BIOME_ID_DEEP_DARK);
    CHECK_EQ_U64(terrain_biome_of_voxel(&column, -250), BIOME_ID_DEEP_DARK);
    const terrain_biome_column_t bare = {.ground = 50.0f32, .biome = BIOME_ID_PLAINS};
    CHECK_EQ_U64(terrain_biome_of_voxel(&bare, -120), BIOME_ID_PLAINS);

    const terrain_biome_inputs_t held = {.wetness = 0.7f32, .erosion = -0.5f32};
    const uint8_t caves = terrain_biome_caves_of(&held);
    CHECK((caves & (1u << lush_bit)) != 0u);
    CHECK((caves & (1u << dark_bit)) != 0u);
    const terrain_biome_inputs_t none = {.wetness = 0.0f32, .erosion = 0.5f32};
    CHECK_EQ_U64(terrain_biome_caves_of(&none), 0u);
}

/* ---- Every biome, in real worlds ---------------------------------------------------------------------------------- */

typedef struct bio_place_t {
    int32_t x;        /* 0 */
    int32_t y;        /* 4 */
    int32_t z;        /* 8: the ground, or for underground biomes the level */
    uint8_t _pad0[4]; /* 12 */
} bio_place_t;
static_assert(sizeof(bio_place_t) == 16);

typedef struct bio_found_t {
    uint64_t count[BIOME_COUNT];  /* columns, or for underground biomes, levels of a column */
    bio_place_t at[BIOME_COUNT];  /* where it first turned up, or first beside itself along a row of the search */
    uint64_t beside[BIOME_COUNT]; /* 1 once at is a place whose neighbour in the search is the same biome */
} bio_found_t;
static_assert(sizeof(bio_found_t) == 32u * BIOME_COUNT);

/* Goes over 65 kilometres of a world, finds where each biome first is, and says how often. */
static void bio_search(test_context_t *t, const terrain_t *terrain, bio_found_t *found) {
    memset(found, 0, sizeof(*found));
    terrain_biome_column_t *const columns = test_alloc(t, (uint64_t)BIO_SEARCH_SIDE * sizeof(terrain_biome_column_t));
    const int32_t half = (int32_t)(BIO_SEARCH_SIDE / 2u * BIO_SEARCH_STEP);
    for (uint32_t iy = 0u; iy < BIO_SEARCH_SIDE; iy++) {
        const terrain_fields_area_t area = {
            .origin_x = -half,
            .origin_y = -half + (int32_t)(iy * BIO_SEARCH_STEP),
            .step_x = (int32_t)BIO_SEARCH_STEP,
            .step_y = 1,
            .count_x = BIO_SEARCH_SIDE,
            .count_y = 1u,
        };
        terrain_biome_columns_area(terrain->borrowed_biome, nullptr, &area, columns);
        for (uint32_t ix = 0u; ix < BIO_SEARCH_SIDE; ix++) {
            const terrain_biome_column_t *const column = &columns[ix];
            const int32_t x = area.origin_x + (int32_t)(ix * BIO_SEARCH_STEP);
            /* Plan 006 phase 03: a family's border can leave a biome a lone column of the search, so where a test looks
             * for it is somewhere it has room, if there is one. */
            if (ix > 0u && columns[ix - 1u].biome == column->biome && found->beside[column->biome] == 0u) {
                found->beside[column->biome] = 1u;
                found->at[column->biome].x = x;
                found->at[column->biome].y = area.origin_y;
                found->at[column->biome].z = (int32_t)column->ground;
            }
            if (found->count[column->biome] == 0u) {
                found->at[column->biome].x = x;
                found->at[column->biome].y = area.origin_y;
                found->at[column->biome].z = (int32_t)column->ground;
            }
            found->count[column->biome]++;
            for (uint32_t level = 0u; level < BIO_CAVE_LEVELS; level++) {
                const uint8_t id = (uint8_t)terrain_biome_of_voxel(column, BIO_CAVE_LEVEL_Z[level]);
                if (biome_defs[id].where == BIOME_WHERE_UNDERGROUND) {
                    if (found->count[id] == 0u) {
                        found->at[id].x = x;
                        found->at[id].y = area.origin_y;
                        found->at[id].z = BIO_CAVE_LEVEL_Z[level];
                    }
                    found->count[id]++;
                }
            }
        }
    }
}

/* The search, for BIO_WORLD: several tests want to know where a biome is, and it takes a moment. */
static const bio_found_t *bio_found_in_world(test_context_t *t, const terrain_t *terrain) {
    static bio_found_t found;
    static bool done = false;
    if (!done) {
        bio_search(t, terrain, &found);
        done = true;
    }
    return &found;
}

/* The phase's gate: a biome-coverage test finds every biome. */
TEST(every_biome_turns_up_in_every_world_searched) {
    bio_found_t found;
    uint64_t totals[BIOME_COUNT] = {0};
    for (uint32_t s = 0u; s < BIO_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, BIO_SEED_LIST[s]));
        bio_search(t, &terrain, &found);
        terrain_deinit(&terrain);
        for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
            if (found.count[i] == 0u) {
                printf("seed 0x%016" PRIX64 ": no %s\n", BIO_SEED_LIST[s], biome_defs[i].name);
            }
            CHECK(found.count[i] != 0u);
            totals[i] += found.count[i];
        }
    }
    const uint64_t all = (uint64_t)BIO_SEEDS * BIO_SEARCH_SIDE * BIO_SEARCH_SIDE;
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        printf("%-26s %s%7.3f%%\n", biome_defs[i].name,
               biome_defs[i].where == BIOME_WHERE_UNDERGROUND ? "of levels " : "          ",
               (libc_double_t)((float64_t)totals[i] * 100.0f64 / (float64_t)all /
                               (biome_defs[i].where == BIOME_WHERE_UNDERGROUND ? (float64_t)BIO_CAVE_LEVELS : 1.0f64)));
    }
    /* No land biome takes over: the commonest of them holds under a seventh of all columns. */
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        if (biome_defs[i].where == BIOME_WHERE_LAND) {
            CHECK(totals[i] * 7u < all);
        }
    }
}

static bool bio_is_sea(uint8_t biome) {
    return biome_defs[biome].where == BIOME_WHERE_OCEAN || biome_defs[biome].where == BIOME_WHERE_DEEP_OCEAN;
}

static bool bio_is_peak(uint8_t biome) {
    return biome == BIOME_ID_STONY_PEAKS || biome == BIOME_ID_JAGGED_PEAKS || biome == BIOME_ID_FROZEN_PEAKS;
}

static bool bio_is_wooded(uint8_t biome) {
    for (uint32_t tree = BIOME_FEATURE_TREE_OAK; tree <= BIOME_FEATURE_TREE_CHERRY; tree++) {
        if (biome_features[biome][tree] >= 1.0f32) {
            return true;
        }
    }
    return false;
}

/* Is there sea within BIO_COAST_CELLS of this cell of the search? */
static bool bio_sea_is_near(const uint8_t *biomes, uint32_t ix, uint32_t iy) {
    const uint32_t y_first = iy > BIO_COAST_CELLS ? iy - BIO_COAST_CELLS : 0u;
    const uint32_t x_first = ix > BIO_COAST_CELLS ? ix - BIO_COAST_CELLS : 0u;
    for (uint32_t y = y_first; y <= iy + BIO_COAST_CELLS && y < BIO_SEARCH_SIDE; y++) {
        for (uint32_t x = x_first; x <= ix + BIO_COAST_CELLS && x < BIO_SEARCH_SIDE; x++) {
            if (bio_is_sea(biomes[x + BIO_SEARCH_SIDE * y])) {
                return true;
            }
        }
    }
    return false;
}

/* Plan 003 phase 8 stage 6: the table is set for the terrain there is, with its coast at about c = -0.46 and its ranges up to about
 * 1200. */
TEST(the_biomes_follow_the_terrain) {
    uint8_t *const biomes = test_alloc(t, (uint64_t)BIO_SEARCH_SIDE * BIO_SEARCH_SIDE);
    float32_t *const grounds = test_alloc(t, (uint64_t)BIO_SEARCH_SIDE * BIO_SEARCH_SIDE * sizeof(float32_t));
    terrain_biome_column_t *const columns = test_alloc(t, (uint64_t)BIO_SEARCH_SIDE * sizeof(terrain_biome_column_t));
    const int32_t half = (int32_t)(BIO_SEARCH_SIDE / 2u * BIO_SEARCH_STEP);
    for (uint32_t s = 0u; s < BIO_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, BIO_SEED_LIST[s]));
        for (uint32_t iy = 0u; iy < BIO_SEARCH_SIDE; iy++) {
            const terrain_fields_area_t area = {
                .origin_x = -half,
                .origin_y = -half + (int32_t)(iy * BIO_SEARCH_STEP),
                .step_x = (int32_t)BIO_SEARCH_STEP,
                .step_y = 1,
                .count_x = BIO_SEARCH_SIDE,
                .count_y = 1u,
            };
            terrain_biome_columns_area(terrain.borrowed_biome, nullptr, &area, columns);
            for (uint32_t ix = 0u; ix < BIO_SEARCH_SIDE; ix++) {
                biomes[ix + BIO_SEARCH_SIDE * iy] = columns[ix].biome;
                grounds[ix + BIO_SEARCH_SIDE * iy] = columns[ix].ground;
            }
        }
        terrain_deinit(&terrain);

        uint64_t land = 0u, peaks = 0u, under_peaks = 0u, far_under_peaks = 0u, low = 0u, low_wooded = 0u,
                 high_not_peak = 0u;
        uint64_t mushrooms = 0u, mushrooms_by_sea = 0u, swamps = 0u, swamps_by_sea = 0u, plains = 0u,
                 plains_by_sea = 0u;
        for (uint32_t iy = 0u; iy < BIO_SEARCH_SIDE; iy++) {
            for (uint32_t ix = 0u; ix < BIO_SEARCH_SIDE; ix++) {
                const uint32_t at = ix + BIO_SEARCH_SIDE * iy;
                const uint8_t biome = biomes[at];
                if (biome_defs[biome].where != BIOME_WHERE_LAND) {
                    continue;
                }
                land++;
                peaks += bio_is_peak(biome) ? 1u : 0u;
                under_peaks += bio_is_peak(biome) && grounds[at] < 500.0f32 ? 1u : 0u;
                far_under_peaks += bio_is_peak(biome) && grounds[at] < 384.0f32 ? 1u : 0u;
                high_not_peak += !bio_is_peak(biome) && grounds[at] >= 500.0f32 ? 1u : 0u;
                if (grounds[at] < 300.0f32) {
                    low++;
                    low_wooded += bio_is_wooded(biome) ? 1u : 0u;
                }
                if (biome == BIOME_ID_MUSHROOM_FIELDS) {
                    mushrooms++;
                    mushrooms_by_sea += bio_sea_is_near(biomes, ix, iy) ? 1u : 0u;
                } else if (biome == BIOME_ID_SWAMP || biome == BIOME_ID_MANGROVE_SWAMP) {
                    swamps++;
                    swamps_by_sea += bio_sea_is_near(biomes, ix, iy) ? 1u : 0u;
                } else if (biome == BIOME_ID_PLAINS) {
                    plains++;
                    plains_by_sea += bio_sea_is_near(biomes, ix, iy) ? 1u : 0u;
                }
            }
        }
        printf("seed 0x%016" PRIX64 ": of %" PRIu64 " land: peaks %.1f%%, under_peaks %" PRIu64
               ", high_not_peak %" PRIu64 "; under 300, wooded %.1f%%; within %u blocks of the sea: mushroom fields "
               "%.1f%%, swamps %.1f%%, plains %.1f%%\n",
               BIO_SEED_LIST[s], land, (libc_double_t)((float64_t)peaks * 100.0f64 / (float64_t)land), under_peaks,
               high_not_peak, (libc_double_t)((float64_t)low_wooded * 100.0f64 / (float64_t)low),
               BIO_COAST_CELLS * BIO_SEARCH_STEP,
               (libc_double_t)((float64_t)mushrooms_by_sea * 100.0f64 / (float64_t)mushrooms),
               (libc_double_t)((float64_t)swamps_by_sea * 100.0f64 / (float64_t)swamps),
               (libc_double_t)((float64_t)plains_by_sea * 100.0f64 / (float64_t)plains));
        /* Bare rock is the top of a range and not half of it. */
        CHECK(peaks * 100u < land * 15u);
        CHECK(peaks * 100u > land * 3u);
        /* Peaks only high up, and high up only peaks: the two sides of the same height. A column in no box of its height goes to the
         * nearest, so a few peaks start a little under 500, and at 116 under it the climate's largest gap outweighs the height's. */
        CHECK(under_peaks * 50u < peaks);
        CHECK_EQ_U64(far_under_peaks, 0u);
        CHECK_EQ_U64(high_not_peak, 0u);
        /* The oddest stretches of coast, and not anywhere inland. */
        CHECK(mushrooms_by_sea * 100u > mushrooms * 85u);
        /* Under the tree line, most of the land has woods. */
        CHECK(low_wooded * 2u > low);
    }
}

TEST(a_column_is_the_climate_on_the_lattice_and_what_hydrology_says_it_is) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    const bio_found_t *const found = bio_found_in_world(t, &terrain);
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        REQUIRE(found->count[i] != 0u);
        if (biome_defs[i].where == BIOME_WHERE_UNDERGROUND) {
            continue;
        }
        /* Off the lattice, a block in from where it was found, in every direction, for each biome. */
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                const int32_t x = found->at[i].x + dx;
                const int32_t y = found->at[i].y + dy;
                const int32_t cx = x & ~3;
                const int32_t cy = y & ~3;
                const float32_t tx = (float32_t)(x - cx) / 4.0f32;
                const float32_t ty = (float32_t)(y - cy) / 4.0f32;
                terrain_fields_point_t corner[4];
                terrain_fields_at(terrain.borrowed_fields, cx, cy, &corner[0]);
                terrain_fields_at(terrain.borrowed_fields, cx + 4, cy, &corner[1]);
                terrain_fields_at(terrain.borrowed_fields, cx, cy + 4, &corner[2]);
                terrain_fields_at(terrain.borrowed_fields, cx + 4, cy + 4, &corner[3]);
#define BIO_BLEND(field)                                                                                               \
    (corner[0].field + tx * (corner[1].field - corner[0].field) +                                                      \
     ty * ((corner[2].field + tx * (corner[3].field - corner[2].field)) -                                              \
           (corner[0].field + tx * (corner[1].field - corner[0].field))))
                const terrain_hydrology_column_t water = terrain_hydrology_column_at(terrain.borrowed_hydrology, x, y);
                terrain_climate_sample_t climate;
                terrain_climate_fallback(BIO_WORLD, x - 8, y - 8, &climate);
                /* Plan 006 phase 03: the climate at the column's own height, through the sample. */
                const int32_t z = terrain_biome_climate_z(water.ground, water.kind);
                /* Read at the column's lattice corner, as the noise fields are, at its own height. */
                const terrain_climate_point_t point = terrain_climate_at(&climate, cx, cy, z);
                const terrain_climate_point_t cell = terrain_climate_nearest_at(&climate, cx, cy, z);
                const terrain_biome_inputs_t inputs = {
                    .variety = BIO_BLEND(variety),
                    .wetness = terrain_biome_wetness(BIO_BLEND(wetness), point.rain_mm_year),
                    .continentalness = BIO_BLEND(continentalness),
                    .erosion = BIO_BLEND(erosion),
                    .weirdness = BIO_BLEND(weirdness),
                    .ground = water.ground,
                    .kind = water.kind,
                    .family = (uint8_t)terrain_biome_family(&cell),
                    .t_warm_cc = cell.t_warm_cc,
                    .t_cold_cc = cell.t_cold_cc,
                    .rain_mm_year = point.rain_mm_year,
                };
#undef BIO_BLEND
                const terrain_biome_column_t column = terrain_biome_column_at(terrain.borrowed_biome, &climate, x, y);
                CHECK_EQ_U64(column.biome, terrain_biome_choose(&inputs));
                CHECK_EQ_U64(column.caves, terrain_biome_caves_of(&inputs));
                CHECK_EQ_U64(column.family, inputs.family);
                CHECK(column.ground == water.ground);
                CHECK(column.softness == water.softness);
                CHECK(column.carve_top == water.carve_top);
                /* And the row it took is of the kind hydrology says. */
                switch (water.kind) {
                case TERRAIN_HYDROLOGY_KIND_LAND:
                case TERRAIN_HYDROLOGY_KIND_LAKE:
                    CHECK_EQ_U64(biome_defs[column.biome].where, BIOME_WHERE_LAND);
                    break;
                case TERRAIN_HYDROLOGY_KIND_OCEAN:
                    CHECK_EQ_U64(biome_defs[column.biome].where, BIOME_WHERE_OCEAN);
                    break;
                case TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN:
                    CHECK_EQ_U64(biome_defs[column.biome].where, BIOME_WHERE_DEEP_OCEAN);
                    break;
                case TERRAIN_HYDROLOGY_KIND_RIVER:
                    CHECK_EQ_U64(biome_defs[column.biome].where, BIOME_WHERE_RIVER);
                    break;
                case TERRAIN_HYDROLOGY_KIND_BEACH:
                    CHECK_EQ_U64(biome_defs[column.biome].where, BIOME_WHERE_BEACH);
                    break;
                case TERRAIN_HYDROLOGY_KIND_STONY_SHORE:
                    CHECK_EQ_U64(biome_defs[column.biome].where, BIOME_WHERE_STONY_SHORE);
                    break;
                case TERRAIN_HYDROLOGY_KIND_COUNT:
                    CHECK(false);
                    break;
                }
            }
        }
    }
    terrain_deinit(&terrain);
}

TEST(columns_do_not_depend_on_the_area_asked_for) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    enum : uint32_t { SIDE = 37u, WIDE = 9u };
    terrain_biome_column_t *const all = test_alloc(t, SIDE * SIDE * sizeof(terrain_biome_column_t));
    const terrain_fields_area_t area = {
        .origin_x = -19, .origin_y = 1003, .step_x = 1, .step_y = 1, .count_x = SIDE, .count_y = SIDE};
    terrain_biome_columns_area(terrain.borrowed_biome, nullptr, &area, all);
    terrain_climate_sample_t climate;
    terrain_climate_fallback(BIO_WORLD, area.origin_x, area.origin_y, &climate);
    for (uint32_t iy = 0u; iy < SIDE; iy += 5u) {
        for (uint32_t ix = 0u; ix < SIDE; ix += 3u) {
            const terrain_biome_column_t one = terrain_biome_column_at(
                terrain.borrowed_biome, &climate, area.origin_x + (int32_t)ix, area.origin_y + (int32_t)iy);
            CHECK(memcmp(&one, &all[ix + SIDE * iy], sizeof(one)) == 0);
            CHECK_EQ_U64(terrain_biome_at(terrain.borrowed_biome, &climate, area.origin_x + (int32_t)ix,
                                          area.origin_y + (int32_t)iy, -60),
                         terrain_biome_of_voxel(&one, -60));
        }
    }
    terrain_biome_column_t wide[WIDE * WIDE];
    const terrain_fields_area_t apart = {
        .origin_x = -19, .origin_y = 1003, .step_x = 4, .step_y = 3, .count_x = WIDE, .count_y = WIDE};
    terrain_biome_columns_area(terrain.borrowed_biome, nullptr, &apart, wide);
    for (uint32_t iy = 0u; iy < WIDE; iy++) {
        for (uint32_t ix = 0u; ix < WIDE; ix++) {
            CHECK(memcmp(&wide[ix + WIDE * iy], &all[ix * 4u + SIDE * (iy * 3u)], sizeof(wide[0])) == 0);
        }
    }
    terrain_deinit(&terrain);
}

/* ---- The rules, by hand ------------------------------------------------------------------------------------------- */

TEST(the_bands_come_from_the_seed_and_hold_every_colour) {
    terrain_t one;
    terrain_t again;
    terrain_t other;
    REQUIRE(terrain_init(&one, BIO_WORLD));
    REQUIRE(terrain_init(&again, BIO_WORLD));
    REQUIRE(terrain_init(&other, 42ull));
    CHECK(memcmp(one.borrowed_surface->bands, again.borrowed_surface->bands, sizeof(one.borrowed_surface->bands)) == 0);
    CHECK(memcmp(one.borrowed_surface->bands, other.borrowed_surface->bands, sizeof(one.borrowed_surface->bands)) != 0);
    static const uint16_t COLOURS[] = {
        BLOCK_ID_TERRACOTTA,     BLOCK_ID_ORANGE_TERRACOTTA, BLOCK_ID_YELLOW_TERRACOTTA,    BLOCK_ID_BROWN_TERRACOTTA,
        BLOCK_ID_RED_TERRACOTTA, BLOCK_ID_WHITE_TERRACOTTA,  BLOCK_ID_LIGHT_GRAY_TERRACOTTA};
    uint32_t plain = 0u;
    for (uint32_t c = 0u; c < sizeof(COLOURS) / sizeof(COLOURS[0]); c++) {
        uint32_t seen = 0u;
        for (uint32_t i = 0u; i < TERRAIN_SURFACE_BANDS; i++) {
            seen += one.borrowed_surface->bands[i] == COLOURS[c] ? 1u : 0u;
        }
        CHECK(seen != 0u);
        plain = c == 0u ? seen : plain;
    }
    printf("plain terracotta: %" PRIu32 " layers of %" PRIu32 "\n", plain, (uint32_t)TERRAIN_SURFACE_BANDS);
    terrain_deinit(&other);
    terrain_deinit(&again);
    terrain_deinit(&one);
}

static uint16_t bio_rule(const biome_def_t *def, const uint16_t *bands, int32_t z, uint32_t depth, uint16_t block,
                         bool wet, float32_t noise, float32_t patch, float32_t steep) {
    const terrain_surface_inputs_t inputs = {
        .depth_noise = noise,
        .patch = patch,
        .steepness = steep,
        .z = z,
        .band_z = z,
        .depth = depth,
        .block = block,
        .wet = wet ? 1u : 0u,
    };
    return terrain_surface_block_from(def, bands, &inputs);
}

TEST(the_rules_are_worked_out_by_hand) {
    uint16_t bands[TERRAIN_SURFACE_BANDS];
    for (uint32_t i = 0u; i < TERRAIN_SURFACE_BANDS; i++) {
        bands[i] = (uint16_t)(1000u + i); /* no block's id, so a band shows which layer was read */
    }
    const uint16_t stone = BLOCK_ID_STONE;

    /* Plains: top, then 3 of dirt and up to 3 more by the noise, then stone. */
    const biome_def_t *const plains = &biome_defs[BIOME_ID_PLAINS];
    CHECK_EQ_U64(bio_rule(plains, bands, 40, 0u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(bio_rule(plains, bands, 39, 1u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_DIRT);
    CHECK_EQ_U64(bio_rule(plains, bands, 37, 3u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_DIRT);
    CHECK_EQ_U64(bio_rule(plains, bands, 36, 4u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    /* (noise + 1) / 2 * 4, whole, at most 3: -0.5 gives 1, 0 gives 2, 0.5 gives 3, 1 gives 3. */
    CHECK_EQ_U64(bio_rule(plains, bands, 36, 4u, stone, false, -0.5f32, 0.0f32, 0.0f32), BLOCK_ID_DIRT);
    CHECK_EQ_U64(bio_rule(plains, bands, 35, 5u, stone, false, -0.5f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(plains, bands, 35, 5u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_DIRT);
    CHECK_EQ_U64(bio_rule(plains, bands, 34, 6u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(plains, bands, 34, 6u, stone, false, 0.5f32, 0.0f32, 0.0f32), BLOCK_ID_DIRT);
    CHECK_EQ_U64(bio_rule(plains, bands, 34, 6u, stone, false, 1.0f32, 0.0f32, 0.0f32), BLOCK_ID_DIRT);
    CHECK_EQ_U64(bio_rule(plains, bands, 33, 7u, stone, false, 1.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);

    /* Only stone is covered: hydrology's materials stay. */
    CHECK_EQ_U64(bio_rule(plains, bands, 2, 0u, BLOCK_ID_SAND, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_SAND);
    CHECK_EQ_U64(bio_rule(plains, bands, 2, 1u, BLOCK_ID_GRAVEL, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_GRAVEL);
    CHECK_EQ_U64(bio_rule(plains, bands, 2, 0u, BLOCK_ID_BEDROCK, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_BEDROCK);
    /* Under water, with no bed of its own: what hydrology left, stone too. */
    CHECK_EQ_U64(bio_rule(plains, bands, -5, 0u, stone, true, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(plains, bands, -5, 0u, BLOCK_ID_CLAY, true, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_CLAY);

    /* The mangroves' mud goes over what hydrology laid, 3 deep. */
    const biome_def_t *const mangrove = &biome_defs[BIOME_ID_MANGROVE_SWAMP];
    CHECK_EQ_U64(bio_rule(mangrove, bands, -5, 0u, BLOCK_ID_SAND, true, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_MUD);
    CHECK_EQ_U64(bio_rule(mangrove, bands, -7, 2u, stone, true, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_MUD);
    CHECK_EQ_U64(bio_rule(mangrove, bands, -8, 3u, stone, true, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(mangrove, bands, -8, 3u, BLOCK_ID_SAND, true, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_SAND);

    /* The desert: sand, then sandstone under it. 3 of sand and 4 of sandstone at the noise's lowest. */
    const biome_def_t *const desert = &biome_defs[BIOME_ID_DESERT];
    CHECK_EQ_U64(bio_rule(desert, bands, 30, 0u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_SAND);
    CHECK_EQ_U64(bio_rule(desert, bands, 27, 3u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_SAND);
    CHECK_EQ_U64(bio_rule(desert, bands, 26, 4u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_SANDSTONE);
    CHECK_EQ_U64(bio_rule(desert, bands, 23, 7u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_SANDSTONE);
    CHECK_EQ_U64(bio_rule(desert, bands, 22, 8u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);

    /* Patches: only the top, and only inside their ranges. */
    const biome_def_t *const taiga = &biome_defs[BIOME_ID_OLD_GROWTH_SPRUCE_TAIGA];
    CHECK_EQ_U64(bio_rule(taiga, bands, 40, 0u, stone, false, 0.0f32, -0.5f32, 0.0f32), BLOCK_ID_PODZOL);
    CHECK_EQ_U64(bio_rule(taiga, bands, 40, 0u, stone, false, 0.0f32, 0.1f32, 0.0f32), BLOCK_ID_COARSE_DIRT);
    CHECK_EQ_U64(bio_rule(taiga, bands, 40, 0u, stone, false, 0.0f32, 0.49f32, 0.0f32), BLOCK_ID_COARSE_DIRT);
    CHECK_EQ_U64(bio_rule(taiga, bands, 40, 0u, stone, false, 0.0f32, 0.5f32, 0.0f32), BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(bio_rule(taiga, bands, 40, 0u, stone, false, 0.0f32, 1.0f32, 0.0f32), BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(bio_rule(taiga, bands, 39, 1u, stone, false, 0.0f32, 0.5f32, 0.0f32), BLOCK_ID_DIRT);

    /* Steep ground is bare, all the way through what would have covered it. And high ground has its own top. */
    const biome_def_t *const hills = &biome_defs[BIOME_ID_WINDSWEPT_HILLS];
    CHECK_EQ_U64(bio_rule(hills, bands, 90, 0u, stone, false, 0.0f32, 0.0f32, 0.99f32), BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(bio_rule(hills, bands, 90, 0u, stone, false, 0.0f32, 0.0f32, 1.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(hills, bands, 89, 1u, stone, false, 0.0f32, 0.0f32, 1.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(hills, bands, 399, 0u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(bio_rule(hills, bands, 400, 0u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_SNOW_BLOCK);
    CHECK_EQ_U64(bio_rule(hills, bands, 400, 0u, stone, false, 0.0f32, 0.0f32, 1.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(hills, bands, 399, 1u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_DIRT);

    /* Badlands: red sand and sandstone low down, then bands all the way, from 40, by layer. */
    const biome_def_t *const badlands = &biome_defs[BIOME_ID_BADLANDS];
    CHECK_EQ_U64(bio_rule(badlands, bands, 39, 0u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_RED_SAND);
    CHECK_EQ_U64(bio_rule(badlands, bands, 38, 1u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_RED_SAND);
    CHECK_EQ_U64(bio_rule(badlands, bands, 37, 2u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_RED_SANDSTONE);
    CHECK_EQ_U64(bio_rule(badlands, bands, 34, 5u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(badlands, bands, 40, 0u, stone, false, -1.0f32, 0.0f32, 0.0f32), 1040u);
    CHECK_EQ_U64(bio_rule(badlands, bands, 50, 13u, stone, false, -1.0f32, 0.0f32, 0.0f32), 1050u);
    CHECK_EQ_U64(bio_rule(badlands, bands, 50, 14u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    /* The layer goes round: 300 and 44 are the same one. */
    CHECK_EQ_U64(bio_rule(badlands, bands, 300, 0u, stone, false, -1.0f32, 0.0f32, 0.0f32), 1044u);
    /* Woods on top of the bands, high up. */
    const biome_def_t *const wooded = &biome_defs[BIOME_ID_WOODED_BADLANDS];
    CHECK_EQ_U64(bio_rule(wooded, bands, 149, 0u, stone, false, -1.0f32, 0.0f32, 0.0f32), 1149u);
    CHECK_EQ_U64(bio_rule(wooded, bands, 150, 0u, stone, false, -1.0f32, 0.0f32, 0.0f32), BLOCK_ID_COARSE_DIRT);
    CHECK_EQ_U64(bio_rule(wooded, bands, 150, 0u, stone, false, -1.0f32, 0.5f32, 0.0f32), BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(bio_rule(wooded, bands, 149, 1u, stone, false, -1.0f32, 0.5f32, 0.0f32), 1149u);

    /* No filler at all on a peak: snow, then stone. */
    const biome_def_t *const jagged = &biome_defs[BIOME_ID_JAGGED_PEAKS];
    CHECK_EQ_U64(bio_rule(jagged, bands, 300, 0u, stone, false, 1.0f32, 0.0f32, 0.0f32), BLOCK_ID_SNOW_BLOCK);
    CHECK_EQ_U64(bio_rule(jagged, bands, 299, 1u, stone, false, 1.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);

    /* A warm peak is bare, up to its snow line. */
    const biome_def_t *const stony = &biome_defs[BIOME_ID_STONY_PEAKS];
    CHECK_EQ_U64(bio_rule(stony, bands, 799, 0u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);
    CHECK_EQ_U64(bio_rule(stony, bands, 800, 0u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_SNOW_BLOCK);
    CHECK_EQ_U64(bio_rule(stony, bands, 799, 1u, stone, false, 0.0f32, 0.0f32, 0.0f32), BLOCK_ID_STONE);

    /* And under the ground. */
    const biome_def_t *const lush = &biome_defs[BIOME_ID_LUSH_CAVES];
    CHECK_EQ_U64(terrain_surface_cave_block_from(lush, stone, 0.0f32, true, false), BLOCK_ID_MOSS_BLOCK);
    CHECK_EQ_U64(terrain_surface_cave_block_from(lush, stone, 0.5f32, true, false), BLOCK_ID_CLAY);
    CHECK_EQ_U64(terrain_surface_cave_block_from(lush, stone, 0.0f32, false, true), BLOCK_ID_ROOTED_DIRT);
    CHECK_EQ_U64(terrain_surface_cave_block_from(lush, stone, 0.5f32, false, true), BLOCK_ID_ROOTED_DIRT);
    CHECK_EQ_U64(terrain_surface_cave_block_from(lush, stone, 0.0f32, true, true), BLOCK_ID_MOSS_BLOCK);
    CHECK_EQ_U64(terrain_surface_cave_block_from(lush, stone, 0.0f32, false, false), BLOCK_ID_STONE);
    CHECK_EQ_U64(terrain_surface_cave_block_from(lush, BLOCK_ID_WATER, 0.0f32, true, false), BLOCK_ID_WATER);
}

/* ---- The ground, voxel by voxel ------------------------------------------------------------------------------------ */

static float32_t bio_spread(float32_t raw) {
    float32_t u = (float32_t)((float64_t)raw / 0.42f64);
    u = u < -1.0f32 ? -1.0f32 : (u > 1.0f32 ? 1.0f32 : u);
    return u * (1.5f32 - 0.5f32 * u * u);
}

/* The README's rule, typed out again from its table. */
static uint16_t bio_expected_block(const biome_def_t *def, const uint16_t *bands, int32_t z, uint32_t depth,
                                   uint16_t block, bool wet, float32_t noise, float32_t patch, float32_t steep,
                                   float32_t wander) {
    if (wet) {
        return depth < def->wet_depth ? def->wet_top : block;
    }
    if (block != BLOCK_ID_STONE) {
        return block;
    }
    uint32_t more = (uint32_t)((noise + 1.0f32) * 2.0f32);
    more = more > 3u ? 3u : more;
    const uint32_t filler = def->filler_depth == 0u ? 0u : def->filler_depth + more;
    if (def->steep_from > 0.0f32 && steep >= def->steep_from && depth <= filler) {
        return def->steep;
    }
    const float32_t bent = wander * 6.0f32;
    const int32_t band_z = z + (int32_t)(bent < 0.0f32 ? bent - 0.5f32 : bent + 0.5f32);
    const uint16_t band = bands[(uint32_t)band_z % 256u];
    const bool banded = def->bands_depth != 0u && z >= def->bands_from;
    uint16_t top = def->high != BLOCK_ID_AIR && z >= def->high_from ? def->high : def->top;
    if (depth == 0u) {
        if (banded && !(def->high != BLOCK_ID_AIR && z >= def->high_from)) {
            return band;
        }
        for (uint32_t i = 0u; i < def->patch_count; i++) {
            const biome_patch_t *const row = &biome_patches[def->first_patch + i];
            if (patch >= row->from && (patch < row->to || (patch >= 1.0f32 && row->to >= 1.0f32))) {
                top = row->block;
                break;
            }
        }
        return top;
    }
    if (banded) {
        return depth < def->bands_depth ? band : BLOCK_ID_STONE;
    }
    if (depth <= filler) {
        return def->filler;
    }
    return depth <= filler + def->under_depth ? def->under : BLOCK_ID_STONE;
}

/* A whole chunk laid out the slow way: every column walked from the top of the world, nothing skipped. */
static void bio_expected_chunk(test_context_t *t, const terrain_t *terrain, int32_t cx, int32_t cy, uint16_t *out) {
    const terrain_surface_t *const surface = terrain->borrowed_surface;
    const terrain_density_box_t box = bio_tall_box(cx, cy, BIO_CHUNK);
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, &box, out);

    terrain_biome_column_t *const columns = test_alloc(t, BIO_CHUNK_COLUMNS * sizeof(terrain_biome_column_t));
    const terrain_fields_area_t area = {
        .origin_x = cx, .origin_y = cy, .step_x = 1, .step_y = 1, .count_x = BIO_CHUNK, .count_y = BIO_CHUNK};
    terrain_biome_columns_area(terrain->borrowed_biome, nullptr, &area, columns);

    float32_t depth_noise[BIO_CHUNK_COLUMNS];
    float32_t patch_noise[BIO_CHUNK_COLUMNS];
    float32_t wander_noise[BIO_CHUNK_COLUMNS];
    const terrain_noise_grid_t grid = {
        .origin_x = cx,
        .origin_y = cy,
        .step_x = 1,
        .step_y = 1,
        .step_z = 1,
        .count_x = BIO_CHUNK,
        .count_y = BIO_CHUNK,
        .count_z = 1u,
    };
    terrain_noise_sample_grid(surface->borrowed_depth, &grid, depth_noise);
    terrain_noise_sample_grid(surface->borrowed_patch, &grid, patch_noise);
    terrain_noise_sample_grid(surface->borrowed_wander, &grid, wander_noise);

    /* The steepness, from the lattice's ground. */
    terrain_hydrology_column_t lattice[25];
    const terrain_fields_area_t corners = {
        .origin_x = cx, .origin_y = cy, .step_x = 4, .step_y = 4, .count_x = 5u, .count_y = 5u};
    terrain_hydrology_columns_area(terrain->borrowed_hydrology, &corners, lattice);

    for (uint32_t iy = 0u; iy < BIO_CHUNK; iy++) {
        for (uint32_t ix = 0u; ix < BIO_CHUNK; ix++) {
            const uint32_t i = ix + BIO_CHUNK * iy;
            const uint32_t at = ix / 4u + 5u * (iy / 4u);
            float32_t steep = 0.0f32;
            const float32_t diffs[4] = {
                lattice[at + 1u].ground - lattice[at].ground,
                lattice[at + 6u].ground - lattice[at + 5u].ground,
                lattice[at + 5u].ground - lattice[at].ground,
                lattice[at + 6u].ground - lattice[at + 1u].ground,
            };
            for (uint32_t d = 0u; d < 4u; d++) {
                const float32_t size = diffs[d] < 0.0f32 ? -diffs[d] : diffs[d];
                steep = size > steep ? size : steep;
            }
            steep /= 4.0f32;

            uint32_t depth = 0u;
            bool wet = false;
            for (uint32_t iz = BIO_LAYERS; iz-- > 0u;) {
                uint16_t *const voxel = &out[i + (uint64_t)BIO_CHUNK_COLUMNS * iz];
                if (bio_is_open(*voxel)) {
                    depth = 0u;
                    wet = *voxel == BLOCK_ID_WATER;
                    continue;
                }
                if (depth < 16u) {
                    *voxel = bio_expected_block(
                        &biome_defs[columns[i].biome], surface->bands, MC_WORLD_Z_MIN + (int32_t)iz, depth, *voxel, wet,
                        bio_spread(depth_noise[i]), bio_spread(patch_noise[i]), steep, bio_spread(wander_noise[i]));
                }
                depth++;
            }
        }
    }
}

/* Chunk by chunk where each biome was found, every voxel against the slow way. So each biome's rules meet real ground. */
TEST(the_ground_is_covered_as_the_readme_says_in_every_biome) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    const bio_found_t *const found = bio_found_in_world(t, &terrain);
    uint16_t *const got = test_alloc(t, BIO_CHUNK_VOXELS * sizeof(uint16_t));
    uint16_t *const expected = test_alloc(t, BIO_CHUNK_VOXELS * sizeof(uint16_t));
    uint64_t changed = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        REQUIRE(found->count[b] != 0u);
        const int32_t cx = found->at[b].x & ~15;
        const int32_t cy = found->at[b].y & ~15;
        const terrain_density_box_t box = bio_tall_box(cx, cy, BIO_CHUNK);
        terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, got);
        terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &box, got);
        const uint64_t mark = t->arena_used;
        bio_expected_chunk(t, &terrain, cx, cy, expected);
        t->arena_used = mark;
        if (memcmp(got, expected, BIO_CHUNK_VOXELS * sizeof(uint16_t)) != 0) {
            for (uint32_t i = 0u; i < BIO_CHUNK_VOXELS; i++) {
                if (got[i] != expected[i]) {
                    printf("%s at chunk (%" PRId32 ", %" PRId32 "), voxel %" PRIu32 ": %s, expected %s\n",
                           biome_defs[b].name, cx, cy, i, block_defs[got[i]].name, block_defs[expected[i]].name);
                    break;
                }
            }
            CHECK(false);
        }
        changed += expected[0] == BLOCK_ID_BEDROCK ? 1u : 0u;
    }
    CHECK(changed != 0u);
    terrain_deinit(&terrain);
}

/* What a biome's dry ground actually shows at the top of each column, so a table that was right but never reached would fail. */
TEST(each_biome_shows_its_own_top_in_a_real_world) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    const bio_found_t *const found = bio_found_in_world(t, &terrain);
    uint16_t *const blocks = test_alloc(t, BIO_CHUNK_VOXELS * sizeof(uint16_t));
    terrain_biome_column_t columns[BIO_CHUNK_COLUMNS];
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        const biome_def_t *const def = &biome_defs[b];
        if (def->where != BIOME_WHERE_LAND) {
            continue;
        }
        /* Where it was found may be all lake, so round there too, until some of it is dry. */
        uint32_t seen = 0u;
        for (int32_t step = 0; step < 25 && seen == 0u; step++) {
            const int32_t cx = (found->at[b].x & ~15) + (step % 5 - 2) * (int32_t)BIO_CHUNK;
            const int32_t cy = (found->at[b].y & ~15) + (step / 5 - 2) * (int32_t)BIO_CHUNK;
            const terrain_density_box_t box = bio_tall_box(cx, cy, BIO_CHUNK);
            terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, blocks);
            terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &box, blocks);
            const terrain_fields_area_t area = {
                .origin_x = cx, .origin_y = cy, .step_x = 1, .step_y = 1, .count_x = BIO_CHUNK, .count_y = BIO_CHUNK};
            terrain_biome_columns_area(terrain.borrowed_biome, nullptr, &area, columns);
            for (uint32_t i = 0u; i < BIO_CHUNK_COLUMNS; i++) {
                if (columns[i].biome != b) {
                    continue;
                }
                uint32_t iz = BIO_LAYERS - 1u;
                while (iz > 0u && blocks[i + (uint64_t)BIO_CHUNK_COLUMNS * iz] == BLOCK_ID_AIR) {
                    iz--;
                }
                const uint16_t top = blocks[i + (uint64_t)BIO_CHUNK_COLUMNS * iz];
                if (bio_is_open(top)) {
                    continue; /* a lake */
                }
                bool allowed = top == def->top || top == def->high || top == def->steep;
                for (uint32_t p = 0u; p < def->patch_count; p++) {
                    allowed = allowed || top == biome_patches[def->first_patch + p].block;
                }
                for (uint32_t c = 0u; def->bands_depth != 0u && c < TERRAIN_SURFACE_BANDS; c++) {
                    allowed = allowed || top == terrain.borrowed_surface->bands[c];
                }
                /* A river's banks are hydrology's. */
                allowed = allowed || top == BLOCK_ID_SAND || top == BLOCK_ID_GRAVEL;
                if (!allowed) {
                    printf("%s, at (%" PRId32 ", %" PRId32 "): %s on top\n", def->name, cx, cy, block_defs[top].name);
                }
                CHECK(allowed);
                seen++;
            }
        }
        if (seen == 0u) {
            printf("%s has no dry column near where it was found\n", def->name);
        }
        CHECK(seen != 0u);
    }
    terrain_deinit(&terrain);
}

/* ---- Any split of the work, and any worker ------------------------------------------------------------------------- */

/* Each biome's chunk cut into boxes, in x and y and at heights close to the ground, where what is over a box matters most. */
TEST(any_split_of_the_work_gives_the_same_bytes) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    const bio_found_t *const found = bio_found_in_world(t, &terrain);
    enum : uint32_t { ACROSS = 24u, VOXELS = ACROSS * ACROSS * BIO_LAYERS };
    uint16_t *const whole = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const parts = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const piece = test_alloc(t, VOXELS * sizeof(uint16_t));
    static const uint32_t XS[] = {0u, 3u, 16u, 17u, ACROSS};
    static const uint32_t YS[] = {0u, 9u, ACROSS};
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        const biome_def_t *const def = &biome_defs[b];
        if (def->where == BIOME_WHERE_UNDERGROUND) {
            continue;
        }
        const int32_t x0 = (found->at[b].x & ~15) - 4;
        const int32_t y0 = (found->at[b].y & ~15) - 4;
        const terrain_density_box_t box = bio_tall_box(x0, y0, ACROSS);
        terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, whole);
        terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &box, whole);

        /* Cuts round the ground, one layer apart at first, and a far one under and over. */
        const int32_t ground = found->at[b].z;
        const int32_t cuts[] = {-20,    ground - 40, ground - 4, ground - 3,  ground - 2, ground - 1,
                                ground, ground + 1,  ground + 2, ground + 12, ground + 40};
        const uint32_t cut_count = sizeof(cuts) / sizeof(cuts[0]);
        uint32_t zs[sizeof(cuts) / sizeof(cuts[0]) + 2u];
        uint32_t z_count = 0u;
        zs[z_count] = 0u;
        z_count++;
        for (uint32_t c = 0u; c < cut_count; c++) {
            const int32_t at = cuts[c] - MC_WORLD_Z_MIN;
            if (at > (int32_t)zs[z_count - 1u] && at < (int32_t)BIO_LAYERS) {
                zs[z_count] = (uint32_t)at;
                z_count++;
            }
        }
        zs[z_count] = BIO_LAYERS;
        z_count++;

        memset(parts, 0, VOXELS * sizeof(uint16_t));
        /* Backwards, so nothing can lean on what ran before. */
        for (uint32_t kz = z_count - 1u; kz-- > 0u;) {
            for (uint32_t ky = 0u; ky + 1u < sizeof(YS) / sizeof(YS[0]); ky++) {
                for (uint32_t kx = 0u; kx + 1u < sizeof(XS) / sizeof(XS[0]); kx++) {
                    const terrain_density_box_t part = {
                        .origin_x = x0 + (int32_t)XS[kx],
                        .origin_y = y0 + (int32_t)YS[ky],
                        .origin_z = MC_WORLD_Z_MIN + (int32_t)zs[kz],
                        .count_x = XS[kx + 1u] - XS[kx],
                        .count_y = YS[ky + 1u] - YS[ky],
                        .count_z = zs[kz + 1u] - zs[kz],
                    };
                    terrain_hydrology_fill_box(terrain.borrowed_hydrology, &part, piece);
                    terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &part, piece);
                    for (uint32_t iz = 0u; iz < part.count_z; iz++) {
                        for (uint32_t iy = 0u; iy < part.count_y; iy++) {
                            memcpy(&parts[XS[kx] + ACROSS * (YS[ky] + iy) + (uint64_t)ACROSS * ACROSS * (zs[kz] + iz)],
                                   &piece[part.count_x * iy + (uint64_t)part.count_x * part.count_y * iz],
                                   part.count_x * sizeof(uint16_t));
                        }
                    }
                }
            }
        }
        if (memcmp(whole, parts, VOXELS * sizeof(uint16_t)) != 0) {
            printf("%s: a split differs\n", def->name);
            CHECK(false);
        }
    }
    terrain_deinit(&terrain);
}

/* ---- The chain ---------------------------------------------------------------------------------------------------- */

/* What the stages come to, in the README's order. */
static void bio_chain(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened, uint16_t *blocks) {
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, box, blocks);
    terrain_surface_fill_box(terrain->borrowed_surface, nullptr, box, blocks);
    terrain_cave_mask_box(terrain->borrowed_cave, box, opened);
    const uint64_t voxels = terrain_density_box_voxels(box);
    /* Only what a cave truly cuts takes its fill from the flood. */
    for (uint64_t i = 0u; i < voxels; i++) {
        opened[i] = opened[i] != 0u && !bio_is_open(blocks[i]) && blocks[i] != BLOCK_ID_BEDROCK ? 1u : 0u;
    }
    (void)terrain_cave_carve_box(terrain->borrowed_cave, box, blocks);
    terrain_hydrology_flood_box(terrain->borrowed_hydrology, box, opened, blocks);
    terrain_surface_line_caves_box(terrain->borrowed_surface, nullptr, box, blocks);
    terrain_ore_fill_box(terrain->borrowed_ore, box, blocks);
}

/* An underground biome's caves, voxel by voxel, against the mask they were cut by. */
TEST(the_caves_of_each_underground_biome_are_lined_and_nothing_else_is) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    const bio_found_t *const found = bio_found_in_world(t, &terrain);
    enum : uint32_t { ACROSS = 48u, TALL = 96u, VOXELS = ACROSS * ACROSS * TALL, COLUMNS = ACROSS * ACROSS };
    uint16_t *const before = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const after = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const split = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const piece = test_alloc(t, VOXELS * sizeof(uint16_t));
    terrain_biome_column_t *const columns = test_alloc(t, COLUMNS * sizeof(terrain_biome_column_t));
    uint8_t *const mask = test_alloc(t, (uint64_t)COLUMNS * (TALL + 2u));
    static const uint8_t UNDERGROUND[] = {BIOME_ID_LUSH_CAVES, BIOME_ID_DRIPSTONE_CAVES, BIOME_ID_DEEP_DARK};
    for (uint32_t u = 0u; u < sizeof(UNDERGROUND) / sizeof(UNDERGROUND[0]); u++) {
        const uint8_t id = UNDERGROUND[u];
        const biome_def_t *const def = &biome_defs[id];
        REQUIRE(found->count[id] != 0u);
        const terrain_density_box_t box = {
            .origin_x = found->at[id].x - 24,
            .origin_y = found->at[id].y - 24,
            .origin_z = found->at[id].z - 48,
            .count_x = ACROSS,
            .count_y = ACROSS,
            .count_z = TALL,
        };
        /* As after caves were cut: stone where the mask is empty. The lining reads only that it is stone. */
        for (uint32_t i = 0u; i < VOXELS; i++) {
            before[i] = BLOCK_ID_STONE;
        }
        const terrain_density_box_t around = {
            .origin_x = box.origin_x,
            .origin_y = box.origin_y,
            .origin_z = box.origin_z - 1,
            .count_x = ACROSS,
            .count_y = ACROSS,
            .count_z = TALL + 2u,
        };
        terrain_cave_mask_box(terrain.borrowed_cave, &around, mask);
        for (uint32_t i = 0u; i < VOXELS; i++) {
            if (mask[i + (uint64_t)COLUMNS] != 0u) {
                before[i] = BLOCK_ID_AIR;
            }
        }
        memcpy(after, before, VOXELS * sizeof(uint16_t));
        terrain_surface_line_caves_box(terrain.borrowed_surface, nullptr, &box, after);

        const terrain_fields_area_t area = {.origin_x = box.origin_x,
                                            .origin_y = box.origin_y,
                                            .step_x = 1,
                                            .step_y = 1,
                                            .count_x = ACROSS,
                                            .count_y = ACROSS};
        terrain_biome_columns_area(terrain.borrowed_biome, nullptr, &area, columns);
        float32_t *const patch = test_alloc(t, COLUMNS * sizeof(float32_t));
        const terrain_noise_grid_t grid = {
            .origin_x = box.origin_x,
            .origin_y = box.origin_y,
            .step_x = 1,
            .step_y = 1,
            .step_z = 1,
            .count_x = ACROSS,
            .count_y = ACROSS,
            .count_z = 1u,
        };
        terrain_noise_sample_grid(terrain.borrowed_surface->borrowed_patch, &grid, patch);

        uint64_t floors = 0u;
        uint64_t ceilings = 0u;
        for (uint32_t iz = 0u; iz < TALL; iz++) {
            const int32_t z = box.origin_z + (int32_t)iz;
            for (uint32_t c = 0u; c < COLUMNS; c++) {
                const uint32_t v = c + COLUMNS * iz;
                const bool above = mask[(uint64_t)c + (uint64_t)COLUMNS * (iz + 2u)] != 0u;
                const bool below = mask[(uint64_t)c + (uint64_t)COLUMNS * iz] != 0u;
                const biome_def_t *const here = &biome_defs[terrain_biome_of_voxel(&columns[c], z)];
                uint16_t expected = before[v];
                if (here->where == BIOME_WHERE_UNDERGROUND && before[v] == BLOCK_ID_STONE) {
                    expected = terrain_surface_cave_block_from(here, before[v], bio_spread(patch[c]), above, below);
                }
                if (after[v] != expected) {
                    printf("%s, column %" PRIu32 ", z %" PRId32 ": %s, expected %s\n", def->name, c, z,
                           block_defs[after[v]].name, block_defs[expected].name);
                    CHECK(false);
                    terrain_deinit(&terrain);
                    return;
                }
                if (here == def && before[v] == BLOCK_ID_STONE) {
                    floors += above ? 1u : 0u;
                    ceilings += !above && below ? 1u : 0u;
                }
            }
        }
        /* Any split of the box comes to the same. */
        static const uint32_t XS[] = {0u, 5u, 16u, 21u, ACROSS};
        static const uint32_t ZS[] = {0u, 1u, 15u, 16u, 49u, TALL};
        memcpy(split, before, VOXELS * sizeof(uint16_t));
        for (uint32_t kz = sizeof(ZS) / sizeof(ZS[0]) - 1u; kz-- > 0u;) {
            for (uint32_t kx = 0u; kx + 1u < sizeof(XS) / sizeof(XS[0]); kx++) {
                const terrain_density_box_t part = {
                    .origin_x = box.origin_x + (int32_t)XS[kx],
                    .origin_y = box.origin_y,
                    .origin_z = box.origin_z + (int32_t)ZS[kz],
                    .count_x = XS[kx + 1u] - XS[kx],
                    .count_y = ACROSS,
                    .count_z = ZS[kz + 1u] - ZS[kz],
                };
                for (uint32_t iz = 0u; iz < part.count_z; iz++) {
                    for (uint32_t iy = 0u; iy < ACROSS; iy++) {
                        memcpy(&piece[part.count_x * iy + (uint64_t)part.count_x * ACROSS * iz],
                               &split[XS[kx] + ACROSS * iy + (uint64_t)COLUMNS * (ZS[kz] + iz)],
                               part.count_x * sizeof(uint16_t));
                    }
                }
                terrain_surface_line_caves_box(terrain.borrowed_surface, nullptr, &part, piece);
                for (uint32_t iz = 0u; iz < part.count_z; iz++) {
                    for (uint32_t iy = 0u; iy < ACROSS; iy++) {
                        memcpy(&split[XS[kx] + ACROSS * iy + (uint64_t)COLUMNS * (ZS[kz] + iz)],
                               &piece[part.count_x * iy + (uint64_t)part.count_x * ACROSS * iz],
                               part.count_x * sizeof(uint16_t));
                    }
                }
            }
        }
        CHECK(memcmp(split, after, VOXELS * sizeof(uint16_t)) == 0);
        /* From a mask of the box already asked for, the same blocks, as generating a world takes it. */
        memcpy(split, before, VOXELS * sizeof(uint16_t));
        terrain_surface_line_caves_with_mask(terrain.borrowed_surface, nullptr, &box, mask + (uint64_t)COLUMNS, split);
        CHECK(memcmp(split, after, VOXELS * sizeof(uint16_t)) == 0);
        printf("%s: %" PRIu64 " floors, %" PRIu64 " ceilings\n", def->name, floors, ceilings);
        CHECK(floors != 0u);
        CHECK(ceilings != 0u);
    }
    terrain_deinit(&terrain);
}

TEST(the_chain_of_stages_keeps_what_each_promised) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    const bio_found_t *const found = bio_found_in_world(t, &terrain);
    uint16_t *const plain = test_alloc(t, BIO_CHUNK_VOXELS * sizeof(uint16_t));
    uint16_t *const covered = test_alloc(t, BIO_CHUNK_VOXELS * sizeof(uint16_t));
    uint16_t *const chain = test_alloc(t, BIO_CHUNK_VOXELS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, BIO_CHUNK_VOXELS);
    uint64_t seen_top = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        const int32_t cx = found->at[b].x & ~15;
        const int32_t cy = found->at[b].y & ~15;
        const terrain_density_box_t box = bio_tall_box(cx, cy, BIO_CHUNK);
        terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, plain);
        memcpy(covered, plain, BIO_CHUNK_VOXELS * sizeof(uint16_t));
        terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &box, covered);
        bio_chain(&terrain, &box, opened, chain);
        for (uint32_t i = 0u; i < BIO_CHUNK_VOXELS; i++) {
            const uint16_t was = plain[i];
            /* The surface opened and filled nothing. */
            CHECK(bio_is_open(was) == bio_is_open(covered[i]));
            /* Water from the fill stays where it was; the caves leave it, the flood too. */
            if (was == BLOCK_ID_WATER || was == BLOCK_ID_AIR) {
                CHECK_EQ_U64(chain[i], was);
            }
            /* The bedrock is the bedrock. */
            CHECK((was == BLOCK_ID_BEDROCK) == (chain[i] == BLOCK_ID_BEDROCK));
            /* What the surface laid, where no cave came, the ore left alone. */
            if (opened[i] == 0u && covered[i] != BLOCK_ID_STONE && !bio_is_open(covered[i])) {
                CHECK_EQ_U64(chain[i], covered[i]);
                seen_top++;
            }
        }
    }
    CHECK(seen_top != 0u);
    terrain_deinit(&terrain);
}

typedef struct bio_run_t {
    _Atomic uint32_t arrived;        /* 0: jobs at the barrier; each takes its number from this */
    uint8_t _pad0[4];                /* 4 */
    _Atomic uint64_t worker_mask;    /* 8: who ran a job */
    _Atomic uint32_t barrier_failed; /* 16 */
    uint32_t barrier;                /* 20: jobs that must all be running before any does its work */
    const terrain_t *borrowed_world; /* 24 */
    uint16_t *borrowed_blocks;       /* 32: BIO_CHUNK_VOXELS per chunk, in map order */
    uint8_t *borrowed_opened;        /* 40: the same */
    const uint32_t *borrowed_order;  /* 48: the order chunks are taken in */
    int32_t map_x;                   /* 56 */
    int32_t map_y;                   /* 60 */
} bio_run_t;
static_assert(sizeof(bio_run_t) == 64);

static void bio_job(job_worker_t *worker, void *borrowed_argument) {
    bio_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t ticket = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    const int64_t deadline = time_now_ns() + BIO_WAIT_NS;
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        if (time_now_ns() > deadline) {
            atomic_store_explicit(&run->barrier_failed, 1u, memory_order_relaxed);
            break;
        }
        (void)sched_yield();
    }
    for (uint32_t i = ticket; i < BIO_CHUNKS; i += run->barrier) {
        const uint32_t chunk = run->borrowed_order[i];
        const terrain_density_box_t box =
            bio_tall_box(run->map_x + (int32_t)(chunk % BIO_MAP_SIDE * BIO_CHUNK),
                         run->map_y + (int32_t)(chunk / BIO_MAP_SIDE * BIO_CHUNK), BIO_CHUNK);
        bio_chain(run->borrowed_world, &box, run->borrowed_opened + (uint64_t)chunk * BIO_CHUNK_VOXELS,
                  run->borrowed_blocks + (uint64_t)chunk * BIO_CHUNK_VOXELS);
    }
}

static uint64_t bio_run_all(job_system_t *system, bio_run_t *run, uint32_t workers, const uint32_t *order,
                            uint16_t *blocks) {
    atomic_store_explicit(&run->arrived, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->worker_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->barrier_failed, 0u, memory_order_relaxed);
    run->barrier = workers;
    run->borrowed_blocks = blocks;
    run->borrowed_order = order;
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, bio_job, run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    if (atomic_load_explicit(&run->barrier_failed, memory_order_relaxed) != 0u) {
        return 0u;
    }
    return atomic_load_explicit(&run->worker_mask, memory_order_relaxed);
}

static uint32_t bio_count_bits(uint64_t bits) {
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

/* The gate: the same bytes from 1 worker and 16, whichever order the chunks are taken in, through every stage so far. And a pin,
 * against any change of a number, and against the presets disagreeing. */
TEST(one_and_sixteen_workers_give_the_same_bytes) {
    const uint64_t voxels = (uint64_t)BIO_CHUNKS * BIO_CHUNK_VOXELS;
    const uint64_t bytes = voxels * sizeof(uint16_t);
    uint16_t *const one = test_alloc(t, bytes);
    uint16_t *const many = test_alloc(t, bytes);
    uint16_t *const shuffled = test_alloc(t, bytes);
    uint8_t *const opened = test_alloc(t, voxels * BIO_WORKERS_MANY);
    uint32_t in_order[BIO_CHUNKS];
    uint32_t out_of_order[BIO_CHUNKS];
    for (uint32_t i = 0u; i < BIO_CHUNKS; i++) {
        in_order[i] = i;
        out_of_order[i] = BIO_CHUNKS - 1u - i;
    }

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    const bio_found_t *const found = bio_found_in_world(t, &terrain);
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));

    /* Somewhere with something on it: where the first mountains were found. */
    bio_run_t run = {
        .borrowed_world = &terrain,
        .borrowed_opened = opened,
        .map_x = (found->at[BIOME_ID_STONY_PEAKS].x & ~15) - 48,
        .map_y = (found->at[BIOME_ID_STONY_PEAKS].y & ~15) - 48,
    };
    const int64_t started = time_now_ns();
    const uint64_t one_mask = bio_run_all(&system, &run, 1u, in_order, one);
    const int64_t one_done = time_now_ns();
    const uint64_t many_mask = bio_run_all(&system, &run, BIO_WORKERS_MANY, in_order, many);
    const int64_t many_done = time_now_ns();
    const uint64_t shuffled_mask = bio_run_all(&system, &run, BIO_WORKERS_MANY, out_of_order, shuffled);
    job_deinit(&system);

    CHECK(one_mask != 0u);
    CHECK_EQ_U64(bio_count_bits(many_mask), BIO_WORKERS_MANY);
    CHECK_EQ_U64(bio_count_bits(shuffled_mask), BIO_WORKERS_MANY);
    CHECK(memcmp(one, many, bytes) == 0);
    CHECK(memcmp(one, shuffled, bytes) == 0);
    const uint64_t digest = bio_digest(0xCBF29CE484222325ull, one, bytes);
    const float64_t columns = (float64_t)(BIO_CHUNKS * BIO_CHUNK_COLUMNS);
    const float64_t one_seconds = (float64_t)(one_done - started) / 1.0e9f64;
    const float64_t many_seconds = (float64_t)(many_done - one_done) / 1.0e9f64;
    printf("%" PRIu32
           " chunks through every stage: %.1f ms on 1 worker, %.1f ms on 16, or %.0f and %.0f block columns a second; "
           "digest 0x%016" PRIX64 "\n",
           (uint32_t)BIO_CHUNKS, (libc_double_t)(one_seconds * 1000.0f64), (libc_double_t)(many_seconds * 1000.0f64),
           (libc_double_t)(columns / one_seconds), (libc_double_t)(columns / many_seconds), digest);
    CHECK_EQ_U64(digest, BIO_DIGEST_PIN);
    terrain_deinit(&terrain);
}

/* The pin above is one patch of ground. This covers every biome's choice over a wide map. */
TEST(the_columns_of_a_wide_map_are_pinned) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    enum : uint32_t { SIDE = 256u };
    terrain_biome_column_t *const columns = test_alloc(t, SIDE * SIDE * sizeof(terrain_biome_column_t));
    const terrain_fields_area_t area = {
        .origin_x = -32768, .origin_y = -32768, .step_x = 256, .step_y = 256, .count_x = SIDE, .count_y = SIDE};
    terrain_biome_columns_area(terrain.borrowed_biome, nullptr, &area, columns);
    const uint64_t digest = bio_digest(0xCBF29CE484222325ull, columns, SIDE * SIDE * sizeof(terrain_biome_column_t));
    printf("columns digest 0x%016" PRIX64 "\n", digest);
    CHECK_EQ_U64(digest, BIO_COLUMNS_PIN);
    terrain_deinit(&terrain);
}

TEST(sampling_stays_inside_its_output_and_allocates_nothing) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    enum : uint32_t { SIDE = 21u, TALL = 64u, VOXELS = SIDE * SIDE * TALL };
    uint16_t *const raw = test_alloc(t, (VOXELS + 2u * BIO_GUARD) * sizeof(uint16_t));
    uint16_t *const blocks = raw + BIO_GUARD;
    for (uint32_t i = 0u; i < BIO_GUARD; i++) {
        raw[i] = 0xBEEFu;
        blocks[VOXELS + i] = 0xBEEFu;
    }
    const terrain_density_box_t box = {
        .origin_x = -5, .origin_y = -5, .origin_z = -32, .count_x = SIDE, .count_y = SIDE, .count_z = TALL};
    terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, blocks);
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &box, blocks);
    terrain_surface_line_caves_box(terrain.borrowed_surface, nullptr, &box, blocks);
    terrain_climate_sample_t climate;
    terrain_climate_fallback(BIO_WORLD, 1, 1, &climate);
    (void)terrain_biome_at(terrain.borrowed_biome, &climate, 1, 1, 0);
    /* Empty boxes do nothing at all. */
    const terrain_density_box_t empty = {.origin_x = 0, .count_x = 0u, .count_y = 3u, .count_z = 3u};
    terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &empty, blocks + VOXELS);
    terrain_surface_line_caves_box(terrain.borrowed_surface, nullptr, &empty, blocks + VOXELS);
    const terrain_density_box_t flat = {.origin_x = 0, .count_x = 3u, .count_y = 3u, .count_z = 0u};
    terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &flat, blocks + VOXELS);
    terrain_surface_line_caves_box(terrain.borrowed_surface, nullptr, &flat, blocks + VOXELS);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    for (uint32_t i = 0u; i < BIO_GUARD; i++) {
        CHECK_EQ_U64(raw[i], 0xBEEFu);
        CHECK_EQ_U64(blocks[VOXELS + i], 0xBEEFu);
    }
    terrain_deinit(&terrain);
}

/* A box under the ground, or over all of it, comes out as it went in. */
TEST(far_from_the_ground_nothing_changes) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    enum : uint32_t { SIDE = 32u, TALL = 16u, VOXELS = SIDE * SIDE * TALL };
    uint16_t *const before = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const after = test_alloc(t, VOXELS * sizeof(uint16_t));
    static const int32_t LEVELS[] = {-249, -200, 492, 484};
    for (uint32_t i = 0u; i < sizeof(LEVELS) / sizeof(LEVELS[0]); i++) {
        const terrain_density_box_t box = {
            .origin_x = 64, .origin_y = -128, .origin_z = LEVELS[i], .count_x = SIDE, .count_y = SIDE, .count_z = TALL};
        terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, before);
        memcpy(after, before, VOXELS * sizeof(uint16_t));
        terrain_surface_fill_box(terrain.borrowed_surface, nullptr, &box, after);
        CHECK(memcmp(before, after, VOXELS * sizeof(uint16_t)) == 0);
    }
    terrain_deinit(&terrain);
}

TEST(a_full_bank_is_refused_and_nothing_is_left_behind) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    uint8_t *const memory = test_alloc(t, 1u << 20);
    arena_t arena;
    arena_init(&arena, "surface_test", memory, 1u << 20);
    terrain_noise_bank_t bank;
    terrain_noise_bank_init(&bank, &arena, BIO_WORLD, TERRAIN_SURFACE_NOISE_FIELDS - 1u);
    terrain_surface_t *surface = (terrain_surface_t *)&bank; /* something that is not nullptr */
    CHECK(!terrain_surface_init(&surface, &arena, &bank, BIO_WORLD, terrain.borrowed_biome, terrain.borrowed_hydrology,
                                terrain.borrowed_cave));
    CHECK(surface == nullptr);
    terrain_noise_bank_deinit(&bank);
    arena_deinit(&arena);
    terrain_deinit(&terrain);
}

TEST(terrain_starts_the_biomes_and_surface_and_stops_them) {
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, BIO_WORLD));
    REQUIRE(terrain.borrowed_biome != nullptr);
    REQUIRE(terrain.borrowed_surface != nullptr);
    CHECK(terrain.borrowed_biome->borrowed_fields == terrain.borrowed_fields);
    CHECK(terrain.borrowed_biome->borrowed_hydrology == terrain.borrowed_hydrology);
    CHECK(terrain.borrowed_surface->borrowed_biome == terrain.borrowed_biome);
    CHECK(terrain.borrowed_surface->borrowed_cave == terrain.borrowed_cave);
    CHECK_EQ_U64(terrain.noise_bank.count, TERRAIN_FIELDS_NOISE_FIELDS + TERRAIN_DENSITY_NOISE_FIELDS +
                                               TERRAIN_HYDROLOGY_NOISE_FIELDS + TERRAIN_CAVE_NOISE_FIELDS +
                                               TERRAIN_SURFACE_NOISE_FIELDS);
    /* A column's rules come from 2D fields. */
    CHECK(terrain.borrowed_surface->borrowed_depth->octaves[0].frequency_z == 0.0f64);
    CHECK(terrain.borrowed_surface->borrowed_patch->octaves[0].frequency_z == 0.0f64);
    CHECK(terrain.borrowed_surface->borrowed_wander->octaves[0].frequency_z == 0.0f64);
    terrain_deinit(&terrain);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK(terrain.borrowed_biome == nullptr);
    CHECK(terrain.borrowed_surface == nullptr);
    terrain_deinit(&terrain); /* safe again */
}

/* ---- Plan 006 phase 03: on the real climate -------------------------------------------------------------------------- */

/* Ahmed's seed, the world of ~/Pictures/1.png. */
static const uint64_t BIO_AHMED_SEED = 2198175988018515243ull;

/* A climate of a seed, built over a square round (x, y). */
typedef struct bio_climate_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
    terrain_t *terrain;          /* 24 */
} bio_climate_rig_t;
static_assert(sizeof(bio_climate_rig_t) == 32);

static void *bio_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool bio_climate_start(test_context_t *t, bio_climate_rig_t *rig, uint64_t seed, int32_t x, int32_t y,
                              int32_t half) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = bio_aligned(t, sizeof(job_system_t));
    rig->climate = bio_aligned(t, sizeof(climate_t));
    rig->terrain = bio_aligned(t, sizeof(terrain_t));
    if (!thread_topology_read(rig->topology) || !job_init(rig->job, rig->topology)) {
        return false;
    }
    if (!climate_init(rig->climate) || !terrain_init(rig->terrain, seed) || !climate_open(rig->climate, rig->terrain)) {
        return false;
    }
    climate_prepare_area(rig->climate, rig->job, x - half, y - half, x + half, y + half, nullptr, nullptr);
    return true;
}

static void bio_climate_stop(bio_climate_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    terrain_deinit(rig->terrain);
    job_deinit(rig->job);
}

/* The column's biome as a column job there has it: its own climate sample from the tiles. */
static terrain_biome_column_t bio_column_on_climate(test_context_t *t, const bio_climate_rig_t *rig, int32_t x,
                                                    int32_t y) {
    terrain_climate_sample_t sample;
    CHECK(climate_terrain_sample(rig->climate, rig->terrain->seed, (int64_t)x - 8, (int64_t)y - 8, &sample));
    return terrain_biome_column_at(rig->terrain->borrowed_biome, &sample, x, y);
}

/* A biome of the snow: one whose dry top is snow, or that only Ice and Tundra have. */
static bool bio_is_cold_member(uint8_t biome) {
    const uint32_t cold = (1u << BIOME_FAMILY_ICE) | (1u << BIOME_FAMILY_TUNDRA);
    return biome_defs[biome].top == BLOCK_ID_SNOW_BLOCK || (biome_defs[biome].families & ~cold) == 0u;
}

/* Ahmed's 1.png: x -68, y 149, 0.35 degrees north, 24.1 degrees, on snow_block at about z 443, then snowy_slopes or grove.
 * Which seed that world had is not known here; on Ahmed's default seed that column is sea. So the place itself, and every
 * column of 2 km round the equator's (0, 0), high ground included, is in a warm family and no snow biome. */
TEST(the_screenshot_place_is_not_snow) {
    bio_climate_rig_t rig;
    REQUIRE(bio_climate_start(t, &rig, BIO_AHMED_SEED, 0, 0, 1536));
    const terrain_biome_column_t place = bio_column_on_climate(t, &rig, -68, 149);
    printf("x -68, y 149: %s, family %s, ground %.1f\n", biome_defs[place.biome].name, biome_family_names[place.family],
           (libc_double_t)place.ground);
    uint32_t land = 0u, high = 0u;
    for (int32_t dy = -1024; dy <= 1024; dy += 16) {
        for (int32_t dx = -1024; dx <= 1024; dx += 16) {
            const terrain_biome_column_t column = bio_column_on_climate(t, &rig, dx, dy);
            land += biome_defs[column.biome].where == BIOME_WHERE_LAND ? 1u : 0u;
            high += column.ground >= 300.0f32 ? 1u : 0u;
            CHECK(column.biome != BIOME_ID_SNOWY_SLOPES && column.biome != BIOME_ID_GROVE);
            CHECK(!bio_is_cold_member(column.biome));
            CHECK(column.family != BIOME_FAMILY_ICE && column.family != BIOME_FAMILY_TUNDRA);
        }
    }
    printf("2 km round (0, 0): %u land columns, %u at 300 or more\n", land, high);
    CHECK(land > 0u);
    bio_climate_stop(&rig);
}

/* At 75 degrees north, every land column above the sea is in the Ice or Tundra family. */
TEST(pole_land_is_ice_or_tundra) {
    const int32_t y = (int32_t)(CLIMATE_POLE_Y * 75.0f64 / 90.0f64);
    bio_climate_rig_t rig;
    REQUIRE(bio_climate_start(t, &rig, BIO_AHMED_SEED, 0, y, 2048));
    uint32_t land = 0u, cold = 0u;
    for (int32_t dy = -1024; dy < 1024; dy += 32) {
        for (int32_t dx = -1024; dx < 1024; dx += 32) {
            const terrain_biome_column_t column = bio_column_on_climate(t, &rig, dx, y + dy);
            if (biome_defs[column.biome].where != BIOME_WHERE_LAND || column.ground < (float32_t)MC_WORLD_Z_SEA) {
                continue;
            }
            land++;
            cold += column.family == BIOME_FAMILY_ICE || column.family == BIOME_FAMILY_TUNDRA ? 1u : 0u;
        }
    }
    printf("75 N: %u land columns, %u in Ice or Tundra\n", land, cold);
    CHECK(land > 0u);
    CHECK_EQ_U64(cold, land);
    bio_climate_stop(&rig);
}

TEST_MAIN()
