/* ctest run_player_felling. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "heap/index.h"
#include "player/felling/index.h"
#include "player/index.h"
#include "tables/blocks/index.h"
#include "terrain/feature/index.h"
#include "time/index.h"
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
    /* The ground: dirt this deep under the foot, over all that is loaded (3 chunks round the middle one). */
    PF_GROUND_DEEP = 6,
    PF_LOADED_LOW = -48,
    PF_LOADED_HIGH = 63,
    /* How deep a generated trunk goes into the ground (plan 010 phase 01): the first log level with the ground's top. */
    PF_ROOT_DEPTH = 2,
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
    /* The most logs of one layer: a mangrove's trunk and its 4 prop roots, with room over. */
    PF_LAYER_MAX = 8u,
    /* Seeds looked through for a tree of a shape, and cuts timed. */
    PF_SEEDS_SHAPED = 32u,
    PF_FIND_TIMED = 100u,
};

typedef struct pf_rig_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
    player_felling_t felling;   /* 1384 */
    uint64_t logs_dropped;      /* 31768 */
    uint64_t felled;            /* 31776: what player_felling_tick said went */
    uint8_t drain;              /* 31784: whether anyone is taking the drops */
    uint8_t torches_dropped;    /* 31785 */
    uint8_t _pad0[6];           /* 31786 */
} pf_rig_t;
static_assert(sizeof(pf_rig_t) == 31792);

/* The logs of one layer of a trunk, as the feller counts them. */
typedef struct pf_layer_t {
    int32_t x[PF_LAYER_MAX]; /* 0 */
    int32_t y[PF_LAYER_MAX]; /* 32 */
    int32_t z;               /* 64 */
    uint32_t count;          /* 68 */
} pf_layer_t;
static_assert(sizeof(pf_layer_t) == 72);

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
    /* Ground under every column that is loaded, for the roots to stand in. */
    for (int32_t y = PF_LOADED_LOW; y <= PF_LOADED_HIGH; y++) {
        for (int32_t x = PF_LOADED_LOW; x <= PF_LOADED_HIGH; x++) {
            if (world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y)) != nullptr) {
                test_world_fill(t, &rig->world, x, y, PF_Z - PF_GROUND_DEEP, x, y, PF_Z - 1, BLOCK_ID_DIRT);
            }
        }
    }
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

/* A trunk straight up, both ends included. One standing on the ground (from PF_Z) goes on into it as a generated tree's does, so that
 * it holds a root log. */
