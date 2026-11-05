/* ctest run_player_movement. See README.md. */
#include "harness/index.h"

#include "run/input/fixtures/fake_evdev.h"
#include "run/input/fixtures/replay.h"
#include "run/physics/fixtures/test_world.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "main/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PM_PI 3.14159265358979f64
#define PM_TICKS_PER_SECOND 20.0f64

static const player_intent_t pm_idle = {
    .yaw = 0.0f32, .forward = 0, .right = 0, .jump = 0u, .sneak = 0u, .sprint = 0u, ._pad0 = {0}};

static void pm_ticks(test_world_t *w, player_movement_t *body, const player_intent_t *intent, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        player_movement_tick(body, w->physics, w->map, intent);
    }
}

/* Stone with its top at z = 0, over all the loaded chunks near the middle. */
static bool pm_begin_flat(test_context_t *t, test_world_t *w) {
    if (!test_world_begin(t, w)) {
        return false;
    }
    test_world_fill(t, w, -32, -32, -1, 40, 40, -1, BLOCK_ID_STONE);
    return true;
}

/* Over the last ticks, blocks per second along the level. */
static float64_t pm_level_speed(test_world_t *w, player_movement_t *body, const player_intent_t *intent,
                                uint32_t ticks) {
    const float64_t x = body->position[0], y = body->position[1];
    pm_ticks(w, body, intent, ticks);
    const float64_t dx = body->position[0] - x, dy = body->position[1] - y;
    return sqrt(dx * dx + dy * dy) * PM_TICKS_PER_SECOND / (float64_t)ticks;
}

/* Over the last ticks, blocks per second up. */
static float64_t pm_rise_speed(test_world_t *w, player_movement_t *body, const player_intent_t *intent,
                               uint32_t ticks) {
    const float64_t z = body->position[2];
    pm_ticks(w, body, intent, ticks);
    return (body->position[2] - z) * PM_TICKS_PER_SECOND / (float64_t)ticks;
}

/* ---- Walking ---------------------------------------------------------------------------------------------------------- */

TEST(walking_sprinting_and_sneaking_reach_their_speeds) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;

    player_movement_start(&body, -30.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_level_speed(&w, &body, &intent, 40u), 4.317f64, 0.002f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_GROUND);
    CHECK_EQ_U64(body.on_ground, 1u);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);
    CHECK_EQ_U64(body.sprinting, 0u);

    intent.sprint = 1u;
    player_movement_start(&body, -30.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_level_speed(&w, &body, &intent, 40u), 5.612f64, 0.002f64);
    CHECK_EQ_U64(body.sprinting, 1u);

    intent.sprint = 0u;
    intent.sneak = 1u;
    player_movement_start(&body, -30.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_level_speed(&w, &body, &intent, 40u), 1.295f64, 0.002f64);
    CHECK_EQ_U64(body.crouched, 1u);

    /* Sideways and backwards never sprint, and neither does a sneak. */
    intent.sneak = 0u;
    intent.sprint = 1u;
    intent.forward = 0;
    intent.right = 1;
    player_movement_start(&body, 0.5f64, 30.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_level_speed(&w, &body, &intent, 40u), 4.317f64, 0.002f64);
    CHECK_EQ_U64(body.sprinting, 0u);
    intent.forward = -1;
    intent.right = 0;
    player_movement_start(&body, 30.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_EQ_U64(body.sprinting, 0u);
    intent.forward = 1;
    intent.sneak = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    CHECK_EQ_U64(body.sprinting, 0u);
    test_world_end(&w);
}

TEST(a_diagonal_is_no_faster_than_straight) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    intent.right = -1;
    player_movement_start(&body, -20.5f64, -20.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_level_speed(&w, &body, &intent, 40u), 4.317f64, 0.002f64);
    test_world_end(&w);
}

TEST(forward_goes_where_it_looks_and_right_goes_to_its_right) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    /* A yaw of 0 looks east, along x. */
    intent.forward = 1;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    CHECK(body.position[0] > 5.0f64);
    CHECK_NEAR(body.position[1], 0.5f64, 1.0e-6f64);
    /* A quarter turn looks north, along y. */
    intent.yaw = (float32_t)(PM_PI / 2.0f64);
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    CHECK(body.position[1] > 5.0f64);
    CHECK_NEAR(body.position[0], 0.5f64, 1.0e-5f64);
    /* Looking east, right is south. */
    intent = pm_idle;
    intent.right = 1;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    CHECK(body.position[1] < -4.0f64);
    /* Back is the other way. */
    intent = pm_idle;
    intent.forward = -1;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    CHECK(body.position[0] < -4.0f64);
    test_world_end(&w);
}

TEST(letting_go_stops_quickly_and_then_entirely) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    player_movement_start(&body, -30.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    const float64_t let_go = body.position[0];
    pm_ticks(&w, &body, &pm_idle, 20u);
    CHECK(body.position[0] - let_go < 0.6f64);
    const player_movement_t still = body;
    pm_ticks(&w, &body, &pm_idle, 10u);
    /* Exactly still: nothing rounds its way along. */
    CHECK(body.position[0] == still.position[0]);
    CHECK(body.position[1] == still.position[1]);
    CHECK(body.position[2] == still.position[2]);
    CHECK(body.velocity[0] == 0.0f64);
    test_world_end(&w);
}

TEST(a_wall_stops_it_and_it_slides_along) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    test_world_fill(t, &w, 5, -32, 0, 5, 40, 3, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 60u);
    CHECK_NEAR(body.position[0], 5.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);
    CHECK(body.velocity[0] == 0.0f64 || fabs(body.velocity[0]) < 0.1f64);
    /* Into it at an angle, it keeps going along it. */
    intent.yaw = (float32_t)(PM_PI / 4.0f64);
    const float64_t y = body.position[1];
    pm_ticks(&w, &body, &intent, 40u);
    CHECK_NEAR(body.position[0], 5.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);
    CHECK(body.position[1] > y + 3.0f64);
    test_world_end(&w);
}

/* ---- Steps, edges, low ceilings ------------------------------------------------------------------------------------------- */

TEST(it_walks_up_a_slab_but_not_a_block) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    test_world_fill(t, &w, 3, -32, 0, 3, 40, 0, BLOCK_ID_OAK_SLAB);
    test_world_fill(t, &w, 4, -32, 0, 7, 40, 0, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    /* Onto the slab, and no further until its next step: a block is half a block above a slab, so that is also a step. */
    float64_t highest = 0.0f64;
    for (uint32_t i = 0u; i < 60u; i++) {
        pm_ticks(&w, &body, &intent, 1u);
        highest = body.position[2] > highest ? body.position[2] : highest;
        CHECK(body.position[2] - body.previous[2] <= 0.6f64);
    }
    CHECK_NEAR(highest, 1.0f64, 1.0e-9f64);
    CHECK(body.position[0] > 8.0f64);

    /* A whole block with no slab before it stops it. */
    test_world_t bare;
    test_world_end(&w);
    REQUIRE(pm_begin_flat(t, &bare));
    test_world_fill(t, &bare, 3, -32, 0, 3, 40, 0, BLOCK_ID_STONE);
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&bare, &body, &intent, 60u);
    CHECK_NEAR(body.position[0], 3.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);
    test_world_end(&bare);
}

