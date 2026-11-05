/* ctest run_player_interaction_wall_torch. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"
#include "world/wall_torch/index.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Hands with no card ------------------------------------------------------------------------------------------- */

typedef struct wt_rig_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
} wt_rig_t;
static_assert(sizeof(wt_rig_t) == 1384);

static bool wt_begin(test_context_t *t, wt_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    player_movement_start(&rig->movement, 40.5f64, 40.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    memset(&rig->target, 0, sizeof(rig->target));
    player_interaction_start(&rig->hands, 1234u);
    return true;
}

static uint16_t wt_block(wt_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = UINT16_MAX;
    (void)world_map_block_get(rig->world.map, x, y, z, &block);
    return block;
}

static void wt_put(test_context_t *t, wt_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    CHECK_EQ_U64(world_edit_block_set(&rig->world.world->edit, x, y, z, block), WORLD_EDIT_STATUS_CHANGED);
}

static void wt_aim(wt_rig_t *rig, int32_t x, int32_t y, int32_t z, uint8_t axis, int8_t sign) {
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    rig->target.hit.block = wt_block(rig, x, y, z);
    rig->target.hit.axis = axis;
    rig->target.hit.normal_sign = sign;
}

static void wt_tick(wt_rig_t *rig, bool attack, bool place) {
    const player_interaction_input_t input = {.borrowed_target = &rig->target,
                                              .borrowed_movement = &rig->movement,
                                              .attack = attack ? 1u : 0u,
                                              .place = place ? 1u : 0u,
                                              ._pad0 = {0}};
    player_interaction_tick(&rig->hands, &rig->world.world->edit, &input);
}

/* Holds the left button on a block until it goes. */
static void wt_mine(wt_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    wt_aim(rig, x, y, z, COLLISION_AXIS_Z, 1);
    const uint16_t block = wt_block(rig, x, y, z);
    for (uint32_t tick = 0u; tick < 400u && wt_block(rig, x, y, z) == block; tick++) {
        wt_tick(rig, true, false);
    }
}

/* How many torches are waiting in the ring. Anything else there is counted by the second number. */
static uint32_t wt_drops_count(wt_rig_t *rig, const char *name, uint32_t *out_others) {
    uint32_t count = 0u, others = 0u;
    player_drop_t drop;
    while (player_interaction_drop_next(&rig->hands, &drop)) {
        if (strcmp(block_drop_items[drop.item].name, name) == 0) {
            count += drop.count;
        } else {
            others += drop.count;
        }
    }
    *out_others = others;
    return count;
}

typedef struct wt_side_t {
    uint16_t block; /* 0 */
    uint8_t axis;   /* 2 */
    int8_t sign;    /* 3 */
    int32_t x;      /* 4: where it goes, from a stone at (3, 3, 5) */
    int32_t y;      /* 8 */
} wt_side_t;
static_assert(sizeof(wt_side_t) == 12);

static const wt_side_t wt_sides[WALL_TORCH_SIDES] = {
    {BLOCK_ID_TORCH_WALL_EAST, COLLISION_AXIS_X, 1, 4, 3},
    {BLOCK_ID_TORCH_WALL_WEST, COLLISION_AXIS_X, -1, 2, 3},
    {BLOCK_ID_TORCH_WALL_NORTH, COLLISION_AXIS_Y, 1, 3, 4},
    {BLOCK_ID_TORCH_WALL_SOUTH, COLLISION_AXIS_Y, -1, 3, 2},
};

TEST(each_side_gives_its_own_torch_and_the_hand_goes_on_holding_a_torch) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        const wt_side_t *const side = &wt_sides[i];
        player_interaction_start(&rig.hands, 1u);
        player_interaction_hand_give(&rig.hands, BLOCK_ID_TORCH, 2u);
        wt_aim(&rig, 3, 3, 5, side->axis, side->sign);
        wt_tick(&rig, false, true);
        CHECK_EQ_U64(wt_block(&rig, side->x, side->y, 5), side->block);
        CHECK_EQ_U64(rig.hands.hand_block, BLOCK_ID_TORCH);
        CHECK_EQ_U64(rig.hands.hand_count, 1u);
        CHECK_EQ_U64(rig.hands.placed_count, 1u);
        wt_put(t, &rig, side->x, side->y, 5, BLOCK_ID_AIR);
    }
    test_world_end(&rig.world);
}

