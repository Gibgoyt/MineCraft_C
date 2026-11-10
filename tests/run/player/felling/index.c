/* ctest run_player_felling. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "heap/index.h"
#include "player/felling/index.h"
#include "player/index.h"
#include "tables/blocks/index.h"
#include "terrain/feature/index.h"
#include "world/index.h"
#include "world/wall_torch/index.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum : int32_t {
    /* Where the trees stand: well inside what is loaded, and in the air. */
    PF_X = 8,
    PF_Y = 8,
    PF_Z = 70,
    /* The box a tree is drawn in. Its reach and height are held inside this by run_terrain_feature. */
    PF_ACROSS = 40,
    PF_TALL = 64,
    /* What the ground may fall away by under a wide trunk, by feature_tree_stands, and more than that under a mangrove's roots. */
    PF_FOOT_GAP = 2,
    PF_ROOT_GAP = 6,
    /* Heights a tree is cut at, from the foot, and the highest of them that the eye of a body on the ground is on. */
    PF_CUT_LEVELS = 9,
    PF_CUT_LEVEL_EYE = 1,
};

enum : uint32_t {
    /* Every tree of every kind that these seeds give, for the biggest. */
    PF_SEEDS_MEASURED = 4000u,
    /* Felled for real, and the biggest as well. */
    PF_SEEDS_FELLED = 24u,
    PF_VOXELS = (uint32_t)PF_ACROSS * (uint32_t)PF_ACROSS * (uint32_t)PF_TALL,
    /* What a cut, a tree's worth of logs at 8 a tick, and a box of leaves at 30 ticks come to, with plenty over. */
    PF_TICKS_MAX = 400u,
    PF_WALL_SIZE = 5u,
    PF_SEEDS_CUT = 2u,
};

typedef struct pf_rig_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
    player_felling_t felling;   /* 1384 */
    uint64_t logs_dropped;      /* 6568 */
    uint64_t felled;            /* 6576: what player_felling_tick said went */
    uint8_t drain;              /* 6584: whether anyone is taking the drops */
    uint8_t torches_dropped;    /* 6585 */
    uint8_t _pad0[6];           /* 6586 */
} pf_rig_t;
static_assert(sizeof(pf_rig_t) == 6592);

