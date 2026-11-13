/* ctest run_terrain_ore. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "types/block_state/index.h"

#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16); /* the plan's gate says 16 */
static_assert(MC_THREADS_WORKERS <= 64); /* the worker mask below */

enum : uint32_t {
    ORE_LAYERS =
        1501u, /* -250 to 1250, both included: all terrain makes, as plan 003 phase 8 stage 4's ranges use the height */
    ORE_WORKERS_MANY = 16u,
    ORE_CHUNK = 16u,
    ORE_CHUNK_VOXELS = ORE_CHUNK * ORE_CHUNK * ORE_LAYERS,
    ORE_MAP_SIDE = 6u,
    ORE_CHUNKS = ORE_MAP_SIDE * ORE_MAP_SIDE,
    ORE_GUARD = 4u,
};
static const int32_t ORE_MAP_MIN = -3;
static const int64_t ORE_WAIT_NS = 60 * TIME_NS_PER_SECOND;
static const uint64_t ORE_WORLD = 0x1234567890ABCDEFull;

/* Pinned: see README.md. */
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
/* Plan 005 phase 11 added chromium ore to config/ores/, laid last: the land differs only by its veins in rock, as the same
 * file at a rate of 0 gave the pin before. */
/* Plan 005 phase 6 added twelve station blocks. The land is the same, as the digest over the numbers before them showed: the numbers of
 * the blocks after them in the alphabet moved. */
/* Plan 007 phase 04: slope shaping, summit rock, knobs, gentler hills and no foothills moved the ground, and the rock the veins lie in with it. Its old value 0x7E24EC83BDAC1B5B. */
static const uint64_t ORE_DIGEST_PIN = 0xF20FF176125D18AEull;

static uint64_t ore_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static terrain_density_box_t ore_tall_box(int32_t x, int32_t y, uint32_t across) {
    return (terrain_density_box_t){
        .origin_x = x,
        .origin_y = y,
        .origin_z = MC_WORLD_Z_MIN,
        .count_x = across,
        .count_y = across,
        .count_z = ORE_LAYERS,
    };
}

static void ore_fill_with(uint16_t *blocks, uint64_t voxels, uint16_t block) {
    for (uint64_t i = 0u; i < voxels; i++) {
        blocks[i] = block;
    }
}

/* ---- The table, against the README ---------------------------------------------------------------------------------- */

typedef struct ore_expected_row_t {
    const char *name;       /* 0 */
    uint32_t per_chunk_256; /* 8 */
    int16_t z_low;          /* 12 */
    int16_t z_high;         /* 14 */
    int16_t min_height;     /* 16 */
    uint16_t block;         /* 18 */
    uint16_t deep_block;    /* 20 */
    uint8_t size;           /* 22 */
    uint8_t spread;         /* 23 */
} ore_expected_row_t;
static_assert(sizeof(ore_expected_row_t) == 24);

enum : uint8_t { U = TERRAIN_ORE_UNIFORM, T = TERRAIN_ORE_TRIANGLE };
enum : int16_t { ANY = INT16_MIN };

/* The README's table, typed out again. */
static const ore_expected_row_t ORE_EXPECTED[TERRAIN_ORE_ROWS] = {
    {"ore/dirt", 3584u, -123, 189, ANY, BLOCK_ID_DIRT, BLOCK_ID_DIRT, 33u, U},
    {"ore/gravel", 7168u, -250, 500, ANY, BLOCK_ID_GRAVEL, BLOCK_ID_GRAVEL, 33u, U},
    {"ore/granite_low", 1024u, -123, -6, ANY, BLOCK_ID_GRANITE, BLOCK_ID_GRANITE, 64u, U},
    {"ore/granite_high", 85u, 2, 127, ANY, BLOCK_ID_GRANITE, BLOCK_ID_GRANITE, 64u, U},
    {"ore/diorite_low", 1024u, -123, -6, ANY, BLOCK_ID_DIORITE, BLOCK_ID_DIORITE, 64u, U},
    {"ore/diorite_high", 85u, 2, 127, ANY, BLOCK_ID_DIORITE, BLOCK_ID_DIORITE, 64u, U},
    {"ore/andesite_low", 1024u, -123, -6, ANY, BLOCK_ID_ANDESITE, BLOCK_ID_ANDESITE, 64u, U},
    {"ore/andesite_high", 85u, 2, 127, ANY, BLOCK_ID_ANDESITE, BLOCK_ID_ANDESITE, 64u, U},
    {"ore/tuff", 1024u, -250, -123, ANY, BLOCK_ID_TUFF, BLOCK_ID_TUFF, 64u, U},
    {"ore/gravel_peak", 7168u, 501, 1250, ANY, BLOCK_ID_GRAVEL, BLOCK_ID_GRAVEL, 33u, U},
    {"ore/coal_high", 15360u, 142, 500, ANY, BLOCK_ID_COAL_ORE, BLOCK_ID_DEEPSLATE_COAL_ORE, 17u, U},
    {"ore/coal_peak", 32000u, 501, 1250, ANY, BLOCK_ID_COAL_ORE, BLOCK_ID_DEEPSLATE_COAL_ORE, 17u, U},
    {"ore/coal_low", 10240u, -123, 252, ANY, BLOCK_ID_COAL_ORE, BLOCK_ID_DEEPSLATE_COAL_ORE, 17u, T},
    {"ore/copper", 8192u, -185, 96, ANY, BLOCK_ID_COPPER_ORE, BLOCK_ID_DEEPSLATE_COPPER_ORE, 10u, T},
    {"ore/iron_high", 46080u, 33, 500, ANY, BLOCK_ID_IRON_ORE, BLOCK_ID_DEEPSLATE_IRON_ORE, 9u, T},
    {"ore/iron_peak", 15360u, 501, 1250, ANY, BLOCK_ID_IRON_ORE, BLOCK_ID_DEEPSLATE_IRON_ORE, 9u, U},
    {"ore/iron_middle", 5120u, -170, -14, ANY, BLOCK_ID_IRON_ORE, BLOCK_ID_DEEPSLATE_IRON_ORE, 9u, T},
    {"ore/iron_low", 5120u, -250, 18, ANY, BLOCK_ID_IRON_ORE, BLOCK_ID_DEEPSLATE_IRON_ORE, 4u, U},
    {"ore/gold", 2048u, -250, -60, ANY, BLOCK_ID_GOLD_ORE, BLOCK_ID_DEEPSLATE_GOLD_ORE, 9u, T},
    {"ore/gold_deep", 128u, -250, -218, ANY, BLOCK_ID_GOLD_ORE, BLOCK_ID_DEEPSLATE_GOLD_ORE, 9u, U},
    {"ore/lapis_middle", 1024u, -185, -60, ANY, BLOCK_ID_LAPIS_ORE, BLOCK_ID_DEEPSLATE_LAPIS_ORE, 7u, T},
    {"ore/lapis_low", 2048u, -250, 2, ANY, BLOCK_ID_LAPIS_ORE, BLOCK_ID_DEEPSLATE_LAPIS_ORE, 7u, U},
    {"ore/diamond_small", 3584u, -406, -92, ANY, BLOCK_ID_DIAMOND_ORE, BLOCK_ID_DEEPSLATE_DIAMOND_ORE, 4u, T},
    {"ore/diamond_large", 57u, -406, -92, ANY, BLOCK_ID_DIAMOND_ORE, BLOCK_ID_DEEPSLATE_DIAMOND_ORE, 12u, T},
    {"ore/emerald", 4096u, -154, 500, 120, BLOCK_ID_EMERALD_ORE, BLOCK_ID_DEEPSLATE_EMERALD_ORE, 3u, U},
    {"ore/emerald_peak", 4608u, 501, 1250, 120, BLOCK_ID_EMERALD_ORE, BLOCK_ID_DEEPSLATE_EMERALD_ORE, 3u, U},
    /* Plan 005 phase 11: config/ores/chromium, laid last. */
    {"ore/chromium", 1024u, -250, -40, ANY, BLOCK_ID_CHROMIUM_ORE, BLOCK_ID_DEEPSLATE_CHROMIUM_ORE, 6u, U},
};