TEST(a_top_gives_a_torch_on_the_floor_and_an_underside_gives_nothing) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    /* With a floor under the cell, it could stand there: it is the aim that says no. */
    wt_put(t, &rig, 3, 3, 3, BLOCK_ID_STONE);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_TORCH, 2u);
    wt_aim(&rig, 3, 3, 5, COLLISION_AXIS_Z, -1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 3, 3, 4), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig.hands.hand_count, 2u);
    CHECK_EQ_U64(rig.hands.placed_count, 0u);
    wt_aim(&rig, 3, 3, 5, COLLISION_AXIS_Z, 1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 3, 3, 6), BLOCK_ID_TORCH);
    CHECK_EQ_U64(rig.hands.hand_count, 1u);
    CHECK_EQ_U64(rig.hands.placed_count, 1u);
    test_world_end(&rig.world);
}

TEST(only_a_torch_goes_by_the_face) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_DIRT, 1u);
    wt_aim(&rig, 3, 3, 5, COLLISION_AXIS_X, 1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 4, 3, 5), BLOCK_ID_DIRT);
    test_world_end(&rig.world);
}

TEST(on_the_side_of_what_is_not_solid_it_stands_on_a_floor_or_does_not_go) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    player_interaction_hand_give(&rig.hands, BLOCK_ID_TORCH, 4u);
    /* A flower with air around it and no floor beside it. */
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_DANDELION);
    wt_aim(&rig, 3, 3, 5, COLLISION_AXIS_X, 1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 4, 3, 5), BLOCK_ID_AIR);
    CHECK_EQ_U64(rig.hands.hand_count, 4u);
    /* With a floor there. */
    wt_put(t, &rig, 4, 3, 4, BLOCK_ID_STONE);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 4, 3, 5), BLOCK_ID_TORCH);
    CHECK_EQ_U64(rig.hands.hand_count, 3u);
    test_world_end(&rig.world);
}

TEST(tall_grass_is_overwritten_by_whichever_it_comes_to) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    REQUIRE((block_defs[BLOCK_ID_TALL_GRASS].flags & BLOCK_FLAG_REPLACEABLE) != 0u);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_TORCH, 4u);
    /* A wall behind it, as seen from the east. */
    wt_put(t, &rig, 2, 3, 5, BLOCK_ID_STONE);
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_TALL_GRASS);
    wt_aim(&rig, 3, 3, 5, COLLISION_AXIS_X, 1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 3, 3, 5), BLOCK_ID_TORCH_WALL_EAST);
    rig.hands.place_delay = 0u;
    /* No wall behind it, and a floor under it. */
    wt_put(t, &rig, 7, 3, 4, BLOCK_ID_STONE);
    wt_put(t, &rig, 7, 3, 5, BLOCK_ID_TALL_GRASS);
    wt_aim(&rig, 7, 3, 5, COLLISION_AXIS_X, 1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 7, 3, 5), BLOCK_ID_TORCH);
    test_world_end(&rig.world);
}

TEST(it_will_not_wash_water_away_or_go_where_something_is) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    player_interaction_hand_give(&rig.hands, BLOCK_ID_TORCH, 4u);
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    wt_put(t, &rig, 4, 3, 5, BLOCK_ID_WATER);
    wt_put(t, &rig, 2, 3, 5, BLOCK_ID_DIRT);
    wt_aim(&rig, 3, 3, 5, COLLISION_AXIS_X, 1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 4, 3, 5), BLOCK_ID_WATER);
    wt_aim(&rig, 3, 3, 5, COLLISION_AXIS_X, -1);
    wt_tick(&rig, false, true);
    CHECK_EQ_U64(wt_block(&rig, 2, 3, 5), BLOCK_ID_DIRT);
    CHECK_EQ_U64(rig.hands.hand_count, 4u);
    test_world_end(&rig.world);
}