/* A wood axe in hand, an empty world and a body at rest away from where the tests work. */
static bool pf_begin(test_context_t *t, pf_rig_t *rig) {
    memset(rig, 0, sizeof(*rig));
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    player_movement_start(&rig->movement, 40.5f64, 40.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    player_interaction_start(&rig->hands, 1234u);
    player_interaction_hold_tool(&rig->hands, BLOCK_TOOL_AXE, BLOCK_TIER_WOOD);
    player_felling_start(&rig->felling);
    rig->drain = 1u;
    return true;
}

static void pf_end(pf_rig_t *rig) {
    test_world_end(&rig->world);
}

static uint16_t pf_block(const pf_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = UINT16_MAX;
    (void)world_map_voxel_get(rig->world.map, x, y, z, &block);
    return block;
}

static void pf_put(test_context_t *t, pf_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    test_world_fill(t, &rig->world, x, y, z, x, y, z, block);
}

static world_tick_t *pf_world_tick(pf_rig_t *rig) {
    return &rig->world.world->tick;
}

static void pf_drop_count(pf_rig_t *rig) {
    player_drop_t drop;
    while (player_interaction_drop_next(&rig->hands, &drop)) {
        const uint16_t block = block_drop_items[drop.item].block;
        if (block != BLOCK_NO_ID && (block_defs[block].flags & BLOCK_FLAG_LOG) != 0u) {
            rig->logs_dropped += drop.count;
        }
        if (block == BLOCK_ID_TORCH) {
            rig->torches_dropped = (uint8_t)(rig->torches_dropped + drop.count);
        }
    }
}

/* As run/src/game/ does between two ticks: what the hands broke, and what went by itself, comes out one block at a time. */
static void pf_drain(pf_rig_t *rig) {
    pf_drop_count(rig);
    world_tick_broken_t broken;
    while (world_tick_broken_next(pf_world_tick(rig), &broken)) {
        player_interaction_drops_spawn(&rig->hands, broken.block, broken.x, broken.y, broken.z);
        pf_drop_count(rig);
    }
}

/* One tick, in the order the game's is in. */
static void pf_tick(pf_rig_t *rig, bool attack) {
    const player_interaction_input_t input = {.borrowed_target = &rig->target,
                                              .borrowed_movement = &rig->movement,
                                              .borrowed_felling = &rig->felling,
                                              .attack = attack ? 1u : 0u,
                                              .place = 0u,
                                              ._pad0 = {0}};
    player_interaction_tick(&rig->hands, &rig->world.world->edit, &input);
    rig->felled += player_felling_tick(&rig->felling, &rig->hands, &rig->world.world->edit, pf_world_tick(rig));
    if (rig->drain != 0u) {
        pf_drain(rig);
    }
    world_tick_step(pf_world_tick(rig));
}

/* Holds the left button on this cell until it goes, for the one tick that does it. */
static bool pf_chop(pf_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    rig->target.hit.block = block_state_id(pf_block(rig, x, y, z));
    rig->target.hit.axis = COLLISION_AXIS_Z;
    rig->target.hit.normal_sign = 1;
    const uint64_t mined = rig->hands.mined_count;
    for (uint32_t tick = 0u; tick < PF_TICKS_MAX; tick++) {
        pf_tick(rig, true);
        if (rig->hands.mined_count != mined) {
            rig->target.hit.result = RAYCAST_RESULT_MISS;
            return true;
        }
    }
    return false;
}

/* Until no log is waiting and no box of leaves is left to look at. */
static bool pf_settle(pf_rig_t *rig) {
    for (uint32_t tick = 0u; tick < PF_TICKS_MAX; tick++) {
        if (player_felling_pending(&rig->felling) == 0u && world_tick_sweeps_pending(pf_world_tick(rig)) == 0u) {
            pf_drain(rig);
            return true;
        }
        pf_tick(rig, false);
    }
    return false;
}

/* A trunk straight up, both ends included. */
static void pf_trunk(test_context_t *t, pf_rig_t *rig, int32_t x, int32_t y, int32_t z_first, int32_t z_last,
                     uint16_t block) {
    test_world_fill(t, &rig->world, x, y, z_first, x, y, z_last, block);
}

/* What looks like a tree: leaves round the top of a trunk. */
static void pf_crown(test_context_t *t, pf_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t leaf) {
    pf_put(t, rig, x + 1, y, z, leaf);
    pf_put(t, rig, x - 1, y, z, leaf);
    pf_put(t, rig, x, y + 1, z, leaf);
    pf_put(t, rig, x, y - 1, z, leaf);
    pf_put(t, rig, x, y, z + 1, leaf);
}

/* ---- Trees as the generator draws them -------------------------------------------------------------------------------- */

static uint16_t pf_kind_log(terrain_feature_tree_t kind) {
    static const uint16_t LOGS[TERRAIN_FEATURE_TREES] = {
        [TERRAIN_FEATURE_TREE_OAK] = BLOCK_ID_OAK_LOG,
        [TERRAIN_FEATURE_TREE_BIRCH] = BLOCK_ID_BIRCH_LOG,
        [TERRAIN_FEATURE_TREE_SPRUCE] = BLOCK_ID_SPRUCE_LOG,
        [TERRAIN_FEATURE_TREE_JUNGLE] = BLOCK_ID_JUNGLE_LOG,
        [TERRAIN_FEATURE_TREE_ACACIA] = BLOCK_ID_ACACIA_LOG,
        [TERRAIN_FEATURE_TREE_DARK_OAK] = BLOCK_ID_DARK_OAK_LOG,
        [TERRAIN_FEATURE_TREE_MANGROVE] = BLOCK_ID_MANGROVE_LOG,
        [TERRAIN_FEATURE_TREE_CHERRY] = BLOCK_ID_CHERRY_LOG,
    };
    return LOGS[kind];
}

static terrain_density_box_t pf_tree_box(void) {
    return (terrain_density_box_t){
        .origin_x = PF_X - PF_ACROSS / 2,
        .origin_y = PF_Y - PF_ACROSS / 2,
        .origin_z = PF_Z - 8,
        .count_x = (uint32_t)PF_ACROSS,
        .count_y = (uint32_t)PF_ACROSS,
        .count_z = (uint32_t)PF_TALL,
    };
}

/* foot_gap is how far the ground falls away beside it: 0 leaves every log at or above the lowest of the middle. */
static void pf_paint(uint16_t *blocks, terrain_feature_tree_t kind, uint64_t seed, int16_t foot_gap, uint8_t lift) {
    const terrain_density_box_t box = pf_tree_box();
    const terrain_feature_canvas_t canvas = terrain_feature_canvas_of(&box, blocks);
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        blocks[i] = BLOCK_ID_AIR;
    }
    const int16_t foot = (int16_t)(PF_Z - 1 - foot_gap);
    const terrain_feature_tree_site_t site = {
        .seed = seed,
        .x = PF_X,
        .y = PF_Y,
        .z = PF_Z,
        .foot = {foot, foot, foot, foot},
        .kind = (uint8_t)kind,
        .strands = 3u,
        .lift = lift,
    };
    terrain_feature_paint_tree(&canvas, &site);
}

static uint32_t pf_painted_logs(const uint16_t *blocks, uint16_t log) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        count += blocks[i] == log ? 1u : 0u;
    }
    return count;
}

static void pf_voxel_place(const terrain_density_box_t *box, uint32_t i, int32_t *x, int32_t *y, int32_t *z) {
    *x = box->origin_x + (int32_t)(i % box->count_x);
    *y = box->origin_y + (int32_t)(i / box->count_x % box->count_y);
    *z = box->origin_z + (int32_t)(i / (box->count_x * box->count_y));
}

