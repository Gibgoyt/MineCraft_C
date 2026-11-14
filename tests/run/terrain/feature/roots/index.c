/* ctest run_terrain_feature_roots. See README.md. */
#include "harness/index.h"

#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : int32_t {
    /* The canvas: a tree's reach and height fit, with ground under it. */
    RT_ACROSS = 40,
    RT_TALL = 64,
    RT_ORIGIN_Z = 0,
    /* The flat ground's top, which a tree on it stands on. */
    RT_GROUND = 20,
};

enum : uint32_t {
    RT_VOXELS = (uint32_t)RT_ACROSS * (uint32_t)RT_ACROSS * (uint32_t)RT_TALL,
    RT_SEEDS = 64u,
    /* The land against the land: worlds, and chunks in each, far apart. */
    RT_WORLDS = 3u,
    RT_CHUNKS = 16u,
    RT_CHUNK_STEP = 37u,
    RT_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    RT_CHUNK_VOXELS = TERRAIN_FEATURE_CHUNK_COLUMNS * RT_LAYERS,
};

/* Ahmed's seed from plan 010, the default and a small one. */
static const uint64_t RT_WORLD_SEEDS[RT_WORLDS] = {TERRAIN_SEED_DEFAULT, 11107447384583428913u, 11u};

/* Pinned: the land of RT_WORLDS by RT_CHUNKS chunks with every voxel at a column's top or the one under it that is a log or
 * ground read as one mark, so that a root and the ground it replaced read the same. Measured on plan 010 phase 01's step 1,
 * the tree code moved and no root yet drawn, and it must hold with roots: a draw that moved would move it. */
static const uint64_t RT_MASKED_LAND_PIN = 0x526F23C145164874ull;

static const uint16_t RT_LOGS[TERRAIN_FEATURE_TREES] = {
    [TERRAIN_FEATURE_TREE_OAK] = BLOCK_ID_OAK_LOG,           [TERRAIN_FEATURE_TREE_BIRCH] = BLOCK_ID_BIRCH_LOG,
    [TERRAIN_FEATURE_TREE_SPRUCE] = BLOCK_ID_SPRUCE_LOG,     [TERRAIN_FEATURE_TREE_JUNGLE] = BLOCK_ID_JUNGLE_LOG,
    [TERRAIN_FEATURE_TREE_ACACIA] = BLOCK_ID_ACACIA_LOG,     [TERRAIN_FEATURE_TREE_DARK_OAK] = BLOCK_ID_DARK_OAK_LOG,
    [TERRAIN_FEATURE_TREE_MANGROVE] = BLOCK_ID_MANGROVE_LOG, [TERRAIN_FEATURE_TREE_CHERRY] = BLOCK_ID_CHERRY_LOG,
};

/* The four sides a mangrove's prop roots stand on, in the site's foot order. */
static const int32_t RT_SIDE_X[4] = {1, -1, 0, 0};
static const int32_t RT_SIDE_Y[4] = {0, 0, 1, -1};

static terrain_density_box_t rt_box(void) {
    return (terrain_density_box_t){
        .origin_x = -RT_ACROSS / 2,
        .origin_y = -RT_ACROSS / 2,
        .origin_z = RT_ORIGIN_Z,
        .count_x = (uint32_t)RT_ACROSS,
        .count_y = (uint32_t)RT_ACROSS,
        .count_z = (uint32_t)RT_TALL,
    };
}

static uint64_t rt_at(int32_t x, int32_t y, int32_t z) {
    return (uint64_t)(x + RT_ACROSS / 2) +
           (uint64_t)RT_ACROSS * ((uint64_t)(y + RT_ACROSS / 2) + (uint64_t)RT_ACROSS * (uint64_t)(z - RT_ORIGIN_Z));
}

static bool rt_is_log(uint16_t block) {
    return (block_defs[block].flags & BLOCK_FLAG_LOG) != 0u;
}