TEST(breaking_the_block_behind_drops_every_torch_it_held_and_no_other) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        wt_put(t, &rig, wt_sides[i].x, wt_sides[i].y, 5, wt_sides[i].block);
    }
    wt_put(t, &rig, 3, 3, 6, BLOCK_ID_TORCH);
    /* Not on this block. In the next cell along, leaning away from it, on a stone of its own. */
    wt_put(t, &rig, 6, 3, 5, BLOCK_ID_STONE);
    wt_put(t, &rig, 5, 3, 5, BLOCK_ID_TORCH_WALL_WEST);
    wt_mine(&rig, 3, 3, 5);
    CHECK_EQ_U64(wt_block(&rig, 3, 3, 5), BLOCK_ID_AIR);
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        CHECK_EQ_U64(wt_block(&rig, wt_sides[i].x, wt_sides[i].y, 5), BLOCK_ID_AIR);
    }
    CHECK_EQ_U64(wt_block(&rig, 3, 3, 6), BLOCK_ID_AIR);
    CHECK_EQ_U64(wt_block(&rig, 5, 3, 5), BLOCK_ID_TORCH_WALL_WEST);
    uint32_t others = 0u;
    CHECK_EQ_U64(wt_drops_count(&rig, "torch", &others), 5u);
    CHECK_EQ_U64(others, 1u); /* the dirt */
    test_world_end(&rig.world);
}

TEST(a_torch_beside_a_broken_block_that_leans_along_it_stays) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    /* Beside the dirt, at (3, 4, 5), on the stone to its west: it is the wrong torch for the dirt to hold. */
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_DIRT);
    wt_put(t, &rig, 2, 4, 5, BLOCK_ID_STONE);
    wt_put(t, &rig, 3, 4, 5, BLOCK_ID_TORCH_WALL_EAST);
    wt_mine(&rig, 3, 3, 5);
    CHECK_EQ_U64(wt_block(&rig, 3, 3, 5), BLOCK_ID_AIR);
    CHECK_EQ_U64(wt_block(&rig, 3, 4, 5), BLOCK_ID_TORCH_WALL_EAST);
    test_world_end(&rig.world);
}

TEST(mined_itself_it_gives_a_torch_and_leaves_the_wall) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        const wt_side_t *const side = &wt_sides[i];
        wt_put(t, &rig, side->x, side->y, 5, side->block);
        wt_mine(&rig, side->x, side->y, 5);
        CHECK_EQ_U64(wt_block(&rig, side->x, side->y, 5), BLOCK_ID_AIR);
        CHECK_EQ_U64(wt_block(&rig, 3, 3, 5), BLOCK_ID_STONE);
        uint32_t others = 0u;
        CHECK_EQ_U64(wt_drops_count(&rig, "torch", &others), 1u);
        CHECK_EQ_U64(others, 0u);
    }
    test_world_end(&rig.world);
}

TEST(what_a_wall_torch_is_lights_like_a_torch_and_nothing_stands_on_it) {
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        const block_def_t *const def = &block_defs[WALL_TORCH_BLOCKS[i]];
        const block_def_t *const floor_torch = &block_defs[BLOCK_ID_TORCH];
        CHECK_EQ_U64(def->light, floor_torch->light);
        CHECK_EQ_U64(def->opacity, floor_torch->opacity);
        CHECK_EQ_U64(def->flags & BLOCK_FLAG_SOLID, 0u);
        CHECK_EQ_U64(def->flags & BLOCK_FLAG_REPLACEABLE, 0u);
        CHECK_EQ_U64(def->drop_count, 1u);
        CHECK_STR_EQ(block_drop_items[block_drops[def->first_drop].item].name, "torch");
    }
}

