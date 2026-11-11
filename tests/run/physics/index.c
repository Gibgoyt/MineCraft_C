/* ctest run_physics. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "heap/index.h"
#include "physics/index.h"
#include "tables/blocks/index.h"
#include "types/index.h"
#include "world/index.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* What a contact may differ from its face by, after the arithmetic of a move. */
#define PT_EXACT 1.0e-9f64

#define PT_BOX(x0, y0, z0, x1, y1, z1) ((collision_box_t){.min = {(x0), (y0), (z0)}, .max = {(x1), (y1), (z1)}})

static void pt_sweep(const collision_box_t *others, uint32_t count, const collision_box_t *box, float64_t x,
                     float64_t y, float64_t z, collision_move_t *out) {
    const float64_t delta[COLLISION_AXES] = {x, y, z};
    collision_sweep(others, count, box, delta, out);
}

/* ---- Sweeps by hand ------------------------------------------------------------------------------------------------- */

TEST(each_direction_stops_exactly_on_the_face_and_says_which) {
    const collision_box_t wall = PT_BOX(10.0f64, 10.0f64, 10.0f64, 11.0f64, 11.0f64, 11.0f64);
    const float64_t size = 0.5f64;
    collision_move_t move;
    /* From each side of it, going at it, from 2 away, with 5 to go. */
    const collision_box_t from_west = PT_BOX(7.5f64, 10.25f64, 10.25f64, 8.0f64, 10.75f64, 10.75f64);
    pt_sweep(&wall, 1u, &from_west, 5.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 2.0f64, PT_EXACT);
    CHECK(move.hit_x == 1u && move.hit_y == 0u && move.hit_z_down == 0u && move.hit_z_up == 0u);
    const collision_box_t from_east = PT_BOX(13.0f64, 10.25f64, 10.25f64, 13.0f64 + size, 10.75f64, 10.75f64);
    pt_sweep(&wall, 1u, &from_east, -5.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], -2.0f64, PT_EXACT);
    CHECK(move.hit_x == 1u && move.hit_y == 0u);
    const collision_box_t from_south = PT_BOX(10.25f64, 7.5f64, 10.25f64, 10.75f64, 8.0f64, 10.75f64);
    pt_sweep(&wall, 1u, &from_south, 0.0f64, 5.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[1], 2.0f64, PT_EXACT);
    CHECK(move.hit_x == 0u && move.hit_y == 1u);
    const collision_box_t from_north = PT_BOX(10.25f64, 13.0f64, 10.25f64, 10.75f64, 13.0f64 + size, 10.75f64);
    pt_sweep(&wall, 1u, &from_north, 0.0f64, -5.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[1], -2.0f64, PT_EXACT);
    CHECK(move.hit_y == 1u);
    const collision_box_t from_below = PT_BOX(10.25f64, 10.25f64, 7.5f64, 10.75f64, 10.75f64, 8.0f64);
    pt_sweep(&wall, 1u, &from_below, 0.0f64, 0.0f64, 5.0f64, &move);
    CHECK_NEAR(move.moved[2], 2.0f64, PT_EXACT);
    CHECK(move.hit_z_up == 1u && move.hit_z_down == 0u);
    const collision_box_t from_above = PT_BOX(10.25f64, 10.25f64, 13.0f64, 10.75f64, 10.75f64, 13.0f64 + size);
    pt_sweep(&wall, 1u, &from_above, 0.0f64, 0.0f64, -5.0f64, &move);
    CHECK_NEAR(move.moved[2], -2.0f64, PT_EXACT);
    CHECK(move.hit_z_down == 1u && move.hit_z_up == 0u);
    /* Short of it, nothing stops it. */
    pt_sweep(&wall, 1u, &from_west, 1.5f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 1.5f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_x, 0u);
    /* Reaching it exactly as the move ends: went all the way. */
    pt_sweep(&wall, 1u, &from_west, 2.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 2.0f64, PT_EXACT);
    /* Beside it and going past: no. */
    const collision_box_t beside = PT_BOX(7.5f64, 12.0f64, 10.25f64, 8.0f64, 12.5f64, 10.75f64);
    pt_sweep(&wall, 1u, &beside, 5.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 5.0f64, PT_EXACT);
    /* Going away: no. */
    pt_sweep(&wall, 1u, &from_west, -5.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], -5.0f64, PT_EXACT);
}