/* Into the world by hand, as terrain would leave it: nothing wakes. Air where there is none. */
static void pf_place_tree(test_context_t *t, pf_rig_t *rig, const uint16_t *blocks) {
    const terrain_density_box_t box = pf_tree_box();
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        pf_put(t, rig, x, y, z, blocks[i]);
    }
}

/* What is left in the box of the tree: logs at or above a height, and leaves and vines anywhere. */
static void pf_left(const pf_rig_t *rig, uint16_t log, int32_t z_from, uint32_t *logs, uint32_t *leaves,
                    uint32_t *vines) {
    const terrain_density_box_t box = pf_tree_box();
    *logs = 0u;
    *leaves = 0u;
    *vines = 0u;
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        const uint16_t block = pf_block(rig, x, y, z);
        *logs += block == log && z >= z_from ? 1u : 0u;
        *leaves += (block_defs[block].flags & BLOCK_FLAG_DECAYS) != 0u ? 1u : 0u;
        *vines += block == BLOCK_ID_VINE ? 1u : 0u;
    }
}

static uint8_t pf_lift_of(terrain_feature_tree_t kind) {
    return kind == TERRAIN_FEATURE_TREE_MANGROVE ? 5u : 0u;
}

/* The biggest of every kind, with the ground as low as it can be beside it, and so what the queue is held above. */
static uint32_t pf_biggest_of(uint16_t *blocks, terrain_feature_tree_t kind, uint64_t *out_seed) {
    const int16_t gap = (int16_t)(kind == TERRAIN_FEATURE_TREE_MANGROVE ? PF_ROOT_GAP : PF_FOOT_GAP);
    uint32_t biggest = 0u;
    for (uint32_t s = 0u; s < PF_SEEDS_MEASURED; s++) {
        const uint64_t seed = 0x9E37ull * s + (uint64_t)kind;
        pf_paint(blocks, kind, seed, gap, pf_lift_of(kind));
        const uint32_t logs = pf_painted_logs(blocks, pf_kind_log(kind));
        if (logs > biggest) {
            biggest = logs;
            *out_seed = seed;
        }
    }
    return biggest;
}

TEST(the_limit_is_above_the_biggest_tree_of_every_kind_measured) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    uint32_t biggest = 0u;
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        uint64_t seed = 0u;
        const uint32_t logs = pf_biggest_of(blocks, (terrain_feature_tree_t)k, &seed);
        printf("    kind %" PRIu32 ": %" PRIu32 " logs at most, seed %" PRIu64 "\n", k, logs, seed);
        biggest = logs > biggest ? logs : biggest;
    }
    printf("    the biggest tree has %" PRIu32 " logs, and the limit is %" PRIu32 "\n", biggest,
           (uint32_t)PLAYER_FELLING_LOGS_MAX);
    /* Above it by enough for two of them grown into each other, which a jungle has. */
    CHECK(2u * biggest < PLAYER_FELLING_LOGS_MAX);
}

/* Cut at its lowest log: every log goes, each once and with its drop, and the leaves and vines after them. */
static void pf_fell_one(test_context_t *t, pf_rig_t *rig, uint16_t *blocks, terrain_feature_tree_t kind,
                        uint64_t seed) {
    const uint16_t log = pf_kind_log(kind);
    pf_paint(blocks, kind, seed, 0, pf_lift_of(kind));
    const uint32_t logs = pf_painted_logs(blocks, log);
    pf_place_tree(t, rig, blocks);
    const uint64_t dropped = rig->logs_dropped, felled = rig->felled, mined = rig->hands.mined_count;
    CHECK(pf_chop(rig, PF_X, PF_Y, PF_Z));
    CHECK(pf_settle(rig));
    uint32_t logs_left = 0u, leaves_left = 0u, vines_left = 0u;
    pf_left(rig, log, PF_Z, &logs_left, &leaves_left, &vines_left);
    CHECK_EQ_U64(logs_left, 0u);
    CHECK_EQ_U64(leaves_left, 0u);
    CHECK_EQ_U64(vines_left, 0u);
    CHECK_EQ_U64(rig->logs_dropped - dropped, logs);
    CHECK_EQ_U64(rig->felled - felled, logs - 1u);
    CHECK(rig->hands.mined_count - mined >= logs);
    CHECK_EQ_U64(rig->hands.drops_lost_count, 0u);
}

TEST(every_kind_of_tree_comes_down_whole_with_a_drop_for_every_log) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        const terrain_feature_tree_t kind = (terrain_feature_tree_t)k;
        uint64_t biggest = 0u;
        (void)pf_biggest_of(blocks, kind, &biggest);
        pf_fell_one(t, rig, blocks, kind, biggest);
        for (uint32_t s = 0u; s < PF_SEEDS_FELLED; s++) {
            pf_fell_one(t, rig, blocks, kind, 0x51ull * s + k);
        }
    }
    CHECK_EQ_U64(pf_world_tick(rig)->broken_lost_count, 0u);
    CHECK_EQ_U64(rig->felling.turned_away_count, 0u);
    pf_end(rig);
}

