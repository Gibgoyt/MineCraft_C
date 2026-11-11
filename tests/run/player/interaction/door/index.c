/* ctest run_player_interaction_door. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "player/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "world/block_state/index.h"
#include "world/door/index.h"
#include "world/index.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct id_rig_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
} id_rig_t;
static_assert(sizeof(id_rig_t) == 1384);

enum : int32_t {
    /* A place to stand well away from what is being done. */
    ID_FAR = 40,
    /* The last x with a column loaded, along y = ID_EDGE_Y. See TEST_WORLD_RADIUS. */
    ID_EDGE_X = 63,
    ID_EDGE_Y = 8,
};

static const uint16_t ID_DOORS[] = {BLOCK_ID_WOOD_DOOR, BLOCK_ID_IRON_DOOR, BLOCK_ID_GLASS_DOOR};
enum : uint32_t { ID_DOOR_KINDS = sizeof(ID_DOORS) / sizeof(ID_DOORS[0]) };

static bool id_begin(test_context_t *t, id_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    player_movement_start(&rig->movement, (float64_t)ID_FAR + 0.5f64, (float64_t)ID_FAR + 0.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    memset(&rig->target, 0, sizeof(rig->target));
    player_interaction_start(&rig->hands, 1234u);
    return true;
}

static uint16_t id_block(id_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = UINT16_MAX;
    (void)world_map_block_get(rig->world.map, x, y, z, &block);
    return block;
}

/* The block with its state. */
static uint16_t id_voxel(id_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t voxel = UINT16_MAX;
    (void)world_map_voxel_get(rig->world.map, x, y, z, &voxel);
    return voxel;
}

static void id_put(test_context_t *t, id_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    CHECK_EQ_U64(world_edit_block_set(&rig->world.world->edit, x, y, z, block), WORLD_EDIT_STATUS_CHANGED);
}

/* On the top of this cell, or on its south side for a door, where a hand opening it would aim. */
static void id_aim(id_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    rig->target.hit.voxel = id_voxel(rig, x, y, z);
    rig->target.hit.block = id_block(rig, x, y, z);
    rig->target.hit.axis = COLLISION_AXIS_Z;
    rig->target.hit.normal_sign = 1;
}

/* The way the player looks, for what is placed. */
static uint8_t id_look_facing = BLOCK_FACING_NORTH;

static void id_tick(id_rig_t *rig, bool attack, bool place) {
    const player_interaction_input_t input = {.borrowed_target = &rig->target,
                                              .borrowed_movement = &rig->movement,
                                              .attack = attack ? 1u : 0u,
                                              .place = place ? 1u : 0u,
                                              .look_facing = id_look_facing,
                                              ._pad0 = {0}};
    player_interaction_tick(&rig->hands, &rig->world.world->edit, &input);
}

/* One press of the right button with a door in hand, on the top of a stone it puts there, and the button let go. */
static void id_place_on(test_context_t *t, id_rig_t *rig, uint16_t door, int32_t x, int32_t y, int32_t z) {
    id_put(t, rig, x, y, z, BLOCK_ID_STONE);
    player_interaction_start(&rig->hands, 1u);
    player_interaction_hand_give(&rig->hands, door, 2u);
    id_aim(rig, x, y, z);
    id_tick(rig, false, true);
    id_tick(rig, false, false);
}

/* One press and release of the right button on this cell. */
static void id_use(id_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    id_aim(rig, x, y, z);
    id_tick(rig, false, true);
    id_tick(rig, false, false);
}

/* Holds the left button on a block until it goes, or for as long as it could take. */
static void id_mine(id_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    id_aim(rig, x, y, z);
    const uint16_t block = id_block(rig, x, y, z);
    for (uint32_t tick = 0u; tick < 400u && id_block(rig, x, y, z) == block; tick++) {
        id_tick(rig, true, false);
    }
}

/* What is waiting in the ring: how many of this item, and how many of anything else. */
static uint32_t id_drops_count(id_rig_t *rig, uint16_t item, uint32_t *out_others) {
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

static uint16_t id_item_of(uint16_t door) {
    return door == BLOCK_ID_WOOD_DOOR ? ITEM_ID_WOOD_DOOR
                                      : (door == BLOCK_ID_IRON_DOOR ? ITEM_ID_IRON_DOOR : ITEM_ID_GLASS_DOOR);
}

/* ---- The blocks ------------------------------------------------------------------------------------------------------- */

TEST(three_doors_each_one_block_with_facing_half_and_open_and_their_items_place_them) {
    uint32_t doors = 0u;
    for (uint32_t id = 0u; id < BLOCK_COUNT; id++) {
        doors += door_is(id) ? 1u : 0u;
    }
    CHECK_EQ_U64(doors, ID_DOOR_KINDS);
    for (uint32_t d = 0u; d < ID_DOOR_KINDS; d++) {
        const uint16_t door = ID_DOORS[d];
        CHECK(door_is(door));
        CHECK_EQ_U64(block_state_allowed(door), BLOCK_STATE_BITS_MASK);
        CHECK_EQ_U64(item_defs[id_item_of(door)].block, door);
        /* Not solid as a block: its body is its state's (tests/run/physics/). */
        CHECK_EQ_U64(block_defs[door].flags & BLOCK_FLAG_SOLID, 0u);
    }
    CHECK(door_opens_by_hand(BLOCK_ID_WOOD_DOOR));
    CHECK(door_opens_by_hand(BLOCK_ID_GLASS_DOOR));
    CHECK(!door_opens_by_hand(BLOCK_ID_IRON_DOOR));
    CHECK(!door_is(BLOCK_ID_STONE));
    CHECK(!door_is(BLOCK_ID_LADDER));
}

TEST(each_half_finds_the_other_above_or_below_with_the_same_facing_and_open) {
    for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
        for (uint32_t open = 0u; open < 2u; open++) {
            const uint16_t lower = block_state_make(BLOCK_ID_WOOD_DOOR, facing, false, open != 0u);
            int32_t step = 0;
            uint16_t other = BLOCK_ID_AIR;
            REQUIRE(door_half_other(lower, &step, &other));
            CHECK_EQ_I64(step, 1);
            CHECK_EQ_U64(other, block_state_make(BLOCK_ID_WOOD_DOOR, facing, true, open != 0u));
            REQUIRE(door_half_other(other, &step, &other));
            CHECK_EQ_I64(step, -1);
            CHECK_EQ_U64(other, lower);
        }
    }
    int32_t step = 0;
    uint16_t other = BLOCK_ID_AIR;
    CHECK(!door_half_other(BLOCK_ID_STONE, &step, &other));
    CHECK(!door_half_other(BLOCK_ID_WOODWORKING_BENCH_WEST, &step, &other));
}

TEST(the_hinge_goes_by_a_door_beside_it_then_by_the_walls_and_otherwise_left) {
    for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
        const uint32_t turned = block_facing_turn(facing, 2u);
        CHECK_EQ_U64(door_hinged_facing(facing, false, false, 0u, 0u), facing);
        CHECK_EQ_U64(door_hinged_facing(facing, false, false, 2u, 2u), facing);
        CHECK_EQ_U64(door_hinged_facing(facing, false, false, 0u, 1u), turned);
        CHECK_EQ_U64(door_hinged_facing(facing, false, false, 1u, 2u), turned);
        CHECK_EQ_U64(door_hinged_facing(facing, false, false, 2u, 0u), facing);
        /* A door beside beats a wall, and two doors are as none. */
        CHECK_EQ_U64(door_hinged_facing(facing, true, false, 2u, 0u), turned);
        CHECK_EQ_U64(door_hinged_facing(facing, false, true, 0u, 2u), facing);
        CHECK_EQ_U64(door_hinged_facing(facing, true, true, 0u, 2u), turned);
    }
}

