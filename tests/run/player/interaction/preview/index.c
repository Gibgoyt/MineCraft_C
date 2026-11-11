/* ctest run_player_interaction_preview. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "player/index.h"
#include "tables/blocks/index.h"
#include "world/index.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct ip_rig_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
} ip_rig_t;
static_assert(sizeof(ip_rig_t) == 1384);

enum : int32_t {
    /* A place to stand well away from what is being done. */
    IP_FAR = 40,
    /* The block every face is aimed at. */
    IP_X = 10,
    IP_Y = 10,
    IP_Z = 5,
    /* The last x with a column loaded, along y = IP_EDGE_Y. See TEST_WORLD_RADIUS. */
    IP_EDGE_X = 63,
    IP_EDGE_Y = 8,
};

/* Every kind of shape there is to place: a cube, a slab, stairs, a torch that stands or hangs, plates on a wall, what needs ground, what
 * lies on the floor, what is seen through, and the bench of two cells. */
static const uint16_t IP_HELD[] = {
    BLOCK_ID_STONE, BLOCK_ID_OAK_SLAB,    BLOCK_ID_OAK_STAIRS, BLOCK_ID_TORCH, BLOCK_ID_LADDER,
    BLOCK_ID_VINE,  BLOCK_ID_OAK_SAPLING, BLOCK_ID_LILY_PAD,   BLOCK_ID_GLASS, BLOCK_ID_WOODWORKING_BENCH_WEST};