TEST(the_table_is_the_one_the_readme_gives) {
    const terrain_ore_row_t *const table = terrain_ore_table();
    for (uint32_t r = 0u; r < TERRAIN_ORE_ROWS; r++) {
        const ore_expected_row_t *const e = &ORE_EXPECTED[r];
        CHECK_STR_EQ(table[r].borrowed_name, e->name);
        CHECK_EQ_U64(table[r].per_chunk_256, e->per_chunk_256);
        CHECK_EQ_I64(table[r].z_low, e->z_low);
        CHECK_EQ_I64(table[r].z_high, e->z_high);
        CHECK_EQ_I64(table[r].min_column_height, e->min_height);
        CHECK_EQ_U64(table[r].block, e->block);
        CHECK_EQ_U64(table[r].deep_block, e->deep_block);
        CHECK_EQ_U64(table[r].size, e->size);
        CHECK_EQ_U64(table[r].spread, e->spread);
        CHECK(e->z_low < e->z_high);
        /* A row must be able to reach the world, and its top must be in it. */
        CHECK(e->z_high >= MC_WORLD_Z_MIN && e->z_high <= MC_WORLD_Z_MAX);
    }
    CHECK_EQ_I64(TERRAIN_ORE_DEEPSLATE_BOTTOM, -128);
    CHECK_EQ_I64(TERRAIN_ORE_DEEPSLATE_TOP, -112);
}

TEST(a_vein_reaches_less_than_a_chunk_and_its_ball_is_the_sum_in_the_readme) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    const terrain_ore_t *const ore = terrain.borrowed_ore;
    const terrain_ore_row_t *const table = terrain_ore_table();
    int32_t largest = 0;
    for (uint32_t r = 0u; r < TERRAIN_ORE_ROWS; r++) {
        const float64_t radius = (float64_t)ore->rows[r].radius;
        const float64_t wanted = (float64_t)table[r].size / (4.0f64 / 3.0f64 * 3.14159265358979f64 * 2.0f64);
        CHECK(fabs(radius * radius * radius - wanted) < 1.0e-4f64 * wanted);
        CHECK_EQ_I64(ore->rows[r].reach, (int32_t)(3.0f64 * radius + 0.5f64) + 1);
        CHECK(ore->rows[r].reach < (int32_t)ORE_CHUNK);
        largest = ore->rows[r].reach > largest ? ore->rows[r].reach : largest;
    }
    printf("the furthest any vein can go: %" PRId32 " blocks\n", largest);
    terrain_deinit(&terrain);
}

/* ---- Deepslate ------------------------------------------------------------------------------------------------------ */

/* Stone everywhere, on a band of layers, and only stone changes. */
TEST(stone_turns_to_deepslate_by_depth) {
    enum : uint32_t { ACROSS = 64u, BOTTOM_Z_COUNT = 40u, VOXELS = ACROSS * ACROSS * BOTTOM_Z_COUNT };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = {
        .origin_x = 200,
        .origin_y = -300,
        .origin_z = -140,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = BOTTOM_Z_COUNT,
    };
    ore_fill_with(blocks, VOXELS, BLOCK_ID_STONE);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);
    uint64_t deep_by_z[BOTTOM_Z_COUNT];
    memset(deep_by_z, 0, sizeof(deep_by_z));
    uint64_t rock_by_z[BOTTOM_Z_COUNT];
    memset(rock_by_z, 0, sizeof(rock_by_z));
    for (uint32_t i = 0u; i < VOXELS; i++) {
        const uint32_t iz = i / (ACROSS * ACROSS);
        rock_by_z[iz] += (blocks[i] == BLOCK_ID_STONE || blocks[i] == BLOCK_ID_DEEPSLATE) ? 1u : 0u;
        deep_by_z[iz] += blocks[i] == BLOCK_ID_DEEPSLATE ? 1u : 0u;
    }
    for (uint32_t iz = 0u; iz < BOTTOM_Z_COUNT; iz++) {
        const int32_t z = box.origin_z + (int32_t)iz;
        const float64_t share = rock_by_z[iz] == 0u ? 0.0f64 : (float64_t)deep_by_z[iz] / (float64_t)rock_by_z[iz];
        if (z <= -128) {
            CHECK_EQ_U64(deep_by_z[iz], rock_by_z[iz]);
        } else if (z >= -112) {
            CHECK_EQ_U64(deep_by_z[iz], 0u);
        } else {
            /* (-112 - z) / 16 of them, on 4096 voxels less what veins took: about 1 in 80 either way, by chance. */
            CHECK(fabs(share - (float64_t)(-112 - z) / 16.0f64) < 0.06f64);
        }
    }
    terrain_deinit(&terrain);
}

TEST(only_rock_is_ever_replaced) {
    enum : uint32_t { ACROSS = 48u, TALL = 200u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const before = test_alloc(t, VOXELS * sizeof(uint16_t));
    static const uint16_t solid_stay[] = {BLOCK_ID_AIR, BLOCK_ID_WATER, BLOCK_ID_LAVA, BLOCK_ID_BEDROCK, BLOCK_ID_SAND};
    const terrain_density_box_t box = {
        .origin_x = -3000,
        .origin_y = 4000,
        .origin_z = -240,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    /* Half rock, half what must not be touched. */
    for (uint32_t i = 0u; i < VOXELS; i++) {
        const uint32_t pick = (i * 2654435761u) >> 20;
        blocks[i] = (pick & 1u) != 0u ? (uint16_t)BLOCK_ID_STONE : solid_stay[(pick >> 1u) % 5u];
        before[i] = blocks[i];
    }
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);
    uint64_t changed = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        if (before[i] != BLOCK_ID_STONE) {
            CHECK_EQ_U64(blocks[i], before[i]);
        } else {
            changed += blocks[i] != BLOCK_ID_STONE ? 1u : 0u;
        }
    }
    CHECK(changed > 0u);
    terrain_deinit(&terrain);
}

/* ---- Each ore, by depth ---------------------------------------------------------------------------------------------- */

typedef struct ore_family_t {
    const char *name;    /* 0 */
    uint16_t block;      /* 8 */
    uint16_t deep_block; /* 10 */
    uint8_t _pad0[4];    /* 12 */
} ore_family_t;
static_assert(sizeof(ore_family_t) == 16);

enum : uint32_t { ORE_FAMILIES = 8u };
static const ore_family_t ORE_FAMILY[ORE_FAMILIES] = {
    {"coal", BLOCK_ID_COAL_ORE, BLOCK_ID_DEEPSLATE_COAL_ORE, {0u}},
    {"copper", BLOCK_ID_COPPER_ORE, BLOCK_ID_DEEPSLATE_COPPER_ORE, {0u}},
    {"iron", BLOCK_ID_IRON_ORE, BLOCK_ID_DEEPSLATE_IRON_ORE, {0u}},
    {"gold", BLOCK_ID_GOLD_ORE, BLOCK_ID_DEEPSLATE_GOLD_ORE, {0u}},
    {"lapis", BLOCK_ID_LAPIS_ORE, BLOCK_ID_DEEPSLATE_LAPIS_ORE, {0u}},
    {"diamond", BLOCK_ID_DIAMOND_ORE, BLOCK_ID_DEEPSLATE_DIAMOND_ORE, {0u}},
    {"emerald", BLOCK_ID_EMERALD_ORE, BLOCK_ID_DEEPSLATE_EMERALD_ORE, {0u}},
    {"chromium", BLOCK_ID_CHROMIUM_ORE, BLOCK_ID_DEEPSLATE_CHROMIUM_ORE, {0u}},
};