static void pf_trunk(test_context_t *t, pf_rig_t *rig, int32_t x, int32_t y, int32_t z_first, int32_t z_last,
                     uint16_t block) {
    const int32_t from = z_first == PF_Z ? PF_Z - PF_ROOT_DEPTH : z_first;
    test_world_fill(t, &rig->world, x, y, from, x, y, z_last, block);
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

static void pf_voxel_place(const terrain_density_box_t *box, uint32_t i, int32_t *x, int32_t *y, int32_t *z) {
    *x = box->origin_x + (int32_t)(i % box->count_x);
    *y = box->origin_y + (int32_t)(i / box->count_x % box->count_y);
    *z = box->origin_z + (int32_t)(i / (box->count_x * box->count_y));
}

/* A painted cell, and air outside the box. */
static uint16_t pf_painted_at(const uint16_t *blocks, int32_t x, int32_t y, int32_t z) {
    const terrain_density_box_t box = pf_tree_box();
    const int32_t at_x = x - box.origin_x, at_y = y - box.origin_y, at_z = z - box.origin_z;
    if (at_x < 0 || at_y < 0 || at_z < 0 || at_x >= PF_ACROSS || at_y >= PF_ACROSS || at_z >= PF_TALL) {
        return BLOCK_ID_AIR;
    }
    return blocks[((uint32_t)at_z * (uint32_t)PF_ACROSS + (uint32_t)at_y) * (uint32_t)PF_ACROSS + (uint32_t)at_x];
}

static bool pf_is_ground(uint16_t block) {
    const uint32_t flags = block_defs[block].flags;
    return (flags & BLOCK_FLAG_SOLID) != 0u && (flags & (BLOCK_FLAG_LOG | BLOCK_FLAG_DECAYS)) == 0u;
}

/* The master's root log, read from the picture rather than from the feller: ground under it, and ground or its wood on 3 of 4 sides. */
static bool pf_painted_root(const uint16_t *blocks, int32_t x, int32_t y, int32_t z) {
    const uint16_t log = pf_painted_at(blocks, x, y, z);
    if ((block_defs[log].flags & BLOCK_FLAG_LOG) == 0u || !pf_is_ground(pf_painted_at(blocks, x, y, z - 1))) {
        return false;
    }
    const uint16_t sides[4] = {pf_painted_at(blocks, x + 1, y, z), pf_painted_at(blocks, x - 1, y, z),
                               pf_painted_at(blocks, x, y + 1, z), pf_painted_at(blocks, x, y - 1, z)};
    uint32_t held = 0u;
    for (uint32_t i = 0u; i < 4u; i++) {
        held += sides[i] == log || pf_is_ground(sides[i]) ? 1u : 0u;
    }
    return held >= 3u;
}

/* Whether this column of the picture holds a root log. */
static bool pf_painted_trunk_column(const uint16_t *blocks, int32_t x, int32_t y) {
    for (int32_t z = PF_Z - 8; z < PF_Z - 8 + PF_TALL; z++) {
        if (pf_painted_root(blocks, x, y, z)) {
            return true;
        }
    }
    return false;
}

/* The trunk's logs at height z in the picture: one tree, so every trunk column is its trunk. */
static void pf_layer_of(const uint16_t *blocks, uint16_t log, int32_t z, pf_layer_t *layer) {
    const terrain_density_box_t box = pf_tree_box();
    memset(layer, 0, sizeof(*layer));
    layer->z = z;
    for (int32_t y = box.origin_y; y < box.origin_y + PF_ACROSS; y++) {
        for (int32_t x = box.origin_x; x < box.origin_x + PF_ACROSS; x++) {
            if (pf_painted_at(blocks, x, y, z) == log && layer->count < PF_LAYER_MAX &&
                pf_painted_trunk_column(blocks, x, y)) {
                layer->x[layer->count] = x;
                layer->y[layer->count] = y;
                layer->count++;
            }
        }
    }
}

/* Where the ground's top is in a painted column: the trunk's own column at the site, the others at the feet. */
static int32_t pf_ground_top(int32_t x, int32_t y, int16_t foot) {
    return x == PF_X && y == PF_Y ? PF_Z - 1 : (int32_t)foot;
}

/* foot_gap is how far the ground falls away beside it: 0 leaves every log over the ground at or above the lowest of the middle.
 * Plan 010 phase 01: the tree stands on dirt, its own column's top at PF_Z - 1 and every other column's at its foot, as the land
 * has it, so its trunk columns root into the ground as a generated tree's do. */
static void pf_paint(uint16_t *blocks, terrain_feature_tree_t kind, uint64_t seed, int16_t foot_gap, uint8_t lift) {
    const terrain_density_box_t box = pf_tree_box();
    const terrain_feature_canvas_t canvas = terrain_feature_canvas_of(&box, blocks);
    const int16_t foot = (int16_t)(PF_Z - 1 - foot_gap);
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        blocks[i] = z <= pf_ground_top(x, y, foot) ? BLOCK_ID_DIRT : BLOCK_ID_AIR;
    }
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

/* Logs of this wood in the picture with z from z_low to z_high, both included. */
static uint32_t pf_painted_logs_between(const uint16_t *blocks, uint16_t log, int32_t z_low, int32_t z_high) {
    const terrain_density_box_t box = pf_tree_box();
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        count += blocks[i] == log && z >= z_low && z <= z_high ? 1u : 0u;
    }
    return count;
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

/* Logs of this wood standing in the box with z from z_low to z_high, both included. */
static uint32_t pf_logs_between(const pf_rig_t *rig, uint16_t log, int32_t z_low, int32_t z_high) {
    const terrain_density_box_t box = pf_tree_box();
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        count += z >= z_low && z <= z_high && pf_block(rig, x, y, z) == log ? 1u : 0u;
    }
    return count;
}