TEST(the_names_and_the_leaning_agree) {
    static const int32_t expected[WALL_TORCH_SIDES][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        int32_t x = 9, y = 9;
        REQUIRE(wall_torch_out(wt_sides[i].block, &x, &y));
        CHECK_EQ_U64((uint64_t)(int64_t)x, (uint64_t)(int64_t)expected[i][0]);
        CHECK_EQ_U64((uint64_t)(int64_t)y, (uint64_t)(int64_t)expected[i][1]);
        CHECK_EQ_U64(wall_torch_of(x, y), wt_sides[i].block);
    }
    CHECK(!wall_torch_out(BLOCK_ID_TORCH, nullptr, nullptr));
    CHECK(!wall_torch_out(BLOCK_ID_STONE, nullptr, nullptr));
    CHECK(!wall_torch_out((uint16_t)BLOCK_COUNT, nullptr, nullptr));
    CHECK_EQ_U64(wall_torch_of(0, 0), BLOCK_NO_ID);
    CHECK_EQ_U64(wall_torch_of(1, 1), BLOCK_NO_ID);
}

/* ---- The real aim ------------------------------------------------------------------------------------------------- */

/* An eye two and a half away from the middle of a stone's side, looking at it. */
TEST(the_real_aim_at_each_side_puts_the_torch_there_and_can_find_it_again) {
    wt_rig_t rig;
    REQUIRE(wt_begin(t, &rig));
    wt_put(t, &rig, 3, 3, 5, BLOCK_ID_STONE);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_TORCH, 4u);
    /* From the east, the west, the north and the south, as yaw is from +x towards +y. */
    const float32_t half_pi = 1.5707963f32;
    const float32_t yaws[WALL_TORCH_SIDES] = {2.0f32 * half_pi, 0.0f32, -half_pi, half_pi};
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        const wt_side_t *const side = &wt_sides[i];
        const float64_t away = 3.0f64;
        const float64_t eye[3] = {3.5f64 + away * (float64_t)((int32_t)side->x - 3),
                                  3.5f64 + away * (float64_t)((int32_t)side->y - 3), 5.5f64};
        player_camera_target(rig.world.map, eye, yaws[i], 0.0f32, &rig.target);
        REQUIRE(rig.target.hit.result == RAYCAST_RESULT_HIT);
        CHECK_EQ_U64(rig.target.hit.block, BLOCK_ID_STONE);
        rig.hands.place_delay = 0u;
        wt_tick(&rig, false, true);
        CHECK_EQ_U64(wt_block(&rig, side->x, side->y, 5), side->block);
        /* Looking again, the same way, the torch is in the way: its middle is in the way, as it leans out at this height. */
        player_camera_target(rig.world.map, eye, yaws[i], 0.0f32, &rig.target);
        REQUIRE(rig.target.hit.result == RAYCAST_RESULT_HIT);
        CHECK_EQ_U64(rig.target.hit.block, side->block);
        CHECK_EQ_U64((uint64_t)(int64_t)rig.target.hit.cell[0], (uint64_t)(int64_t)side->x);
        CHECK_EQ_U64((uint64_t)(int64_t)rig.target.hit.cell[1], (uint64_t)(int64_t)side->y);
        /* Over its top, the stone is still there to aim at, for the next one. */
        const float64_t higher[3] = {eye[0], eye[1], 5.97f64};
        player_camera_target(rig.world.map, higher, yaws[i], 0.0f32, &rig.target);
        REQUIRE(rig.target.hit.result == RAYCAST_RESULT_HIT);
        CHECK_EQ_U64(rig.target.hit.block, BLOCK_ID_STONE);
    }
    test_world_end(&rig.world);
}