/* All-stone columns, as a guide to what the rows do on their own, with no land's shape in the way. Over many chunks. */
TEST(each_ore_lies_at_its_depths_in_its_own_form) {
    enum : uint32_t { ACROSS = 256u, VOXELS = ACROSS * ACROSS * ORE_LAYERS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const blocks = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = ore_tall_box(-2048, 4096, ACROSS);
    ore_fill_with(blocks, VOXELS, BLOCK_ID_STONE);
    ore_fill_with(blocks, (uint64_t)ACROSS * ACROSS, BLOCK_ID_BEDROCK);
    /* Everything at the top of the world and below the bottom is out of the columns' reach: this is about the rows. */
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);

    uint64_t counts[ORE_FAMILIES] = {0u};
    uint64_t deep_counts[ORE_FAMILIES] = {0u};
    int32_t lowest[ORE_FAMILIES];
    int32_t highest[ORE_FAMILIES];
    int64_t z_total[ORE_FAMILIES] = {0};
    for (uint32_t f = 0u; f < ORE_FAMILIES; f++) {
        lowest[f] = INT32_MAX;
        highest[f] = INT32_MIN;
    }
    for (uint64_t i = 0u; i < VOXELS; i++) {
        const int32_t z = MC_WORLD_Z_MIN + (int32_t)(i / ((uint64_t)ACROSS * ACROSS));
        for (uint32_t f = 0u; f < ORE_FAMILIES; f++) {
            const bool shallow = blocks[i] == ORE_FAMILY[f].block;
            const bool deep = blocks[i] == ORE_FAMILY[f].deep_block;
            if (!shallow && !deep) {
                continue;
            }
            counts[f] += shallow ? 1u : 0u;
            deep_counts[f] += deep ? 1u : 0u;
            lowest[f] = z < lowest[f] ? z : lowest[f];
            highest[f] = z > highest[f] ? z : highest[f];
            z_total[f] += z;
            /* The deep form never above the band; the plain one never below it, except in rock that was laid over it. */
            if (deep) {
                CHECK(z < TERRAIN_ORE_DEEPSLATE_TOP);
            }
            break;
        }
    }
    const uint64_t chunks = ((uint64_t)ACROSS / ORE_CHUNK) * ((uint64_t)ACROSS / ORE_CHUNK);
    for (uint32_t f = 0u; f < ORE_FAMILIES; f++) {
        const uint64_t all = counts[f] + deep_counts[f];
        printf("%-8s %6.1f a chunk, %5.1f%% deepslate, z from %" PRId32 " to %" PRId32 ", mean %.0f\n",
               ORE_FAMILY[f].name, (libc_double_t)((float64_t)all / (float64_t)chunks),
               (libc_double_t)(all == 0u ? 0.0f64 : (float64_t)deep_counts[f] / (float64_t)all * 100.0f64), lowest[f],
               highest[f], (libc_double_t)(all == 0u ? 0.0f64 : (float64_t)z_total[f] / (float64_t)all));
    }
    /* Each row's own range, plus a vein's reach, with what the table's rows together cover. */
    CHECK(counts[0] + deep_counts[0] > 0u);
    CHECK(lowest[0] >= -123 - 7 && highest[0] <= MC_WORLD_Z_MAX);
    CHECK(lowest[1] >= -185 - 7 && highest[1] <= 96 + 7);
    CHECK(lowest[2] >= MC_WORLD_Z_MIN + 1 && highest[2] <= MC_WORLD_Z_MAX);
    CHECK(lowest[3] >= MC_WORLD_Z_MIN + 1 && highest[3] <= -60 + 7);
    CHECK(lowest[4] >= MC_WORLD_Z_MIN + 1 && highest[4] <= 2 + 7);
    CHECK(lowest[5] >= MC_WORLD_Z_MIN + 1 && highest[5] <= -92 + 7);
    CHECK(lowest[7] >= MC_WORLD_Z_MIN + 1 && highest[7] <= -40 + 3);
    /* What the table is for: which is common, and where. */
    const uint64_t coal = counts[0] + deep_counts[0];
    const uint64_t iron = counts[2] + deep_counts[2];
    const uint64_t gold = counts[3] + deep_counts[3];
    const uint64_t diamond = counts[5] + deep_counts[5];
    CHECK(coal > gold);
    CHECK(iron > gold);
    CHECK(gold > diamond);
    CHECK(diamond > 0u);
    CHECK(deep_counts[5] > counts[5] * 20u); /* a diamond is nearly always in deepslate */
    CHECK(deep_counts[3] > counts[3]);       /* gold is mostly below it */
    CHECK(counts[0] > deep_counts[0]);       /* coal mostly above it */
    terrain_deinit(&terrain);
}

/* Plan 005 phase 11: chromium, from config/ores/chromium, only from -250 to -40 and at its rate: 1024 veins in 256 chunks is 4 a
 * chunk, and none is lost, as the whole range is in the world. Into all-stone columns from the bottom of the world to well above
 * its range, so what limits it is the row alone. */
TEST(chromium_lies_only_from_minus_250_to_minus_40_at_4_veins_a_chunk) {
    enum : uint32_t { ACROSS = 128u, TALL = 351u, LAYER = ACROSS * ACROSS, VOXELS = LAYER * TALL };
    const terrain_ore_row_t *const table = terrain_ore_table();
    uint32_t row = TERRAIN_ORE_ROWS;
    for (uint32_t r = 0u; r < TERRAIN_ORE_ROWS; r++) {
        row = strcmp(table[r].borrowed_name, "ore/chromium") == 0 ? r : row;
    }
    REQUIRE(row == TERRAIN_ORE_ROWS - 1u); /* laid last, so it moves no vein of another row */
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    const terrain_density_box_t box = {.origin_x = 4096,
                                       .origin_y = -2048,
                                       .origin_z = MC_WORLD_Z_MIN,
                                       .count_x = ACROSS,
                                       .count_y = ACROSS,
                                       .count_z = TALL};
    uint64_t veins = 0u;
    uint32_t fewest = UINT32_MAX;
    uint32_t most = 0u;
    for (int32_t cy = box.origin_y >> 4; cy < (box.origin_y + (int32_t)ACROSS) >> 4; cy++) {
        for (int32_t cx = box.origin_x >> 4; cx < (box.origin_x + (int32_t)ACROSS) >> 4; cx++) {
            const uint32_t here = terrain_ore_chunk_veins(terrain.borrowed_ore, row, cx, cy);
            veins += here;
            fewest = here < fewest ? here : fewest;
            most = here > most ? here : most;
        }
    }
    const uint64_t chunks = (uint64_t)(ACROSS / ORE_CHUNK) * (ACROSS / ORE_CHUNK);
    uint16_t *const blocks = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    ore_fill_with(blocks, VOXELS, BLOCK_ID_STONE);
    ore_fill_with(blocks, LAYER, BLOCK_ID_BEDROCK);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);
    uint64_t found = 0u;
    uint64_t deep = 0u;
    int32_t lowest = INT32_MAX;
    int32_t highest = INT32_MIN;
    for (uint64_t i = 0u; i < VOXELS; i++) {
        const uint16_t id = block_state_id(blocks[i]);
        if (id != BLOCK_ID_CHROMIUM_ORE && id != BLOCK_ID_DEEPSLATE_CHROMIUM_ORE) {
            continue;
        }
        const int32_t z = box.origin_z + (int32_t)(i / LAYER);
        found++;
        deep += id == BLOCK_ID_DEEPSLATE_CHROMIUM_ORE ? 1u : 0u;
        lowest = z < lowest ? z : lowest;
        highest = z > highest ? z : highest;
    }
    const float64_t per_chunk = (float64_t)found / (float64_t)chunks;
    printf("chromium: %" PRIu64 " veins in %" PRIu64 " chunks (%" PRIu32 " to %" PRIu32
           " a chunk), %.1f voxels a chunk, "
           "%.0f%% deepslate, z from %" PRId32 " to %" PRId32 "\n",
           veins, chunks, fewest, most, (libc_double_t)per_chunk,
           (libc_double_t)((float64_t)deep / (float64_t)(found == 0u ? 1u : found) * 100.0f64), lowest, highest);
    CHECK_EQ_U64(fewest, 4u);
    CHECK_EQ_U64(most, 4u);
    /* Within a vein's reach, 3 for a size of 6, of the range, and never in the bedrock at the bottom. */
    CHECK(lowest >= MC_WORLD_Z_MIN + 1);
    CHECK(highest <= -40 + terrain.borrowed_ore->rows[row].reach);
    CHECK(highest > -60);
    /* 4 veins of about 6, less what overlaps and what earlier rows' ore holds, which chromium does not replace. */
    CHECK(per_chunk > 0.5f64 * 4.0f64 * 6.0f64);
    CHECK(per_chunk < 1.5f64 * 4.0f64 * 6.0f64);
    CHECK(deep > found / 2u); /* most of its range is below the deepslate band */
    terrain_deinit(&terrain);
}