/* A wide trunk on falling ground, as the ground under it leaves it: each column down to its own foot, of which the cut's level and up go. */
TEST(a_giant_cut_at_the_level_of_its_middle_leaves_only_what_is_below_that) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    uint64_t seed = 0u;
    (void)pf_biggest_of(blocks, TERRAIN_FEATURE_TREE_JUNGLE, &seed);
    pf_paint(blocks, TERRAIN_FEATURE_TREE_JUNGLE, seed, PF_FOOT_GAP, 0u);
    REQUIRE(terrain_feature_tree_is_wide(TERRAIN_FEATURE_TREE_JUNGLE, seed));
    pf_place_tree(t, rig, blocks);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    uint32_t logs = 0u, leaves = 0u, vines = 0u;
    pf_left(rig, BLOCK_ID_JUNGLE_LOG, PF_Z, &logs, &leaves, &vines);
    CHECK_EQ_U64(logs, 0u);
    CHECK_EQ_U64(leaves, 0u);
    CHECK_EQ_U64(pf_block(rig, PF_X + 1, PF_Y, PF_Z - 1), BLOCK_ID_JUNGLE_LOG);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y + 1, PF_Z - 2), BLOCK_ID_JUNGLE_LOG);
    pf_end(rig);
}

/* ---- What it takes and what it leaves --------------------------------------------------------------------------------- */

TEST(a_cut_part_way_up_leaves_the_stump_and_takes_the_rest) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 14, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 14, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z + 5));
    REQUIRE(pf_settle(rig));
    for (int32_t z = PF_Z; z < PF_Z + 5; z++) {
        CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, z), BLOCK_ID_OAK_LOG);
    }
    for (int32_t z = PF_Z + 5; z <= PF_Z + 14; z++) {
        CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, z), BLOCK_ID_AIR);
    }
    CHECK_EQ_U64(pf_block(rig, PF_X + 1, PF_Y, PF_Z + 14), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig->logs_dropped, 10u);
    CHECK_EQ_U64(rig->felled, 9u);
    pf_end(rig);
}

static void pf_wall_stays(test_context_t *t, uint8_t tool, uint8_t tier) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    player_interaction_hold_tool(&rig->hands, tool, tier);
    const int32_t size = (int32_t)PF_WALL_SIZE;
    test_world_fill(t, &rig->world, PF_X, PF_Y, PF_Z, PF_X + size - 1, PF_Y, PF_Z + size - 1, BLOCK_ID_OAK_LOG);
    REQUIRE(pf_chop(rig, PF_X + 2, PF_Y, PF_Z));
    CHECK_EQ_U64(player_felling_pending(&rig->felling), 0u);
    CHECK_EQ_U64(world_tick_sweeps_pending(pf_world_tick(rig)), 0u);
    REQUIRE(pf_settle(rig));
    uint32_t standing = 0u;
    for (int32_t z = PF_Z; z < PF_Z + size; z++) {
        for (int32_t x = PF_X; x < PF_X + size; x++) {
            standing += pf_block(rig, x, PF_Y, z) == BLOCK_ID_OAK_LOG ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(standing, PF_WALL_SIZE * PF_WALL_SIZE - 1u);
    CHECK_EQ_U64(rig->felled, 0u);
    pf_end(rig);
}

TEST(a_log_wall_with_no_leaves_is_left_alone) {
    pf_wall_stays(t, BLOCK_TOOL_AXE, BLOCK_TIER_WOOD);
}

TEST(a_log_wall_with_no_leaves_is_left_alone_by_bare_hands_too) {
    pf_wall_stays(t, BLOCK_TOOL_NONE, BLOCK_TIER_NONE);
}

TEST(other_wood_touching_it_stays) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 6, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 6, BLOCK_ID_OAK_LEAVES);
    /* By a face, and by a corner. */
    pf_trunk(t, rig, PF_X + 1, PF_Y, PF_Z, PF_Z + 3, BLOCK_ID_BIRCH_LOG);
    pf_put(t, rig, PF_X - 1, PF_Y - 1, PF_Z + 2, BLOCK_ID_SPRUCE_LOG);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 6), BLOCK_ID_AIR);
    for (int32_t z = PF_Z; z <= PF_Z + 3; z++) {
        CHECK_EQ_U64(pf_block(rig, PF_X + 1, PF_Y, z), BLOCK_ID_BIRCH_LOG);
    }
    CHECK_EQ_U64(pf_block(rig, PF_X - 1, PF_Y - 1, PF_Z + 2), BLOCK_ID_SPRUCE_LOG);
    pf_end(rig);
}

TEST(branches_that_touch_only_at_a_corner_go_with_the_tree) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 4, BLOCK_ID_ACACIA_LOG);
    pf_put(t, rig, PF_X + 1, PF_Y + 1, PF_Z + 5, BLOCK_ID_ACACIA_LOG);
    pf_put(t, rig, PF_X + 2, PF_Y + 2, PF_Z + 6, BLOCK_ID_ACACIA_LOG);
    pf_put(t, rig, PF_X + 3, PF_Y + 2, PF_Z + 6, BLOCK_ID_ACACIA_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, PF_X + 2, PF_Y + 2, PF_Z + 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X + 3, PF_Y + 2, PF_Z + 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig->logs_dropped, 7u);
    pf_end(rig);
}