TEST(the_box_aimed_at_holds_the_wall_side_and_nothing_of_the_far_side) {
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        collision_box_t boxes[RAYCAST_BOXES_PER_BLOCK];
        REQUIRE(raycast_block_boxes(wt_sides[i].block, 10, 20, 30, boxes) == 1u);
        int32_t x = 0, y = 0;
        REQUIRE(wall_torch_out(wt_sides[i].block, &x, &y));
        /* It reaches the wall, and stops well short of the far side of the cell. */
        if (x != 0) {
            CHECK_NEAR(x > 0 ? boxes[0].min[0] : boxes[0].max[0], x > 0 ? 10.0f64 : 11.0f64, 0.0f64);
            CHECK(x > 0 ? boxes[0].max[0] <= 10.5f64 : boxes[0].min[0] >= 10.5f64);
        } else {
            CHECK_NEAR(y > 0 ? boxes[0].min[1] : boxes[0].max[1], y > 0 ? 20.0f64 : 21.0f64, 0.0f64);
            CHECK(y > 0 ? boxes[0].max[1] <= 20.5f64 : boxes[0].min[1] >= 20.5f64);
        }
    }
}

/* ---- A real picture ------------------------------------------------------------------------------------------------ */

enum : uint32_t {
    WT_WIDTH = 640u,
    WT_HEIGHT = 360u,
    WT_VIEWS = 4u,
};

typedef struct wt_game_t {
    gpu_bridge_t *bridge;           /* 0 */
    job_system_t *job;              /* 8 */
    world_t *world;                 /* 16 */
    terrain_t *terrain;             /* 24 */
    render_t *render;               /* 32 */
    texture_t *texture;             /* 40 */
    game_t *game;                   /* 48 */
    thread_topology_t *topology;    /* 56 */
    display_script_state_t *state;  /* 64 */
    player_interaction_t *hands;    /* 72 */
    player_movement_t *movement;    /* 80 */
    display_headless_scene_t scene; /* 88 */
    display_buffer_t target;        /* 112 */
} wt_game_t;
static_assert(sizeof(wt_game_t) == 168);