/* A diamond's triangle peaks at the bottom of the world: more of them the deeper it goes. */
TEST(diamonds_are_more_common_the_deeper_they_are) {
    enum : uint32_t { ACROSS = 256u, TALL = 170u, VOXELS = ACROSS * ACROSS * TALL, BANDS = 3u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const blocks = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = {
        .origin_x = 8000,
        .origin_y = -8000,
        .origin_z = -249,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    uint64_t by_band[BANDS] = {0u, 0u, 0u};
    ore_fill_with(blocks, VOXELS, BLOCK_ID_STONE);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);
    for (uint64_t i = 0u; i < VOXELS; i++) {
        if (blocks[i] == BLOCK_ID_DEEPSLATE_DIAMOND_ORE || blocks[i] == BLOCK_ID_DIAMOND_ORE) {
            by_band[i / ((uint64_t)ACROSS * ACROSS) * BANDS / TALL]++;
        }
    }
    printf("diamond voxels, deepest band first: %" PRIu64 ", %" PRIu64 ", %" PRIu64 "\n", by_band[0], by_band[1],
           by_band[2]);
    CHECK(by_band[0] > by_band[1]);
    CHECK(by_band[1] > by_band[2]);
    terrain_deinit(&terrain);
}

/* Above where the world first stopped, up to its last layer: rock of a mountain nobody has made yet has its ore. Each row's own block, in each of three
 * bands, and no deepslate form so high. */
TEST(rock_above_the_old_top_has_ore_up_to_the_last_layer) {
    enum : uint32_t {
        ACROSS = 64u,
        TALL = 750u,
        VOXELS = ACROSS * ACROSS * TALL,
        BANDS = 3u,
        BAND_LAYERS = TALL / BANDS
    };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const blocks = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = {
        .origin_x = 96, .origin_y = -64, .origin_z = 501, .count_x = ACROSS, .count_y = ACROSS, .count_z = TALL};
    ore_fill_with(blocks, VOXELS, BLOCK_ID_STONE);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);
    uint64_t gravel[BANDS] = {0u}, coal[BANDS] = {0u}, iron[BANDS] = {0u};
    uint64_t deep = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        const uint32_t band = (i / (ACROSS * ACROSS)) / BAND_LAYERS;
        gravel[band] += blocks[i] == BLOCK_ID_GRAVEL ? 1u : 0u;
        coal[band] += blocks[i] == BLOCK_ID_COAL_ORE ? 1u : 0u;
        iron[band] += blocks[i] == BLOCK_ID_IRON_ORE ? 1u : 0u;
        deep += blocks[i] == BLOCK_ID_DEEPSLATE || blocks[i] == BLOCK_ID_DEEPSLATE_COAL_ORE ? 1u : 0u;
    }
    for (uint32_t band = 0u; band < BANDS; band++) {
        printf("above 500, band %" PRIu32 ": gravel %" PRIu64 ", coal %" PRIu64 ", iron %" PRIu64 "\n", band,
               gravel[band], coal[band], iron[band]);
        CHECK(gravel[band] > 1000u);
        CHECK(coal[band] > 1000u);
        CHECK(iron[band] > 500u);
    }
    CHECK_EQ_U64(deep, 0u);
    terrain_deinit(&terrain);
}

/* Rows over everything in a box are left out for it, and that can move nothing: rock up to a height comes out as the same box all of rock does, to that
 * height, and what is air stays air. A box all of air is left all air. */
TEST(leaving_out_rows_over_the_rock_moves_nothing) {
    enum : uint32_t { ACROSS = 32u, TALL = 900u, LAYER = ACROSS * ACROSS, VOXELS = LAYER * TALL, ROCK_LAYERS = 620u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const whole = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    uint16_t *const lower = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = {
        .origin_x = -32, .origin_y = 48, .origin_z = 0, .count_x = ACROSS, .count_y = ACROSS, .count_z = TALL};
    ore_fill_with(whole, VOXELS, BLOCK_ID_STONE);
    ore_fill_with(lower, VOXELS, BLOCK_ID_AIR);
    ore_fill_with(lower, (uint64_t)LAYER * ROCK_LAYERS, BLOCK_ID_STONE);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, whole);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, lower);
    uint64_t wrong = 0u, changed_above_rock = 0u, peak_ore = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        if (i < (uint64_t)LAYER * ROCK_LAYERS) {
            wrong += lower[i] != whole[i] ? 1u : 0u;
            /* z = 501 and up, in that rock: what comes from the rows the upper layers do not ask for. */
            peak_ore += i >= (uint64_t)LAYER * 501u && lower[i] != BLOCK_ID_STONE ? 1u : 0u;
        } else {
            changed_above_rock += lower[i] != BLOCK_ID_AIR ? 1u : 0u;
        }
    }
    printf("rock to z = %" PRIu32 ": %" PRIu64 " voxels of ore above 500\n", (uint32_t)ROCK_LAYERS - 1u, peak_ore);
    CHECK_EQ_U64(wrong, 0u);
    CHECK_EQ_U64(changed_above_rock, 0u);
    CHECK(peak_ore > 1000u);
    ore_fill_with(lower, VOXELS, BLOCK_ID_AIR);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, lower);
    uint64_t in_air = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        in_air += lower[i] != BLOCK_ID_AIR ? 1u : 0u;
    }
    CHECK_EQ_U64(in_air, 0u);
    terrain_deinit(&terrain);
}