/* Every log of a layer, by the hands, in the order the layer has them. */
static bool pf_chop_layer(pf_rig_t *rig, const pf_layer_t *layer) {
    bool chopped = layer->count > 0u;
    for (uint32_t i = 0u; i < layer->count; i++) {
        /* One the hands took already, as a test cutting the trunk first does. */
        if (pf_block(rig, layer->x[i], layer->y[i], layer->z) != BLOCK_ID_AIR) {
            chopped = pf_chop(rig, layer->x[i], layer->y[i], layer->z) && chopped;
        }
    }
    return chopped;
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

/* Cut through its first layer over the ground, every log of it: every log above goes, each once and with its drop, and the leaves and
 * vines after them. The stump stays: the roots of every trunk column, the top one level with the ground. */
static void pf_fell_one(test_context_t *t, pf_rig_t *rig, uint16_t *blocks, terrain_feature_tree_t kind,
                        uint64_t seed) {
    const uint16_t log = pf_kind_log(kind);
    pf_paint(blocks, kind, seed, 0, pf_lift_of(kind));
    pf_layer_t layer;
    pf_layer_of(blocks, log, PF_Z, &layer);
    const uint32_t above = pf_painted_logs_between(blocks, log, PF_Z + 1, INT32_MAX);
    const uint32_t stump = pf_painted_logs_between(blocks, log, INT32_MIN, PF_Z - 1);
    CHECK(layer.count >= 1u);
    CHECK_EQ_U64(stump, (uint64_t)layer.count * PF_ROOT_DEPTH);
    pf_place_tree(t, rig, blocks);
    const uint64_t dropped = rig->logs_dropped, felled = rig->felled, mined = rig->hands.mined_count;
    CHECK(pf_chop_layer(rig, &layer));
    CHECK(pf_settle(rig));
    uint32_t logs_left = 0u, leaves_left = 0u, vines_left = 0u;
    pf_left(rig, log, PF_Z, &logs_left, &leaves_left, &vines_left);
    CHECK_EQ_U64(logs_left, 0u);
    CHECK_EQ_U64(leaves_left, 0u);
    CHECK_EQ_U64(vines_left, 0u);
    CHECK_EQ_U64(rig->logs_dropped - dropped, above + layer.count);
    CHECK_EQ_U64(rig->felled - felled, above);
    CHECK(rig->hands.mined_count - mined >= above + layer.count);
    CHECK_EQ_U64(rig->hands.drops_lost_count, 0u);
    CHECK_EQ_U64(pf_logs_between(rig, log, INT32_MIN, PF_Z - 1), stump);
    for (uint32_t i = 0u; i < layer.count; i++) {
        CHECK_EQ_U64(pf_block(rig, layer.x[i], layer.y[i], PF_Z - 1), log);
        CHECK_EQ_U64(pf_block(rig, layer.x[i], layer.y[i], PF_Z - 2), log);
    }
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
    CHECK_EQ_U64(rig->felling.find_overflow_count, 0u);
    CHECK_EQ_U64(rig->felling.find_unloaded_count, 0u);
    pf_end(rig);
}

/* Each kind's stump, on a few more seeds: what pf_fell_one checks of it, by name. */
TEST(the_stump_is_flush_and_rooted) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        for (uint32_t s = 0u; s < 4u; s++) {
            pf_fell_one(t, rig, blocks, (terrain_feature_tree_t)k, 0x3Bull * s + 7u * k + 1000u);
        }
    }
    pf_end(rig);
}

/* Thin trunks cut at each height of the trunk: everything above goes, everything under stays, roots and all. */
TEST(a_thin_trunk_falls_when_its_one_log_layer_is_cut) {
    static const terrain_feature_tree_t KINDS[] = {TERRAIN_FEATURE_TREE_OAK, TERRAIN_FEATURE_TREE_BIRCH,
                                                   TERRAIN_FEATURE_TREE_SPRUCE, TERRAIN_FEATURE_TREE_JUNGLE,
                                                   TERRAIN_FEATURE_TREE_CHERRY};
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    uint32_t cuts = 0u;
    for (uint32_t k = 0u; k < sizeof(KINDS) / sizeof(KINDS[0]); k++) {
        const uint16_t log = pf_kind_log(KINDS[k]);
        for (uint32_t s = 0u, painted = 0u; s < PF_SEEDS_SHAPED && painted < 2u; s++) {
            const uint64_t seed = 0x77ull * s + k;
            if (terrain_feature_tree_is_wide(KINDS[k], seed)) {
                continue;
            }
            painted++;
            pf_paint(blocks, KINDS[k], seed, 0, 0u);
            for (int32_t h = PF_Z - 1; pf_painted_at(blocks, PF_X, PF_Y, h) == log; h++) {
                pf_layer_t layer;
                pf_layer_of(blocks, log, h, &layer);
                CHECK_EQ_U64(layer.count, 1u);
                const uint32_t under = pf_painted_logs_between(blocks, log, INT32_MIN, h - 1);
                pf_place_tree(t, rig, blocks);
                CHECK(pf_chop_layer(rig, &layer));
                CHECK(pf_settle(rig));
                CHECK_EQ_U64(pf_logs_between(rig, log, h, INT32_MAX), 0u);
                CHECK_EQ_U64(pf_logs_between(rig, log, INT32_MIN, h - 1), under);
                cuts++;
            }
        }
    }
    printf("    %" PRIu32 " cuts of a thin trunk\n", cuts);
    CHECK(cuts > 20u);
    pf_end(rig);
}