/* Ahmed: "tree feller is not a mod in my minecraft it is a built in setting please make it always on!!". Whatever is in hand, and sneaking
 * or not, a log cut brings the whole tree down. */
static void pf_whole_tree(test_context_t *t, uint8_t tool, uint8_t tier, bool crouched) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    player_interaction_hold_tool(&rig->hands, tool, tier);
    rig->movement.crouched = crouched ? 1u : 0u;
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 6, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 6, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    for (int32_t z = PF_Z; z <= PF_Z + 6; z++) {
        CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, z), BLOCK_ID_AIR);
    }
    CHECK_EQ_U64(pf_block(rig, PF_X + 1, PF_Y, PF_Z + 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig->logs_dropped, 7u);
    CHECK_EQ_U64(rig->felled, 6u);
    pf_end(rig);
}

TEST(an_axe_fells_the_whole_tree) {
    pf_whole_tree(t, BLOCK_TOOL_AXE, BLOCK_TIER_WOOD, false);
}

TEST(bare_hands_fell_the_whole_tree) {
    pf_whole_tree(t, BLOCK_TOOL_NONE, BLOCK_TIER_NONE, false);
}

TEST(a_pickaxe_fells_the_whole_tree) {
    pf_whole_tree(t, BLOCK_TOOL_PICKAXE, BLOCK_TIER_DIAMOND, false);
}

TEST(sneaking_fells_the_whole_tree_too) {
    pf_whole_tree(t, BLOCK_TOOL_AXE, BLOCK_TIER_WOOD, true);
}

TEST(what_is_no_log_starts_nothing) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    /* Planks are the axe's block too, and leaves touching them make them no tree. */
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 3, BLOCK_ID_OAK_PLANKS);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 3, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    CHECK_EQ_U64(player_felling_pending(&rig->felling), 0u);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 1), BLOCK_ID_OAK_PLANKS);
    pf_end(rig);
}

/* ---- The axe ------------------------------------------------------------------------------------------------------------ */

/* What is in hand once the tree is going makes no difference: the axe's wear is the cut's alone, run/src/game/'s to charge. */
TEST(putting_the_axe_away_while_it_comes_down_leaves_nothing_up) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 20, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 20, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    CHECK(player_felling_pending(&rig->felling) > 0u);
    player_interaction_hold_tool(&rig->hands, BLOCK_TOOL_NONE, BLOCK_TIER_NONE);
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->felled, 20u);
    for (int32_t z = PF_Z; z <= PF_Z + 20; z++) {
        CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, z), BLOCK_ID_AIR);
    }
    CHECK_EQ_U64(rig->logs_dropped, 21u);
    pf_end(rig);
}

/* ---- Its pace, and its limits ------------------------------------------------------------------------------------------- */

TEST(it_goes_from_the_cut_upward_at_eight_logs_a_tick) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 40, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 40, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    for (uint32_t tick = 1u; tick <= 5u; tick++) {
        const int32_t gone = (int32_t)(tick * PLAYER_FELLING_PER_TICK);
        CHECK_EQ_U64(rig->felled, (uint64_t)gone);
        CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + gone), BLOCK_ID_AIR);
        if (tick < 5u) {
            CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + gone + 1), BLOCK_ID_OAK_LOG);
        }
        pf_tick(rig, false);
    }
    pf_end(rig);
}

TEST(no_drop_is_lost_on_the_biggest_tree_with_or_without_anyone_taking_them_at_once) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    uint64_t seed = 0u;
    const uint32_t biggest = pf_biggest_of(blocks, TERRAIN_FEATURE_TREE_JUNGLE, &seed);
    pf_paint(blocks, TERRAIN_FEATURE_TREE_JUNGLE, seed, 0, 0u);
    const uint32_t logs = pf_painted_logs(blocks, BLOCK_ID_JUNGLE_LOG);
    REQUIRE(logs > PLAYER_INTERACTION_DROPS);
    REQUIRE(logs <= biggest);
    pf_place_tree(t, rig, blocks);
    /* Nobody takes them: it holds back at a full ring, loses nothing, and goes on when someone comes. */
    rig->drain = 0u;
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    for (uint32_t tick = 0u; tick < 40u; tick++) {
        pf_tick(rig, false);
    }
    CHECK(player_felling_pending(&rig->felling) > 0u);
    CHECK(rig->hands.drop_count <= PLAYER_INTERACTION_DROPS);
    CHECK_EQ_U64(rig->hands.drops_lost_count, 0u);
    rig->drain = 1u;
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->logs_dropped, logs);
    CHECK_EQ_U64(rig->hands.drops_lost_count, 0u);
    CHECK_EQ_U64(pf_world_tick(rig)->broken_lost_count, 0u);
    pf_end(rig);
}