TEST(a_jump_at_a_slab_is_a_jump_and_not_a_step) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    test_world_fill(t, &w, 3, -8, 0, 3, 8, 0, BLOCK_ID_OAK_SLAB);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    player_movement_start(&body, 2.7f64 - 0.05f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &pm_idle, 2u);
    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    CHECK_NEAR(body.position[2], 0.42f64, 1.0e-9f64);
    CHECK(body.velocity[2] > 0.3f64);
    CHECK_EQ_U64(body.on_ground, 0u);
    test_world_end(&w);
}

TEST(a_step_must_land_on_something) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* Ground, a slab, and a pit straight after it, at a speed that would carry it over the slab within one tick. */
    test_world_fill(t, &w, -8, -8, -1, 3, 8, -1, BLOCK_ID_STONE);
    test_world_fill(t, &w, 3, -8, 0, 3, 8, 0, BLOCK_ID_OAK_SLAB);
    player_movement_t body;
    player_movement_start(&body, 2.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &pm_idle, 2u);
    CHECK_EQ_U64(body.on_ground, 1u);
    body.velocity[0] = 2.0f64;
    pm_ticks(&w, &body, &pm_idle, 1u);
    CHECK_NEAR(body.position[0], 3.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);
    test_world_end(&w);
}

TEST(it_walks_up_stairs_to_the_north) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    /* Stairs in a row going north, each a whole block higher than the last. */
    for (int32_t i = 0; i < 4; i++) {
        test_world_fill(t, &w, -5, 3 + i, 0, 5, 3 + i, i - 1, BLOCK_ID_STONE);
        test_world_fill(t, &w, -5, 3 + i, i, 5, 3 + i, i, BLOCK_ID_OAK_STAIRS);
    }
    /* And a landing at the top. */
    test_world_fill(t, &w, -5, 7, 0, 5, 30, 3, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.yaw = (float32_t)(PM_PI / 2.0f64);
    intent.forward = 1;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 60u);
    CHECK_NEAR(body.position[2], 4.0f64, 1.0e-9f64);
    CHECK(body.position[1] > 7.0f64);
    test_world_end(&w);
}

TEST(sneaking_will_not_walk_off_an_edge_and_walking_will) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* A platform ending at x = 5, and the ground 3 below. */
    test_world_fill(t, &w, -32, -32, -4, 40, 40, -4, BLOCK_ID_STONE);
    test_world_fill(t, &w, -10, -10, -1, 4, 10, -1, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    intent.sneak = 1u;
    player_movement_start(&body, 2.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 200u);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);
    /* It leans out until only the box's edge is over the platform. */
    CHECK(body.position[0] > 5.0f64);
    CHECK(body.position[0] <= 5.0f64 + PLAYER_MOVEMENT_WIDTH / 2.0f64);
    CHECK_EQ_U64(body.on_ground, 1u);
    /* Diagonally too, along the edge and not over it. */
    intent.yaw = (float32_t)(PM_PI / 4.0f64);
    const float64_t y = body.position[1];
    pm_ticks(&w, &body, &intent, 40u);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);
    CHECK(body.position[1] > y + 0.5f64);
    /* Without sneak it steps off and falls. */
    intent = pm_idle;
    intent.forward = 1;
    pm_ticks(&w, &body, &intent, 40u);
    CHECK_NEAR(body.position[2], -3.0f64, 1.0e-9f64);
    test_world_end(&w);
}

TEST(a_gap_of_a_block_and_a_half_fits_only_crouched_and_it_stays_crouched_inside) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    /* A slab floor and a stone ceiling above it at z = 2, so 1.5 between them. */
    test_world_fill(t, &w, 2, -32, 0, 7, 40, 0, BLOCK_ID_OAK_SLAB);
    test_world_fill(t, &w, 3, -32, 2, 6, 40, 2, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;

    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    /* Standing, its head is at 0.5 + 1.8 and the ceiling at 2. */
    CHECK_NEAR(body.position[0], 3.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);

    intent.sneak = 1u;
    pm_ticks(&w, &body, &intent, 60u);
    CHECK(body.position[0] > 4.0f64);
    CHECK_NEAR(body.position[2], 0.5f64, 1.0e-9f64);
    CHECK_EQ_U64(body.crouched, 1u);

    /* Let go of sneak under it: no room to stand, so no. */
    intent.sneak = 0u;
    intent.forward = 0;
    pm_ticks(&w, &body, &intent, 2u);
    CHECK_EQ_U64(body.crouched, 1u);
    /* And walked out, it stands. */
    intent.forward = 1;
    pm_ticks(&w, &body, &intent, 60u);
    CHECK(body.position[0] > 6.5f64);
    CHECK_EQ_U64(body.crouched, 0u);
    test_world_end(&w);
}

TEST(the_eye_eases_down_when_crouching) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 3u);
    CHECK_NEAR(body.eye_height, PLAYER_MOVEMENT_EYE_STANDING, 1.0e-9f64);
    intent.sneak = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    CHECK(body.eye_height < PLAYER_MOVEMENT_EYE_STANDING && body.eye_height > PLAYER_MOVEMENT_EYE_CROUCHED);
    CHECK_NEAR(body.previous_eye, PLAYER_MOVEMENT_EYE_STANDING, 1.0e-9f64);
    pm_ticks(&w, &body, &intent, 30u);
    CHECK_NEAR(body.eye_height, PLAYER_MOVEMENT_EYE_CROUCHED, 1.0e-6f64);
    test_world_end(&w);
}

/* ---- Jumping and falling ----------------------------------------------------------------------------------------------------- */

TEST(a_jump_is_as_high_and_as_long_as_it_should_be_and_only_from_the_ground) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 3u);
    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    intent.jump = 0u;
    CHECK_NEAR(body.position[2], 0.42f64, 1.0e-9f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_AIR);
    float64_t highest = body.position[2];
    uint32_t ticks = 1u;
    while (body.on_ground == 0u && ticks < 100u) {
        pm_ticks(&w, &body, &intent, 1u);
        highest = body.position[2] > highest ? body.position[2] : highest;
        ticks++;
    }
    CHECK_NEAR(highest, 1.2522f64, 0.0005f64);
    CHECK_EQ_U64(ticks, 12u);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);

    /* Holding it does not jump again in the air, and it goes again on landing. */
    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    const float64_t speed = body.velocity[2];
    pm_ticks(&w, &body, &intent, 1u);
    CHECK(body.velocity[2] < speed);
    pm_ticks(&w, &body, &intent, 10u);
    CHECK_EQ_U64(body.on_ground, 1u);
    pm_ticks(&w, &body, &intent, 1u);
    CHECK_EQ_U64(body.on_ground, 0u);
    CHECK(body.position[2] > 0.3f64);
    test_world_end(&w);
}