/* A painted wide tree of this kind, its seed out. */
static bool pf_wide_seed(terrain_feature_tree_t kind, uint64_t from, uint64_t *seed) {
    for (uint64_t s = from; s < from + PF_SEEDS_SHAPED; s++) {
        if (terrain_feature_tree_is_wide(kind, s)) {
            *seed = s;
            return true;
        }
    }
    return false;
}

/* Ahmed's 10.png: a notch in a 2 by 2 layer, and the tree standing. All 24 orders of its 4 cuts. */
TEST(a_wide_trunk_stands_until_its_fourth_log_in_every_order) {
    static const terrain_feature_tree_t KINDS[2] = {TERRAIN_FEATURE_TREE_JUNGLE, TERRAIN_FEATURE_TREE_DARK_OAK};
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    for (uint32_t k = 0u; k < 2u; k++) {
        const uint16_t log = pf_kind_log(KINDS[k]);
        uint64_t seed = 0u;
        REQUIRE(pf_wide_seed(KINDS[k], 40u + k, &seed));
        pf_paint(blocks, KINDS[k], seed, 0, 0u);
        const int32_t h = PF_Z + 1;
        pf_layer_t layer;
        pf_layer_of(blocks, log, h, &layer);
        REQUIRE(layer.count == 4u);
        const uint32_t above = pf_painted_logs_between(blocks, log, h + 1, INT32_MAX);
        const uint32_t under = pf_painted_logs_between(blocks, log, INT32_MIN, h - 1);
        for (uint32_t order = 0u; order < 24u; order++) {
            /* The order-th permutation of 0 to 3, by the factorial number system. */
            uint32_t left[4] = {0u, 1u, 2u, 3u}, picked[4] = {0u, 0u, 0u, 0u};
            for (uint32_t i = 0u, rest = order, size = 4u; i < 4u; i++, size--) {
                const uint32_t radix = i == 0u ? 6u : i == 1u ? 2u : 1u;
                const uint32_t at = rest / radix;
                rest %= radix;
                picked[i] = left[at];
                for (uint32_t j = at; j + 1u < size; j++) {
                    left[j] = left[j + 1u];
                }
            }
            pf_place_tree(t, rig, blocks);
            const uint64_t felled = rig->felled;
            for (uint32_t i = 0u; i < 4u; i++) {
                CHECK(pf_chop(rig, layer.x[picked[i]], layer.y[picked[i]], h));
                CHECK(pf_settle(rig));
                if (i < 3u) {
                    CHECK_EQ_U64(rig->felled, felled);
                    CHECK_EQ_U64(pf_logs_between(rig, log, h + 1, INT32_MAX), above);
                }
            }
            CHECK_EQ_U64(rig->felled - felled, above);
            CHECK_EQ_U64(pf_logs_between(rig, log, h, INT32_MAX), 0u);
            CHECK_EQ_U64(pf_logs_between(rig, log, INT32_MIN, h - 1), under);
        }
    }
    pf_end(rig);
}

/* A wide jungle branch's first log is beside the trunk at from + 1, and joins the trunk at from by a corner. Cut the layer at from + 1:
 * the crown falls, though that branch log would join it to the stump, and the branch log stays with the stump. So does the rest of
 * that branch, which only its first log joins to anything (master 2.3: what falls is what a path above the layer joins to the trunk
 * above it): at most its 2 more logs each, none in the trunk's columns. */