/* ---- Placing ---------------------------------------------------------------------------------------------------------- */

TEST(a_door_goes_down_two_tall_facing_the_way_the_player_looks_for_one_item) {
    for (uint32_t d = 0u; d < ID_DOOR_KINDS; d++) {
        for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
            id_rig_t rig;
            REQUIRE(id_begin(t, &rig));
            id_look_facing = (uint8_t)facing;
            id_place_on(t, &rig, ID_DOORS[d], 10, 10, 5);
            CHECK_EQ_U64(id_voxel(&rig, 10, 10, 6), block_state_make(ID_DOORS[d], facing, false, false));
            CHECK_EQ_U64(id_voxel(&rig, 10, 10, 7), block_state_make(ID_DOORS[d], facing, true, false));
            CHECK_EQ_U64(id_block(&rig, 10, 10, 8), BLOCK_ID_AIR);
            CHECK_EQ_U64(rig.hands.hand_count, 1u);
            CHECK_EQ_U64(rig.hands.placed_count, 1u);
            test_world_end(&rig.world);
        }
    }
    id_look_facing = BLOCK_FACING_NORTH;
}

TEST(r_turns_a_door_before_it_is_placed) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_put(t, &rig, 10, 10, 5, BLOCK_ID_STONE);
    player_interaction_start(&rig.hands, 1u);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_WOOD_DOOR, 2u);
    player_interaction_turn(&rig.hands);
    id_aim(&rig, 10, 10, 5);
    id_tick(&rig, false, true);
    CHECK_EQ_U64(id_voxel(&rig, 10, 10, 6), block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_EAST, false, false));
    CHECK_EQ_U64(id_voxel(&rig, 10, 10, 7), block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_EAST, true, false));
    test_world_end(&rig.world);
}