TEST(a_jump_clears_one_block_but_not_two) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    test_world_fill(t, &w, 3, -32, 0, 3, 40, 0, BLOCK_ID_STONE);
    test_world_fill(t, &w, 8, -32, 0, 8, 40, 1, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    intent.jump = 1u;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 100u);
    CHECK(body.position[0] > 4.0f64);
    CHECK_NEAR(body.position[0], 8.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);
    test_world_end(&w);
}

TEST(a_sprint_jump_goes_further_with_its_push) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    intent.sprint = 1u;
    player_movement_start(&body, -30.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    const float64_t speed = body.velocity[0];
    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    /* The velocity the jump moved it by has the push in it, on top of the steady run. */
    CHECK_NEAR(body.position[0] - body.previous[0], speed + 0.1274f64 + 0.2f64, 0.001f64);
    test_world_end(&w);
}

TEST(a_head_hitting_a_ceiling_stops_going_up) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    test_world_fill(t, &w, -5, -5, 2, 5, 5, 2, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 2u);
    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    intent.jump = 0u;
    float64_t highest = 0.0f64;
    for (uint32_t i = 0u; i < 20u; i++) {
        pm_ticks(&w, &body, &intent, 1u);
        highest = body.position[2] > highest ? body.position[2] : highest;
    }
    /* Head at 1.8 above the feet, under a ceiling at 2. */
    CHECK_NEAR(highest, 0.2f64, 0.2f64 + 1.0e-9f64);
    CHECK(highest <= 2.0f64 - PLAYER_MOVEMENT_HEIGHT_STANDING + 1.0e-9f64);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);
    test_world_end(&w);
}

TEST(a_fall_has_a_terminal_speed_and_says_how_far_it_was_when_it_lands) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    player_movement_t body;
    /* From the top of the world to the bottom. */
    const float64_t top = (float64_t)COLUMN_Z_GAMEPLAY_MAX, bottom = (float64_t)COLUMN_Z_GAMEPLAY_MIN;
    player_movement_start(&body, 0.5f64, 0.5f64, top);
    float64_t fastest = 0.0f64;
    uint32_t ticks = 0u;
    while (body.on_ground == 0u && ticks < 1000u) {
        const float64_t was = body.fall_distance;
        pm_ticks(&w, &body, &pm_idle, 1u);
        const float64_t fall = body.previous[2] - body.position[2];
        fastest = fall > fastest ? fall : fastest;
        if (body.on_ground == 0u) {
            CHECK(body.fall_distance >= was);
            CHECK(body.landed_fall == 0.0f64);
            CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_AIR);
        }
        ticks++;
    }
    CHECK_NEAR(body.position[2], bottom, 1.0e-9f64);
    /* 78.4 blocks per second is 3.92 per tick. */
    CHECK(fastest * PM_TICKS_PER_SECOND <= 78.4f64);
    CHECK(fastest * PM_TICKS_PER_SECOND > 77.0f64);
    CHECK_NEAR(body.landed_fall, top - bottom, 1.0e-6f64);
    CHECK(body.fall_distance == 0.0f64);
    CHECK(body.velocity[2] < 0.0f64 &&
          body.velocity[2] > -0.1f64); /* the tick's own gravity, and nothing kept from the fall */
    /* For that one tick only. */
    pm_ticks(&w, &body, &pm_idle, 1u);
    CHECK(body.landed_fall == 0.0f64);
    test_world_end(&w);
}

TEST(a_short_step_down_is_no_fall_worth_noting_but_a_jump_counts_only_from_its_top) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    player_movement_start(&body, 0.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 2u);
    CHECK(body.landed_fall == 0.0f64);
    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 1u);
    intent.jump = 0u;
    while (body.on_ground == 0u) {
        pm_ticks(&w, &body, &intent, 1u);
    }
    CHECK_NEAR(body.landed_fall, 1.2522f64, 0.001f64);
    test_world_end(&w);
}

/* ---- Swimming ------------------------------------------------------------------------------------------------------------------ */

static bool pm_begin_pool(test_context_t *t, test_world_t *w, uint16_t fluid) {
    if (!pm_begin_flat(t, w)) {
        return false;
    }
    /* Twenty deep, its top at z = 20, and banks at the sides of x from -10 to 10. */
    test_world_fill(t, w, -10, -30, 0, 10, 30, 19, fluid);
    test_world_fill(t, w, 11, -30, 0, 11, 30, 19, BLOCK_ID_STONE);
    return true;
}

TEST(in_water_it_sinks_slowly_swims_and_rises) {
    test_world_t w;
    REQUIRE(pm_begin_pool(t, &w, BLOCK_ID_WATER));
    player_movement_t body;
    player_intent_t intent = pm_idle;

    player_movement_start(&body, 0.5f64, 0.5f64, 18.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_rise_speed(&w, &body, &intent, 40u), -2.0f64, 0.01f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_WATER);
    CHECK_EQ_U64(body.eyes_under_water, 1u);
    CHECK(body.fall_distance == 0.0f64);

    /* It settles on the bottom. */
    pm_ticks(&w, &body, &intent, 400u);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);

    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_rise_speed(&w, &body, &intent, 40u), 2.0f64, 0.05f64);

    /* Sneak goes down faster than sinking does. */
    intent.jump = 0u;
    intent.sneak = 1u;
    player_movement_start(&body, 0.5f64, 0.5f64, 18.0f64);
    pm_ticks(&w, &body, &intent, 10u);
    const float64_t sneak_sink = pm_rise_speed(&w, &body, &intent, 20u);
    CHECK(sneak_sink < -2.5f64);

    /* Swimming is far slower than walking. */
    intent = pm_idle;
    intent.forward = 1;
    player_movement_start(&body, -9.5f64, 0.5f64, 15.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_level_speed(&w, &body, &intent, 40u), 1.96f64, 0.01f64);
    CHECK_EQ_U64(body.sprinting, 0u);
    test_world_end(&w);
}

TEST(swimming_into_a_bank_at_the_top_hops_out_and_lower_down_does_not) {
    test_world_t w;
    REQUIRE(pm_begin_pool(t, &w, BLOCK_ID_WATER));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    /* Halfway down it only pushes against the bank, and goes on sinking. */
    player_movement_start(&body, 9.5f64, 0.5f64, 10.0f64);
    pm_ticks(&w, &body, &intent, 60u);
    CHECK_NEAR(body.position[0], 11.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);
    CHECK(body.position[2] < 10.0f64);
    /* With the bank's top less than a step above its feet, out it comes with no Space held, and on across the bank. */
    test_world_fill(t, &w, 11, -30, 19, 30, 30, 19, BLOCK_ID_STONE);
    player_movement_start(&body, 11.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 0.5f64, 19.5f64);
    pm_ticks(&w, &body, &intent, 40u);
    CHECK(body.position[0] > 11.0f64);
    CHECK(body.position[2] >= 20.0f64 - 1.0e-9f64);
    /* With Space held, from lower down, too. */
    intent.jump = 1u;
    player_movement_start(&body, 9.5f64, 0.5f64, 15.0f64);
    pm_ticks(&w, &body, &intent, 120u);
    CHECK(body.position[0] > 11.0f64);
    CHECK(body.position[2] >= 20.0f64 - 1.0e-9f64);
    test_world_end(&w);
}