TEST(a_branch_beside_the_layer_does_not_hold_the_crown) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    const terrain_density_box_t box = pf_tree_box();
    uint32_t tried = 0u;
    for (uint64_t s = 0u; s < PF_SEEDS_SHAPED; s++) {
        if (!terrain_feature_tree_is_wide(TERRAIN_FEATURE_TREE_JUNGLE, s)) {
            continue;
        }
        pf_paint(blocks, TERRAIN_FEATURE_TREE_JUNGLE, s, 0, 0u);
        /* The lowest branch log: outside the trunk's 4 columns. */
        int32_t branch_x = 0, branch_y = 0, branch_z = INT32_MAX;
        for (uint32_t i = 0u; i < PF_VOXELS; i++) {
            int32_t x = 0, y = 0, z = 0;
            pf_voxel_place(&box, i, &x, &y, &z);
            const bool trunk = (x == PF_X || x == PF_X + 1) && (y == PF_Y || y == PF_Y + 1);
            if (blocks[i] == BLOCK_ID_JUNGLE_LOG && !trunk && z < branch_z) {
                branch_x = x;
                branch_y = y;
                branch_z = z;
            }
        }
        pf_layer_t layer;
        pf_layer_of(blocks, BLOCK_ID_JUNGLE_LOG, branch_z, &layer);
        if (branch_z == INT32_MAX || layer.count != 4u) {
            continue;
        }
        tried++;
        pf_place_tree(t, rig, blocks);
        CHECK(pf_chop_layer(rig, &layer));
        CHECK(pf_settle(rig));
        /* Branches starting at the same height each keep theirs. */
        const uint32_t starts = pf_painted_logs_between(blocks, BLOCK_ID_JUNGLE_LOG, branch_z, branch_z) - layer.count;
        CHECK(pf_logs_between(rig, BLOCK_ID_JUNGLE_LOG, branch_z + 1, INT32_MAX) <= 2u * starts);
        for (int32_t z = branch_z; z < PF_Z + PF_TALL; z++) {
            for (uint32_t i = 0u; i < layer.count; i++) {
                CHECK_EQ_U64(pf_block(rig, layer.x[i], layer.y[i], z), BLOCK_ID_AIR);
            }
        }
        CHECK_EQ_U64(pf_block(rig, branch_x, branch_y, branch_z), BLOCK_ID_JUNGLE_LOG);
    }
    printf("    %" PRIu32 " wide jungle trees cut beside a branch\n", tried);
    CHECK(tried >= 8u);
    pf_end(rig);
}

/* Logs of this wood standing in the box, sorted by where they are against the trunk's column: in it, or one way or the other along
 * (dx, dy). */
static void pf_logs_by_side(const pf_rig_t *rig, uint16_t log, int32_t dx, int32_t dy, uint32_t *in_trunk,
                            uint32_t *this_way, uint32_t *other_way) {
    const terrain_density_box_t box = pf_tree_box();
    *in_trunk = 0u;
    *this_way = 0u;
    *other_way = 0u;
    for (uint32_t i = 0u; i < PF_VOXELS; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        if (pf_block(rig, x, y, z) != log) {
            continue;
        }
        const int32_t along = (x - PF_X) * dx + (y - PF_Y) * dy;
        *in_trunk += x == PF_X && y == PF_Y ? 1u : 0u;
        *this_way += along > 0 ? 1u : 0u;
        *other_way += along < 0 ? 1u : 0u;
    }
}

/* The lowest log of the picture outside the trunk's column, or false. */
static bool pf_lowest_off_trunk(const uint16_t *blocks, uint16_t log, int32_t *out_x, int32_t *out_y, int32_t *out_z) {
    const terrain_density_box_t box = pf_tree_box();
    bool found = false;
    for (uint32_t i = 0u; i < PF_VOXELS && !found; i++) {
        int32_t x = 0, y = 0, z = 0;
        pf_voxel_place(&box, i, &x, &y, &z);
        if (blocks[i] == log && (x != PF_X || y != PF_Y)) {
            *out_x = x;
            *out_y = y;
            *out_z = z;
            found = true;
        }
    }
    return found;
}