TEST(a_diagonal_stops_on_one_axis_and_slides_along_the_others) {
    /* A wall along y and z at x = 10. The box gets there halfway through, and goes on along it. */
    const collision_box_t wall = PT_BOX(10.0f64, -100.0f64, -100.0f64, 11.0f64, 100.0f64, 100.0f64);
    const collision_box_t box = PT_BOX(8.0f64, 0.0f64, 0.0f64, 9.0f64, 1.0f64, 1.0f64);
    collision_move_t move;
    pt_sweep(&wall, 1u, &box, 2.0f64, 3.0f64, -4.0f64, &move);
    CHECK_NEAR(move.moved[0], 1.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[1], 3.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[2], -4.0f64, PT_EXACT);
    CHECK(move.hit_x == 1u && move.hit_y == 0u && move.hit_z_down == 0u);

    /* Into a corner of the room: stops on all three, in whatever order they are met. */
    const collision_box_t room[3] = {
        wall,
        PT_BOX(-100.0f64, 5.0f64, -100.0f64, 100.0f64, 6.0f64, 100.0f64),
        PT_BOX(-100.0f64, -100.0f64, -3.0f64, 100.0f64, 100.0f64, -2.0f64),
    };
    pt_sweep(room, 3u, &box, 20.0f64, 20.0f64, -20.0f64, &move);
    CHECK_NEAR(move.moved[0], 1.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[1], 4.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[2], -2.0f64, PT_EXACT);
    CHECK(move.hit_x == 1u && move.hit_y == 1u && move.hit_z_down == 1u);
}

TEST(a_diagonal_passes_a_corner_it_misses_and_stops_at_one_it_clips) {
    const collision_box_t post = PT_BOX(10.0f64, 10.0f64, 0.0f64, 11.0f64, 11.0f64, 5.0f64);
    /* Its top reaches the post's south side at y = 10 only after its west face has passed the post's east side: it goes by. */
    const collision_box_t south = PT_BOX(8.0f64, 7.3f64, 1.0f64, 9.0f64, 8.3f64, 2.0f64);
    collision_move_t move;
    pt_sweep(&post, 1u, &south, 4.0f64, 2.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 4.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[1], 2.0f64, PT_EXACT);
    CHECK(move.hit_x == 0u && move.hit_y == 0u);
    /* The same line further north meets the post's south side while it is still under it, and slides along it to the east. */
    const collision_box_t clips = PT_BOX(8.5f64, 8.5f64, 1.0f64, 9.5f64, 9.5f64, 2.0f64);
    pt_sweep(&post, 1u, &clips, 4.0f64, 2.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 4.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[1], 0.5f64, PT_EXACT);
    CHECK(move.hit_x == 0u && move.hit_y == 1u);
}

TEST(a_box_resting_on_a_floor_or_along_a_wall_is_free_to_go_along_it_and_leave) {
    const collision_box_t floor_cells[2] = {
        PT_BOX(0.0f64, 0.0f64, -1.0f64, 1.0f64, 1.0f64, 0.0f64),
        PT_BOX(1.0f64, 0.0f64, -1.0f64, 2.0f64, 1.0f64, 0.0f64),
    };
    const collision_box_t on = PT_BOX(0.2f64, 0.2f64, 0.0f64, 0.8f64, 0.8f64, 1.8f64);
    collision_move_t move;
    /* Across the seam between the two, on its top. */
    pt_sweep(floor_cells, 2u, &on, 0.9f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 0.9f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_x, 0u);
    /* Pushed down on it, it still goes along: the floor stops z and only z. */
    pt_sweep(floor_cells, 2u, &on, 0.9f64, 0.0f64, -0.5f64, &move);
    CHECK_NEAR(move.moved[0], 0.9f64, PT_EXACT);
    CHECK_NEAR(move.moved[2], 0.0f64, PT_EXACT);
    CHECK(move.hit_x == 0u && move.hit_z_down == 1u);
    /* Up and away. */
    pt_sweep(floor_cells, 2u, &on, 0.0f64, 0.0f64, 3.0f64, &move);
    CHECK_NEAR(move.moved[2], 3.0f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_z_up, 0u);
    /* A hair into a wall it is leaving, at the corner where another of its faces is reaching that wall: rounding, and it goes. */
    const collision_box_t cube = PT_BOX(0.0f64, 0.0f64, 0.0f64, 1.0f64, 1.0f64, 1.0f64);
    const collision_box_t cornered = PT_BOX(1.0f64 - 1.0e-8f64, 1.0f64, 0.2f64, 2.0f64, 2.0f64, 0.8f64);
    pt_sweep(&cube, 1u, &cornered, 1.0f64, -1.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 1.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[1], -1.0f64, PT_EXACT);
    /* A hair into the floor is rounding, and is treated as touching. */
    const collision_box_t rounded = PT_BOX(0.2f64, 0.2f64, -1.0e-8f64, 0.8f64, 0.8f64, 1.8f64);
    pt_sweep(floor_cells, 2u, &rounded, 0.5f64, 0.0f64, -0.5f64, &move);
    CHECK_NEAR(move.moved[0], 0.5f64, PT_EXACT);
    CHECK_NEAR(move.moved[2], 1.0e-8f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_z_down, 1u);
}