TEST(a_wall_on_the_right_puts_the_hinge_there_by_facing_the_door_the_other_way) {
    for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
        id_rig_t rig;
        REQUIRE(id_begin(t, &rig));
        id_look_facing = (uint8_t)facing;
        /* The right hand of whoever looks this way: a quarter clockwise. */
        const uint32_t right = block_facing_turn(facing, 1u);
        const int32_t rx = 10 + block_facing_step_x(right), ry = 10 + block_facing_step_y(right);
        id_put(t, &rig, rx, ry, 6, BLOCK_ID_STONE);
        id_put(t, &rig, rx, ry, 7, BLOCK_ID_STONE);
        id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, 10, 10, 5);
        const uint32_t turned = block_facing_turn(facing, 2u);
        CHECK_EQ_U64(id_voxel(&rig, 10, 10, 6), block_state_make(BLOCK_ID_WOOD_DOOR, turned, false, false));
        CHECK_EQ_U64(id_voxel(&rig, 10, 10, 7), block_state_make(BLOCK_ID_WOOD_DOOR, turned, true, false));
        /* A second door to its left makes a pair: hinged on its own left, the wall's side of the first is not its business. */
        const uint32_t left = block_facing_turn(facing, 3u);
        const int32_t lx = 10 + block_facing_step_x(left), ly = 10 + block_facing_step_y(left);
        id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, lx, ly, 5);
        CHECK_EQ_U64(id_voxel(&rig, lx, ly, 6), block_state_make(BLOCK_ID_WOOD_DOOR, facing, false, false));
        test_world_end(&rig.world);
    }
    id_look_facing = BLOCK_FACING_NORTH;
}

TEST(a_door_to_the_left_hinges_the_new_one_on_the_right) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, 10, 10, 5);
    /* Looking north, the next one east: the first is to its left. */
    id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, 11, 10, 5);
    CHECK_EQ_U64(id_voxel(&rig, 11, 10, 6), block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_SOUTH, false, false));
    test_world_end(&rig.world);
}