/* An acacia's lean holds its crown up off the straight trunk. Cut the lean's first log: the lean and its crown go, the trunk stays. */
TEST(an_acacia_cut_at_its_lean_drops_the_crown_and_keeps_the_trunk) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    for (uint64_t s = 0u; s < PF_SEEDS_SHAPED; s++) {
        pf_paint(blocks, TERRAIN_FEATURE_TREE_ACACIA, 0x101ull * s, 0, 0u);
        int32_t x = 0, y = 0, z = 0;
        REQUIRE(pf_lowest_off_trunk(blocks, BLOCK_ID_ACACIA_LOG, &x, &y, &z));
        pf_place_tree(t, rig, blocks);
        uint32_t trunk_before = 0u, this_way = 0u, other_way = 0u;
        pf_logs_by_side(rig, BLOCK_ID_ACACIA_LOG, x - PF_X, y - PF_Y, &trunk_before, &this_way, &other_way);
        CHECK(this_way >= 1u);
        CHECK(pf_chop(rig, x, y, z));
        CHECK(pf_settle(rig));
        uint32_t trunk_after = 0u;
        pf_logs_by_side(rig, BLOCK_ID_ACACIA_LOG, x - PF_X, y - PF_Y, &trunk_after, &this_way, &other_way);
        CHECK_EQ_U64(trunk_after, trunk_before);
        CHECK_EQ_U64(this_way, 0u);
        CHECK_EQ_U64(other_way, 0u);
    }
    pf_end(rig);
}

/* A cherry's two boughs go opposite ways from the top of its trunk. Cut one at its first log: that bough goes, the other and the trunk
 * stay. */
TEST(a_cherry_bough_cut_drops_only_that_bough) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    for (uint64_t s = 0u; s < PF_SEEDS_SHAPED; s++) {
        pf_paint(blocks, TERRAIN_FEATURE_TREE_CHERRY, 0x89ull * s + 3u, 0, 0u);
        int32_t x = 0, y = 0, z = 0;
        REQUIRE(pf_lowest_off_trunk(blocks, BLOCK_ID_CHERRY_LOG, &x, &y, &z));
        pf_place_tree(t, rig, blocks);
        uint32_t trunk_before = 0u, this_way = 0u, other_before = 0u;
        pf_logs_by_side(rig, BLOCK_ID_CHERRY_LOG, x - PF_X, y - PF_Y, &trunk_before, &this_way, &other_before);
        CHECK(this_way >= 2u);
        CHECK(other_before >= 2u);
        CHECK(pf_chop(rig, x, y, z));
        CHECK(pf_settle(rig));
        uint32_t trunk_after = 0u, other_after = 0u;
        pf_logs_by_side(rig, BLOCK_ID_CHERRY_LOG, x - PF_X, y - PF_Y, &trunk_after, &this_way, &other_after);
        CHECK_EQ_U64(trunk_after, trunk_before);
        CHECK_EQ_U64(this_way, 0u);
        CHECK_EQ_U64(other_after, other_before);
    }
    pf_end(rig);
}

/* A mangrove's prop roots are trunk columns: at a height they reach, its layer is the trunk and them. Its trunk alone fells nothing. */
TEST(a_mangrove_layer_holds_its_prop_roots) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    uint32_t tried = 0u;
    const uint8_t lift = pf_lift_of(TERRAIN_FEATURE_TREE_MANGROVE);
    for (uint64_t s = 0u; s < PF_SEEDS_SHAPED; s++) {
        pf_paint(blocks, TERRAIN_FEATURE_TREE_MANGROVE, 0x2Dull * s + 6u, 0, lift);
        const int32_t h = PF_Z + 2;
        pf_layer_t layer;
        pf_layer_of(blocks, BLOCK_ID_MANGROVE_LOG, h, &layer);
        if (layer.count < 3u) {
            continue;
        }
        tried++;
        const uint32_t above = pf_painted_logs_between(blocks, BLOCK_ID_MANGROVE_LOG, h + 1, INT32_MAX);
        pf_place_tree(t, rig, blocks);
        const uint64_t felled = rig->felled;
        CHECK(pf_chop(rig, PF_X, PF_Y, h));
        CHECK(pf_settle(rig));
        CHECK_EQ_U64(rig->felled, felled);
        CHECK_EQ_U64(pf_logs_between(rig, BLOCK_ID_MANGROVE_LOG, h + 1, INT32_MAX), above);
        CHECK(pf_chop_layer(rig, &layer));
        CHECK(pf_settle(rig));
        CHECK_EQ_U64(rig->felled - felled, above);
        CHECK_EQ_U64(pf_logs_between(rig, BLOCK_ID_MANGROVE_LOG, h, INT32_MAX), 0u);
    }
    printf("    %" PRIu32 " mangroves with 2 or more prop roots\n", tried);
    CHECK(tried >= 4u);
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
    /* All four, the middle's own first: it stands on a pillar of ground, no root log, so its cut is a branch's. */
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_chop(rig, PF_X + 1, PF_Y, PF_Z));
    REQUIRE(pf_chop(rig, PF_X, PF_Y + 1, PF_Z));
    REQUIRE(pf_chop(rig, PF_X + 1, PF_Y + 1, PF_Z));
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