TEST(a_fall_into_water_is_no_fall) {
    test_world_t w;
    REQUIRE(pm_begin_pool(t, &w, BLOCK_ID_WATER));
    player_movement_t body;
    player_movement_start(&body, 0.5f64, 0.5f64, 60.0f64);
    float64_t most = 0.0f64;
    for (uint32_t i = 0u; i < 200u; i++) {
        pm_ticks(&w, &body, &pm_idle, 1u);
        most = body.fall_distance > most ? body.fall_distance : most;
        CHECK(body.landed_fall == 0.0f64);
    }
    CHECK(most > 20.0f64);
    CHECK(body.fall_distance == 0.0f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_WATER);
    test_world_end(&w);
}

TEST(lava_is_slower_than_water) {
    test_world_t w;
    REQUIRE(pm_begin_pool(t, &w, BLOCK_ID_LAVA));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    player_movement_start(&body, 0.5f64, 0.5f64, 18.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    /* Gravity over what the drag leaves: 0.02 / 0.5 per tick. */
    CHECK_NEAR(pm_rise_speed(&w, &body, &intent, 40u), -0.8f64, 0.01f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_LAVA);
    CHECK_EQ_U64(body.eyes_under_water, 0u); /* it is not water */
    intent.forward = 1;
    player_movement_start(&body, -9.5f64, 0.5f64, 15.0f64);
    pm_ticks(&w, &body, &intent, 20u);
    CHECK_NEAR(pm_level_speed(&w, &body, &intent, 40u), 0.784f64, 0.01f64);
    test_world_end(&w);
}

TEST(a_body_beside_water_is_not_in_it) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    test_world_fill(t, &w, 3, -5, 0, 5, 5, 0, BLOCK_ID_WATER);
    player_movement_t body;
    player_movement_start(&body, 3.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &pm_idle, 2u);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_GROUND);
    /* On a block over it neither. */
    player_movement_start(&body, 4.5f64, 0.5f64, 1.0f64);
    test_world_fill(t, &w, 4, 0, 0, 4, 0, 0, BLOCK_ID_STONE);
    pm_ticks(&w, &body, &pm_idle, 2u);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_GROUND);
    test_world_end(&w);
}

/* ---- Climbing ----------------------------------------------------------------------------------------------------------------- */

static bool pm_begin_ladder(test_context_t *t, test_world_t *w, uint16_t climbable) {
    if (!pm_begin_flat(t, w)) {
        return false;
    }
    /* In the cell at (2, 2), against a wall to the east, ten high. */
    test_world_fill(t, w, 2, 2, 0, 2, 2, 9, climbable);
    test_world_fill(t, w, 3, 0, 0, 3, 4, 20, BLOCK_ID_STONE);
    return true;
}

TEST(a_ladder_goes_up_and_down_at_its_speeds_and_holds_on_a_sneak) {
    test_world_t w;
    REQUIRE(pm_begin_ladder(t, &w, BLOCK_ID_LADDER));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    /* Up with Space. */
    intent.jump = 1u;
    player_movement_start(&body, 2.5f64, 2.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 5u);
    CHECK_NEAR(pm_rise_speed(&w, &body, &intent, 40u), 2.35f64, 0.01f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_CLIMB);
    CHECK(body.fall_distance == 0.0f64);
    /* Down, by letting go. */
    intent.jump = 0u;
    pm_ticks(&w, &body, &intent, 8u);
    CHECK_NEAR(pm_rise_speed(&w, &body, &intent, 20u), -3.0f64, 0.01f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_CLIMB);
    /* Held. */
    intent.sneak = 1u;
    pm_ticks(&w, &body, &intent, 2u);
    const float64_t held = body.position[2];
    pm_ticks(&w, &body, &intent, 40u);
    CHECK(body.position[2] == held);
    test_world_end(&w);
}

TEST(walking_into_the_wall_behind_a_ladder_climbs_it) {
    test_world_t w;
    REQUIRE(pm_begin_ladder(t, &w, BLOCK_ID_LADDER));
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1; /* a yaw of 0 goes east, into the wall */
    player_movement_start(&body, 2.5f64, 2.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    CHECK(body.position[2] > 3.0f64);
    /* With no ladder, the same wall is only a wall. */
    player_movement_start(&body, 2.5f64, 0.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 40u);
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);
    test_world_end(&w);
}

TEST(a_vine_climbs_like_a_ladder_a_lily_pad_does_not_and_a_ladder_breaks_a_fall) {
    test_world_t w;
    REQUIRE(pm_begin_ladder(t, &w, BLOCK_ID_VINE));
    test_world_fill(t, &w, -5, -5, 0, -5, -5, 9, BLOCK_ID_LILY_PAD);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.jump = 1u;
    player_movement_start(&body, 2.5f64, 2.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 5u);
    CHECK_NEAR(pm_rise_speed(&w, &body, &intent, 40u), 2.35f64, 0.01f64);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_CLIMB);
    player_movement_start(&body, -4.5f64, -4.5f64, 3.0f64);
    pm_ticks(&w, &body, &pm_idle, 1u);
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_AIR);
    /* Falling onto the top of it from high up: the fall stops counting there, and slows at once. */
    player_movement_start(&body, 2.5f64, 2.5f64, 40.0f64);
    uint32_t ticks = 0u;
    while (body.mode != PLAYER_MOVEMENT_MODE_CLIMB && ticks < 200u) {
        pm_ticks(&w, &body, &pm_idle, 1u);
        ticks++;
    }
    CHECK_EQ_U64(body.mode, PLAYER_MOVEMENT_MODE_CLIMB);
    CHECK(body.fall_distance == 0.0f64);
    pm_ticks(&w, &body, &pm_idle, 1u);
    CHECK_NEAR(body.position[2] - body.previous[2], -0.15f64, 1.0e-9f64);
    while (body.on_ground == 0u && ticks < 400u) {
        pm_ticks(&w, &body, &pm_idle, 1u);
        CHECK(body.landed_fall == 0.0f64);
        ticks++;
    }
    CHECK_NEAR(body.position[2], 0.0f64, 1.0e-9f64);
    test_world_end(&w);
}

/* ---- Waiting for the world ------------------------------------------------------------------------------------------------------- */

TEST(a_body_waits_where_it_is_until_its_column_is_there) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* (3, 3) is further than radius 3 from the center. */
    const float64_t x = 3.0f64 * 16.0f64 + 8.5f64, y = x;
    player_movement_t body;
    player_movement_start(&body, x, y, 50.0f64);
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    intent.jump = 1u;
    pm_ticks(&w, &body, &intent, 40u);
    CHECK_EQ_U64(body.waiting_for_column, 1u);
    CHECK(body.position[0] == x);
    CHECK(body.position[1] == y);
    CHECK(body.position[2] == 50.0f64);
    CHECK(body.velocity[2] == 0.0f64);
    CHECK(body.previous[2] == 50.0f64);
    /* And the moment it arrives, it falls. */
    world_map_set_center(w.map, 1, 1);
    test_world_stream_all(t, &w);
    pm_ticks(&w, &body, &pm_idle, 10u);
    CHECK_EQ_U64(body.waiting_for_column, 0u);
    CHECK(body.position[2] < 50.0f64);
    test_world_end(&w);
}