TEST(the_plan_holds_both_halves_and_placing_does_what_it_said) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_put(t, &rig, 10, 10, 5, BLOCK_ID_STONE);
    player_interaction_start(&rig.hands, 1u);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_GLASS_DOOR, 2u);
    id_aim(&rig, 10, 10, 5);
    player_interaction_input_t input = {.borrowed_target = &rig.target,
                                        .borrowed_movement = &rig.movement,
                                        .place = 1u,
                                        .look_facing = BLOCK_FACING_WEST,
                                        ._pad0 = {0}};
    player_place_plan_t plan;
    REQUIRE(player_interaction_place_plan(&rig.hands, rig.world.map, &input, &plan) == PLAYER_PLACE_OK);
    CHECK_EQ_U64(plan.count, 2u);
    CHECK_EQ_U64(plan.facing, BLOCK_FACING_WEST);
    for (uint32_t a = 0u; a < 2u; a++) {
        CHECK_EQ_I64(plan.cells[0][a], 10);
        CHECK_EQ_I64(plan.cells[1][a], 10);
    }
    CHECK_EQ_I64(plan.cells[0][2], 6);
    CHECK_EQ_I64(plan.cells[1][2], 7);
    CHECK_EQ_U64(plan.voxels[0], block_state_make(BLOCK_ID_GLASS_DOOR, BLOCK_FACING_WEST, false, false));
    CHECK_EQ_U64(plan.voxels[1], block_state_make(BLOCK_ID_GLASS_DOOR, BLOCK_FACING_WEST, true, false));
    CHECK_EQ_U64(id_block(&rig, 10, 10, 6), BLOCK_ID_AIR);
    /* The upper cell taken: the same cells, and blocked. */
    id_put(t, &rig, 10, 10, 7, BLOCK_ID_STONE);
    CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &input, &plan), PLAYER_PLACE_BLOCKED);
    CHECK_EQ_U64(plan.count, 2u);
    CHECK_EQ_I64(plan.cells[1][2], 7);
    id_put(t, &rig, 10, 10, 7, BLOCK_ID_AIR);
    REQUIRE(player_interaction_place_plan(&rig.hands, rig.world.map, &input, &plan) == PLAYER_PLACE_OK);
    player_interaction_tick(&rig.hands, &rig.world.world->edit, &input);
    CHECK_EQ_U64(id_voxel(&rig, 10, 10, 6), plan.voxels[0]);
    CHECK_EQ_U64(id_voxel(&rig, 10, 10, 7), plan.voxels[1]);
    test_world_end(&rig.world);
}

TEST(either_cell_taken_or_the_body_in_the_way_refuses_and_keeps_the_item) {
    for (uint32_t which = 0u; which < 4u; which++) {
        id_rig_t rig;
        REQUIRE(id_begin(t, &rig));
        if (which == 0u) {
            id_put(t, &rig, 10, 10, 7, BLOCK_ID_STONE); /* the upper cell */
        } else if (which == 1u) {
            id_put(t, &rig, 10, 10, 6,
                   BLOCK_ID_STONE); /* the lower: aimed at its top, it goes above it, so the top is then taken */
            id_put(t, &rig, 10, 10, 8, BLOCK_ID_STONE);
        } else {
            /* The body across the plate's side of the cell, in the lower half or only the upper. */
            player_movement_start(&rig.movement, 10.5f64, 10.9f64, which == 2u ? 6.0f64 : 7.5f64);
        }
        id_put(t, &rig, 10, 10, 5, BLOCK_ID_STONE);
        player_interaction_start(&rig.hands, 1u);
        player_interaction_hand_give(&rig.hands, BLOCK_ID_WOOD_DOOR, 2u);
        id_aim(&rig, 10, 10, which == 1u ? 6 : 5);
        id_tick(&rig, false, true);
        CHECK_EQ_U64(rig.hands.hand_count, 2u);
        CHECK_EQ_U64(rig.hands.placed_count, 0u);
        CHECK(!door_is(id_block(&rig, 10, 10, 6)));
        CHECK(!door_is(id_block(&rig, 10, 10, 7)));
        test_world_end(&rig.world);
    }
}

TEST(a_column_not_loaded_beside_does_not_stop_it_but_the_top_of_the_world_does) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, ID_EDGE_X, ID_EDGE_Y, 5);
    CHECK(door_is(id_block(&rig, ID_EDGE_X, ID_EDGE_Y, 6)));
    id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, 10, 10, COLUMN_Z_GAMEPLAY_MAX - 1);
    CHECK_EQ_U64(id_block(&rig, 10, 10, COLUMN_Z_GAMEPLAY_MAX), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig.hands.placed_count, 0u);
    test_world_end(&rig.world);
}