/* Veins come to about the size the table asks for. Each is laid alone, into a world of stone, for each kind of row. */
TEST(a_vein_comes_to_about_the_size_the_table_asks_for) {
    enum : uint32_t { ACROSS = 256u, TALL = 80u, VOXELS = ACROSS * ACROSS * TALL };
    const terrain_ore_row_t *const table = terrain_ore_table();
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const blocks = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    /* Well inside the band where coal_low is near its peak and nothing else makes coal. */
    const terrain_density_box_t box = {
        .origin_x = -500,
        .origin_y = 777,
        .origin_z = 40,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    ore_fill_with(blocks, VOXELS, BLOCK_ID_STONE);
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);
    uint64_t coal = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        coal += blocks[i] == BLOCK_ID_COAL_ORE ? 1u : 0u;
    }
    /* How many veins started in that volume, on average, from the triangle: 40 a chunk over 376 layers, peak at the middle. */
    const uint32_t row = 12u;
    CHECK_STR_EQ(table[row].borrowed_name, "ore/coal_low");
    const float64_t chunks = (float64_t)ACROSS * (float64_t)ACROSS / 256.0f64;
    const float64_t range = (float64_t)((int32_t)table[row].z_high - (int32_t)table[row].z_low);
    const float64_t middle = 0.5f64 * ((float64_t)table[row].z_low + (float64_t)table[row].z_high);
    float64_t share = 0.0f64;
    for (uint32_t iz = 0u; iz < TALL; iz++) {
        const float64_t z = (float64_t)(box.origin_z + (int32_t)iz);
        share += (1.0f64 - fabs(z - middle) / (range * 0.5f64)) / (range * 0.5f64);
    }
    const float64_t veins = chunks * 40.0f64 * share;
    const float64_t per_vein = (float64_t)coal / veins;
    printf("coal: %" PRIu64 " voxels from about %.0f veins: %.1f a vein, against a size of %" PRIu32 "\n", coal,
           (libc_double_t)veins, (libc_double_t)per_vein, (uint32_t)table[row].size);
    /* coal_high is out of this band, and later rows take a little of what it laid. */
    CHECK(per_vein > 0.5f64 * (float64_t)table[row].size);
    CHECK(per_vein < 1.5f64 * (float64_t)table[row].size);
    terrain_deinit(&terrain);
}

/* Emerald asks the column under it, and nothing else in the table does. */
TEST(emeralds_only_lie_under_high_ground) {
    enum : uint32_t { SEARCH_SIDE = 256u, SEARCH_COLUMNS = SEARCH_SIDE * SEARCH_SIDE, ACROSS = 16u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t *const heights = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    float32_t *const softness = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    uint16_t *const blocks = test_alloc(t, (uint64_t)ACROSS * ACROSS * ORE_LAYERS * sizeof(uint16_t));
    const terrain_fields_area_t search = {
        .origin_x = -32768 + 8,
        .origin_y = -32768 + 8,
        .step_x = 256,
        .step_y = 256,
        .count_x = SEARCH_SIDE,
        .count_y = SEARCH_SIDE,
    };
    terrain_density_profile_area(density, &search, heights, softness);
    uint32_t chunks_high = 0u;
    uint32_t chunks_low = 0u;
    uint64_t emeralds_high = 0u;
    for (uint32_t i = 0u; i < SEARCH_COLUMNS && (chunks_high < 8u || chunks_low < 64u); i++) {
        const bool high = heights[i] >= 160.0f32;
        /* Far enough from 120 that the neighbours' veins, which are not this column's to allow, say the same. */
        const bool low = heights[i] < 60.0f32;
        if ((!high || chunks_high >= 8u) && (!low || chunks_low >= 64u)) {
            continue;
        }
        const terrain_density_box_t box =
            ore_tall_box(search.origin_x - 8 + (int32_t)(i % SEARCH_SIDE) * search.step_x,
                         search.origin_y - 8 + (int32_t)(i / SEARCH_SIDE) * search.step_y, ACROSS);
        /* Only a vein that starts in this chunk may be asked about, so only count its middle's voxels, away from the edge. */
        const uint64_t voxels = terrain_density_box_voxels(&box);
        ore_fill_with(blocks, voxels, BLOCK_ID_STONE);
        terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks);
        uint64_t found = 0u;
        for (uint64_t v = 0u; v < voxels; v++) {
            const uint32_t ix = (uint32_t)(v % ACROSS);
            const uint32_t iy = (uint32_t)(v / ACROSS % ACROSS);
            if (ix < 4u || ix >= ACROSS - 4u || iy < 4u || iy >= ACROSS - 4u) {
                continue; /* a neighbour's vein can come this far */
            }
            found += (blocks[v] == BLOCK_ID_EMERALD_ORE || blocks[v] == BLOCK_ID_DEEPSLATE_EMERALD_ORE) ? 1u : 0u;
        }
        if (high) {
            chunks_high++;
            emeralds_high += found;
        } else {
            chunks_low++;
            CHECK_EQ_U64(found, 0u);
        }
    }
    printf("%" PRIu32 " high chunks hold %" PRIu64 " emeralds near their middle; %" PRIu32 " low chunks hold none\n",
           chunks_high, emeralds_high, chunks_low);
    REQUIRE(chunks_high == 8u);
    REQUIRE(chunks_low == 64u);
    CHECK(emeralds_high > 0u);
    terrain_deinit(&terrain);
}

/* ---- Each vein laid as the README says ------------------------------------------------------------------------------- */

static uint16_t ore_expected_replace(uint16_t block, uint16_t deep_block, uint16_t host) {
    if (block == deep_block) {
        return (host == BLOCK_ID_STONE || host == BLOCK_ID_DEEPSLATE) ? block : host;
    }
    if (host == BLOCK_ID_STONE || host == BLOCK_ID_GRANITE || host == BLOCK_ID_DIORITE || host == BLOCK_ID_ANDESITE) {
        return block;
    }
    if (host == BLOCK_ID_DEEPSLATE || host == BLOCK_ID_TUFF) {
        return deep_block;
    }
    return host;
}

/* The README's steps, written again in one go and box-free: every voxel a row's chunk can reach, in a small world. */
static void ore_expected_chunk(const ore_expected_row_t *row, uint64_t seed, int32_t chunk_x, int32_t chunk_y,
                               const terrain_density_box_t *box, uint16_t *blocks) {
    const uint64_t row_seed = terrain_rng_derive(seed, terrain_rng_salt(row->name));
    terrain_rng_t stream = terrain_rng_start(terrain_rng_hash2(row_seed, chunk_x, chunk_y));
    uint32_t veins = row->per_chunk_256 / 256u;
    if (terrain_rng_next_below(&stream, 256u) < row->per_chunk_256 % 256u) {
        veins++;
    }
    const uint32_t range = (uint32_t)((int32_t)row->z_high - (int32_t)row->z_low);
    const float64_t wanted = (float64_t)row->size / (4.18879020478639f64 * 2.0f64);
    /* The radius, by halving, as the README says, up to the float32_t it is kept in. */
    float64_t low = 0.0f64;
    float64_t high = wanted + 1.0f64;
    for (uint32_t i = 0u; i < 64u; i++) {
        const float64_t m = 0.5f64 * (low + high);
        if (m * m * m < wanted) {
            low = m;
        } else {
            high = m;
        }
    }
    const float32_t radius = (float32_t)high;
    for (uint32_t v = 0u; v < veins; v++) {
        const uint32_t where = terrain_rng_next_u32(&stream);
        int32_t z;
        if (row->spread == TERRAIN_ORE_TRIANGLE) {
            const uint32_t a = terrain_rng_next_below(&stream, range / 2u + 1u);
            const uint32_t b = terrain_rng_next_below(&stream, range - range / 2u + 1u);
            z = (int32_t)row->z_low + (int32_t)(a + b);
        } else {
            z = (int32_t)row->z_low + (int32_t)terrain_rng_next_below(&stream, range + 1u);
        }
        const uint64_t vein_seed = terrain_rng_next_u64(&stream);
        if (z < MC_WORLD_Z_MIN || z > MC_WORLD_Z_MAX) {
            continue;
        }
        const int32_t start_x = chunk_x * 16 + (int32_t)(where & 15u);
        const int32_t start_y = chunk_y * 16 + (int32_t)((where >> 4u) & 15u);
        terrain_rng_t vein = terrain_rng_start(vein_seed);
        float32_t middle[3];
        for (uint32_t ball = 0u; ball < 3u; ball++) {
            for (uint32_t axis = 0u; axis < 3u; axis++) {
                const float32_t draw = terrain_rng_next_f32(&vein);
                middle[axis] = ball == 0u ? draw - 0.5f32 : middle[axis] + (draw * 2.0f32 - 1.0f32) * radius;
            }
            /* By brute force over the box: as far from the code's loops as a test can be. */
            for (uint32_t iz = 0u; iz < box->count_z; iz++) {
                const float32_t dz = (float32_t)(box->origin_z + (int32_t)iz - z) - middle[2];
                for (uint32_t iy = 0u; iy < box->count_y; iy++) {
                    const float32_t dy = (float32_t)(box->origin_y + (int32_t)iy - start_y) - middle[1];
                    if (dz * dz + dy * dy > radius * radius) {
                        continue;
                    }
                    for (uint32_t ix = 0u; ix < box->count_x; ix++) {
                        const float32_t dx = (float32_t)(box->origin_x + (int32_t)ix - start_x) - middle[0];
                        if (dz * dz + dy * dy + dx * dx <= radius * radius) {
                            uint16_t *const voxel = &blocks[ix + box->count_x * (iy + box->count_y * iz)];
                            *voxel = ore_expected_replace(row->block, row->deep_block, *voxel);
                        }
                    }
                }
            }
        }
    }
}