TEST(coming_down_on_a_seam_the_floor_counts_before_the_wall) {
    /* The box is over the first cell, and its front face and bottom face reach the second cell's corner at the same moment. If the
     * side of that cell counted, it would stop going east on flat ground. */
    const collision_box_t floor_cells[2] = {
        PT_BOX(0.0f64, 0.0f64, -1.0f64, 1.0f64, 1.0f64, 0.0f64),
        PT_BOX(1.0f64, 0.0f64, -1.0f64, 2.0f64, 1.0f64, 0.0f64),
    };
    const collision_box_t box = PT_BOX(0.0f64, 0.2f64, 0.5f64, 0.5f64, 0.8f64, 2.0f64);
    collision_move_t move;
    pt_sweep(floor_cells, 2u, &box, 1.0f64, 0.0f64, -1.0f64, &move);
    CHECK_NEAR(move.moved[0], 1.0f64, PT_EXACT);
    CHECK_NEAR(move.moved[2], -0.5f64, PT_EXACT);
    CHECK(move.hit_x == 0u && move.hit_z_down == 1u);
    /* In either order in the list. */
    const collision_box_t turned[2] = {floor_cells[1], floor_cells[0]};
    pt_sweep(turned, 2u, &box, 1.0f64, 0.0f64, -1.0f64, &move);
    CHECK_NEAR(move.moved[0], 1.0f64, PT_EXACT);
    CHECK(move.hit_x == 0u && move.hit_z_down == 1u);
}

TEST(a_box_already_inside_another_can_leave_and_is_stopped_by_the_next) {
    const collision_box_t others[2] = {
        PT_BOX(0.0f64, 0.0f64, 0.0f64, 1.0f64, 1.0f64, 1.0f64),
        PT_BOX(3.0f64, 0.0f64, 0.0f64, 4.0f64, 1.0f64, 1.0f64),
    };
    const collision_box_t inside = PT_BOX(0.2f64, 0.2f64, 0.2f64, 0.8f64, 0.8f64, 0.8f64);
    collision_move_t move;
    pt_sweep(others, 2u, &inside, 5.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 2.2f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_x, 1u);
}

TEST(overlap_means_shared_volume_and_not_a_touch) {
    const collision_box_t other = PT_BOX(0.0f64, 0.0f64, 0.0f64, 1.0f64, 1.0f64, 1.0f64);
    const collision_box_t touching = PT_BOX(1.0f64, 0.0f64, 0.0f64, 2.0f64, 1.0f64, 1.0f64);
    const collision_box_t sharing = PT_BOX(0.5f64, 0.5f64, 0.5f64, 2.0f64, 2.0f64, 2.0f64);
    const collision_box_t off = PT_BOX(1.0f64, 2.0f64, 0.0f64, 2.0f64, 3.0f64, 1.0f64);
    CHECK(!collision_overlaps_any(&other, 1u, &touching));
    CHECK(collision_overlaps_any(&other, 1u, &sharing));
    CHECK(!collision_overlaps_any(&other, 1u, &off));
    CHECK(!collision_overlaps_any(&other, 0u, &sharing));
}

/* ---- Blocks ----------------------------------------------------------------------------------------------------------- */

TEST(every_block_collides_as_its_config_says) {
    collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
    uint32_t solid = 0u;
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        const block_def_t *const def = &block_defs[block];
        const uint32_t count = collision_block_boxes((uint16_t)block, -3, 7, 100, boxes);
        /* A door's body goes by its state, below. */
        if (def->shape == BLOCK_SHAPE_DOOR) {
            CHECK_EQ_U64(count, 1u);
            continue;
        }
        if ((def->flags & BLOCK_FLAG_SOLID) == 0u) {
            CHECK_EQ_U64(count, 0u);
            continue;
        }
        solid++;
        CHECK_EQ_U64(count, def->shape == BLOCK_SHAPE_STAIRS ? 2u : 1u);
        CHECK_NEAR(boxes[0].min[0], -3.0f64, PT_EXACT);
        CHECK_NEAR(boxes[0].max[0], -2.0f64, PT_EXACT);
        CHECK_NEAR(boxes[0].min[2], 100.0f64, PT_EXACT);
        const bool half = def->shape == BLOCK_SHAPE_SLAB || def->shape == BLOCK_SHAPE_STAIRS;
        CHECK_NEAR(boxes[0].max[2], half ? 100.5f64 : 101.0f64, PT_EXACT);
    }
    CHECK(solid > 50u);
    /* By name, what a player will walk into. */
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_AIR, 0, 0, 0, boxes), 0u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_WATER, 0, 0, 0, boxes), 0u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_LAVA, 0, 0, 0, boxes), 0u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_LADDER, 0, 0, 0, boxes), 0u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_VINE, 0, 0, 0, boxes), 0u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_DANDELION, 0, 0, 0, boxes), 0u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_STONE, 0, 0, 0, boxes), 1u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_OAK_LEAVES, 0, 0, 0, boxes), 1u);
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_OAK_SLAB, 0, 0, 0, boxes), 1u);
    CHECK_NEAR(boxes[0].max[2], 0.5f64, PT_EXACT);
    /* Stairs: the lower half, and the top of the north half. */
    CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_OAK_STAIRS, 0, 0, 0, boxes), 2u);
    CHECK_NEAR(boxes[0].max[2], 0.5f64, PT_EXACT);
    CHECK_NEAR(boxes[0].min[1], 0.0f64, PT_EXACT);
    CHECK_NEAR(boxes[1].min[1], 0.5f64, PT_EXACT);
    CHECK_NEAR(boxes[1].max[1], 1.0f64, PT_EXACT);
    CHECK_NEAR(boxes[1].min[2], 0.5f64, PT_EXACT);
    CHECK_NEAR(boxes[1].max[2], 1.0f64, PT_EXACT);
    /* A number no block has stands like a block. */
    CHECK_EQ_U64(collision_block_boxes((uint16_t)BLOCK_COUNT, 0, 0, 0, boxes), 1u);
    CHECK_EQ_U64(collision_block_boxes(UINT16_MAX, 0, 0, 0, boxes), 1u);
}