/* ---- Opening ---------------------------------------------------------------------------------------------------------- */

TEST(the_right_button_opens_both_halves_once_a_press_and_shuts_them_on_the_next) {
    for (uint32_t aim_upper = 0u; aim_upper < 2u; aim_upper++) {
        id_rig_t rig;
        REQUIRE(id_begin(t, &rig));
        id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, 10, 10, 5);
        const int32_t z = aim_upper != 0u ? 7 : 6;
        /* Pressed and held: open, and held on it stays open. */
        id_aim(&rig, 10, 10, z);
        for (uint32_t tick = 0u; tick < 20u; tick++) {
            id_tick(&rig, false, true);
        }
        CHECK_EQ_U64(id_voxel(&rig, 10, 10, 6), block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, false, true));
        CHECK_EQ_U64(id_voxel(&rig, 10, 10, 7), block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, true, true));
        /* Nothing was put down on it, and the hand still has its door. */
        CHECK_EQ_U64(rig.hands.hand_count, 1u);
        CHECK_EQ_U64(rig.hands.placed_count, 1u);
        id_tick(&rig, false, false);
        id_use(&rig, 10, 10, z);
        CHECK_EQ_U64(id_voxel(&rig, 10, 10, 6), block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, false, false));
        CHECK_EQ_U64(id_voxel(&rig, 10, 10, 7), block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, true, false));
        test_world_end(&rig.world);
    }
}

TEST(the_button_held_on_after_placing_a_door_does_not_open_it) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_put(t, &rig, 10, 10, 5, BLOCK_ID_STONE);
    player_interaction_start(&rig.hands, 1u);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_WOOD_DOOR, 2u);
    id_aim(&rig, 10, 10, 5);
    id_tick(&rig, false, true);
    REQUIRE(door_is(id_block(&rig, 10, 10, 6)));
    /* The next tick aims at the door just placed, with the button still down. */
    id_aim(&rig, 10, 10, 6);
    for (uint32_t tick = 0u; tick < 20u; tick++) {
        id_tick(&rig, false, true);
    }
    CHECK(!block_state_open(id_voxel(&rig, 10, 10, 6)));
    test_world_end(&rig.world);
}

TEST(an_iron_door_stays_shut_by_hand) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_place_on(t, &rig, BLOCK_ID_IRON_DOOR, 10, 10, 5);
    player_interaction_hand_set(&rig.hands, BLOCK_ID_AIR, 0u);
    id_use(&rig, 10, 10, 6);
    id_use(&rig, 10, 10, 7);
    CHECK_EQ_U64(id_voxel(&rig, 10, 10, 6), block_state_make(BLOCK_ID_IRON_DOOR, BLOCK_FACING_NORTH, false, false));
    CHECK_EQ_U64(id_voxel(&rig, 10, 10, 7), block_state_make(BLOCK_ID_IRON_DOOR, BLOCK_FACING_NORTH, true, false));
    test_world_end(&rig.world);
}

/* Whether a corner of a box on the floor plan is this point, to within rounding. */
static bool id_has(const collision_box_t *box, float64_t x, float64_t y) {
    const float64_t margin = 1.0e-9f64;
    const bool on_x = fabs(box->min[0] - x) < margin || fabs(box->max[0] - x) < margin;
    const bool on_y = fabs(box->min[1] - y) < margin || fabs(box->max[1] - y) < margin;
    return on_x && on_y;
}