/* Ground up to top_all in every column, air over it. */
static void rt_ground(uint16_t *blocks, int32_t top_all, uint16_t ground) {
    for (int32_t z = RT_ORIGIN_Z; z < RT_ORIGIN_Z + RT_TALL; z++) {
        for (int32_t y = -RT_ACROSS / 2; y < RT_ACROSS / 2; y++) {
            for (int32_t x = -RT_ACROSS / 2; x < RT_ACROSS / 2; x++) {
                blocks[rt_at(x, y, z)] = z <= top_all ? ground : (uint16_t)BLOCK_ID_AIR;
            }
        }
    }
}

/* One column's top moved to top, with ground under it. */
static void rt_column(uint16_t *blocks, int32_t x, int32_t y, int32_t top, uint16_t ground) {
    for (int32_t z = RT_ORIGIN_Z; z < RT_ORIGIN_Z + RT_TALL; z++) {
        blocks[rt_at(x, y, z)] = z <= top ? ground : (uint16_t)BLOCK_ID_AIR;
    }
}

static terrain_feature_tree_site_t rt_site(terrain_feature_tree_t kind, uint64_t seed, int16_t foot, uint8_t lift) {
    return (terrain_feature_tree_site_t){
        .seed = seed,
        .x = 0,
        .y = 0,
        .z = RT_GROUND + 1,
        .foot = {foot, foot, foot, foot},
        .kind = (uint8_t)kind,
        .strands = 3u,
        .lift = lift,
    };
}

static void rt_paint(uint16_t *blocks, const terrain_feature_tree_site_t *site) {
    const terrain_density_box_t box = rt_box();
    const terrain_feature_canvas_t canvas = terrain_feature_canvas_of(&box, blocks);
    terrain_feature_paint_tree(&canvas, site);
}

/* Logs at top and top - 1 of (x, y), and the ground's own block at top - 2. */
static bool rt_rooted(const uint16_t *blocks, int32_t x, int32_t y, int32_t top, uint16_t log, uint16_t ground) {
    return blocks[rt_at(x, y, top)] == log && blocks[rt_at(x, y, top - 1)] == log &&
           blocks[rt_at(x, y, top - 2)] == ground;
}

/* ---- Every column of every kind ------------------------------------------------------------------------------------ */

TEST(every_trunk_column_roots_two_blocks_into_the_ground) {
    static_assert(TERRAIN_FEATURE_ROOT_DEPTH == 2);
    uint16_t *const blocks = test_alloc(t, RT_VOXELS * sizeof(uint16_t));
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        const terrain_feature_tree_t kind = (terrain_feature_tree_t)k;
        uint32_t columns = 0u;
        for (uint32_t s = 0u; s < RT_SEEDS; s++) {
            const uint32_t before = columns;
            const uint64_t seed = 0x51ull * s + k;
            rt_ground(blocks, RT_GROUND, BLOCK_ID_DIRT);
            const terrain_feature_tree_site_t site = rt_site(kind, seed, RT_GROUND, 0u);
            rt_paint(blocks, &site);
            CHECK(rt_rooted(blocks, 0, 0, RT_GROUND, RT_LOGS[k], BLOCK_ID_DIRT));
            columns++;
            if (terrain_feature_tree_is_wide(kind, seed)) {
                CHECK(rt_rooted(blocks, 1, 0, RT_GROUND, RT_LOGS[k], BLOCK_ID_DIRT));
                CHECK(rt_rooted(blocks, 0, 1, RT_GROUND, RT_LOGS[k], BLOCK_ID_DIRT));
                CHECK(rt_rooted(blocks, 1, 1, RT_GROUND, RT_LOGS[k], BLOCK_ID_DIRT));
                columns += 3u;
            }
            if (kind == TERRAIN_FEATURE_TREE_MANGROVE) {
                for (uint32_t side = 0u; side < 4u; side++) {
                    const bool drawn = blocks[rt_at(RT_SIDE_X[side], RT_SIDE_Y[side], RT_GROUND + 1)] == RT_LOGS[k];
                    if (drawn) {
                        CHECK(
                            rt_rooted(blocks, RT_SIDE_X[side], RT_SIDE_Y[side], RT_GROUND, RT_LOGS[k], BLOCK_ID_DIRT));
                        columns++;
                    }
                }
            }
            /* No other column has a log in the ground: no flare, and nothing else rooted. */
            uint32_t underground = 0u;
            for (int32_t y = -RT_ACROSS / 2; y < RT_ACROSS / 2; y++) {
                for (int32_t x = -RT_ACROSS / 2; x < RT_ACROSS / 2; x++) {
                    for (int32_t z = RT_ORIGIN_Z; z <= RT_GROUND; z++) {
                        underground += rt_is_log(blocks[rt_at(x, y, z)]) ? 1u : 0u;
                    }
                }
            }
            CHECK_EQ_U64(underground, 2u * (columns - before));
        }
        printf("    kind %" PRIu32 ": %" PRIu32 " trunk columns rooted over %" PRIu32 " seeds\n", k, columns, RT_SEEDS);
    }
}