/* Plan 005 phase 12: each facing puts the step on its own side, and a body comes down on the step there and on the lower half
 * elsewhere. The boxes are whole 32nds, so these are exact. */
TEST(stairs_facing_each_way_collide_with_the_step_on_the_side_they_face) {
    /* Where the step is, on x and y, for north, east, south and west. */
    static const float64_t step[BLOCK_FACINGS][4] = {
        {0.0f64, 0.5f64, 1.0f64, 1.0f64},
        {0.5f64, 0.0f64, 1.0f64, 1.0f64},
        {0.0f64, 0.0f64, 1.0f64, 0.5f64},
        {0.0f64, 0.0f64, 0.5f64, 1.0f64},
    };
    for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
        collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
        const uint16_t stairs = block_state_make(BLOCK_ID_OAK_STAIRS, facing, false, false);
        REQUIRE(collision_block_boxes(stairs, 10, 20, 30, boxes) == 2u);
        /* The lower half is the whole cell, whichever way. */
        CHECK_NEAR(boxes[0].min[0], 10.0f64, PT_EXACT);
        CHECK_NEAR(boxes[0].max[0], 11.0f64, PT_EXACT);
        CHECK_NEAR(boxes[0].min[1], 20.0f64, PT_EXACT);
        CHECK_NEAR(boxes[0].max[1], 21.0f64, PT_EXACT);
        CHECK_NEAR(boxes[0].max[2], 30.5f64, PT_EXACT);
        CHECK_NEAR(boxes[1].min[0], 10.0f64 + step[facing][0], PT_EXACT);
        CHECK_NEAR(boxes[1].min[1], 20.0f64 + step[facing][1], PT_EXACT);
        CHECK_NEAR(boxes[1].max[0], 10.0f64 + step[facing][2], PT_EXACT);
        CHECK_NEAR(boxes[1].max[1], 20.0f64 + step[facing][3], PT_EXACT);
        CHECK_NEAR(boxes[1].min[2], 30.5f64, PT_EXACT);
        CHECK_NEAR(boxes[1].max[2], 31.0f64, PT_EXACT);
        /* A small box coming down in the middle of the step's half stops on the step, and in the other half on the lower half. */
        const float64_t step_x = 10.0f64 + (step[facing][0] + step[facing][2]) * 0.5f64;
        const float64_t step_y = 20.0f64 + (step[facing][1] + step[facing][3]) * 0.5f64;
        /* The middle of the other half: mirrored through the middle of the cell. */
        const float64_t low_x = 21.0f64 - step_x;
        const float64_t low_y = 41.0f64 - step_y;
        const float64_t places[2][2] = {{step_x, step_y}, {low_x, low_y}};
        const float64_t lands[2] = {31.0f64, 30.5f64};
        for (uint32_t p = 0u; p < 2u; p++) {
            const collision_box_t falling = PT_BOX(places[p][0] - 0.1f64, places[p][1] - 0.1f64, 32.0f64,
                                                   places[p][0] + 0.1f64, places[p][1] + 0.1f64, 32.2f64);
            collision_move_t move;
            pt_sweep(boxes, 2u, &falling, 0.0f64, 0.0f64, -3.0f64, &move);
            CHECK(move.hit_z_down != 0u);
            CHECK_NEAR(32.0f64 + move.moved[2], lands[p], PT_EXACT);
        }
    }
    /* An id alone is facing north, as every stairs was. */
    collision_box_t north[COLLISION_BOXES_PER_BLOCK], plain[COLLISION_BOXES_PER_BLOCK];
    REQUIRE(collision_block_boxes(BLOCK_ID_OAK_STAIRS, 0, 0, 0, plain) == 2u);
    REQUIRE(collision_block_boxes(block_state_make(BLOCK_ID_OAK_STAIRS, BLOCK_FACING_NORTH, false, false), 0, 0, 0,
                                  north) == 2u);
    CHECK_MEM_EQ(north, plain, sizeof(plain));
}

/* Plan 005 phase 2: a shut door is its plate, three sixteenths thick, on the side it faces, and an open one is nothing, whichever way it
 * faces and whichever half. The boxes are whole 32nds, so these are exact. */