static bool ip_begin(test_context_t *t, ip_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    player_movement_start(&rig->movement, (float64_t)IP_FAR + 0.5f64, (float64_t)IP_FAR + 0.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    memset(&rig->target, 0, sizeof(rig->target));
    player_interaction_start(&rig->hands, 77u);
    return true;
}

static uint16_t ip_voxel(ip_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t voxel = UINT16_MAX;
    (void)world_map_voxel_get(rig->world.map, x, y, z, &voxel);
    return voxel;
}

static void ip_put(test_context_t *t, ip_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    const world_edit_status_t status = world_edit_block_set(&rig->world.world->edit, x, y, z, block);
    CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
}

/* On this face of this cell: axis and the way out of it. */
static void ip_aim(ip_rig_t *rig, int32_t x, int32_t y, int32_t z, uint8_t axis, int8_t sign) {
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    rig->target.hit.block = block_state_id(ip_voxel(rig, x, y, z));
    rig->target.hit.axis = axis;
    rig->target.hit.normal_sign = sign;
}

static player_interaction_input_t ip_input(ip_rig_t *rig, uint8_t look_facing, bool place) {
    return (player_interaction_input_t){.borrowed_target = &rig->target,
                                        .borrowed_movement = &rig->movement,
                                        .place = place ? 1u : 0u,
                                        .look_facing = look_facing,
                                        ._pad0 = {0}};
}

typedef struct ip_counts_t {
    uint32_t ok;      /* 0 */
    uint32_t blocked; /* 4 */
    uint32_t none;    /* 8 */
    uint32_t pairs;   /* 12: OK plans of two cells */
} ip_counts_t;
static_assert(sizeof(ip_counts_t) == 16);

/* The plan, then the click: an OK plan is exactly what goes down, cell for cell and voxel for voxel, for one of the hand; anything else
 * places nothing and keeps the hand. Then what went down goes again, so the next case starts from the same world. */
static void ip_check_case(test_context_t *t, ip_rig_t *rig, uint16_t held, uint8_t look_facing, uint32_t turns,
                          ip_counts_t *counts) {
    player_interaction_start(&rig->hands, 77u);
    player_interaction_hand_give(&rig->hands, held, 8u);
    for (uint32_t i = 0u; i < turns; i++) {
        player_interaction_turn(&rig->hands);
    }
    const player_interaction_input_t look = ip_input(rig, look_facing, false);
    player_place_plan_t plan;
    const player_place_status_t status = player_interaction_place_plan(&rig->hands, rig->world.map, &look, &plan);
    CHECK_EQ_U64(status, plan.status);
    uint16_t before[PLAYER_PLACE_CELLS] = {0};
    for (uint32_t c = 0u; c < plan.count; c++) {
        before[c] = ip_voxel(rig, plan.cells[c][0], plan.cells[c][1], plan.cells[c][2]);
    }
    /* Asking again changes nothing and says the same. */
    player_place_plan_t again;
    (void)player_interaction_place_plan(&rig->hands, rig->world.map, &look, &again);
    CHECK_MEM_EQ(&plan, &again, sizeof(plan));

    const player_interaction_input_t click = ip_input(rig, look_facing, true);
    player_interaction_tick(&rig->hands, &rig->world.world->edit, &click);
    if (status == PLAYER_PLACE_OK) {
        counts->ok++;
        counts->pairs += plan.count == 2u ? 1u : 0u;
        CHECK(plan.count >= 1u);
        CHECK_EQ_U64(rig->hands.hand_count, 7u);
        for (uint32_t c = 0u; c < plan.count; c++) {
            if (!CHECK_EQ_U64(ip_voxel(rig, plan.cells[c][0], plan.cells[c][1], plan.cells[c][2]), plan.voxels[c])) {
                printf("  held %u, look %u, turns %u, cell %u\n", (uint32_t)held, (uint32_t)look_facing, turns, c);
            }
        }
    } else {
        counts->blocked += status == PLAYER_PLACE_BLOCKED ? 1u : 0u;
        counts->none += status == PLAYER_PLACE_NONE ? 1u : 0u;
        CHECK_EQ_U64(rig->hands.hand_count, 8u);
        for (uint32_t c = 0u; c < plan.count; c++) {
            CHECK_EQ_U64(ip_voxel(rig, plan.cells[c][0], plan.cells[c][1], plan.cells[c][2]), before[c]);
        }
    }
    for (uint32_t c = 0u; c < plan.count; c++) {
        if (ip_voxel(rig, plan.cells[c][0], plan.cells[c][1], plan.cells[c][2]) != before[c]) {
            ip_put(t, rig, plan.cells[c][0], plan.cells[c][1], plan.cells[c][2], before[c]);
        }
    }
}

TEST(the_preview_is_where_and_what_the_click_places_on_every_face_for_every_shape_and_facing) {
    ip_rig_t rig;
    REQUIRE(ip_begin(t, &rig));
    ip_put(t, &rig, IP_X, IP_Y, IP_Z, BLOCK_ID_STONE);
    ip_counts_t counts = {0};
    for (uint32_t h = 0u; h < sizeof(IP_HELD) / sizeof(IP_HELD[0]); h++) {
        for (uint8_t axis = 0u; axis < 3u; axis++) {
            for (int8_t sign = -1; sign <= 1; sign += 2) {
                for (uint8_t look = 0u; look < 4u; look++) {
                    for (uint32_t turns = 0u; turns < 4u; turns++) {
                        ip_aim(&rig, IP_X, IP_Y, IP_Z, axis, sign);
                        ip_check_case(t, &rig, IP_HELD[h], look, turns, &counts);
                    }
                }
            }
        }
    }
    printf("  %u OK (%u of two cells), %u blocked, %u with nothing to show\n", counts.ok, counts.pairs, counts.blocked,
           counts.none);
    /* Each kind came up: a torch under a block shows nothing, a sapling on a side has no ground, a bench goes down in pairs. */
    CHECK(counts.ok > 0u);
    CHECK(counts.pairs > 0u);
    CHECK(counts.blocked > 0u);
    CHECK(counts.none > 0u);
    test_world_end(&rig.world);
}

TEST(what_can_be_overwritten_is_previewed_and_placed_in_its_own_cell) {
    ip_rig_t rig;
    REQUIRE(ip_begin(t, &rig));
    ip_put(t, &rig, IP_X, IP_Y, IP_Z - 1, BLOCK_ID_DIRT);
    ip_put(t, &rig, IP_X, IP_Y, IP_Z, BLOCK_ID_TALL_GRASS);
    ip_counts_t counts = {0};
    for (uint32_t h = 0u; h < sizeof(IP_HELD) / sizeof(IP_HELD[0]); h++) {
        for (uint8_t axis = 0u; axis < 3u; axis++) {
            ip_aim(&rig, IP_X, IP_Y, IP_Z, axis, 1);
            player_place_plan_t plan;
            const player_interaction_input_t look = ip_input(&rig, BLOCK_FACING_EAST, false);
            if (player_interaction_place_plan(&rig.hands, rig.world.map, &look, &plan) != PLAYER_PLACE_NONE) {
                CHECK_EQ_I64(plan.cells[0][0], IP_X);
                CHECK_EQ_I64(plan.cells[0][2], IP_Z);
            }
            ip_check_case(t, &rig, IP_HELD[h], BLOCK_FACING_EAST, 0u, &counts);
            ip_put(t, &rig, IP_X, IP_Y, IP_Z, BLOCK_ID_TALL_GRASS);
        }
    }
    CHECK(counts.ok > 0u);
    test_world_end(&rig.world);
}

TEST(placing_that_would_fail_is_blocked_with_its_cells_and_places_nothing) {
    ip_rig_t rig;
    REQUIRE(ip_begin(t, &rig));
    ip_counts_t counts = {0};
    /* Into a column that is not loaded. */
    ip_put(t, &rig, IP_EDGE_X, IP_EDGE_Y, IP_Z, BLOCK_ID_STONE);
    ip_aim(&rig, IP_EDGE_X, IP_EDGE_Y, IP_Z, COLLISION_AXIS_X, 1);
    ip_check_case(t, &rig, BLOCK_ID_STONE, BLOCK_FACING_NORTH, 0u, &counts);
    CHECK_EQ_U64(counts.blocked, 1u);
    player_place_plan_t plan;
    const player_interaction_input_t look = ip_input(&rig, BLOCK_FACING_NORTH, false);
    player_interaction_hand_give(&rig.hands, BLOCK_ID_STONE, 1u);
    CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look, &plan), PLAYER_PLACE_BLOCKED);
    CHECK_EQ_I64(plan.cells[0][0], IP_EDGE_X + 1);
    CHECK_EQ_U64(plan.voxels[0], BLOCK_ID_STONE);
    /* Into the body. */
    ip_put(t, &rig, IP_FAR, IP_FAR, -1, BLOCK_ID_STONE);
    ip_aim(&rig, IP_FAR, IP_FAR, -1, COLLISION_AXIS_Z, 1);
    ip_check_case(t, &rig, BLOCK_ID_STONE, BLOCK_FACING_NORTH, 0u, &counts);
    CHECK_EQ_U64(counts.blocked, 2u);
    /* A torch with no ground under it, aimed at the side of what is not solid: tall grass, so into its cell, with nothing below. */
    ip_put(t, &rig, IP_X, IP_Y, IP_Z, BLOCK_ID_TALL_GRASS);
    ip_aim(&rig, IP_X, IP_Y, IP_Z, COLLISION_AXIS_Y, 1);
    ip_check_case(t, &rig, BLOCK_ID_TORCH, BLOCK_FACING_NORTH, 0u, &counts);
    CHECK_EQ_U64(counts.blocked, 3u);
    /* A bench's second cell taken. Looking north, the second half is at x + 1. */
    ip_put(t, &rig, IP_X + 4, IP_Y, IP_Z, BLOCK_ID_STONE);
    ip_put(t, &rig, IP_X + 5, IP_Y, IP_Z + 1, BLOCK_ID_STONE);
    ip_aim(&rig, IP_X + 4, IP_Y, IP_Z, COLLISION_AXIS_Z, 1);
    ip_check_case(t, &rig, BLOCK_ID_WOODWORKING_BENCH_WEST, BLOCK_FACING_NORTH, 0u, &counts);
    CHECK_EQ_U64(counts.blocked, 4u);
    CHECK_EQ_U64(counts.ok, 0u);
    test_world_end(&rig.world);
}