/* Six logs put on the ground, leaves round the top: a tower a player made. No log of it is set in the ground, so it is no tree. */
TEST(a_log_tower_with_leaves_is_never_felled) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    test_world_fill(t, &rig->world, PF_X, PF_Y, PF_Z, PF_X, PF_Y, PF_Z + 5, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 5, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z + 2));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->felled, 0u);
    CHECK_EQ_U64(rig->felling.not_a_tree_count, 1u);
    for (int32_t z = PF_Z; z <= PF_Z + 5; z++) {
        CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, z), z == PF_Z + 2 ? BLOCK_ID_AIR : BLOCK_ID_OAK_LOG);
    }
    pf_end(rig);
}

/* The stump left by a fall is logs set in the ground with no leaves: cutting one takes that one. */
TEST(a_stump_loses_only_the_log_cut) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 6, BLOCK_ID_OAK_LOG);
    pf_crown(t, rig, PF_X, PF_Y, PF_Z + 6, BLOCK_ID_OAK_LEAVES);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->felled, 6u);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z - 1), BLOCK_ID_OAK_LOG);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z - 1));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->felled, 6u);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z - 2), BLOCK_ID_OAK_LOG);
    CHECK_EQ_U64(rig->logs_dropped, 8u);
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
    pf_layer_t layer;
    pf_layer_of(blocks, BLOCK_ID_JUNGLE_LOG, PF_Z, &layer);
    REQUIRE(layer.count == 4u);
    /* The layer cut by hand, and every log above it felled; the stump stays. */
    const uint32_t logs = pf_painted_logs_between(blocks, BLOCK_ID_JUNGLE_LOG, PF_Z, INT32_MAX);
    REQUIRE(logs > PLAYER_INTERACTION_DROPS);
    REQUIRE(logs <= biggest);
    pf_place_tree(t, rig, blocks);
    /* Nobody takes them: it holds back at a full ring, loses nothing, and goes on when someone comes. */
    rig->drain = 0u;
    REQUIRE(pf_chop_layer(rig, &layer));
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

/* A rooted trunk under a block of logs side by side, size by size by height, with leaves on top. */
static void pf_log_block(test_context_t *t, pf_rig_t *rig, int32_t size, int32_t height) {
    pf_trunk(t, rig, PF_X, PF_Y, PF_Z, PF_Z + 3, BLOCK_ID_OAK_LOG);
    test_world_fill(t, &rig->world, PF_X - size / 2, PF_Y - size / 2, PF_Z + 4, PF_X - size / 2 + size - 1,
                    PF_Y - size / 2 + size - 1, PF_Z + 3 + height, BLOCK_ID_OAK_LOG);
    pf_put(t, rig, PF_X, PF_Y, PF_Z + 4 + height, BLOCK_ID_OAK_LEAVES);
}

/* Past the find's limit, the find is unsure: only the cut goes. */
TEST(past_the_finds_limit_nothing_falls) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    /* 11 by 11 by 9 is 1089 logs, over the 1024. */
    pf_log_block(t, rig, 11, 9);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z + 1));
    CHECK_EQ_U64(player_felling_pending(&rig->felling), 0u);
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->felling.find_overflow_count, 1u);
    CHECK_EQ_U64(rig->felled, 0u);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 2), BLOCK_ID_OAK_LOG);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 12), BLOCK_ID_OAK_LOG);
    pf_end(rig);
}

/* More falling than the queue holds: none of it falls, as half a tree would float. */
TEST(past_the_queues_limit_nothing_falls) {
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    /* 11 by 11 by 3 is 363 logs, over the 256 the queue holds and under the 1024 the find does. */
    pf_log_block(t, rig, 11, 3);
    REQUIRE(pf_chop(rig, PF_X, PF_Y, PF_Z + 1));
    REQUIRE(pf_settle(rig));
    CHECK_EQ_U64(rig->felling.find_overflow_count, 0u);
    CHECK(rig->felling.turned_away_count > PLAYER_FELLING_LOGS_MAX);
    CHECK_EQ_U64(rig->felled, 0u);
    CHECK_EQ_U64(pf_block(rig, PF_X, PF_Y, PF_Z + 2), BLOCK_ID_OAK_LOG);
    pf_end(rig);
}