TEST(the_top_root_is_level_with_the_ground) {
    uint16_t *const blocks = test_alloc(t, RT_VOXELS * sizeof(uint16_t));
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        for (uint32_t s = 0u; s < RT_SEEDS; s++) {
            rt_ground(blocks, RT_GROUND, BLOCK_ID_GRASS_BLOCK);
            const terrain_feature_tree_site_t site = rt_site((terrain_feature_tree_t)k, 0x93ull * s + k, RT_GROUND, 0u);
            rt_paint(blocks, &site);
            /* The ground's top round the trunk, away from any prop root, is still grass at the top root's height. */
            CHECK_EQ_U64(blocks[rt_at(-2, -2, RT_GROUND)], BLOCK_ID_GRASS_BLOCK);
            CHECK(blocks[rt_at(-2, -2, RT_GROUND + 1)] != BLOCK_ID_GRASS_BLOCK);
            CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND)], RT_LOGS[k]);
            /* The log over the top root is the first one in the air. */
            CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND + 1)], RT_LOGS[k]);
            CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND - 2)], BLOCK_ID_GRASS_BLOCK);
        }
    }
}

/* Each of a wide trunk's four columns from its own foot, on ground up to 2 above or below the site's. */
TEST(a_wide_trunk_roots_from_each_columns_own_foot) {
    static const int32_t GAPS[5] = {0, -1, -2, 1, 2};
    static const terrain_feature_tree_t KINDS[2] = {TERRAIN_FEATURE_TREE_JUNGLE, TERRAIN_FEATURE_TREE_DARK_OAK};
    static const int32_t OTHER_X[3] = {1, 0, 1};
    static const int32_t OTHER_Y[3] = {0, 1, 1};
    uint16_t *const blocks = test_alloc(t, RT_VOXELS * sizeof(uint16_t));
    uint32_t trees = 0u;
    for (uint32_t k = 0u; k < 2u; k++) {
        for (uint32_t s = 0u; s < RT_SEEDS; s++) {
            const uint64_t seed = 0x2Bull * s + KINDS[k];
            if (!terrain_feature_tree_is_wide(KINDS[k], seed)) {
                continue;
            }
            const uint16_t log = RT_LOGS[KINDS[k]];
            int16_t feet[3];
            for (uint32_t i = 0u; i < 3u; i++) {
                feet[i] = (int16_t)(RT_GROUND + GAPS[(s + i * 2u) % 5u]);
            }
            rt_ground(blocks, RT_GROUND, BLOCK_ID_DIRT);
            for (uint32_t i = 0u; i < 3u; i++) {
                rt_column(blocks, OTHER_X[i], OTHER_Y[i], feet[i], BLOCK_ID_DIRT);
            }
            terrain_feature_tree_site_t site = rt_site(KINDS[k], seed, RT_GROUND, 0u);
            site.foot[0] = feet[0], site.foot[1] = feet[1], site.foot[2] = feet[2];
            rt_paint(blocks, &site);
            CHECK(rt_rooted(blocks, 0, 0, RT_GROUND, log, BLOCK_ID_DIRT));
            for (uint32_t i = 0u; i < 3u; i++) {
                CHECK(rt_rooted(blocks, OTHER_X[i], OTHER_Y[i], feet[i], log, BLOCK_ID_DIRT));
                CHECK_EQ_U64(blocks[rt_at(OTHER_X[i], OTHER_Y[i], feet[i] + 1)], log);
            }
            trees++;
        }
    }
    printf("    %" PRIu32 " wide trees on uneven feet\n", trees);
    CHECK(trees >= RT_SEEDS);
}