TEST(each_vein_is_laid_as_the_readme_says) {
    enum : uint32_t { ACROSS = 40u, TALL = 1501u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    const terrain_ore_t *const ore = terrain.borrowed_ore;
    const terrain_density_box_t box = ore_tall_box(-27, 38, ACROSS);
    uint16_t *const actual = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const expected = test_alloc(t, VOXELS * sizeof(uint16_t));
    ore_fill_with(actual, VOXELS, BLOCK_ID_STONE);
    ore_fill_with(expected, VOXELS, BLOCK_ID_STONE);
    /* Some not rock, so that "replaces" has something to refuse. */
    for (uint32_t i = 0u; i < VOXELS; i += 7u) {
        actual[i] = BLOCK_ID_AIR;
        expected[i] = BLOCK_ID_AIR;
    }
    terrain_ore_fill_box(ore, &box, actual);

    /* Deepslate, by the README's sum. */
    const uint64_t strata = terrain_rng_derive(ORE_WORLD, terrain_rng_salt("ore/strata"));
    for (uint32_t i = 0u; i < VOXELS; i++) {
        const int32_t z = box.origin_z + (int32_t)(i / (ACROSS * ACROSS));
        if (expected[i] != BLOCK_ID_STONE || z >= -112) {
            continue;
        }
        const int32_t x = box.origin_x + (int32_t)(i % ACROSS);
        const int32_t y = box.origin_y + (int32_t)(i / ACROSS % ACROSS);
        const bool deep = z <= -128 || terrain_rng_hash3(strata, x, y, z) % 16u < (uint64_t)(-112 - z);
        expected[i] = deep ? (uint16_t)BLOCK_ID_DEEPSLATE : expected[i];
    }

    /* Every row, every chunk that could reach it, in the README's order. Emerald is left out of the sum and of what is compared,
     * as a chunk's mountain is the density's to say. */
    uint64_t veins_voxels = 0u;
    for (uint32_t r = 0u; r < TERRAIN_ORE_ROWS; r++) {
        if (ORE_EXPECTED[r].min_height != ANY) {
            continue;
        }
        for (int32_t chunk_y = (box.origin_y - 16) >> 4; chunk_y <= (box.origin_y + (int32_t)ACROSS + 16) >> 4;
             chunk_y++) {
            for (int32_t chunk_x = (box.origin_x - 16) >> 4; chunk_x <= (box.origin_x + (int32_t)ACROSS + 16) >> 4;
                 chunk_x++) {
                ore_expected_chunk(&ORE_EXPECTED[r], ORE_WORLD, chunk_x, chunk_y, &box, expected);
            }
        }
    }
    uint64_t compared = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        const bool emerald = actual[i] == BLOCK_ID_EMERALD_ORE || actual[i] == BLOCK_ID_DEEPSLATE_EMERALD_ORE;
        if (emerald) {
            continue;
        }
        CHECK_EQ_U64(actual[i], expected[i]);
        compared++;
        veins_voxels +=
            (expected[i] != BLOCK_ID_STONE && expected[i] != BLOCK_ID_DEEPSLATE && expected[i] != BLOCK_ID_AIR) ? 1u
                                                                                                                : 0u;
    }
    printf("%" PRIu64 " voxels compared, %" PRIu64 " of them in veins\n", compared, veins_voxels);
    CHECK(veins_voxels > 1000u);
    terrain_deinit(&terrain);
}