TEST(past_the_limit_the_rest_stay) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    const int32_t top = PF_Z + 2 * (int32_t)PLAYER_FELLING_LOGS_MAX;
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, top, BLOCK_ID_OAK_LOG);
    pf_put(t, rig, PF_X + 1, PF_Y, PF_Z + 1, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK(rig->felling.turned_away_count > 0u);
    /* The queue had the cut in it, so that one fewer than it holds went above it. */
    CHECK_EQ_U64(rig->felled, PLAYER_FELLING_LOGS_MAX - 1u);
    const int32_t last = PF_Z + (int32_t)PLAYER_FELLING_LOGS_MAX - 1;
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, last), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, last + 1), BLOCK_ID_OAK_LOG);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, top), BLOCK_ID_OAK_LOG);
    pf_end(rig);
}

TEST(a_column_not_loaded_stops_the_walk_and_nothing_goes_wrong) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    /* x = 63 is the last that is loaded along this row. */
    const int32_t edge = 63;
    REQUIRE(world_map_column_at(rig->world.map, column_chunk_of(edge), column_chunk_of(PF_Y)) != nullptr);
    REQUIRE(world_map_column_at(rig->world.map, column_chunk_of(edge + 1), column_chunk_of(PF_Y)) == nullptr);
    pf_trunk(t, rig, edge, PF_Y, PF_Z, PF_Z + 5, BLOCK_ID_OAK_LOG);
    pf_put(t, rig, edge, PF_Y + 1, PF_Z + 5, BLOCK_ID_OAK_LEAVES);
    pf_put(t, rig, edge - 1, PF_Y, PF_Z + 5, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, edge, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, edge, PF_Y, PF_Z + 5), BLOCK_ID_AIR);
    /* Against what is not loaded, which might hold it: the rule says it stays. The other has only air between. */
    CHECK_EQ_U64(pf_block(rig, edge, PF_Y + 1, PF_Z + 5), BLOCK_ID_OAK_LEAVES);
    CHECK_EQ_U64(pf_block(rig, edge - 1, PF_Y, PF_Z + 5), BLOCK_ID_AIR);
    pf_end(rig);
}

/* ---- Wherever it is cut ------------------------------------------------------------------------------------------------ */

/* Logs of this wood the ground does not hold: not joined, by the corners the walk goes by, to one at the level of the foot. */
static uint32_t pf_logs_floating(const pf_rig_t *rig, uint16_t log, uint8_t *held, uint32_t *queue) {
    const terrain_density_box_t box = pf_tree_box();
    const uint32_t across = box.count_x, floor_size = box.count_x * box.count_y;
    uint32_t tail = 0u, logs = 0u;
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        held[i] = 0u;
        if (pf_block(rig, x, y, z) == log) {
            logs++;
            if (z <= PF_Z) {
                held[i] = 1u;
                queue[tail++] = i;
            }
        }
    }
    for (uint32_t head = 0u; head < tail; head++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, queue[head], &x, &y, &z);
        for (int32_t dz = -1; dz <= 1; dz++) {
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    const int32_t at_x = x + dx - box.origin_x, at_y = y + dy - box.origin_y;
                    const int32_t at_z = z + dz - box.origin_z;
                    if (at_x < 0 || at_y < 0 || at_z < 0 || at_x >= PF_ACROSS || at_y >= PF_ACROSS || at_z >= PF_TALL) {
                        continue;
                    }
                    const uint32_t next = (uint32_t)at_z * floor_size + (uint32_t)at_y * across + (uint32_t)at_x;
                    if (held[next] == 0u && pf_block(rig, x + dx, y + dy, z + dz) == log) {
                        held[next] = 1u;
                        queue[tail++] = next;
                    }
                }
            }
        }
    }
    return logs - tail;
}

static bool pf_is_solid(const pf_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    return (block_defs[pf_block(rig, x, y, z)].flags & BLOCK_FLAG_SOLID) != 0u;
}

/* Vines with nothing beside or over them, or over the strand they hang from. */
static uint32_t pf_vines_floating(const pf_rig_t *rig) {
    const terrain_density_box_t box = pf_tree_box();
    uint32_t floating = 0u;
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        if (pf_block(rig, x, y, z) != BLOCK_ID_VINE) {
            continue;
        }
        bool held = false;
        for (int32_t at = z; !held; at++) {
            held = pf_is_solid(rig, x, y, at + 1) || pf_is_solid(rig, x + 1, y, at) || pf_is_solid(rig, x - 1, y, at) ||
                   pf_is_solid(rig, x, y + 1, at) || pf_is_solid(rig, x, y - 1, at);
            if (pf_block(rig, x, y, at + 1) != BLOCK_ID_VINE) {
                break;
            }
        }
        floating += held ? 0u : 1u;
    }
    return floating;
}

/* The tests above cut at the foot. A person cuts where the eye is, or higher, and on a tree with branches. Above a fork, what the other branch holds
 * stands on the stump and so stays: that is no floating wood. */