/* In 1 to 5 layers of water on mud, with the sides' ground a layer lower here and there: each drawn prop root goes 2 into it. */
TEST(mangrove_prop_roots_root_too) {
    uint16_t *const blocks = test_alloc(t, RT_VOXELS * sizeof(uint16_t));
    const uint16_t log = BLOCK_ID_MANGROVE_LOG;
    uint32_t props = 0u;
    for (uint32_t s = 0u; s < RT_SEEDS; s++) {
        const uint8_t lift = (uint8_t)(1u + s % 5u);
        rt_ground(blocks, RT_GROUND, BLOCK_ID_MUD);
        int16_t feet[4];
        for (uint32_t side = 0u; side < 4u; side++) {
            feet[side] = (int16_t)(RT_GROUND - (int32_t)((s >> side) & 1u));
            rt_column(blocks, RT_SIDE_X[side], RT_SIDE_Y[side], feet[side], BLOCK_ID_MUD);
        }
        for (int32_t z = RT_GROUND - 1; z <= RT_GROUND + (int32_t)lift; z++) {
            for (int32_t y = -RT_ACROSS / 2; y < RT_ACROSS / 2; y++) {
                for (int32_t x = -RT_ACROSS / 2; x < RT_ACROSS / 2; x++) {
                    if (blocks[rt_at(x, y, z)] == BLOCK_ID_AIR) {
                        blocks[rt_at(x, y, z)] = BLOCK_ID_WATER;
                    }
                }
            }
        }
        terrain_feature_tree_site_t site = rt_site(TERRAIN_FEATURE_TREE_MANGROVE, 0x6Dull * s + 6u, RT_GROUND, lift);
        memcpy(site.foot, feet, sizeof(feet));
        rt_paint(blocks, &site);
        CHECK(rt_rooted(blocks, 0, 0, RT_GROUND, log, BLOCK_ID_MUD));
        for (uint32_t side = 0u; side < 4u; side++) {
            if (blocks[rt_at(RT_SIDE_X[side], RT_SIDE_Y[side], feet[side] + 1)] != log) {
                /* Not drawn: then nothing went into its ground either. */
                CHECK_EQ_U64(blocks[rt_at(RT_SIDE_X[side], RT_SIDE_Y[side], feet[side])], BLOCK_ID_MUD);
                continue;
            }
            CHECK(rt_rooted(blocks, RT_SIDE_X[side], RT_SIDE_Y[side], feet[side], log, BLOCK_ID_MUD));
            props++;
        }
    }
    printf("    %" PRIu32 " prop roots over %" PRIu32 " mangroves\n", props, RT_SEEDS);
    CHECK(props >= RT_SEEDS);
}

/* ---- What a root goes over ------------------------------------------------------------------------------------------ */

/* Under the top, what no root takes: it is left, and the root is the one log at the top. */
static void rt_stops_at(test_context_t *t, uint16_t *blocks, uint16_t under) {
    rt_ground(blocks, RT_GROUND, BLOCK_ID_DIRT);
    blocks[rt_at(0, 0, RT_GROUND - 1)] = under;
    const terrain_feature_tree_site_t site = rt_site(TERRAIN_FEATURE_TREE_OAK, 0x0AE5ull, RT_GROUND, 0u);
    rt_paint(blocks, &site);
    CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND)], BLOCK_ID_OAK_LOG);
    CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND - 1)], under);
}