/* Against what is not loaded, which might be more of the tree or might hold it up, the find is unsure: only the cut goes, and the
 * leaves stay for the rule of a leaf. */
TEST(a_column_not_loaded_fells_nothing) {
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
    CHECK_EQ_U64(rig->felling.find_unloaded_count, 1u);
    CHECK_EQ_U64(rig->felled, 0u);
    CHECK_EQ_U64(pf_block(rig, edge, PF_Y, PF_Z), BLOCK_ID_AIR);
    for (int32_t z = PF_Z + 1; z <= PF_Z + 5; z++) {
        CHECK_EQ_U64(pf_block(rig, edge, PF_Y, z), BLOCK_ID_OAK_LOG);
    }
    CHECK_EQ_U64(pf_block(rig, edge, PF_Y + 1, PF_Z + 5), BLOCK_ID_OAK_LEAVES);
    CHECK_EQ_U64(pf_block(rig, edge - 1, PF_Y, PF_Z + 5), BLOCK_ID_OAK_LEAVES);
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
                const int32_t cut_z = PF_Z + level;
                /* The trunk's whole layer there; or, above the trunk, one log of a branch. */
                pf_layer_t layer;
                pf_layer_of(blocks, log, cut_z, &layer);
                bool found = layer.count > 0u;
                for (uint32_t i = 0u; i < PF_VOXELS && !found; i++) {
                    int32_t x = 0, y = 0, z = 0;
                    pf_voxel_place(&box, i, &x, &y, &z);
                    if (blocks[i] == log && z == cut_z) {
                        layer.x[0] = x;
                        layer.y[0] = y;
                        layer.count = 1u;
                        found = true;
                    }
                }
                if (!found) {
                    continue;
                }
                pf_place_tree(t, rig, blocks);
                CHECK(pf_chop_layer(rig, &layer));
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

/* Master 4: the find reads at most 1024 logs' 26 neighbours. Timed on the biggest tree of every kind, 100 cuts each, with the first log
 * of its trunk over the ground cut. Under 1 ms in the release build; the debug build's only printed. */
TEST(the_find_takes_under_a_millisecond) {
    uint16_t *const blocks = test_alloc(t, PF_VOXELS * sizeof(uint16_t));
    pf_rig_t *const rig = test_alloc(t, sizeof(pf_rig_t));
    REQUIRE(pf_begin(t, rig));
    int64_t worst_ns = 0;
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        const terrain_feature_tree_t kind = (terrain_feature_tree_t)k;
        const uint16_t log = pf_kind_log(kind);
        uint64_t seed = 0u;
        (void)pf_biggest_of(blocks, kind, &seed);
        pf_paint(blocks, kind, seed, 0, pf_lift_of(kind));
        pf_place_tree(t, rig, blocks);
        pf_put(t, rig, PF_X, PF_Y, PF_Z, BLOCK_ID_AIR);
        int64_t sum_ns = 0, kind_worst_ns = 0;
        uint32_t found = 0u;
        for (uint32_t i = 0u; i < PF_FIND_TIMED; i++) {
            player_felling_start(&rig->felling);
            const int64_t started = time_now_ns();
            const uint32_t queued = player_felling_cut(&rig->felling, rig->world.map, log, PF_X, PF_Y, PF_Z);
            const int64_t spent = time_now_ns() - started;
            CHECK(queued >= 1u);
            found = rig->felling.found_count;
            sum_ns += spent;
            kind_worst_ns = spent > kind_worst_ns ? spent : kind_worst_ns;
        }
        printf("    kind %" PRIu32 ": %" PRIu32 " logs found, mean %" PRId64 " ns, worst %" PRId64 " ns\n", k, found,
               sum_ns / (int64_t)PF_FIND_TIMED, kind_worst_ns);
        worst_ns = kind_worst_ns > worst_ns ? kind_worst_ns : worst_ns;
    }
    player_felling_start(&rig->felling);
#if defined(NDEBUG)
    CHECK(worst_ns < 1000000);
#endif
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