TEST(wherever_a_tree_is_cut_no_wood_is_left_floating) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    uint8_t *const held = test_alloc(t, PF_VOXELS);
    uint32_t *const queue = test_alloc(t, PF_VOXELS * sizeof(uint32_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    const terrain_density_box_t box = pf_tree_box();
    uint32_t cuts = 0u;
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        const terrain_feature_tree_t kind = (terrain_feature_tree_t)k;
        const uint16_t log = pf_kind_log(kind);
        for (uint32_t s = 0u; s < PF_SEEDS_CUT; s++) {
            pf_paint(blocks, kind, 0x51ull * s + k, 0, pf_lift_of(kind));
            for (int32_t level = 0; level < PF_CUT_LEVELS; level++) {
                int32_t cut_x = 0, cut_y = 0;
                const int32_t cut_z = PF_Z + level;
                bool found = false;
                for (uint32_t i = 0u; i < PF_VOXELS && !found; i++) {
                    int32_t x = 0, y = 0, z = 0;
                    pf_voxel_place(&box, i, &x, &y, &z);
                    if (blocks[i] == log && z == cut_z) {
                        cut_x = x;
                        cut_y = y;
                        found = true;
                    }
                }
                if (!found) {
                    continue;
                }
                pf_place_tree(t, rig, blocks);
                CHECK(pf_chop(rig, cut_x, cut_y, cut_z));
                CHECK(pf_settle(rig));
                cuts++;
                CHECK_EQ_U64(pf_logs_floating(rig, log, held, queue), 0u);
                CHECK_EQ_U64(pf_vines_floating(rig), 0u);
                uint32_t logs = 0u, leaves = 0u, vines = 0u;
                pf_left(rig, log, cut_z, &logs, &leaves, &vines);
                /* At the foot, and where the eye is: a stump that low touches no leaf. Higher, a stump in the crown keeps what touches it, by the rule of a leaf. */
                if (level <= PF_CUT_LEVEL_EYE) {
                    CHECK_EQ_U64(leaves, 0u);
                }
            }
        }
    }
    printf("    %" PRIu32 " cuts, none left wood floating\n", cuts);
    CHECK(cuts >= TERRAIN_FEATURE_TREES * PF_SEEDS_CUT);
    pf_end(rig);
}

/* ---- With other things going on ----------------------------------------------------------------------------------------- */

TEST(a_second_tree_cut_while_the_first_comes_down_goes_too) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 30, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 30, BLOCK_ID_OAK_LEAVES);
    pf_trunk(t, rig, PF_X + 20, PF_Y, PF_Z, PF_Z + 10, BLOCK_ID_BIRCH_LOG);
    pf_crown(t, rig, PF_X + 20, PF_Y, PF_Z + 10, BLOCK_ID_BIRCH_LEAVES);
    /* A log wall in the middle of it all, which keeps what has been queued from being forgotten. */
    pf_trunk(t, rig, PF_X - 20, PF_Y, PF_Z, PF_Z + 10, BLOCK_ID_SPRUCE_LOG);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    CHECK(player_felling_pending(&rig->felling) > 0u);
    pf_put(t, rig, PF_X - 20, PF_Y, PF_Z, BLOCK_ID_AIR);
    CHECK_EQ_U64(player_felling_cut(&rig->felling, rig->world.map, BLOCK_ID_SPRUCE_LOG, PF_X - 20, PF_Y, PF_Z), 0u);
    CHECK(player_felling_pending(&rig->felling) > 0u);
    pf_put(t, rig, PF_X + 20, PF_Y, PF_Z, BLOCK_ID_AIR);
    CHECK_EQ_U64(player_felling_cut(&rig->felling, rig->world.map, BLOCK_ID_BIRCH_LOG, PF_X + 20, PF_Y, PF_Z), 11u);
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 30), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X + 20, PF_Y, PF_Z + 10), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X + 1, PF_Y, PF_Z + 30), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X + 21, PF_Y, PF_Z + 10), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X - 20, PF_Y, PF_Z + 1), BLOCK_ID_SPRUCE_LOG);
    CHECK_EQ_U64(rig->felled, 40u);
    pf_end(rig);
}

TEST(a_log_the_hands_got_to_first_costs_nothing_more) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 30, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 30, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    /* Gone by someone else's hand, and something else in its place. */
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 20), BLOCK_ID_OAK_LOG);
    pf_put(t, rig, PF_X, PF_Y, PF_Z + 20, BLOCK_ID_STONE);
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 20), BLOCK_ID_STONE);
    CHECK_EQ_U64(rig->felled, 29u);
    pf_end(rig);
}

TEST(what_stood_on_a_log_goes_with_it) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 4, BLOCK_ID_OAK_LOG);
    pf_put(t, rig, PF_X + 1, PF_Y, PF_Z + 4, BLOCK_ID_OAK_LEAVES);
    pf_put(t, rig, PF_X, PF_Y, PF_Z + 5, BLOCK_ID_TORCH);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 5), BLOCK_ID_AIR);
    pf_end(rig);
}