TEST(an_open_door_is_aimed_at_by_its_plate_turned_on_its_hinge) {
    for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
        collision_box_t shut[RAYCAST_BOXES_PER_BLOCK], open[RAYCAST_BOXES_PER_BLOCK], turned[RAYCAST_BOXES_PER_BLOCK];
        const uint16_t door = block_state_make(BLOCK_ID_WOOD_DOOR, facing, false, false);
        REQUIRE(raycast_block_boxes(door, 0, 0, 0, shut) == 1u);
        REQUIRE(raycast_block_boxes(block_state_with_open(door, true), 0, 0, 0, open) == 1u);
        /* Open, it is where a shut door a quarter anticlockwise would be: against its left side. */
        REQUIRE(raycast_block_boxes(block_state_make(BLOCK_ID_WOOD_DOOR, block_facing_turn(facing, 3u), false, false),
                                    0, 0, 0, turned) == 1u);
        CHECK_MEM_EQ(open, turned, sizeof(open[0]));
        /* And they share the hinge's corner: the open plate's far end is the shut plate's near end. */
        const uint32_t left = block_facing_turn(facing, 3u);
        const float64_t hinge_x = block_facing_step_x(left) < 0 || block_facing_step_x(facing) < 0 ? 0.0f64 : 1.0f64;
        const float64_t hinge_y = block_facing_step_y(left) < 0 || block_facing_step_y(facing) < 0 ? 0.0f64 : 1.0f64;
        CHECK(id_has(&shut[0], hinge_x, hinge_y));
        CHECK(id_has(&open[0], hinge_x, hinge_y));
    }
}

TEST(walking_through_a_door_takes_opening_it_first) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, 10, 10, 5);
    const collision_box_t body = {.min = {10.2f64, 8.2f64, 6.0f64}, .max = {10.8f64, 8.8f64, 7.8f64}};
    const float64_t north[COLLISION_AXES] = {0.0f64, 4.0f64, 0.0f64};
    collision_move_t move;
    physics_move(rig.world.physics, rig.world.map, &body, north, &move);
    CHECK_EQ_U64(move.hit_y, 1u);
    CHECK_NEAR(move.moved[1], 10.0f64 + 26.0f64 / 32.0f64 - 8.8f64, 1.0e-9f64);
    player_interaction_hand_set(&rig.hands, BLOCK_ID_AIR, 0u);
    id_use(&rig, 10, 10, 6);
    physics_move(rig.world.physics, rig.world.map, &body, north, &move);
    CHECK_EQ_U64(move.hit_y, 0u);
    CHECK(move.moved[1] > 3.9f64);
    test_world_end(&rig.world);
}

/* ---- Breaking --------------------------------------------------------------------------------------------------------- */

TEST(mining_either_half_shut_or_open_takes_both_and_drops_exactly_one_door) {
    for (uint32_t d = 0u; d < ID_DOOR_KINDS; d++) {
        for (uint32_t which = 0u; which < 4u; which++) {
            id_rig_t rig;
            REQUIRE(id_begin(t, &rig));
            id_place_on(t, &rig, ID_DOORS[d], 10, 10, 5);
            if (which >= 2u && door_opens_by_hand(ID_DOORS[d])) {
                id_use(&rig, 10, 10, 6);
                REQUIRE(block_state_open(id_voxel(&rig, 10, 10, 7)));
            }
            /* An iron door needs its pickaxe to drop. */
            player_interaction_hold_tool(&rig.hands, BLOCK_TOOL_PICKAXE, BLOCK_TIER_IRON);
            const uint64_t placed = rig.hands.placed_count;
            id_mine(&rig, 10, 10, (which & 1u) == 0u ? 6 : 7);
            CHECK_EQ_U64(id_block(&rig, 10, 10, 6), BLOCK_ID_AIR);
            CHECK_EQ_U64(id_block(&rig, 10, 10, 7), BLOCK_ID_AIR);
            CHECK_EQ_U64(id_block(&rig, 10, 10, 5), BLOCK_ID_STONE);
            CHECK_EQ_U64(rig.hands.mined_count, 1u);
            CHECK_EQ_U64(rig.hands.placed_count, placed);
            uint32_t others = 0u;
            CHECK_EQ_U64(id_drops_count(&rig, id_item_of(ID_DOORS[d]), &others), 1u);
            CHECK_EQ_U64(others, 0u);
            test_world_end(&rig.world);
        }
    }
}