TEST(roots_never_hang_or_eat_ores) {
    uint16_t *const blocks = test_alloc(t, RT_VOXELS * sizeof(uint16_t));
    rt_stops_at(t, blocks, BLOCK_ID_AIR);
    rt_stops_at(t, blocks, BLOCK_ID_WATER);
    rt_stops_at(t, blocks, BLOCK_ID_LAVA);
    rt_stops_at(t, blocks, BLOCK_ID_BEDROCK);
    rt_stops_at(t, blocks, BLOCK_ID_SPRUCE_LOG);
    rt_stops_at(t, blocks, BLOCK_ID_OAK_LEAVES);
    rt_stops_at(t, blocks, BLOCK_ID_TALL_GRASS);
    /* Every ore, by its name, so that one added later is held to it too; and every unbreakable block. */
    uint32_t ores = 0u;
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        const char *const name = block_defs[block].name;
        const uint64_t length = strlen(name);
        const bool ore = length > 4u && strcmp(name + length - 4u, "_ore") == 0;
        if (ore || (block_defs[block].flags & BLOCK_FLAG_UNBREAKABLE) != 0u) {
            rt_stops_at(t, blocks, (uint16_t)block);
            ores += ore ? 1u : 0u;
        }
    }
    printf("    %" PRIu32 " ores left in place\n", ores);
    CHECK(ores >= 16u);
    /* A site's top is soil or snow (feature_tree_stands), so the top root always has ground to take. Each root voxel is decided
     * by that voxel alone, as a box may hold one and not the other. */
}

TEST(roots_split_stone) {
    static const uint16_t ROCKS[] = {BLOCK_ID_STONE, BLOCK_ID_DEEPSLATE, BLOCK_ID_GRANITE, BLOCK_ID_TUFF,
                                     BLOCK_ID_SANDSTONE};
    uint16_t *const blocks = test_alloc(t, RT_VOXELS * sizeof(uint16_t));
    for (uint32_t r = 0u; r < sizeof(ROCKS) / sizeof(ROCKS[0]); r++) {
        rt_ground(blocks, RT_GROUND - 1, ROCKS[r]);
        for (int32_t y = -RT_ACROSS / 2; y < RT_ACROSS / 2; y++) {
            for (int32_t x = -RT_ACROSS / 2; x < RT_ACROSS / 2; x++) {
                blocks[rt_at(x, y, RT_GROUND)] = BLOCK_ID_GRASS_BLOCK;
            }
        }
        const terrain_feature_tree_site_t site = rt_site(TERRAIN_FEATURE_TREE_BIRCH, 0xB1ull + r, RT_GROUND, 0u);
        rt_paint(blocks, &site);
        CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND)], BLOCK_ID_BIRCH_LOG);
        CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND - 1)], BLOCK_ID_BIRCH_LOG);
        CHECK_EQ_U64(blocks[rt_at(0, 0, RT_GROUND - 2)], ROCKS[r]);
    }
}

/* A trunk beside a hill as tall as a tree: the hill keeps every block over the ground, whatever leans or branches into it. */
TEST(ordinary_logs_still_never_go_into_the_ground) {
    enum : int32_t { HILL = 12 };
    uint16_t *const blocks = test_alloc(t, RT_VOXELS * sizeof(uint16_t));
    uint32_t trees = 0u;
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        if (k == TERRAIN_FEATURE_TREE_DARK_OAK || k == TERRAIN_FEATURE_TREE_MANGROVE) {
            continue; /* their sites ask for the ground beside them, which a hill is not */
        }
        for (uint32_t s = 0u; s < RT_SEEDS; s++) {
            const uint64_t seed = 0x17ull * s + k;
            if (terrain_feature_tree_is_wide((terrain_feature_tree_t)k, seed)) {
                continue;
            }
            for (uint32_t side = 0u; side < 4u; side++) {
                rt_ground(blocks, RT_GROUND, BLOCK_ID_DIRT);
                for (int32_t y = -RT_ACROSS / 2; y < RT_ACROSS / 2; y++) {
                    for (int32_t x = -RT_ACROSS / 2; x < RT_ACROSS / 2; x++) {
                        const int32_t along = side == 0u ? x : (side == 1u ? -x : (side == 2u ? y : -y));
                        if (along >= 1) {
                            rt_column(blocks, x, y, RT_GROUND + HILL, BLOCK_ID_DIRT);
                        }
                    }
                }
                const terrain_feature_tree_site_t site = rt_site((terrain_feature_tree_t)k, seed, RT_GROUND, 0u);
                rt_paint(blocks, &site);
                uint32_t eaten = 0u;
                for (int32_t y = -RT_ACROSS / 2; y < RT_ACROSS / 2; y++) {
                    for (int32_t x = -RT_ACROSS / 2; x < RT_ACROSS / 2; x++) {
                        const int32_t along = side == 0u ? x : (side == 1u ? -x : (side == 2u ? y : -y));
                        for (int32_t z = RT_GROUND + 1; along >= 1 && z <= RT_GROUND + HILL; z++) {
                            eaten += blocks[rt_at(x, y, z)] != BLOCK_ID_DIRT ? 1u : 0u;
                        }
                    }
                }
                CHECK_EQ_U64(eaten, 0u);
                CHECK(rt_rooted(blocks, 0, 0, RT_GROUND, RT_LOGS[k], BLOCK_ID_DIRT));
                trees++;
            }
        }
    }
    printf("    %" PRIu32 " trees against a hill\n", trees);
}