/* ---- Through the player, as the game asks -------------------------------------------------------------------------------- */

typedef struct ip_player_rig_t {
    test_world_t world; /* 0 */
    player_t *player;   /* 24 */
    int64_t now_ns;     /* 32 */
} ip_player_rig_t;
static_assert(sizeof(ip_player_rig_t) == 40);

/* One tick, looking this way, with or without the right button. */
static void ip_player_tick(ip_player_rig_t *rig, float32_t yaw, float32_t pitch, bool place) {
    const player_intent_t intent = {.yaw = yaw, ._pad0 = {0}};
    rig->now_ns += PLAYER_TICK_NS;
    player_send(rig->player, &intent, rig->now_ns);
    player_look_send(rig->player, yaw, pitch);
    player_hands_send(rig->player, false, place);
    player_interact_tick(rig->player, &rig->world.world->edit, rig->now_ns);
    player_tick(rig->player, rig->world.map, rig->now_ns);
}

/* What the game shows after a tick is what the next click puts down, looking all round, up and down, at a floor and at blocks around. */
TEST(the_player_s_preview_is_what_the_next_click_places_looking_every_way) {
    ip_player_rig_t rig;
    REQUIRE(test_world_begin(t, &rig.world));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    /* Pillars and a step around, at the distance a hand reaches, for sides and tops to aim at. */
    test_world_fill(t, &rig.world, 3, -1, 0, 3, 1, 2, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, -1, 3, 0, 1, 3, 0, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, -3, -1, 0, -3, 0, 1, BLOCK_ID_GLASS);
    rig.player = test_world_aligned(t, sizeof(player_t));
    player_init(rig.player, rig.world.physics);
    player_spawn(rig.player, 0.5f64, 0.5f64, 0.0f64);
    rig.now_ns = 1000000000;
    ip_counts_t counts = {0};
    for (uint32_t h = 0u; h < sizeof(IP_HELD) / sizeof(IP_HELD[0]); h++) {
        for (uint32_t step = 0u; step < 16u; step++) {
            for (uint32_t tilt = 0u; tilt < 4u; tilt++) {
                const float32_t yaw = (float32_t)step * 0.3927f32;
                /* The last straight down, into the body. */
                const float32_t pitch = tilt == 3u ? -1.5f32 : -0.35f32 - 0.4f32 * (float32_t)tilt;
                player_interaction_hand_set(&rig.player->interaction, IP_HELD[h], 8u);
                ip_player_tick(&rig, yaw, pitch, false);
                ip_player_tick(&rig, yaw, pitch, false);
                player_place_plan_t plan;
                const player_place_status_t status = player_place_preview(rig.player, rig.world.map, &plan);
                uint16_t before[PLAYER_PLACE_CELLS] = {0};
                for (uint32_t c = 0u; c < plan.count; c++) {
                    (void)world_map_voxel_get(rig.world.map, plan.cells[c][0], plan.cells[c][1], plan.cells[c][2],
                                              &before[c]);
                }
                ip_player_tick(&rig, yaw, pitch, true);
                const bool placed = rig.player->interaction.hand_count == 7u;
                CHECK_EQ_U64(placed, status == PLAYER_PLACE_OK);
                counts.ok += status == PLAYER_PLACE_OK ? 1u : 0u;
                counts.blocked += status == PLAYER_PLACE_BLOCKED ? 1u : 0u;
                counts.none += status == PLAYER_PLACE_NONE ? 1u : 0u;
                for (uint32_t c = 0u; c < plan.count; c++) {
                    uint16_t now = UINT16_MAX;
                    (void)world_map_voxel_get(rig.world.map, plan.cells[c][0], plan.cells[c][1], plan.cells[c][2],
                                              &now);
                    if (!CHECK_EQ_U64(now, placed ? plan.voxels[c] : before[c])) {
                        printf("  held %u, yaw step %u, tilt %u, cell %u\n", (uint32_t)IP_HELD[h], step, tilt, c);
                    }
                    if (now != before[c]) {
                        (void)world_edit_block_set(&rig.world.world->edit, plan.cells[c][0], plan.cells[c][1],
                                                   plan.cells[c][2], before[c]);
                    }
                }
                /* Let go, and wait out the repeat. */
                for (uint32_t i = 0u; i < 4u; i++) {
                    ip_player_tick(&rig, yaw, pitch, false);
                }
            }
        }
    }
    printf("  through the player: %u OK, %u blocked, %u with nothing to show\n", counts.ok, counts.blocked,
           counts.none);
    CHECK(counts.ok > 100u);
    CHECK(counts.blocked > 0u);
    player_deinit(rig.player);
    test_world_end(&rig.world);
}

TEST_MAIN()