TEST(it_cannot_walk_into_a_column_that_is_not_there) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* Ground right up to the edge of chunk 3, and chunk 4 is not loaded. */
    const int32_t edge = 4 * 16;
    test_world_fill(t, &w, edge - 16, 0, -1, edge - 1, 15, -1, BLOCK_ID_STONE);
    player_movement_t body;
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    player_movement_start(&body, (float64_t)edge - 8.0f64, 8.5f64, 0.0f64);
    pm_ticks(&w, &body, &intent, 60u);
    CHECK_NEAR(body.position[0], (float64_t)edge - PLAYER_MOVEMENT_WIDTH / 2.0f64, 1.0e-9f64);
    CHECK_EQ_U64(body.waiting_for_column, 0u);
    test_world_end(&w);
}

/* ---- Any input at all ---------------------------------------------------------------------------------------------------------- */

static uint64_t pm_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

/* A rough place: stone, slabs, stairs, water, lava, ladders and leaves, at random, around a floor. */
static void pm_fill_random(test_context_t *t, test_world_t *w, uint64_t *rng) {
    static const uint16_t kinds[] = {BLOCK_ID_STONE,      BLOCK_ID_OAK_SLAB, BLOCK_ID_OAK_STAIRS,
                                     BLOCK_ID_WATER,      BLOCK_ID_LAVA,     BLOCK_ID_LADDER,
                                     BLOCK_ID_OAK_LEAVES, BLOCK_ID_VINE,     BLOCK_ID_DANDELION};
    const uint32_t count = sizeof(kinds) / sizeof(kinds[0]);
    test_world_fill(t, w, -32, -32, -1, 40, 40, -1, BLOCK_ID_BEDROCK);
    for (uint32_t i = 0u; i < 3000u; i++) {
        const int32_t x = (int32_t)(pm_random(rng) % 40u) - 20;
        const int32_t y = (int32_t)(pm_random(rng) % 40u) - 20;
        const int32_t z = (int32_t)(pm_random(rng) % 8u);
        const uint16_t kind = kinds[pm_random(rng) % count];
        test_world_fill(t, w, x, y, z, x, y, z, kind);
    }
}

static player_intent_t pm_random_intent(uint64_t *rng) {
    const uint64_t bits = pm_random(rng);
    player_intent_t intent = pm_idle;
    intent.yaw = (float32_t)(bits & 0xffffu) * 0.0001f32;
    intent.forward = (int8_t)((bits >> 16) % 3u) - 1;
    intent.right = (int8_t)((bits >> 20) % 3u) - 1;
    intent.jump = (bits >> 24) % 3u == 0u ? 1u : 0u;
    intent.sneak = (bits >> 28) % 5u == 0u ? 1u : 0u;
    intent.sprint = (bits >> 32) % 2u == 0u ? 1u : 0u;
    return intent;
}

TEST(any_input_through_any_place_stays_clear_of_everything_and_gives_the_same_bytes) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    uint64_t rng = 0xfeedface12345678ull;
    pm_fill_random(t, &w, &rng);
    player_movement_t runs[2];
    uint8_t modes_seen = 0u;
    heap_counters_t before, after;
    for (uint32_t run = 0u; run < 2u; run++) {
        uint64_t intents = 0xabad1dea87654321ull;
        player_movement_start(&runs[run], 0.5f64, 0.5f64, 12.0f64);
        heap_counters_read(&before);
        for (uint32_t tick = 0u; tick < 6000u; tick++) {
            /* Not every tick: a body holds a key for a while. */
            static player_intent_t intent;
            if (tick % 8u == 0u) {
                intent = pm_random_intent(&intents);
            }
            player_movement_tick(&runs[run], w.physics, w.map, &intent);
            modes_seen |= (uint8_t)(1u << runs[run].mode);
            const float64_t height =
                runs[run].crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
            collision_box_t box;
            player_movement_box(runs[run].position, height, &box);
            /* It started clear, so it is clear now. */
            if (!CHECK(physics_box_is_clear(w.physics, w.map, &box))) {
                test_world_end(&w);
                return;
            }
        }
        heap_counters_read(&after);
        CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    }
    CHECK_MEM_EQ(&runs[0], &runs[1], sizeof(player_movement_t));
    /* It really went everywhere it could. */
    CHECK_EQ_U64(modes_seen & 0x1fu, 0x1fu);
    test_world_end(&w);
}

/* ---- The threads' side -------------------------------------------------------------------------------------------------------------- */

static player_t *pm_player_begin(test_context_t *t, test_world_t *w, float64_t x, float64_t y, float64_t z) {
    player_t *const player = test_world_aligned(t, sizeof(player_t));
    player_deinit(player); /* safe on zero */
    player_init(player, w->physics);
    player_spawn(player, x, y, z);
    /* Main reads a slot the sim does not write. */
    CHECK(player->view_slot != player->views.back);
    return player;
}

TEST(what_is_sent_arrives_whole) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_t *const player = pm_player_begin(t, &w, 0.5f64, 0.5f64, 0.0f64);
    int64_t now = 1000;
    /* Negative numbers in their bytes, and a yaw with all the bits on. */
    player_intent_t intent = pm_idle;
    intent.yaw = -1.5707964f32;
    intent.forward = -1;
    intent.right = -1;
    player_send(player, &intent, now);
    player_tick(player, w.map, now + PLAYER_TICK_NS);
    /* A yaw of -90 degrees looks south. Back is north, and left is east... */
    for (uint32_t i = 0u; i < 20u; i++) {
        now += PLAYER_TICK_NS;
        player_send(player, &intent, now);
        player_tick(player, w.map, now);
    }
    CHECK(player->movement.position[1] > 3.0f64);
    /* ...and left, looking south, is east: the same in each. */
    CHECK(player->movement.position[0] > 3.0f64);
    CHECK_NEAR(player->movement.position[0] - 0.5f64, player->movement.position[1] - 0.5f64, 1.0e-3f64);
    /* Each flag on its own. */
    intent = pm_idle;
    intent.sneak = 1u;
    player_send(player, &intent, now);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->movement.crouched, 1u);
    CHECK_EQ_U64(player->movement.sprinting, 0u);
    intent = pm_idle;
    intent.forward = 1;
    intent.sprint = 1u;
    player_send(player, &intent, now);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->movement.sprinting, 1u);
    CHECK_EQ_U64(player->movement.crouched, 0u);
    CHECK_EQ_U64(player->tick_count, 23u);
    player_deinit(player);
    CHECK(player->borrowed_physics == nullptr);
    test_world_end(&w);
}