TEST(a_shut_door_collides_with_its_plate_on_the_side_it_faces_and_an_open_one_not_at_all) {
    /* Where the plate is, low x, low y, high x, high y, for north, east, south and west. */
    static const float64_t plate[BLOCK_FACINGS][4] = {
        {0.0f64, 26.0f64 / 32.0f64, 1.0f64, 1.0f64},
        {26.0f64 / 32.0f64, 0.0f64, 1.0f64, 1.0f64},
        {0.0f64, 0.0f64, 1.0f64, 6.0f64 / 32.0f64},
        {0.0f64, 0.0f64, 6.0f64 / 32.0f64, 1.0f64},
    };
    static const uint16_t doors[] = {BLOCK_ID_WOOD_DOOR, BLOCK_ID_IRON_DOOR, BLOCK_ID_GLASS_DOOR};
    for (uint32_t d = 0u; d < sizeof(doors) / sizeof(doors[0]); d++) {
        CHECK_EQ_U64(block_defs[doors[d]].shape, BLOCK_SHAPE_DOOR);
        for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
            for (uint32_t upper = 0u; upper < 2u; upper++) {
                collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
                const uint16_t shut = block_state_make(doors[d], facing, upper != 0u, false);
                REQUIRE(collision_block_boxes(shut, 10, 20, 30, boxes) == 1u);
                CHECK_NEAR(boxes[0].min[0], 10.0f64 + plate[facing][0], PT_EXACT);
                CHECK_NEAR(boxes[0].min[1], 20.0f64 + plate[facing][1], PT_EXACT);
                CHECK_NEAR(boxes[0].max[0], 10.0f64 + plate[facing][2], PT_EXACT);
                CHECK_NEAR(boxes[0].max[1], 20.0f64 + plate[facing][3], PT_EXACT);
                CHECK_NEAR(boxes[0].min[2], 30.0f64, PT_EXACT);
                CHECK_NEAR(boxes[0].max[2], 31.0f64, PT_EXACT);
                CHECK_EQ_U64(collision_block_boxes(block_state_with_open(shut, true), 10, 20, 30, boxes), 0u);
            }
        }
    }
}

/* ---- In a real world -------------------------------------------------------------------------------------------------- */

static void pt_move(test_world_t *w, const collision_box_t *box, float64_t x, float64_t y, float64_t z,
                    collision_move_t *out) {
    const float64_t delta[COLLISION_AXES] = {x, y, z};
    physics_move(w->physics, w->map, box, delta, out);
}

TEST(a_cell_reads_as_what_is_in_it_with_bedrock_under_the_world_and_air_over_it) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    test_world_fill(t, &w, 1, 1, 1, 1, 1, 1, BLOCK_ID_STONE);
    uint16_t block = UINT16_MAX;
    CHECK(collision_cell_read(w.map, 1, 1, 1, &block));
    CHECK_EQ_U64(block, BLOCK_ID_STONE);
    CHECK(collision_cell_read(w.map, 1, 1, 2, &block));
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK(collision_cell_read(w.map, -1, -1, COLUMN_Z_GAMEPLAY_MIN - 1, &block));
    CHECK_EQ_U64(block, BLOCK_ID_BEDROCK);
    CHECK(collision_cell_read(w.map, -1, -1, COLUMN_Z_GAMEPLAY_MIN - 100000, &block));
    CHECK_EQ_U64(block, BLOCK_ID_BEDROCK);
    block = UINT16_MAX;
    CHECK(collision_cell_read(w.map, -1, -1, COLUMN_Z_GAMEPLAY_MAX + 1, &block));
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    /* Not loaded leaves it alone. */
    block = 77u;
    CHECK(!collision_cell_read(w.map, 3 * 16, 3 * 16, 0, &block));
    CHECK_EQ_U64(block, 77u);
    test_world_end(&w);
}

TEST(no_speed_gets_through_the_thinnest_wall) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* A slab's thickness of stone, standing as a floor at z = 20.5, and a wall one block thick. */
    test_world_fill(t, &w, -8, -8, 20, 8, 8, 20, BLOCK_ID_OAK_SLAB);
    test_world_fill(t, &w, 30, -8, 0, 30, 40, 16, BLOCK_ID_STONE);
    const collision_box_t above = PT_BOX(0.2f64, 0.2f64, 40.0f64, 0.8f64, 0.8f64, 41.8f64);
    collision_move_t move;
    pt_move(&w, &above, 0.0f64, 0.0f64, -4000.0f64, &move);
    CHECK_NEAR(move.moved[2], 20.5f64 - 40.0f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_z_down, 1u);
    /* From below, the slab's underside. */
    const collision_box_t below = PT_BOX(0.2f64, 0.2f64, 10.0f64, 0.8f64, 0.8f64, 11.8f64);
    pt_move(&w, &below, 0.0f64, 0.0f64, 4000.0f64, &move);
    CHECK_NEAR(move.moved[2], 20.0f64 - 11.8f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_z_up, 1u);
    /* Going at it along x, far faster than any step, on a long enough move to have several pieces. */
    const collision_box_t west = PT_BOX(0.2f64, 0.2f64, 5.0f64, 0.8f64, 0.8f64, 6.8f64);
    pt_move(&w, &west, 40.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 30.0f64 - 0.8f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_x, 1u);
    /* A piece that stops on one axis stops it there for all the pieces after it, and the others go on. */
    pt_move(&w, &west, 40.0f64, 20.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 30.0f64 - 0.8f64, PT_EXACT);
    CHECK_NEAR(move.moved[1], 20.0f64, 1.0e-6f64);
    /* The longest move allowed, with a large box, which only pieces can fit in the region. */
    const collision_box_t large = PT_BOX(0.0f64, 0.0f64, 5.0f64, 4.0f64, 4.0f64, 9.0f64);
    pt_move(&w, &large, 4000.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 26.0f64, PT_EXACT);
    test_world_end(&w);
}