TEST(a_torch_on_a_log_goes_with_it_and_gives_itself_up) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 4, BLOCK_ID_OAK_LOG);
    pf_put(t, rig, PF_X + 1, PF_Y, PF_Z + 4, BLOCK_ID_OAK_LEAVES);
    pf_put(t, rig, PF_X + 1, PF_Y, PF_Z + 3, WALL_TORCH_FACING(BLOCK_FACING_WEST));
    pf_put(t, rig, PF_X - 1, PF_Y, PF_Z + 3, WALL_TORCH_FACING(BLOCK_FACING_EAST));
    pf_put(t, rig, PF_X, PF_Y + 1, PF_Z + 3, WALL_TORCH_FACING(BLOCK_FACING_SOUTH));
    pf_put(t, rig, PF_X, PF_Y - 1, PF_Z + 3, WALL_TORCH_FACING(BLOCK_FACING_NORTH));
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, PF_X + 1, PF_Y, PF_Z + 3), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X - 1, PF_Y, PF_Z + 3), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y + 1, PF_Z + 3), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y - 1, PF_Z + 3), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig->hands.drops_lost_count, 0u);
    CHECK_EQ_U64(rig->torches_dropped, 4u);
    pf_end(rig);
}

/* Five drops to a log, and a full ring loses the oldest: so it waits while no one takes them. */
TEST(a_log_with_a_torch_on_every_side_waits_for_room_in_the_ring_and_nothing_is_lost) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    enum : int32_t { PF_TORCH_LOGS = 12 };
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + PF_TORCH_LOGS - 1, BLOCK_ID_OAK_LOG);
    pf_put(t, rig, PF_X, PF_Y, PF_Z + PF_TORCH_LOGS, BLOCK_ID_OAK_LEAVES);
    for (int32_t z = PF_Z; z < PF_Z + PF_TORCH_LOGS; z++) {
        pf_put(t, rig, PF_X + 1, PF_Y, z, WALL_TORCH_FACING(BLOCK_FACING_WEST));
        pf_put(t, rig, PF_X - 1, PF_Y, z, WALL_TORCH_FACING(BLOCK_FACING_EAST));
        pf_put(t, rig, PF_X, PF_Y + 1, z, WALL_TORCH_FACING(BLOCK_FACING_SOUTH));
        pf_put(t, rig, PF_X, PF_Y - 1, z, WALL_TORCH_FACING(BLOCK_FACING_NORTH));
    }
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    rig->drain = 0u;
    for (uint32_t tick = 0u; tick < 40u; tick++) {
        pf_tick(rig, false);
    }
    CHECK_EQ_U64(rig->hands.drops_lost_count, 0u);
    rig->drain = 1u;
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->hands.drops_lost_count, 0u);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + PF_TORCH_LOGS - 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X + 1, PF_Y, PF_Z + PF_TORCH_LOGS - 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig->torches_dropped, 4u * (uint64_t)PF_TORCH_LOGS);
    pf_end(rig);
}

TEST(a_tree_beside_it_keeps_its_leaves) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 5, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 5, BLOCK_ID_OAK_LEAVES);
    /* Not touching in wood, and its crown inside the box looked at. */
    pf_trunk(t, rig, PF_X + 4, PF_Y, PF_Z, PF_Z + 5, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X + 4, PF_Y, PF_Z + 5, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 5), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X - 1, PF_Y, PF_Z + 5), BLOCK_ID_AIR);
    CHECK_EQ_U64(pf_block(rig, PF_X + 4, PF_Y, PF_Z + 5), BLOCK_ID_OAK_LOG);
    CHECK_EQ_U64(pf_block(rig, PF_X + 3, PF_Y, PF_Z + 5), BLOCK_ID_OAK_LEAVES);
    CHECK_EQ_U64(pf_block(rig, PF_X + 5, PF_Y, PF_Z + 5), BLOCK_ID_OAK_LEAVES);
    pf_end(rig);
}

TEST(the_same_cut_gives_the_same_bytes_and_no_heap) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rigs = test_alloc(t, 2u * sizeof(pf_rig_t));
    pf_paint(blocks, TERRAIN_FEATURE_TREE_CHERRY, 7u, 0, 0u);
    for (uint32_t run = 0u; run < 2u; run++) {
        pf_rig_t *const rig = &rigs[run];
        REQUIRE(pf_begin(t, rig));
        pf_place_tree(t, rig, blocks);
        heap_counters_t before, after;
        heap_counters_read(&before);
        REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
        REQUIRE(pf_settle(rig));
        heap_counters_read(&after);
        CHECK_MEM_EQ(&before, &after, sizeof(before));
    }
    CHECK(rigs[0].felled > 0u);
    CHECK_EQ_U64(rigs[0].felled, rigs[1].felled);
    CHECK_MEM_EQ(&rigs[0].felling, &rigs[1].felling, sizeof(rigs[0].felling));
    CHECK_MEM_EQ(&rigs[0].hands, &rigs[1].hands, sizeof(rigs[0].hands));
    pf_end(&rigs[0]);
    pf_end(&rigs[1]);
}

TEST_MAIN()