TEST(a_tap_between_two_ticks_still_jumps_once) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_t *const player = pm_player_begin(t, &w, 0.5f64, 0.5f64, 0.0f64);
    const int64_t now = 1000;
    player_send(player, &pm_idle, now);
    player_tick(player, w.map, now);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->movement.on_ground, 1u);
    /* Down and up, both before the next tick. */
    player_intent_t intent = pm_idle;
    intent.jump = 1u;
    player_send(player, &intent, now);
    player_send(player, &pm_idle, now);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->movement.on_ground, 0u);
    CHECK_NEAR(player->movement.position[2], 0.42f64, 1.0e-9f64);
    /* Once. */
    for (uint32_t i = 0u; i < 11u; i++) {
        player_tick(player, w.map, now);
    }
    CHECK_EQ_U64(player->movement.on_ground, 1u);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->movement.on_ground, 1u);
    test_world_end(&w);
}

TEST(with_no_one_at_the_controls_it_lets_go) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_t *const player = pm_player_begin(t, &w, 0.5f64, 0.5f64, 0.0f64);
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    int64_t now = 1000;
    player_send(player, &intent, now);
    /* Fresh enough, right up to the limit. */
    player_tick(player, w.map, now + PLAYER_INTENT_STALE_NS);
    CHECK(player->movement.position[0] > 0.5f64);
    for (uint32_t i = 0u; i < 20u; i++) {
        player_tick(player, w.map, now + PLAYER_INTENT_STALE_NS + 1);
    }
    /* Past it, it stops. */
    const float64_t stopped = player->movement.position[0];
    player_tick(player, w.map, now + PLAYER_INTENT_STALE_NS + 1);
    CHECK(player->movement.position[0] == stopped);
    test_world_end(&w);
}

TEST(the_eye_goes_evenly_from_a_ticks_start_to_its_end_across_the_tick_after) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_t *const player = pm_player_begin(t, &w, 0.5f64, 0.5f64, 0.0f64);
    float64_t eye[3];
    /* Before any tick it is where it stands. */
    player_eye_at(player, 123456, eye);
    CHECK_NEAR(eye[0], 0.5f64, 1.0e-12f64);
    CHECK_NEAR(eye[2], PLAYER_MOVEMENT_EYE_STANDING, 1.0e-12f64);

    player_intent_t intent = pm_idle;
    intent.forward = 1;
    const int64_t start = 1000000000;
    player_send(player, &intent, start);
    for (int64_t i = 0; i < 20; i++) {
        player_send(player, &intent, start + i * PLAYER_TICK_NS);
        player_tick(player, w.map, start + i * PLAYER_TICK_NS);
    }
    const int64_t due = start + 19 * PLAYER_TICK_NS;
    const float64_t from = player->movement.previous[0], to = player->movement.position[0];
    CHECK(to > from);
    player_eye_at(player, due, eye);
    CHECK_NEAR(eye[0], from, 1.0e-12f64);
    player_eye_at(player, due + PLAYER_TICK_NS / 4, eye);
    CHECK_NEAR(eye[0], from + (to - from) * 0.25f64, 1.0e-9f64);
    player_eye_at(player, due + PLAYER_TICK_NS / 2, eye);
    CHECK_NEAR(eye[0], from + (to - from) * 0.5f64, 1.0e-9f64);
    player_eye_at(player, due + PLAYER_TICK_NS, eye);
    CHECK_NEAR(eye[0], to, 1.0e-12f64);
    /* A late tick holds it at the end, and it never goes back or past. */
    player_eye_at(player, due + 100 * PLAYER_TICK_NS, eye);
    CHECK_NEAR(eye[0], to, 1.0e-12f64);
    /* A clock read just before the tick's time holds it at the start. */
    player_eye_at(player, due - PLAYER_TICK_NS, eye);
    CHECK_NEAR(eye[0], from, 1.0e-12f64);
    test_world_end(&w);
}

TEST(a_picture_at_any_rate_never_goes_back_and_never_jumps) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_t *const player = pm_player_begin(t, &w, -30.5f64, 0.5f64, 0.0f64);
    player_intent_t intent = pm_idle;
    intent.forward = 1;
    const int64_t start = 1000000000;
    /* Pictures at 144 Hz, ticks at 20. */
    const int64_t frame_ns = 1000000000 / 144;
    int64_t next_tick = start;
    float64_t last = 0.0f64;
    uint32_t frames = 0u;
    for (int64_t now = start; now < start + 2000000000; now += frame_ns) {
        player_send(player, &intent, now);
        while (next_tick <= now) {
            player_tick(player, w.map, next_tick);
            next_tick += PLAYER_TICK_NS;
        }
        float64_t eye[3];
        player_eye_at(player, now, eye);
        if (frames != 0u) {
            CHECK(eye[0] >= last);
            /* No frame moves further than the top speed lets it in a frame, with a little for rounding. */
            CHECK(eye[0] - last <= 4.4f64 * (float64_t)frame_ns / 1.0e9f64);
        }
        last = eye[0];
        frames++;
    }
    CHECK(frames > 280u);
    CHECK(last > -30.5f64 + 7.0f64);
    test_world_end(&w);
}

TEST(the_eye_is_above_the_feet_and_crouching_lowers_it_between_ticks) {
    test_world_t w;
    REQUIRE(pm_begin_flat(t, &w));
    player_t *const player = pm_player_begin(t, &w, 0.5f64, 0.5f64, 0.0f64);
    player_intent_t intent = pm_idle;
    const int64_t now = 1000000000;
    player_send(player, &intent, now);
    player_tick(player, w.map, now);
    float64_t eye[3];
    player_eye_at(player, now + PLAYER_TICK_NS, eye);
    CHECK_NEAR(eye[2], PLAYER_MOVEMENT_EYE_STANDING, 1.0e-9f64);
    intent.sneak = 1u;
    player_send(player, &intent, now);
    player_tick(player, w.map, now);
    player_eye_at(player, now, eye);
    const float64_t high = eye[2];
    player_eye_at(player, now + PLAYER_TICK_NS, eye);
    CHECK(eye[2] < high);
    CHECK_NEAR(high, PLAYER_MOVEMENT_EYE_STANDING, 1.0e-9f64);
    test_world_end(&w);
}

/* ---- The game --------------------------------------------------------------------------------------------------------------------- */

enum : uint32_t {
    PM_WIDTH = 160u,
    PM_HEIGHT = 90u,
    PM_PROGRAM_TIMEOUT_MS = 120000u,
};

/* A rig: everything a real run has, on memory of its own. */
typedef struct pm_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64 */
    player_t *player;            /* 72 */
} pm_rig_t;
static_assert(sizeof(pm_rig_t) == 80);