/* Plan 005 phase 2: walking north into a shut door stops on its plate, and the same door open lets the body through, in a real world. */
TEST(a_shut_door_stops_a_body_walking_into_it_and_an_open_one_lets_it_through) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    for (uint32_t open = 0u; open < 2u; open++) {
        test_world_fill(t, &w, 5, 5, 1, 5, 5, 1,
                        block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, false, open != 0u));
        test_world_fill(t, &w, 5, 5, 2, 5, 5, 2,
                        block_state_make(BLOCK_ID_WOOD_DOOR, BLOCK_FACING_NORTH, true, open != 0u));
        /* A body as wide as a player's, 1.8 tall, from the cell south of the door, walking 3 north. */
        const collision_box_t body = PT_BOX(5.2f64, 3.2f64, 1.0f64, 5.8f64, 3.8f64, 2.8f64);
        collision_move_t move;
        pt_move(&w, &body, 0.0f64, 3.0f64, 0.0f64, &move);
        if (open == 0u) {
            CHECK_EQ_U64(move.hit_y, 1u);
            CHECK_NEAR(move.moved[1], 5.0f64 + 26.0f64 / 32.0f64 - 3.8f64, PT_EXACT);
        } else {
            CHECK_EQ_U64(move.hit_y, 0u);
            CHECK_NEAR(move.moved[1], 3.0f64, 1.0e-6f64);
        }
    }
    test_world_end(&w);
}

TEST(what_stopped_a_piece_stops_the_pieces_after_it_even_past_the_end_of_the_wall) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* It reaches this wall a tenth of the way along, slides off the end of it soon after, and a single sweep would stay on that line. */
    test_world_fill(t, &w, 10, -8, 0, 10, 8, 16, BLOCK_ID_STONE);
    const collision_box_t box = PT_BOX(5.2f64, 0.2f64, 5.0f64, 5.8f64, 0.8f64, 6.8f64);
    collision_move_t move;
    pt_move(&w, &box, 40.0f64, 30.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 10.0f64 - 5.8f64, PT_EXACT);
    CHECK_NEAR(move.moved[1], 30.0f64, 1.0e-6f64);
    CHECK_EQ_U64(move.hit_x, 1u);
    test_world_end(&w);
}

TEST(nothing_falls_out_of_the_world_and_up_is_open) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    const float64_t bottom = (float64_t)COLUMN_Z_GAMEPLAY_MIN;
    const collision_box_t box = PT_BOX(0.2f64, 0.2f64, bottom + 5.0f64, 0.8f64, 0.8f64, bottom + 6.8f64);
    collision_move_t move;
    pt_move(&w, &box, 0.0f64, 0.0f64, -100.0f64, &move);
    CHECK_NEAR(move.moved[2], -5.0f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_z_down, 1u);
    const float64_t top = (float64_t)COLUMN_Z_GAMEPLAY_MAX;
    const collision_box_t high = PT_BOX(0.2f64, 0.2f64, top - 1.0f64, 0.8f64, 0.8f64, top + 0.8f64);
    pt_move(&w, &high, 0.0f64, 0.0f64, 50.0f64, &move);
    CHECK_NEAR(move.moved[2], 50.0f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_z_up, 0u);
    test_world_end(&w);
}

TEST(a_column_that_is_not_loaded_is_a_wall_on_every_side) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* Chunk 3 along x is at distance 3, and chunk 4 is past the view. */
    const float64_t edge = 4.0f64 * 16.0f64;
    const collision_box_t box = PT_BOX(edge - 2.0f64, 0.2f64, 5.0f64, edge - 1.4f64, 0.8f64, 6.8f64);
    collision_move_t move;
    pt_move(&w, &box, 5.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 1.4f64, PT_EXACT);
    CHECK_EQ_U64(move.hit_x, 1u);
    /* And is not clear to stand in. */
    const collision_box_t past = PT_BOX(edge + 1.0f64, 0.2f64, 5.0f64, edge + 1.6f64, 0.8f64, 6.8f64);
    CHECK(!physics_box_is_clear(w.physics, w.map, &past));
    /* Where it has been loaded, it no longer is. */
    world_map_set_center(w.map, 1, 0);
    test_world_stream_all(t, &w);
    CHECK(physics_box_is_clear(w.physics, w.map, &past));
    pt_move(&w, &box, 5.0f64, 0.0f64, 0.0f64, &move);
    CHECK_NEAR(move.moved[0], 5.0f64, PT_EXACT);
    test_world_end(&w);
}