/* ---- The land ------------------------------------------------------------------------------------------------------- */

static uint64_t rt_digest(uint64_t hash, uint16_t value) {
    hash = (hash ^ (value & 0xFFu)) * 0x00000100000001B3ull;
    return (hash ^ (value >> 8)) * 0x00000100000001B3ull;
}

static bool rt_is_ground(uint16_t block) {
    const uint8_t flags = block_defs[block].flags;
    return (flags & BLOCK_FLAG_SOLID) != 0u && (flags & (BLOCK_FLAG_LOG | BLOCK_FLAG_DECAYS)) == 0u;
}

/* Generated chunks, each read with the voxels a root can take marked as one, against a pin taken before roots were drawn. */
TEST(nothing_but_trees_changes) {
    uint16_t *const blocks = test_alloc(t, (uint64_t)RT_CHUNK_VOXELS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, RT_CHUNK_VOXELS);
    terrain_feature_column_t *const columns =
        test_alloc(t, sizeof(terrain_feature_column_t) * TERRAIN_FEATURE_CHUNK_COLUMNS);
    uint64_t hash = 0xCBF29CE484222325ull;
    uint64_t roots = 0u;
    for (uint32_t w = 0u; w < RT_WORLDS; w++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, RT_WORLD_SEEDS[w]));
        for (uint32_t c = 0u; c < RT_CHUNKS; c++) {
            const int32_t chunk_x = ((int32_t)(c % 4u) - 2) * (int32_t)RT_CHUNK_STEP + 3;
            const int32_t chunk_y = ((int32_t)(c / 4u) - 2) * (int32_t)RT_CHUNK_STEP - 1;
            const terrain_density_box_t box = {
                .origin_x = chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
                .origin_y = chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK,
                .origin_z = COLUMN_Z_STORAGE_MIN,
                .count_x = TERRAIN_FEATURE_CHUNK,
                .count_y = TERRAIN_FEATURE_CHUNK,
                .count_z = RT_LAYERS,
            };
            terrain_generate_box_fallback(&terrain, &box, opened, blocks);
            terrain_feature_chunk_columns(terrain.borrowed_feature, nullptr, chunk_x, chunk_y, columns);
            for (uint32_t i = 0u; i < RT_CHUNK_VOXELS; i++) {
                const uint32_t column = i % TERRAIN_FEATURE_CHUNK_COLUMNS;
                const int32_t z = COLUMN_Z_STORAGE_MIN + (int32_t)(i / TERRAIN_FEATURE_CHUNK_COLUMNS);
                const int32_t top = columns[column].top;
                uint16_t value = blocks[i];
                const bool may_root = top != TERRAIN_FEATURE_NO_GROUND && (z == top || z == top - 1);
                if (may_root && (rt_is_log(value) || rt_is_ground(value))) {
                    roots += rt_is_log(value) ? 1u : 0u;
                    value = 0xFFFFu;
                }
                hash = rt_digest(hash, value);
            }
        }
        terrain_deinit(&terrain);
    }
    printf("    masked land: 0x%016" PRIX64 ", %" PRIu64 " root logs in it\n", hash, roots);
    CHECK(roots > 0u);
    CHECK_EQ_U64(hash, RT_MASKED_LAND_PIN);
}

TEST_MAIN()