static bool pm_rig_begin(test_context_t *t, pm_rig_t *rig) {
    rig->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    rig->job = test_world_aligned(t, sizeof(job_system_t));
    rig->world = test_world_aligned(t, sizeof(world_t));
    rig->terrain = test_world_aligned(t, sizeof(terrain_t));
    rig->render = test_world_aligned(t, sizeof(render_t));
    rig->texture = test_world_aligned(t, sizeof(texture_t));
    rig->game = test_world_aligned(t, sizeof(game_t));
    rig->physics = test_world_aligned(t, sizeof(physics_t));
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    return true;
}

static void pm_rig_end(test_context_t *t, pm_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static const game_config_t pm_small_game = {
    .render_distance = 5u, .width = PM_WIDTH, .height = PM_HEIGHT, .show_date = 0u};

static float64_t pm_flat_distance(const float64_t from[3], const float64_t to[3]) {
    const float64_t x = to[0] - from[0], y = to[1] - from[1];
    return sqrt(x * x + y * y);
}

/* Runs the real frames until the body's column has come and it has landed, for a few seconds at most. */
static void pm_frames_until_standing(test_context_t *t, pm_rig_t *rig, display_buffer_t *target) {
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    const int64_t give_up = time_now_ns() + 30000000000;
    while (time_now_ns() < give_up &&
           (rig->player->movement.waiting_for_column != 0u || rig->player->tick_count == 0u ||
            (rig->player->movement.on_ground == 0u && rig->player->movement.mode == PLAYER_MOVEMENT_MODE_AIR))) {
        game_walk_send(rig->game, &none, false, false, time_now_ns());
        CHECK(game_loop_frame(rig->game, &none, 0.0f32, target));
        time_sleep_ns(2000000);
    }
    CHECK(time_now_ns() < give_up);
}

TEST(the_real_threads_walk_a_real_world_at_the_speed_of_the_tick) {
    pm_rig_t rig;
    REQUIRE(pm_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &pm_small_game));
    player_t *const player = rig.player;
    CHECK_NEAR(game->camera.position[0], player->movement.position[0], 1.0e-12f64);
    CHECK_NEAR(game->camera.position[1], player->movement.position[1], 1.0e-12f64);
    CHECK_NEAR(game->camera.position[2], player->movement.position[2] + PLAYER_MOVEMENT_EYE_STANDING, 1.0e-12f64);
    const float64_t spawned = player->movement.position[2];
    const uint64_t bytes = post_xrgb_bytes(PM_WIDTH, PM_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = PM_WIDTH * 4u,
                               .width = PM_WIDTH,
                               .height = PM_HEIGHT};
    REQUIRE(game_loop_start(game, nullptr));
    pm_frames_until_standing(t, &rig, &target);
    /* It came down on the dry land it was put on, and went through nothing. */
    CHECK_NEAR(player->movement.position[2], spawned, 1.0e-9f64);
    CHECK_EQ_U64(player->movement.mode, PLAYER_MOVEMENT_MODE_GROUND);
    CHECK_EQ_U64(player->movement.eyes_under_water, 0u);

    /* The ticks keep their rate. Reading these while the sim runs is a race the test accepts: a count, and only ever going up. */
    render_fly_input_t input;
    memset(&input, 0, sizeof(input));
    input.forward = 1;
    const float64_t from[3] = {game->camera.position[0], game->camera.position[1], game->camera.position[2]};
    const uint64_t ticks_from = player->tick_count;
    const int64_t started = time_now_ns();
    float64_t last_x = from[0], last_y = from[1];
    uint32_t frames_moved = 0u;
    uint32_t frames = 0u;
    while (time_now_ns() < started + 2000000000) {
        game_walk_send(game, &input, false, false, time_now_ns());
        CHECK(game_loop_frame(game, &input, 0.0f32, &target));
        const float64_t dx = game->camera.position[0] - last_x, dy = game->camera.position[1] - last_y;
        frames_moved += dx != 0.0f64 || dy != 0.0f64 ? 1u : 0u;
        last_x = game->camera.position[0];
        last_y = game->camera.position[1];
        frames++;
        time_sleep_ns(4000000);
    }
    const float64_t seconds = time_ns_to_seconds(time_now_ns() - started);
    const uint64_t ticks = player->tick_count - ticks_from;
    /* 20 Hz, whatever the frames were doing. */
    CHECK_NEAR((float64_t)ticks / seconds, 20.0f64, 2.0f64);
    /* The camera is the body's: it stayed below what walking gets, and a wall on the way may have held it to less. */
    const float64_t walked = pm_flat_distance(from, game->camera.position);
    CHECK(walked > 0.5f64);
    CHECK(walked <= 4.4f64 * seconds);
    /* It moves on most frames, which is many more than there were ticks: with no interpolation it could only move on a tick. How
     * large any one step is depends on how late a frame was on a busy machine, so that is left to the test with its own clock. */
    CHECK(frames > 80u);
    CHECK(frames_moved > ticks * 2u);
    CHECK(game_loop_stop(game));
    game_close(game);
    pm_rig_end(t, &rig);
}

TEST(real_keys_through_the_source_walk_and_jump_and_the_mouse_turns_at_once) {
    pm_rig_t rig;
    REQUIRE(pm_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &pm_small_game));
    /* The fixtures' other helpers, which only their own tests need. */
    (void)fake_record;
    (void)fake_caps_touchpad;
    (void)fake_caps_power_button;
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    fake_device_t *const keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = test_world_aligned(t, sizeof(input_t));
    REQUIRE(input_init(input, FAKE_DIRECTORY, &fake_ops, fake));
    game_loop_source_begin(game, input);
    const uint64_t bytes = post_xrgb_bytes(PM_WIDTH, PM_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = PM_WIDTH * 4u,
                               .width = PM_WIDTH,
                               .height = PM_HEIGHT};
    CHECK(game_loop_source(game, &target));
    CHECK_EQ_U64(game->threads_running, 1u);
    const int64_t give_up = time_now_ns() + 30000000000;
    while (time_now_ns() < give_up &&
           (rig.player->movement.waiting_for_column != 0u || rig.player->movement.on_ground == 0u)) {
        CHECK(game_loop_source(game, &target));
        time_sleep_ns(2000000);
    }

    /* The mouse turns the camera on the frame it arrives, with no tick. */
    const float32_t yaw_start = game->camera.yaw;
    fake_replay(mouse, replay_mouse_moves, REPLAY_COUNT(replay_mouse_moves));
    CHECK(game_loop_source(game, &target));
    CHECK(game->camera.yaw != yaw_start);

    /* Space leaves the ground, and flies nothing: the camera follows the body. */
    static const struct input_event space[] = {REPLAY_EVENT(EV_KEY, KEY_SPACE, 1), REPLAY_SYN};
    static const struct input_event space_up[] = {REPLAY_EVENT(EV_KEY, KEY_SPACE, 0), REPLAY_SYN};
    fake_replay(keyboard, space, REPLAY_COUNT(space));
    bool left_ground = false;
    const int64_t jump_until = time_now_ns() + 400000000;
    while (time_now_ns() < jump_until) {
        CHECK(game_loop_source(game, &target));
        left_ground = left_ground || rig.player->movement.on_ground == 0u;
        time_sleep_ns(2000000);
    }
    CHECK(left_ground);
    fake_replay(keyboard, space_up, REPLAY_COUNT(space_up));

    /* W walks, by the tick and not by how many frames there were. */
    fake_replay(keyboard, replay_w_held, REPLAY_COUNT(replay_w_held));
    const float64_t from[3] = {rig.player->movement.position[0], rig.player->movement.position[1], 0.0f64};
    const int64_t walk_until = time_now_ns() + 1000000000;
    while (time_now_ns() < walk_until) {
        CHECK(game_loop_source(game, &target));
        time_sleep_ns(2000000);
    }
    const float64_t to[3] = {rig.player->movement.position[0], rig.player->movement.position[1], 0.0f64};
    CHECK(pm_flat_distance(from, to) > 0.2f64);
    CHECK(pm_flat_distance(from, to) <= 6.0f64);

    static const struct input_event esc[] = {REPLAY_EVENT(EV_KEY, KEY_ESC, 1), REPLAY_SYN};
    fake_replay(keyboard, esc, REPLAY_COUNT(esc));
    CHECK(!game_loop_source(game, &target));
    CHECK(game_loop_stop(game));
    input_deinit(input);
    game_close(game);
    pm_rig_end(t, &rig);
}