TEST(a_box_is_clear_only_where_nothing_solid_is) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    test_world_fill(t, &w, 0, 0, 0, 0, 0, 0, BLOCK_ID_STONE);
    test_world_fill(t, &w, 2, 0, 0, 2, 0, 0, BLOCK_ID_WATER);
    test_world_fill(t, &w, 4, 0, 0, 4, 0, 0, BLOCK_ID_OAK_SLAB);
    collision_box_t box = PT_BOX(0.2f64, 0.2f64, 0.2f64, 0.8f64, 0.8f64, 0.8f64);
    CHECK(!physics_box_is_clear(w.physics, w.map, &box));
    box = PT_BOX(2.2f64, 0.2f64, 0.2f64, 2.8f64, 0.8f64, 0.8f64);
    CHECK(physics_box_is_clear(w.physics, w.map, &box));
    /* Standing on the slab is clear, and in it is not. */
    box = PT_BOX(4.2f64, 0.2f64, 0.5f64, 4.8f64, 0.8f64, 0.9f64);
    CHECK(physics_box_is_clear(w.physics, w.map, &box));
    box = PT_BOX(4.2f64, 0.2f64, 0.4f64, 4.8f64, 0.8f64, 0.9f64);
    CHECK(!physics_box_is_clear(w.physics, w.map, &box));
    test_world_end(&w);
}

/* ---- Random sweeps against brute force --------------------------------------------------------------------------------- */

enum : uint32_t {
    PT_RANDOM_SIDE = 12u,
    PT_RANDOM_SWEEPS = 4000u,
    PT_RANDOM_MARCH_STEPS = 4096u,
};

static uint64_t pt_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

/* From 0 up to 1. */
static float64_t pt_random_unit(uint64_t *rng) {
    return (float64_t)(pt_random(rng) >> 11) * 0x1.0p-53f64;
}

/* Whether a box overlaps a solid in the grid, by its cells, and nothing of the code under test. Outside the grid is open. */
static bool pt_grid_overlaps(const uint8_t *grid, float64_t x0, float64_t y0, float64_t z0, float64_t x1, float64_t y1,
                             float64_t z1) {
    const int32_t side = (int32_t)PT_RANDOM_SIDE;
    for (int32_t x = (int32_t)floor(x0); x <= (int32_t)floor(x1); x++) {
        for (int32_t y = (int32_t)floor(y0); y <= (int32_t)floor(y1); y++) {
            for (int32_t z = (int32_t)floor(z0); z <= (int32_t)floor(z1); z++) {
                if (x < 0 || y < 0 || z < 0 || x >= side || y >= side || z >= side) {
                    continue;
                }
                if (grid[(x * side + y) * side + z] != 0u && x0 < (float64_t)x + 1.0f64 && x1 > (float64_t)x &&
                    y0 < (float64_t)y + 1.0f64 && y1 > (float64_t)y && z0 < (float64_t)z + 1.0f64 &&
                    z1 > (float64_t)z) {
                    return true;
                }
            }
        }
    }
    return false;
}