static bool wt_game_begin(test_context_t *t, wt_game_t *g) {
    g->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    g->job = test_world_aligned(t, sizeof(job_system_t));
    g->world = test_world_aligned(t, sizeof(world_t));
    g->terrain = test_world_aligned(t, sizeof(terrain_t));
    g->render = test_world_aligned(t, sizeof(render_t));
    g->texture = test_world_aligned(t, sizeof(texture_t));
    g->game = test_world_aligned(t, sizeof(game_t));
    g->topology = test_alloc(t, sizeof(thread_topology_t));
    g->state = test_alloc(t, sizeof(display_script_state_t));
    g->hands = test_world_aligned(t, sizeof(player_interaction_t));
    g->movement = test_world_aligned(t, sizeof(player_movement_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(g->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(g->topology)) || !CHECK(job_init(g->job, g->topology)) ||
        !CHECK(texture_init(g->texture)) || !CHECK(world_init(g->world, world_limits_default())) ||
        !CHECK(terrain_init(g->terrain, TERRAIN_SEED_DEFAULT)) ||
        !CHECK(render_init(g->render, &g->bridge->device_memory, g->job, render_limits_default()))) {
        return false;
    }
    game_init(g->game, g->world, g->terrain, g->render, g->job, g->texture, g->topology);
    static const game_config_t config = {.render_distance = 4u, .width = WT_WIDTH, .height = WT_HEIGHT, ._pad0 = {0}};
    if (!CHECK(game_open(g->game, &config)) || !CHECK(game_settle(g->game))) {
        return false;
    }
    game_headless_scene(g->game, &g->scene);
    const uint64_t bytes = post_xrgb_bytes(WT_WIDTH, WT_HEIGHT);
    g->target = (display_buffer_t){.owned_map = test_world_aligned(t, bytes),
                                   .map_bytes = bytes,
                                   .pitch = WT_WIDTH * 4u,
                                   .width = WT_WIDTH,
                                   .height = WT_HEIGHT};
    memset(g->state, 0, sizeof(*g->state));
    return true;
}

static void wt_game_end(test_context_t *t, wt_game_t *g) {
    game_close(g->game);
    game_deinit(g->game);
    render_deinit(g->render);
    CHECK(gpu_bridge_exit_check(g->bridge));
    terrain_deinit(g->terrain);
    world_deinit(g->world);
    texture_deinit(g->texture);
    job_deinit(g->job);
    gpu_bridge_deinit(g->bridge);
}

/* One tick and one picture of the game's own scene, as display_headless_run gives them. */
static void wt_game_draw(test_context_t *t, wt_game_t *g, uint32_t tick) {
    display_script_state_begin_tick(g->state, tick);
    const display_headless_tick_t info = {.events = nullptr, .state = g->state, .event_count = 0u, .tick = tick};
    CHECK(g->scene.tick(g->scene.borrowed_context, &info));
    CHECK(g->scene.draw(g->scene.borrowed_context, &info, &g->target));
}

/* How many pixels are the colour of a flame: bright, and far more red than blue. */
static uint32_t wt_flame_pixels(const display_buffer_t *target) {
    uint32_t count = 0u;
    for (uint32_t y = 0u; y < target->height; y++) {
        const uint8_t *const row = (const uint8_t *)target->owned_map + (uint64_t)y * target->pitch;
        for (uint32_t x = 0u; x < target->width; x++) {
            const uint8_t *const pixel = row + (uint64_t)x * 4u;
            /* XRGB in memory: blue, green, red, nothing. */
            if (pixel[2] > 180u && pixel[2] > (uint32_t)pixel[0] + 60u) {
                count++;
            }
        }
    }
    return count;
}

/* Only for someone wanting to look: MC_WALL_TORCH_SHOTS names a directory that exists. */
static void wt_shot_write(test_context_t *t, const display_buffer_t *target, const char *name) {
    const char *const directory = getenv("MC_WALL_TORCH_SHOTS");
    if (directory == nullptr) {
        return;
    }
    const uint64_t capacity = display_png_bytes_needed(target->width, target->height);
    uint8_t *const png = test_alloc(t, capacity);
    uint64_t bytes = 0u;
    REQUIRE(display_png_encode(target->owned_map, target->pitch, target->width, target->height, png, capacity, &bytes));
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    CHECK(file_write_atomic(path, png, bytes) == FILE_OK);
}

/* Placed by the hands, each by a look at a face from a real eye, and drawn by the real renderer, at night in the air. */
TEST(all_four_are_drawn_where_they_hang_and_show_their_flames) {
    wt_game_t *const g = test_alloc(t, sizeof(wt_game_t));
    memset(g, 0, sizeof(*g));
    REQUIRE(wt_game_begin(t, g));
    game_t *const game = g->game;
    /* Midnight, and it stays there: what shows is the flames. */
    game_day_set(game, 0.0f32);
    const int32_t x = (int32_t)floor(game->camera.position[0]);
    const int32_t y = (int32_t)floor(game->camera.position[1]);
    const int32_t z = (int32_t)floor(game->camera.position[2]) + 8;
    const float64_t middle[3] = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)z + 0.5f64};
    world_edit_t *const edit = &g->world->edit;
    REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);

    /* A row of the view from each side. */
    typedef struct wt_view_t {
        float64_t from[2]; /* 0: from the stone's middle */
        float32_t yaw;     /* 16 */
        uint8_t _pad0[4];  /* 20 */
    } wt_view_t;
    static_assert(sizeof(wt_view_t) == 24);
    const float32_t half_pi = 1.5707963f32;
    const wt_view_t views[WT_VIEWS] = {
        {{2.2f64, 1.2f64}, 2.0f32 * half_pi + 0.5f32, {0}},
        {{-2.2f64, -1.2f64}, 0.5f32, {0}},
        {{1.2f64, 2.2f64}, -half_pi - 0.5f32, {0}},
        {{-1.2f64, -2.2f64}, half_pi - 0.5f32, {0}},
    };
    static const char *const names[WT_VIEWS] = {"from_east.png", "from_west.png", "from_north.png", "from_south.png"};

    uint32_t tick = 0u;
    /* What is seen with the stone alone, to compare. */
    game->camera.position[0] = middle[0] + views[0].from[0];
    game->camera.position[1] = middle[1] + views[0].from[1];
    game->camera.position[2] = middle[2] + 0.4f64;
    game->camera.yaw = views[0].yaw;
    game->camera.pitch = -0.1f32;
    wt_game_draw(t, g, tick++);
    const uint32_t flames_before = wt_flame_pixels(&g->target);
    CHECK_EQ_U64(flames_before, 0u);

    player_interaction_start(g->hands, 77u);
    player_interaction_hand_give(g->hands, BLOCK_ID_TORCH, 16u);
    player_movement_start(g->movement, (float64_t)x + 40.5f64, (float64_t)y + 40.5f64, (float64_t)z);
    g->movement->on_ground = 1u;
    const float32_t yaws[WALL_TORCH_SIDES] = {2.0f32 * half_pi, 0.0f32, -half_pi, half_pi};
    for (uint32_t i = 0u; i < WALL_TORCH_SIDES; i++) {
        int32_t out_x = 0, out_y = 0;
        REQUIRE(wall_torch_out(WALL_TORCH_BLOCKS[i], &out_x, &out_y));
        const float64_t eye[3] = {middle[0] + 3.0f64 * (float64_t)out_x, middle[1] + 3.0f64 * (float64_t)out_y,
                                  middle[2]};
        player_target_t target;
        player_camera_target(&g->world->map, eye, yaws[i], 0.0f32, &target);
        REQUIRE(target.hit.result == RAYCAST_RESULT_HIT);
        const player_interaction_input_t input = {
            .borrowed_target = &target, .borrowed_movement = g->movement, .attack = 0u, .place = 1u, ._pad0 = {0}};
        g->hands->place_delay = 0u;
        player_interaction_tick(g->hands, edit, &input);
        uint16_t placed = BLOCK_ID_AIR;
        CHECK(world_map_block_get(&g->world->map, x + out_x, y + out_y, z, &placed) == WORLD_MAP_STATUS_OK);
        CHECK_EQ_U64(placed, WALL_TORCH_BLOCKS[i]);
    }
    /* And one on the floor, on top of the same stone, to see them together. */
    const float64_t above[3] = {middle[0], middle[1], middle[2] + 3.0f64};
    player_target_t from_above;
    player_camera_target(&g->world->map, above, 0.0f32, -half_pi, &from_above);
    REQUIRE(from_above.hit.result == RAYCAST_RESULT_HIT);
    const player_interaction_input_t on_top = {
        .borrowed_target = &from_above, .borrowed_movement = g->movement, .attack = 0u, .place = 1u, ._pad0 = {0}};
    g->hands->place_delay = 0u;
    player_interaction_tick(g->hands, edit, &on_top);
    uint16_t standing = BLOCK_ID_AIR;
    CHECK(world_map_block_get(&g->world->map, x, y, z + 1, &standing) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(standing, BLOCK_ID_TORCH);
    for (uint32_t v = 0u; v < WT_VIEWS; v++) {
        game->camera.position[0] = middle[0] + views[v].from[0];
        game->camera.position[1] = middle[1] + views[v].from[1];
        game->camera.position[2] = middle[2] + 0.4f64;
        game->camera.yaw = views[v].yaw;
        game->camera.pitch = -0.1f32;
        /* Enough ticks for the light to have gone out, and for a picture of what is there now. */
        for (uint32_t i = 0u; i < 4u; i++) {
            wt_game_draw(t, g, tick++);
        }
        const uint32_t flames = wt_flame_pixels(&g->target);
        printf("  %s: %u pixels the colour of a flame\n", names[v], flames);
        CHECK(flames >= 20u);
        wt_shot_write(t, &g->target, names[v]);
    }
    wt_game_end(t, g);
}

TEST_MAIN()