TEST(without_a_player_the_game_flies_as_it_always_did) {
    pm_rig_t rig;
    REQUIRE(pm_rig_begin(t, &rig));
    game_t *const game = rig.game;
    game_player_set(game, nullptr);
    REQUIRE(game_open(game, &pm_small_game));
    render_fly_input_t input;
    memset(&input, 0, sizeof(input));
    input.forward = 1;
    const float64_t x = game->camera.position[0];
    game_fly(game, &input, 1.0f32);
    CHECK(game->camera.position[0] != x);
    CHECK_EQ_U64(rig.player->tick_count, 0u);
    game_close(game);
    pm_rig_end(t, &rig);
}

/* ---- The built program, under --headless ------------------------------------------------------------------------------------------ */

typedef struct pm_temp_dir_t {
    char path[64]; /* 0 */
} pm_temp_dir_t;
static_assert(sizeof(pm_temp_dir_t) == 64);

static bool pm_temp_dir_make(pm_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_player_movement_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void pm_temp_dir_remove(const pm_temp_dir_t *dir) {
    DIR *const handle = opendir(dir->path);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir->path, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir->path);
}

/* Stands, walks, jumps, turns and walks on, with screenshots along the way. It draws few ticks' worth, on purpose: it shows nothing
 * depends on a draw. */
static const char pm_walk_script[] = "0 screenshot start.png\n"
                                     "10 key w down\n"
                                     "30 key space down\n"
                                     "31 key space up\n"
                                     "40 move 300 0\n"
                                     "70 key lctrl down\n"
                                     "90 key w up\n"
                                     "90 screenshot walked.png\n"
                                     "100 end\n";

static void pm_program_walk(test_context_t *t, const pm_temp_dir_t *dir, const char *script, bool walk,
                            test_process_t *out) {
    char *const walking[] = {t->borrowed_args[0],
                             (char *)"--headless",
                             (char *)"--test-world",
                             (char *)"--walk",
                             (char *)"--render-distance",
                             (char *)"4",
                             (char *)"--script",
                             (char *)script,
                             (char *)"--screenshot-dir",
                             (char *)dir->path,
                             (char *)"--width",
                             (char *)"160",
                             (char *)"--height",
                             (char *)"90",
                             nullptr};
    char *const flying[] = {t->borrowed_args[0],
                            (char *)"--headless",
                            (char *)"--test-world",
                            (char *)"--render-distance",
                            (char *)"4",
                            (char *)"--script",
                            (char *)script,
                            (char *)"--screenshot-dir",
                            (char *)dir->path,
                            (char *)"--width",
                            (char *)"160",
                            (char *)"--height",
                            (char *)"90",
                            nullptr};
    test_process_run(t, walk ? walking : flying, PM_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *pm_file_read(test_context_t *t, const pm_temp_dir_t *dir, const char *name, uint64_t *out_length) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = 16ull << 20;
    uint8_t *const bytes = test_alloc(t, capacity);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, out_length), FILE_OK)) {
        *out_length = 0u;
    }
    return bytes;
}

TEST(the_program_walks_a_script_and_gives_the_same_bytes_each_time) {
    pm_temp_dir_t first, second, flown;
    REQUIRE(pm_temp_dir_make(&first));
    REQUIRE(pm_temp_dir_make(&second));
    REQUIRE(pm_temp_dir_make(&flown));
    char script[128];
    snprintf(script, sizeof(script), "%s/walk.script", first.path);
    REQUIRE(file_write_atomic(script, pm_walk_script, sizeof(pm_walk_script) - 1u) == FILE_OK);
    test_process_t run;
    pm_program_walk(t, &first, script, true, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "101 frames, 2 screenshots");
    CHECK_CONTAINS(run.output, "the player is at");
    CHECK_CONTAINS(run.output, "after 101 ticks");
    CHECK_CONTAINS(run.output, "0 bytes live");
    /* Where it ended, as the program says it, and the same the second time, to the digit. */
    const char *const at = strstr(run.output, "the player is at");
    REQUIRE(at != nullptr);
    char first_place[128];
    snprintf(first_place, sizeof(first_place), "%.*s", (int32_t)(strcspn(at, "\n")), at);
    pm_program_walk(t, &second, script, true, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, first_place);
    uint64_t start_a = 0u, start_b = 0u, walked_a = 0u, walked_b = 0u;
    const uint8_t *const start_first = pm_file_read(t, &first, "start.png", &start_a);
    const uint8_t *const start_second = pm_file_read(t, &second, "start.png", &start_b);
    const uint8_t *const walked_first = pm_file_read(t, &first, "walked.png", &walked_a);
    const uint8_t *const walked_second = pm_file_read(t, &second, "walked.png", &walked_b);
    REQUIRE(start_a == start_b && walked_a == walked_b && start_a != 0u && walked_a != 0u);
    CHECK_MEM_EQ(start_first, start_second, start_a);
    CHECK_MEM_EQ(walked_first, walked_second, walked_a);
    /* And walking changed the picture. */
    CHECK(start_a != walked_a || memcmp(start_first, walked_first, start_a) != 0);
    /* And it is not the picture of the same script flying. */
    pm_program_walk(t, &flown, script, false, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    uint64_t flew = 0u;
    const uint8_t *const flew_bytes = pm_file_read(t, &flown, "walked.png", &flew);
    CHECK(flew != walked_a || memcmp(flew_bytes, walked_first, flew) != 0);
    pm_temp_dir_remove(&first);
    pm_temp_dir_remove(&second);
    pm_temp_dir_remove(&flown);
}

TEST(the_program_refuses_a_walk_with_no_world) {
    test_process_t run;
    char *const alone[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks",
                           (char *)"1",         (char *)"--walk",     nullptr};
    test_process_run(t, alone, PM_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "--walk only means something with --test-world");
}

TEST_MAIN()
