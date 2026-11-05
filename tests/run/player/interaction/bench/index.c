/* ctest run_player_interaction_bench. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "player/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "world/bench/index.h"
#include "world/index.h"

#include <stdint.h>
#include <string.h>

typedef struct ib_rig_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
} ib_rig_t;
static_assert(sizeof(ib_rig_t) == 1384);

enum : int32_t {
    /* A place to stand well away from what is being done. */
    IB_FAR = 40,
    /* The last x with a column loaded, along y = IB_EDGE_Y. See TEST_WORLD_RADIUS. */
    IB_EDGE_X = 63,
    IB_EDGE_Y = 8,
};

static bool ib_begin(test_context_t *t, ib_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    player_movement_start(&rig->movement, (float64_t)IB_FAR + 0.5f64, (float64_t)IB_FAR + 0.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    memset(&rig->target, 0, sizeof(rig->target));
    player_interaction_start(&rig->hands, 1234u);
    return true;
}

static uint16_t ib_block(ib_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = UINT16_MAX;
    (void)world_map_block_get(rig->world.map, x, y, z, &block);
    return block;
}

static void ib_put(test_context_t *t, ib_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    CHECK_EQ_U64(world_edit_block_set(&rig->world.world->edit, x, y, z, block), WORLD_EDIT_STATUS_CHANGED);
}

/* On the top of this cell. */
static void ib_aim(ib_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    rig->target.hit.block = ib_block(rig, x, y, z);
    rig->target.hit.axis = COLLISION_AXIS_Z;
    rig->target.hit.normal_sign = 1;
}

static void ib_tick(ib_rig_t *rig, bool attack, bool place) {
    const player_interaction_input_t input = {.borrowed_target = &rig->target,
                                              .borrowed_movement = &rig->movement,
                                              .attack = attack ? 1u : 0u,
                                              .place = place ? 1u : 0u,
                                              ._pad0 = {0}};
    player_interaction_tick(&rig->hands, &rig->world.world->edit, &input);
}

/* One press of the right button with a bench in hand, on the top of a stone it puts there. */
static void ib_place_on(test_context_t *t, ib_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    ib_put(t, rig, x, y, z, BLOCK_ID_STONE);
    player_interaction_start(&rig->hands, 1u);
    player_interaction_hand_give(&rig->hands, BLOCK_ID_WOODWORKING_BENCH_WEST, 2u);
    ib_aim(rig, x, y, z);
    ib_tick(rig, false, true);
}

/* Holds the left button on a block until it goes, or for as long as it could take. */
static void ib_mine(ib_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    ib_aim(rig, x, y, z);
    const uint16_t block = ib_block(rig, x, y, z);
    for (uint32_t tick = 0u; tick < 400u && ib_block(rig, x, y, z) == block; tick++) {
        ib_tick(rig, true, false);
    }
}

/* What is waiting in the ring: how many of this item, and how many of anything else. */
static uint32_t ib_drops_count(ib_rig_t *rig, uint16_t item, uint32_t *out_others) {
    uint32_t count = 0u, others = 0u;
    player_drop_t drop;
    while (player_interaction_drop_next(&rig->hands, &drop)) {
        if (block_drop_items[drop.item].item == item) {
            count += drop.count;
        } else {
            others += drop.count;
        }
    }
    *out_others = others;
    return count;
}

/* Nothing was placed and nothing was used. */
static void ib_check_refused(test_context_t *t, ib_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    CHECK_EQ_U64(ib_block(rig, x, y, z), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig->hands.hand_count, 2u);
    CHECK_EQ_U64(rig->hands.placed_count, 0u);
}

TEST(each_half_knows_the_other_and_nothing_else_is_half_of_anything) {
    int32_t step = 0;
    uint16_t other = BLOCK_ID_AIR;
    REQUIRE(bench_half_other(BLOCK_ID_WOODWORKING_BENCH_WEST, &step, &other));
    CHECK_EQ_I64(step, 1);
    CHECK_EQ_U64(other, BLOCK_ID_WOODWORKING_BENCH_EAST);
    REQUIRE(bench_half_other(BLOCK_ID_WOODWORKING_BENCH_EAST, &step, &other));
    CHECK_EQ_I64(step, -1);
    CHECK_EQ_U64(other, BLOCK_ID_WOODWORKING_BENCH_WEST);
    uint32_t halves = 0u;
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        halves += bench_half_other((uint16_t)block, &step, &other) ? 1u : 0u;
    }
    CHECK_EQ_U64(halves, 2u);
}