TEST(a_door_on_a_door_stays_when_the_lower_one_is_mined) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_place_on(t, &rig, BLOCK_ID_WOOD_DOOR, 10, 10, 5);
    /* A second door stacked on the first: its lower half is above the first's upper, and is not its other half. */
    id_put(t, &rig, 10, 10, 8, block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, false, false));
    id_put(t, &rig, 10, 10, 9, block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, true, false));
    id_mine(&rig, 10, 10, 7);
    CHECK_EQ_U64(id_block(&rig, 10, 10, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(id_block(&rig, 10, 10, 7), BLOCK_ID_AIR);
    CHECK(door_is(id_block(&rig, 10, 10, 8)));
    CHECK(door_is(id_block(&rig, 10, 10, 9)));
    test_world_end(&rig.world);
}

TEST(a_half_alone_breaks_alone_and_gives_one_door) {
    id_rig_t rig;
    REQUIRE(id_begin(t, &rig));
    id_put(t, &rig, 10, 10, 6, block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_EAST, true, false));
    id_mine(&rig, 10, 10, 6);
    CHECK_EQ_U64(id_block(&rig, 10, 10, 6), BLOCK_ID_AIR);
    uint32_t others = 0u;
    CHECK_EQ_U64(id_drops_count(&rig, ITEM_ID_WOOD_DOOR, &others), 1u);
    CHECK_EQ_U64(others, 0u);
    test_world_end(&rig.world);
}

/* ---- Saved and opened again ------------------------------------------------------------------------------------------- */

enum : uint32_t { ID_SAVE_RADIUS = 2u };
enum : uint64_t {
    ID_COLUMN_BYTES = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS * sizeof(uint16_t),
    ID_PATH_BYTES = 512u
};

typedef struct id_save_rig_t {
    world_t *world;   /* 0 */
    save_t *save;     /* 8 */
    uint16_t *states; /* 16: one column */
    uint8_t *scratch; /* 24 */
    char *root;       /* 32 */
} id_save_rig_t;
static_assert(sizeof(id_save_rig_t) == 40);

/* A session on the world in root: air, with what is saved over it. */
static bool id_session_begin(test_context_t *t, id_save_rig_t *rig) {
    const uint64_t side = 2ull * ID_SAVE_RADIUS + 1ull;
    const world_limits_t limits = {.section_bytes = 1ull << 28, .column_count = side * side};
    if (!CHECK(save_init(rig->save)) || !CHECK(world_init(rig->world, limits)) ||
        !CHECK_EQ_U64(save_open(rig->save, rig->root, 77u), SAVE_WORLD_OK) ||
        !CHECK(world_open(rig->world, ID_SAVE_RADIUS, 0, 0))) {
        return false;
    }
    world_t *const world = rig->world;
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        memset(rig->states, 0, ID_COLUMN_BYTES);
        (void)save_world_column_overlay(&rig->save->world, request.chunk_x, request.chunk_y, rig->states, rig->scratch);
        column_t *const column = world_map_stream_commit(&world->map, &request);
        if (!CHECK(column != nullptr)) {
            return false;
        }
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            if (!CHECK(section_blocks_import(&column->sections[section], &world->section_pool,
                                             rig->states + (uint64_t)section * SECTION_VOXELS))) {
                return false;
            }
        }
        world_edit_column_filled(&world->edit, request.slot);
    }
    return true;
}

static void id_session_end(id_save_rig_t *rig) {
    world_close(rig->world);
    world_deinit(rig->world);
    save_deinit(rig->save);
}