/* A vein does not stop at a chunk's edge: there is one cut by no boundary, in any chunk, when the chunks are made apart. */
TEST(veins_cross_chunks_with_no_seam) {
    enum : uint32_t { CHUNKS_ACROSS = 4u, ACROSS = CHUNKS_ACROSS * ORE_CHUNK, VOXELS = ACROSS * ACROSS * ORE_LAYERS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    const terrain_ore_t *const ore = terrain.borrowed_ore;
    uint16_t *const whole = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    uint16_t *const parts = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    uint16_t *const one = test_alloc(t, (uint64_t)ORE_CHUNK_VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = ore_tall_box(-32, -32, ACROSS);
    ore_fill_with(whole, VOXELS, BLOCK_ID_STONE);
    terrain_ore_fill_box(ore, &box, whole);
    /* One chunk at a time, last first. */
    for (uint32_t c = CHUNKS_ACROSS * CHUNKS_ACROSS; c-- > 0u;) {
        const uint32_t cx = c % CHUNKS_ACROSS;
        const uint32_t cy = c / CHUNKS_ACROSS;
        const terrain_density_box_t chunk =
            ore_tall_box(box.origin_x + (int32_t)(cx * ORE_CHUNK), box.origin_y + (int32_t)(cy * ORE_CHUNK), ORE_CHUNK);
        ore_fill_with(one, ORE_CHUNK_VOXELS, BLOCK_ID_STONE);
        terrain_ore_fill_box(ore, &chunk, one);
        for (uint32_t iz = 0u; iz < ORE_LAYERS; iz++) {
            for (uint32_t iy = 0u; iy < ORE_CHUNK; iy++) {
                for (uint32_t ix = 0u; ix < ORE_CHUNK; ix++) {
                    const uint64_t to = ((uint64_t)iz * ACROSS + cy * ORE_CHUNK + iy) * ACROSS + cx * ORE_CHUNK + ix;
                    parts[to] = one[ix + ORE_CHUNK * (iy + ORE_CHUNK * iz)];
                }
            }
        }
    }
    CHECK_MEM_EQ(whole, parts, (uint64_t)VOXELS * sizeof(uint16_t));
    /* And a seam was really there to cross: an ore on one side of a chunk's edge, the same ore on the other. */
    uint64_t across_edges = 0u;
    for (uint32_t iz = 0u; iz < ORE_LAYERS; iz++) {
        for (uint32_t iy = 0u; iy < ACROSS; iy++) {
            for (uint32_t ix = ORE_CHUNK - 1u; ix + 1u < ACROSS; ix += ORE_CHUNK) {
                const uint64_t at = ((uint64_t)iz * ACROSS + iy) * ACROSS + ix;
                const uint16_t block = whole[at];
                if (block != BLOCK_ID_STONE && block != BLOCK_ID_DEEPSLATE && block == whole[at + 1u]) {
                    across_edges++;
                }
            }
        }
    }
    printf("%" PRIu64 " voxel pairs of one vein's kind across a chunk's edge\n", across_edges);
    CHECK(across_edges > 0u);
    terrain_deinit(&terrain);
}

/* The whole order the world will use: density, caves, ore. Ore shows in a cave's wall, and the cave stays open. */
TEST(density_then_caves_then_ore_keeps_caves_open_and_ore_in_their_walls) {
    enum : uint32_t { ACROSS = 96u, VOXELS = ACROSS * ACROSS * ORE_LAYERS };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    const terrain_density_box_t box = ore_tall_box(1800, -900, ACROSS);
    uint16_t *const carved = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    uint16_t *const filled = test_alloc(t, (uint64_t)VOXELS * sizeof(uint16_t));
    terrain_density_fill_box(terrain.borrowed_density, &box, carved);
    const uint64_t cut = terrain_cave_carve_box(terrain.borrowed_cave, &box, carved);
    memcpy(filled, carved, (uint64_t)VOXELS * sizeof(uint16_t));
    terrain_ore_fill_box(terrain.borrowed_ore, &box, filled);
    uint64_t ore_in_walls = 0u;
    uint64_t ore_total = 0u;
    for (uint32_t iz = 1u; iz + 1u < ORE_LAYERS; iz++) {
        for (uint32_t iy = 1u; iy + 1u < ACROSS; iy++) {
            for (uint32_t ix = 1u; ix + 1u < ACROSS; ix++) {
                const uint64_t at = ((uint64_t)iz * ACROSS + iy) * ACROSS + ix;
                const uint16_t was = carved[at];
                const uint16_t now = filled[at];
                if (was != BLOCK_ID_STONE) {
                    CHECK_EQ_U64(now, was);
                    continue;
                }
                bool is_ore = false;
                for (uint32_t f = 0u; f < ORE_FAMILIES; f++) {
                    is_ore = is_ore || now == ORE_FAMILY[f].block || now == ORE_FAMILY[f].deep_block;
                }
                if (!is_ore) {
                    continue;
                }
                ore_total++;
                ore_in_walls += (carved[at - 1u] == BLOCK_ID_AIR || carved[at + 1u] == BLOCK_ID_AIR ||
                                 carved[at - ACROSS] == BLOCK_ID_AIR || carved[at + ACROSS] == BLOCK_ID_AIR)
                                    ? 1u
                                    : 0u;
            }
        }
    }
    printf("%" PRIu64 " voxels cut; %" PRIu64 " voxels of ore, %" PRIu64
           " of them in a cave's wall or open to the air\n",
           cut, ore_total, ore_in_walls);
    /* What real ground holds, which is what the table comes to in play. */
    const float64_t chunks = (float64_t)ACROSS * (float64_t)ACROSS / 256.0f64;
    for (uint32_t f = 0u; f < ORE_FAMILIES; f++) {
        uint64_t count = 0u;
        for (uint32_t i = 0u; i < VOXELS; i++) {
            count += (filled[i] == ORE_FAMILY[f].block || filled[i] == ORE_FAMILY[f].deep_block) ? 1u : 0u;
        }
        printf("%-8s %.1f a chunk here\n", ORE_FAMILY[f].name, (libc_double_t)((float64_t)count / chunks));
    }
    CHECK(cut > 0u);
    CHECK(ore_total > 0u);
    CHECK(ore_in_walls > 0u);
    terrain_deinit(&terrain);
}

/* ---- Any split of the work, and any worker ------------------------------------------------------------------------- */

TEST(any_split_of_the_work_gives_the_same_bytes) {
    enum : uint32_t { ACROSS = 70u, TALL = 120u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    const terrain_ore_t *const ore = terrain.borrowed_ore;
    const terrain_density_box_t whole = {
        .origin_x = -35,
        .origin_y = 3,
        .origin_z = -181,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const parts = test_alloc(t, VOXELS * sizeof(uint16_t));
    ore_fill_with(blocks, VOXELS, BLOCK_ID_STONE);
    terrain_ore_fill_box(ore, &whole, blocks);
    const uint32_t lengths[] = {1u, 3u, 7u, 16u, 33u};
    for (uint32_t which = 0u; which < sizeof(lengths) / sizeof(lengths[0]); which++) {
        ore_fill_with(parts, VOXELS, BLOCK_ID_STONE);
        for (uint32_t z_end = TALL; z_end > 0u;) {
            const uint32_t length = z_end < lengths[which] ? z_end : lengths[which];
            const uint32_t iz = z_end - length;
            terrain_density_box_t slice = whole;
            slice.origin_z = whole.origin_z + (int32_t)iz;
            slice.count_z = length;
            terrain_ore_fill_box(ore, &slice, parts + (uint64_t)iz * ACROSS * ACROSS);
            z_end = iz;
        }
        CHECK_MEM_EQ(blocks, parts, VOXELS * sizeof(uint16_t));

        ore_fill_with(parts, VOXELS, BLOCK_ID_STONE);
        for (uint32_t ix = 0u; ix < ACROSS; ix += lengths[which]) {
            const uint32_t length = ACROSS - ix < lengths[which] ? ACROSS - ix : lengths[which];
            terrain_density_box_t slice = whole;
            slice.origin_x = whole.origin_x + (int32_t)ix;
            slice.count_x = length;
            uint16_t *const piece = test_alloc(t, (uint64_t)length * ACROSS * TALL * sizeof(uint16_t));
            ore_fill_with(piece, (uint64_t)length * ACROSS * TALL, BLOCK_ID_STONE);
            terrain_ore_fill_box(ore, &slice, piece);
            for (uint32_t iz = 0u; iz < TALL; iz++) {
                for (uint32_t iy = 0u; iy < ACROSS; iy++) {
                    const uint64_t from = ((uint64_t)iz * ACROSS + iy) * length;
                    const uint64_t to = ((uint64_t)iz * ACROSS + iy) * ACROSS + ix;
                    memcpy(parts + to, piece + from, length * sizeof(uint16_t));
                }
            }
        }
        CHECK_MEM_EQ(blocks, parts, VOXELS * sizeof(uint16_t));
    }
    /* Nothing asked for: no crash. */
    const terrain_density_box_t nothing = {.origin_x = 5, .origin_y = 5, .origin_z = 5, .count_x = 0u, .count_y = 3u};
    terrain_ore_fill_box(ore, &nothing, parts);
    /* And at the very ends of the coordinates, a box past them. Nothing wraps. */
    const terrain_density_box_t corner = {
        .origin_x = INT32_MAX - 1,
        .origin_y = INT32_MIN,
        .origin_z = 40,
        .count_x = 2u,
        .count_y = 2u,
        .count_z = 4u,
    };
    uint16_t small[16];
    ore_fill_with(small, 16u, BLOCK_ID_STONE);
    terrain_ore_fill_box(ore, &corner, small);
    terrain_deinit(&terrain);
}

/* One run over all the chunks: as tests/run/terrain/density/ does it, so that every worker of the run really makes some. */
typedef struct ore_run_t {
    _Atomic uint32_t arrived;          /* 0 */
    uint8_t _pad0[4];                  /* 4 */
    _Atomic uint64_t worker_mask;      /* 8 */
    _Atomic uint32_t barrier_failed;   /* 16 */
    uint32_t barrier;                  /* 20 */
    const terrain_t *borrowed_terrain; /* 24 */
    uint16_t *borrowed_blocks;         /* 32 */
    const uint32_t *borrowed_order;    /* 40 */
} ore_run_t;
static_assert(sizeof(ore_run_t) == 48);

static void ore_job(job_worker_t *worker, void *borrowed_argument) {
    ore_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t ticket = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    const int64_t deadline = time_now_ns() + ORE_WAIT_NS;
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        if (time_now_ns() > deadline) {
            atomic_store_explicit(&run->barrier_failed, 1u, memory_order_relaxed);
            break;
        }
        (void)sched_yield();
    }
    for (uint32_t i = ticket; i < ORE_CHUNKS; i += run->barrier) {
        const uint32_t chunk = run->borrowed_order[i];
        const terrain_density_box_t box =
            ore_tall_box((ORE_MAP_MIN + (int32_t)(chunk % ORE_MAP_SIDE)) * (int32_t)ORE_CHUNK,
                         (ORE_MAP_MIN + (int32_t)(chunk / ORE_MAP_SIDE)) * (int32_t)ORE_CHUNK, ORE_CHUNK);
        uint16_t *const blocks = run->borrowed_blocks + (uint64_t)chunk * ORE_CHUNK_VOXELS;
        /* The whole order, on a worker: what a chunk's job will be. */
        terrain_density_fill_box(run->borrowed_terrain->borrowed_density, &box, blocks);
        (void)terrain_cave_carve_box(run->borrowed_terrain->borrowed_cave, &box, blocks);
        terrain_ore_fill_box(run->borrowed_terrain->borrowed_ore, &box, blocks);
    }
}

static uint64_t ore_run_all(job_system_t *system, const terrain_t *terrain, uint32_t workers, const uint32_t *order,
                            uint16_t *blocks) {
    ore_run_t run = {
        .barrier = workers,
        .borrowed_terrain = terrain,
        .borrowed_blocks = blocks,
        .borrowed_order = order,
    };
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, ore_job, &run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    if (atomic_load_explicit(&run.barrier_failed, memory_order_relaxed) != 0u) {
        return 0u;
    }
    return atomic_load_explicit(&run.worker_mask, memory_order_relaxed);
}

static uint32_t ore_count_bits(uint64_t bits) {
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

/* The gate: the same bytes from 1 worker and 16, whichever order the chunks are taken in, for ground, caves and ore together.
 * And a pin, against any change of the table or a vein, and against the presets disagreeing. */
TEST(one_and_sixteen_workers_give_the_same_bytes) {
    const uint64_t voxels = (uint64_t)ORE_CHUNKS * ORE_CHUNK_VOXELS;
    const uint64_t bytes = voxels * sizeof(uint16_t);
    uint16_t *const one = test_alloc(t, bytes + 2u * ORE_GUARD * sizeof(uint16_t));
    uint16_t *const many = test_alloc(t, bytes);
    uint16_t *const shuffled = test_alloc(t, bytes);
    uint32_t in_order[ORE_CHUNKS];
    uint32_t out_of_order[ORE_CHUNKS];
    for (uint32_t i = 0u; i < ORE_CHUNKS; i++) {
        in_order[i] = i;
        out_of_order[i] = ORE_CHUNKS - 1u - i;
    }

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));

    uint16_t *const inside = one + ORE_GUARD;
    for (uint32_t i = 0u; i < ORE_GUARD; i++) {
        one[i] = 0xBEEFu;
        inside[voxels + i] = 0xBEEFu;
    }

    const int64_t started = time_now_ns();
    const uint64_t one_mask = ore_run_all(&system, &terrain, 1u, in_order, inside);
    const int64_t one_done = time_now_ns();
    const uint64_t many_mask = ore_run_all(&system, &terrain, ORE_WORKERS_MANY, in_order, many);
    const int64_t many_done = time_now_ns();
    const uint64_t shuffled_mask = ore_run_all(&system, &terrain, ORE_WORKERS_MANY, out_of_order, shuffled);
    job_deinit(&system);

    printf("%" PRIu32 " chunks of ground, caves and ore: %.1f ms on 1 worker, %.1f ms on 16\n", (uint32_t)ORE_CHUNKS,
           (libc_double_t)((float64_t)(one_done - started) / 1.0e6f64),
           (libc_double_t)((float64_t)(many_done - one_done) / 1.0e6f64));
    CHECK_EQ_U64(ore_count_bits(one_mask), 1u);
    CHECK_EQ_U64(ore_count_bits(many_mask), ORE_WORKERS_MANY);
    CHECK_EQ_U64(ore_count_bits(shuffled_mask), ORE_WORKERS_MANY);
    CHECK_MEM_EQ(inside, many, bytes);
    CHECK_MEM_EQ(inside, shuffled, bytes);
    for (uint32_t i = 0u; i < ORE_GUARD; i++) {
        CHECK_EQ_U64(one[i], 0xBEEFu);
        CHECK_EQ_U64(inside[voxels + i], 0xBEEFu);
    }

    const uint64_t digest = ore_digest(0xCBF29CE484222325ull, inside, bytes);
    printf("digest 0x%016" PRIX64 "\n", digest);
    CHECK_EQ_U64(digest, ORE_DIGEST_PIN);
    terrain_deinit(&terrain);
}

TEST(filling_stays_inside_its_output_and_allocates_nothing) {
    enum : uint32_t { ACROSS = 37u, TALL = 41u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    uint16_t *const blocks = test_alloc(t, (VOXELS + 2u * ORE_GUARD) * sizeof(uint16_t));
    ore_fill_with(blocks, VOXELS + 2u * ORE_GUARD, BLOCK_ID_STONE);
    const terrain_density_box_t box = {
        .origin_x = 1800,
        .origin_y = -900,
        .origin_z = -150,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    heap_counters_t before;
    heap_counters_read(&before);
    const uint64_t arena_before = terrain.arena_tables.alloc_count;
    terrain_ore_fill_box(terrain.borrowed_ore, &box, blocks + ORE_GUARD);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(terrain.arena_tables.alloc_count, arena_before);
    for (uint32_t i = 0u; i < ORE_GUARD; i++) {
        CHECK_EQ_U64(blocks[i], BLOCK_ID_STONE);
        CHECK_EQ_U64(blocks[VOXELS + ORE_GUARD + i], BLOCK_ID_STONE);
    }
    terrain_deinit(&terrain);
}

TEST(a_seed_gives_its_own_ore_and_the_same_every_time) {
    enum : uint32_t { ACROSS = 48u, VOXELS = ACROSS * ACROSS * ORE_LAYERS };
    const terrain_density_box_t box = ore_tall_box(0, 0, ACROSS);
    uint16_t *const first = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const again = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const other = test_alloc(t, VOXELS * sizeof(uint16_t));
    ore_fill_with(first, VOXELS, BLOCK_ID_STONE);
    ore_fill_with(again, VOXELS, BLOCK_ID_STONE);
    ore_fill_with(other, VOXELS, BLOCK_ID_STONE);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    terrain_ore_fill_box(terrain.borrowed_ore, &box, first);
    terrain_deinit(&terrain);
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    terrain_ore_fill_box(terrain.borrowed_ore, &box, again);
    terrain_deinit(&terrain);
    REQUIRE(terrain_init(&terrain, ORE_WORLD + 1u));
    terrain_ore_fill_box(terrain.borrowed_ore, &box, other);
    terrain_deinit(&terrain);
    CHECK_MEM_EQ(first, again, VOXELS * sizeof(uint16_t));
    CHECK(memcmp(first, other, VOXELS * sizeof(uint16_t)) != 0);
}

TEST(terrain_starts_the_ores_and_stops_them) {
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, ORE_WORLD));
    REQUIRE(terrain.borrowed_ore != nullptr);
    const uint64_t in_bank = terrain.noise_bank.count;
    CHECK_EQ_U64(in_bank, TERRAIN_FIELDS_NOISE_FIELDS + TERRAIN_LANDFORM_NOISE_FIELDS + TERRAIN_DENSITY_NOISE_FIELDS +
                              TERRAIN_HYDROLOGY_NOISE_FIELDS + TERRAIN_CAVE_NOISE_FIELDS +
                              TERRAIN_SURFACE_NOISE_FIELDS);
    /* Each row has its own stream: two with the same blocks and depths would otherwise lay the same veins. */
    for (uint32_t a = 0u; a < TERRAIN_ORE_ROWS; a++) {
        for (uint32_t b = a + 1u; b < TERRAIN_ORE_ROWS; b++) {
            CHECK(terrain.borrowed_ore->rows[a].seed != terrain.borrowed_ore->rows[b].seed);
        }
    }
    terrain_deinit(&terrain);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK(terrain.borrowed_ore == nullptr);
}

TEST_MAIN()