TEST(it_goes_west_where_aimed_and_east_beside_it_for_one_item) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    ib_place_on(t, &rig, 3, 3, 5);
    CHECK_EQ_U64(ib_block(&rig, 3, 3, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(ib_block(&rig, 4, 3, 6), BLOCK_ID_WOODWORKING_BENCH_EAST);
    CHECK_EQ_U64(ib_block(&rig, 2, 3, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(ib_block(&rig, 3, 4, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(ib_block(&rig, 3, 3, 7), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig.hands.hand_count, 1u);
    CHECK_EQ_U64(rig.hands.placed_count, 1u);
    test_world_end(&rig.world);
}

TEST(a_block_in_the_east_cell_refuses_and_keeps_the_item) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    ib_put(t, &rig, 4, 3, 6, BLOCK_ID_STONE);
    ib_place_on(t, &rig, 3, 3, 5);
    ib_check_refused(t, &rig, 3, 3, 6);
    CHECK_EQ_U64(ib_block(&rig, 4, 3, 6), BLOCK_ID_STONE);
    /* A bench is no more room than a stone, and half of one is not made into another. */
    ib_put(t, &rig, 4, 3, 6, BLOCK_ID_WOODWORKING_BENCH_WEST);
    player_interaction_start(&rig.hands, 1u);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_WOODWORKING_BENCH_WEST, 2u);
    ib_tick(&rig, false, true);
    ib_check_refused(t, &rig, 3, 3, 6);
    CHECK_EQ_U64(ib_block(&rig, 4, 3, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    test_world_end(&rig.world);
}

TEST(aimed_into_what_is_there_a_block_in_either_cell_refuses) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    /* Tall grass is overwritten where it is aimed at, so that the west half goes in its cell, and the stone beside it says no. */
    ib_put(t, &rig, 3, 3, 6, BLOCK_ID_TALL_GRASS);
    ib_put(t, &rig, 4, 3, 6, BLOCK_ID_STONE);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_WOODWORKING_BENCH_WEST, 2u);
    ib_aim(&rig, 3, 3, 6);
    ib_tick(&rig, false, true);
    CHECK_EQ_U64(ib_block(&rig, 3, 3, 6), BLOCK_ID_TALL_GRASS);
    CHECK_EQ_U64(rig.hands.hand_count, 2u);
    CHECK_EQ_U64(rig.hands.placed_count, 0u);
    test_world_end(&rig.world);
}

TEST(what_can_be_overwritten_is_overwritten_in_either_cell) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    ib_put(t, &rig, 4, 3, 6, BLOCK_ID_TALL_GRASS);
    ib_place_on(t, &rig, 3, 3, 5);
    CHECK_EQ_U64(ib_block(&rig, 3, 3, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(ib_block(&rig, 4, 3, 6), BLOCK_ID_WOODWORKING_BENCH_EAST);
    CHECK_EQ_U64(rig.hands.hand_count, 1u);
    /* And water: it is solid, and takes the place of what flows. */
    ib_put(t, &rig, 8, 3, 6, BLOCK_ID_WATER);
    ib_place_on(t, &rig, 7, 3, 5);
    CHECK_EQ_U64(ib_block(&rig, 7, 3, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(ib_block(&rig, 8, 3, 6), BLOCK_ID_WOODWORKING_BENCH_EAST);
    test_world_end(&rig.world);
}

TEST(the_player_in_either_cell_refuses) {
    static const float64_t standing_x[2] = {3.5f64, 4.5f64};
    for (uint32_t i = 0u; i < 2u; i++) {
        ib_rig_t rig;
        REQUIRE(ib_begin(t, &rig));
        player_movement_start(&rig.movement, standing_x[i], 3.5f64, 6.0f64);
        rig.movement.on_ground = 1u;
        ib_place_on(t, &rig, 3, 3, 5);
        ib_check_refused(t, &rig, 3, 3, 6);
        CHECK_EQ_U64(ib_block(&rig, 4, 3, 6), BLOCK_ID_AIR);
        test_world_end(&rig.world);
    }
}

TEST(a_neighbouring_column_not_loaded_refuses) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    uint16_t block = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(rig.world.map, IB_EDGE_X, IB_EDGE_Y, 6, &block) == WORLD_MAP_STATUS_OK);
    REQUIRE(world_map_block_get(rig.world.map, IB_EDGE_X + 1, IB_EDGE_Y, 6, &block) == WORLD_MAP_STATUS_NOT_LOADED);
    ib_place_on(t, &rig, IB_EDGE_X, IB_EDGE_Y, 5);
    ib_check_refused(t, &rig, IB_EDGE_X, IB_EDGE_Y, 6);
    /* One further in, and it goes up to the edge. */
    ib_place_on(t, &rig, IB_EDGE_X - 1, IB_EDGE_Y, 5);
    CHECK_EQ_U64(ib_block(&rig, IB_EDGE_X - 1, IB_EDGE_Y, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(ib_block(&rig, IB_EDGE_X, IB_EDGE_Y, 6), BLOCK_ID_WOODWORKING_BENCH_EAST);
    test_world_end(&rig.world);
}

TEST(mining_either_half_takes_both_and_drops_exactly_one_bench) {
    for (uint32_t which = 0u; which < 2u; which++) {
        ib_rig_t rig;
        REQUIRE(ib_begin(t, &rig));
        ib_place_on(t, &rig, 3, 3, 5);
        const uint64_t placed = rig.hands.placed_count;
        ib_mine(&rig, which == 0u ? 3 : 4, 3, 6);
        CHECK_EQ_U64(ib_block(&rig, 3, 3, 6), BLOCK_ID_AIR);
        CHECK_EQ_U64(ib_block(&rig, 4, 3, 6), BLOCK_ID_AIR);
        CHECK_EQ_U64(rig.hands.mined_count, 1u);
        CHECK_EQ_U64(rig.hands.last_mined,
                     which == 0u ? BLOCK_ID_WOODWORKING_BENCH_WEST : BLOCK_ID_WOODWORKING_BENCH_EAST);
        CHECK_EQ_U64(rig.hands.placed_count, placed);
        uint32_t others = 0u;
        CHECK_EQ_U64(ib_drops_count(&rig, ITEM_ID_WOODWORKING_BENCH, &others), 1u);
        CHECK_EQ_U64(others, 0u);
        /* The stone under it is still there. */
        CHECK_EQ_U64(ib_block(&rig, 3, 3, 5), BLOCK_ID_STONE);
        test_world_end(&rig.world);
    }
}

TEST(what_stood_on_or_hung_from_the_other_half_goes_with_it) {
    for (uint32_t which = 0u; which < 2u; which++) {
        ib_rig_t rig;
        REQUIRE(ib_begin(t, &rig));
        ib_place_on(t, &rig, 3, 3, 5);
        /* Mining one half, what is on the other. */
        const int32_t mined_x = which == 0u ? 3 : 4, other_x = which == 0u ? 4 : 3;
        ib_put(t, &rig, other_x, 3, 7, BLOCK_ID_TORCH);
        ib_put(t, &rig, other_x, 4, 6, BLOCK_ID_TORCH_WALL_NORTH);
        ib_mine(&rig, mined_x, 3, 6);
        CHECK_EQ_U64(ib_block(&rig, other_x, 3, 6), BLOCK_ID_AIR);
        CHECK_EQ_U64(ib_block(&rig, other_x, 3, 7), BLOCK_ID_AIR);
        CHECK_EQ_U64(ib_block(&rig, other_x, 4, 6), BLOCK_ID_AIR);
        uint32_t others = 0u;
        CHECK_EQ_U64(ib_drops_count(&rig, ITEM_ID_TORCH, &others), 2u);
        CHECK_EQ_U64(others, 1u);
        test_world_end(&rig.world);
    }
}

TEST(a_bench_beside_it_stays) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    ib_place_on(t, &rig, 3, 3, 5);
    ib_place_on(t, &rig, 5, 3, 5);
    CHECK_EQ_U64(ib_block(&rig, 5, 3, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(ib_block(&rig, 6, 3, 6), BLOCK_ID_WOODWORKING_BENCH_EAST);
    /* The first one's east half has the second one's west half beside it, which is not its other half. */
    ib_mine(&rig, 4, 3, 6);
    CHECK_EQ_U64(ib_block(&rig, 3, 3, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(ib_block(&rig, 4, 3, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(ib_block(&rig, 5, 3, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(ib_block(&rig, 6, 3, 6), BLOCK_ID_WOODWORKING_BENCH_EAST);
    test_world_end(&rig.world);
}

TEST(a_half_alone_breaks_alone_and_gives_one_bench) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    ib_put(t, &rig, 3, 3, 6, BLOCK_ID_WOODWORKING_BENCH_EAST);
    ib_put(t, &rig, 2, 3, 6, BLOCK_ID_STONE);
    ib_mine(&rig, 3, 3, 6);
    CHECK_EQ_U64(ib_block(&rig, 3, 3, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(ib_block(&rig, 2, 3, 6), BLOCK_ID_STONE);
    uint32_t others = 0u;
    CHECK_EQ_U64(ib_drops_count(&rig, ITEM_ID_WOODWORKING_BENCH, &others), 1u);
    CHECK_EQ_U64(others, 0u);
    test_world_end(&rig.world);
}

TEST(with_its_other_half_not_loaded_a_half_stays_rather_than_leave_one_behind) {
    ib_rig_t rig;
    REQUIRE(ib_begin(t, &rig));
    ib_put(t, &rig, IB_EDGE_X, IB_EDGE_Y, 6, BLOCK_ID_WOODWORKING_BENCH_WEST);
    ib_mine(&rig, IB_EDGE_X, IB_EDGE_Y, 6);
    CHECK_EQ_U64(ib_block(&rig, IB_EDGE_X, IB_EDGE_Y, 6), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(rig.hands.mined_count, 0u);
    uint32_t others = 0u;
    CHECK_EQ_U64(ib_drops_count(&rig, ITEM_ID_WOODWORKING_BENCH, &others), 0u);
    test_world_end(&rig.world);
}

TEST_MAIN()