/* One press of the right button on the top or a side of a cell, with these hands and a body far off. */
static void id_save_press(id_save_rig_t *rig, player_interaction_t *hands, int32_t x, int32_t y, int32_t z,
                          uint8_t look_facing) {
    player_movement_t movement;
    player_movement_start(&movement, 0.5f64, 0.5f64, 30.0f64);
    player_target_t target;
    memset(&target, 0, sizeof(target));
    target.hit.result = RAYCAST_RESULT_HIT;
    target.hit.cell[0] = x;
    target.hit.cell[1] = y;
    target.hit.cell[2] = z;
    (void)world_map_voxel_get(&rig->world->map, x, y, z, &target.hit.voxel);
    target.hit.block = block_state_id(target.hit.voxel);
    target.hit.axis = COLLISION_AXIS_Z;
    target.hit.normal_sign = 1;
    for (uint32_t down = 0u; down < 2u; down++) {
        const player_interaction_input_t input = {.borrowed_target = &target,
                                                  .borrowed_movement = &movement,
                                                  .place = down == 0u ? 1u : 0u,
                                                  .look_facing = look_facing,
                                                  ._pad0 = {0}};
        player_interaction_tick(hands, &rig->world->edit, &input);
    }
}

TEST(an_open_door_is_saved_and_opens_again_open_and_still_shuts) {
    id_save_rig_t rig;
    rig.world = test_world_aligned(t, sizeof(world_t));
    rig.save = test_world_aligned(t, sizeof(save_t));
    rig.states = test_alloc(t, ID_COLUMN_BYTES);
    rig.scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    rig.root = test_alloc(t, ID_PATH_BYTES);
    snprintf(rig.root, ID_PATH_BYTES, "/tmp/mc_interaction_door_XXXXXX");
    REQUIRE(mkdtemp(rig.root) != nullptr);
    REQUIRE(id_session_begin(t, &rig));
    player_interaction_t *const hands = test_alloc(t, sizeof(player_interaction_t));
    player_interaction_start(hands, 5u);
    /* A wooden door looking east, opened, and a glass one looking south, left shut. */
    REQUIRE(world_edit_block_set(&rig.world->edit, 3, 3, 10, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(&rig.world->edit, 6, 3, 10, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    player_interaction_hand_give(hands, BLOCK_ID_WOOD_DOOR, 1u);
    id_save_press(&rig, hands, 3, 3, 10, BLOCK_FACING_EAST);
    id_save_press(&rig, hands, 3, 3, 11, BLOCK_FACING_EAST);
    player_interaction_hand_give(hands, BLOCK_ID_GLASS_DOOR, 1u);
    id_save_press(&rig, hands, 6, 3, 10, BLOCK_FACING_SOUTH);
    const uint16_t open_lower = block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_EAST, false, true);
    const uint16_t open_upper = block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_EAST, true, true);
    const uint16_t glass_lower = block_state_make(BLOCK_ID_GLASS_DOOR, BLOCK_FACING_SOUTH, false, false);
    const uint16_t glass_upper = block_state_make(BLOCK_ID_GLASS_DOOR, BLOCK_FACING_SOUTH, true, false);
    uint16_t voxel = 0u;
    REQUIRE(world_map_voxel_get(&rig.world->map, 3, 3, 11, &voxel) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(voxel, open_lower);
    CHECK_EQ_U64(save_world_flush(&rig.save->world, &rig.world->edit), SAVE_WORLD_OK);
    id_session_end(&rig);

    REQUIRE(id_session_begin(t, &rig));
    const int32_t cells[4][3] = {{3, 3, 11}, {3, 3, 12}, {6, 3, 11}, {6, 3, 12}};
    const uint16_t want[4] = {open_lower, open_upper, glass_lower, glass_upper};
    for (uint32_t i = 0u; i < 4u; i++) {
        REQUIRE(world_map_voxel_get(&rig.world->map, cells[i][0], cells[i][1], cells[i][2], &voxel) ==
                WORLD_MAP_STATUS_OK);
        CHECK_EQ_U64(voxel, want[i]);
    }
    /* Still one door: a press on its upper half shuts both. */
    player_interaction_start(hands, 6u);
    id_save_press(&rig, hands, 3, 3, 12, BLOCK_FACING_EAST);
    REQUIRE(world_map_voxel_get(&rig.world->map, 3, 3, 11, &voxel) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(voxel, block_state_with_open(open_lower, false));
    REQUIRE(world_map_voxel_get(&rig.world->map, 3, 3, 12, &voxel) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(voxel, block_state_with_open(open_upper, false));
    id_session_end(&rig);

    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", rig.root, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

TEST_MAIN()