TEST(random_sweeps_end_where_a_march_in_tiny_steps_ends) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    const uint32_t side = PT_RANDOM_SIDE;
    uint8_t *const grid = test_alloc(t, (uint64_t)side * side * side);
    uint64_t rng = 0x1234abcd5678ef01ull;
    for (uint32_t x = 0u; x < side; x++) {
        for (uint32_t y = 0u; y < side; y++) {
            for (uint32_t z = 0u; z < side; z++) {
                if (pt_random(&rng) % 8u == 0u) {
                    grid[(x * side + y) * side + z] = 1u;
                    test_world_fill(t, &w, (int32_t)x, (int32_t)y, (int32_t)z, (int32_t)x, (int32_t)y, (int32_t)z,
                                    BLOCK_ID_STONE);
                }
            }
        }
    }
    uint32_t swept = 0u, stopped = 0u, disagreed = 0u;
    for (uint32_t trial = 0u; trial < PT_RANDOM_SWEEPS; trial++) {
        const float64_t size_x = 0.1f64 + pt_random_unit(&rng) * 1.5f64;
        const float64_t size_y = 0.1f64 + pt_random_unit(&rng) * 1.5f64;
        const float64_t size_z = 0.1f64 + pt_random_unit(&rng) * 2.5f64;
        const float64_t x0 = pt_random_unit(&rng) * (float64_t)side;
        const float64_t y0 = pt_random_unit(&rng) * (float64_t)side;
        const float64_t z0 = pt_random_unit(&rng) * (float64_t)side;
        const collision_box_t box = PT_BOX(x0, y0, z0, x0 + size_x, y0 + size_y, z0 + size_z);
        if (pt_grid_overlaps(grid, box.min[0], box.min[1], box.min[2], box.max[0], box.max[1], box.max[2])) {
            continue; /* a sweep that starts free is what the march can compare */
        }
        const float64_t delta[COLLISION_AXES] = {
            (pt_random_unit(&rng) - 0.5f64) * 12.0f64,
            (pt_random_unit(&rng) - 0.5f64) * 12.0f64,
            (pt_random_unit(&rng) - 0.5f64) * 12.0f64,
        };
        collision_move_t move;
        physics_move(w.physics, w.map, &box, delta, &move);
        swept++;
        stopped += move.hit_x != 0u || move.hit_y != 0u || move.hit_z_down != 0u || move.hit_z_up != 0u ? 1u : 0u;

        /* The march: a tiny step on each axis in turn, kept unless it overlaps. */
        float64_t at[COLLISION_AXES] = {box.min[0], box.min[1], box.min[2]};
        const float64_t sizes[COLLISION_AXES] = {size_x, size_y, size_z};
        uint8_t blocked[COLLISION_AXES] = {0u, 0u, 0u};
        for (uint32_t step = 0u; step < PT_RANDOM_MARCH_STEPS; step++) {
            for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
                if (blocked[a] != 0u) {
                    continue;
                }
                float64_t tried[COLLISION_AXES] = {at[0], at[1], at[2]};
                tried[a] += delta[a] / (float64_t)PT_RANDOM_MARCH_STEPS;
                if (pt_grid_overlaps(grid, tried[0], tried[1], tried[2], tried[0] + sizes[0], tried[1] + sizes[1],
                                     tried[2] + sizes[2])) {
                    blocked[a] = 1u;
                } else {
                    at[a] = tried[a];
                }
            }
        }
        /* One step on each axis of slack, plus what a grazed corner can come to: it is a difference only near a tie. */
        const float64_t slack = 12.0f64 / (float64_t)PT_RANDOM_MARCH_STEPS * 2.0f64;
        bool same = true;
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            same = same && fabs(box.min[a] + move.moved[a] - at[a]) <= slack;
            CHECK(fabs(move.moved[a]) <= fabs(delta[a]) + PT_EXACT);
        }
        disagreed += same ? 0u : 1u;
        /* Wherever it ended, it is clear. */
        const collision_box_t end =
            PT_BOX(box.min[0] + move.moved[0], box.min[1] + move.moved[1], box.min[2] + move.moved[2],
                   box.max[0] + move.moved[0], box.max[1] + move.moved[1], box.max[2] + move.moved[2]);
        CHECK(physics_box_is_clear(w.physics, w.map, &end));
    }
    /* Most boxes start free, and a good share hit something: the test is not passing on nothing. */
    CHECK(swept > PT_RANDOM_SWEEPS / 4u);
    CHECK(stopped > swept / 3u);
    /* A tiny-step march and an exact sweep may part where a box grazes a corner in the same tiny step. In 1 sweep in 200 at most. */
    CHECK(disagreed <= swept / 200u);
    test_world_end(&w);
}

/* ---- Memory ------------------------------------------------------------------------------------------------------------ */

TEST(init_and_deinit_are_a_pair_and_deinit_is_safe_on_zero) {
    heap_counters_t before, after;
    heap_counters_read(&before);
    physics_t *const physics = test_world_aligned(t, sizeof(physics_t));
    physics_deinit(physics);
    REQUIRE(physics_init(physics));
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls + 1u);
    CHECK(physics->owned_region.owned_base != nullptr);
    CHECK(physics->arena_boxes.capacity >= 1ull << 20);
    physics_deinit(physics);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK(physics->owned_region.owned_base == nullptr);
    physics_deinit(physics); /* twice does nothing */
}

TEST(the_largest_box_on_the_largest_step_in_solid_rock_fits_the_region) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    test_world_fill(t, &w, -20, -20, 0, 20, 20, 40, BLOCK_ID_OAK_STAIRS);
    const collision_box_t box = PT_BOX(-4.5f64, -4.5f64, 16.5f64, 3.5f64, 3.5f64, 24.5f64);
    collision_move_t move;
    pt_move(&w, &box, 8.0f64, 8.0f64, 8.0f64, &move);
    CHECK(w.physics->arena_boxes.failed_count == 0u);
    CHECK(w.physics->arena_boxes.peak <= w.physics->arena_boxes.capacity);
    test_world_end(&w);
}

TEST(a_move_never_touches_the_heap_and_keeps_to_its_arena) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    test_world_fill(t, &w, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    const collision_box_t box = PT_BOX(0.2f64, 0.2f64, 0.0f64, 0.8f64, 0.8f64, 1.8f64);
    collision_move_t move;
    pt_move(&w, &box, 1.0f64, 1.0f64, -1.0f64, &move);
    const uint64_t peak = w.physics->arena_boxes.peak;
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 10000u; i++) {
        pt_move(&w, &box, 1.0f64, 1.0f64, -1.0f64, &move);
        CHECK(physics_box_is_clear(w.physics, w.map, &box));
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(w.physics->arena_boxes.peak, peak);
    test_world_end(&w);
}

TEST_MAIN()
