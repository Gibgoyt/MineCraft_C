/* ctest run_player_interaction_forward. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "player/index.h"
#include "tables/blocks/index.h"
#include "world/index.h"
#include "world/block_state/index.h"
#include "world/wall_torch/index.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum : int32_t {
    /* The bridge's level: the platform's top block and every block of the bridge are at this z, in water this deep below it. */
    FW_LEVEL = -1,
    FW_WATER_FLOOR = -8,
    /* The platform is x from FW_PLATFORM_WEST to 0, at y = 0; the water from there east to FW_WATER_EAST. */
    FW_PLATFORM_WEST = -3,
    FW_WATER_EAST = 40,
    FW_WATER_HALF = 6,
    FW_BRIDGE = 20,
};

/* The look a person bridging has: level is pitch 0 and this is down a little, so that the aim goes past the bridge's end. */
#define FW_PITCH_AHEAD (-0.5f32)

/* ---- Through the player, as the game asks ---------------------------------------------------------------------------------- */

typedef struct fw_rig_t {
    test_world_t world; /* 0 */
    player_t *player;   /* 24 */
    int64_t now_ns;     /* 32 */
} fw_rig_t;
static_assert(sizeof(fw_rig_t) == 40);

/* A stone platform on a lake, and a body on its east end facing east. */
static bool fw_begin(test_context_t *t, fw_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    test_world_fill(t, &rig->world, FW_PLATFORM_WEST - 4, -FW_WATER_HALF, FW_WATER_FLOOR - 1, FW_WATER_EAST,
                    FW_WATER_HALF, FW_WATER_FLOOR - 1, BLOCK_ID_STONE);
    test_world_fill(t, &rig->world, FW_PLATFORM_WEST - 4, -FW_WATER_HALF, FW_WATER_FLOOR, FW_WATER_EAST, FW_WATER_HALF,
                    FW_LEVEL, BLOCK_ID_WATER);
    test_world_fill(t, &rig->world, FW_PLATFORM_WEST, 0, FW_WATER_FLOOR, 0, 0, FW_LEVEL, BLOCK_ID_STONE);
    rig->player = test_world_aligned(t, sizeof(player_t));
    player_init(rig->player, rig->world.physics);
    player_spawn(rig->player, 0.3f64, 0.5f64, (float64_t)(FW_LEVEL + 1));
    rig->now_ns = 1000000000;
    return true;
}

static void fw_end(fw_rig_t *rig) {
    player_deinit(rig->player);
    test_world_end(&rig->world);
}

static uint16_t fw_block(fw_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = UINT16_MAX;
    (void)world_map_block_get(rig->world.map, x, y, z, &block);
    return block;
}

/* One tick of a person at the controls. */
static void fw_tick(fw_rig_t *rig, float32_t yaw, float32_t pitch, int8_t forward, bool sneak, bool place) {
    const player_intent_t intent = {.yaw = yaw, .forward = forward, .sneak = sneak ? 1u : 0u, ._pad0 = {0}};
    rig->now_ns += PLAYER_TICK_NS;
    player_send(rig->player, &intent, rig->now_ns);
    player_look_send(rig->player, yaw, pitch);
    player_hands_send(rig->player, false, place);
    player_interact_tick(rig->player, &rig->world.world->edit, rig->now_ns);
    player_tick(rig->player, rig->world.map, rig->now_ns);
}

/* How many cells in the lake's box are not water, the platform and the floor aside: what went in. */
static uint32_t fw_placed_anywhere(fw_rig_t *rig) {
    uint32_t count = 0u;
    for (int32_t z = FW_WATER_FLOOR; z <= FW_LEVEL + 3; z++) {
        for (int32_t y = -FW_WATER_HALF; y <= FW_WATER_HALF; y++) {
            for (int32_t x = 1; x <= FW_WATER_EAST; x++) {
                const uint16_t block = fw_block(rig, x, y, z);
                count += block != BLOCK_ID_WATER && block != BLOCK_ID_AIR ? 1u : 0u;
            }
        }
    }
    return count;
}

/* Walks east on the lake, holding FW_BRIDGE planks and clicking every few ticks while looking ahead. Before every click what the game
 * shows is what the click places. It stops walking when the hand is empty. */
static void fw_bridge_walk(test_context_t *t, bool sneak, uint32_t click_every) {
    fw_rig_t rig;
    REQUIRE(fw_begin(t, &rig));
    player_interaction_hand_set(&rig.player->interaction, BLOCK_ID_OAK_PLANKS, FW_BRIDGE);
    float64_t lowest = 1.0e9f64;
    uint32_t shown = 0u;
    for (uint32_t tick = 0u; tick < 2000u && rig.player->interaction.hand_count != 0u; tick++) {
        const bool click = tick % click_every == 0u;
        player_place_plan_t plan;
        const player_place_status_t status = player_place_preview(rig.player, rig.world.map, &plan);
        const uint32_t before = rig.player->interaction.hand_count;
        fw_tick(&rig, 0.0f32, FW_PITCH_AHEAD, 1, sneak, click);
        if (rig.player->interaction.hand_count != before) {
            /* Whatever went down was shown blue first, there and as it went. */
            CHECK_EQ_U64(status, PLAYER_PLACE_OK);
            CHECK_EQ_U64(fw_block(&rig, plan.cells[0][0], plan.cells[0][1], plan.cells[0][2]), BLOCK_ID_OAK_PLANKS);
            CHECK_EQ_I64(plan.cells[0][1], 0);
            CHECK_EQ_I64(plan.cells[0][2], FW_LEVEL);
            shown++;
        }
        if (click) {
            fw_tick(&rig, 0.0f32, FW_PITCH_AHEAD, 1, sneak, false);
        }
        lowest = rig.player->movement.position[2] < lowest ? rig.player->movement.position[2] : lowest;
    }
    printf("  %s, clicking every %u ticks: %u placed, at x = %.2f, lowest feet %.3f\n", sneak ? "sneaking" : "walking",
           click_every, shown, (libc_double_t)rig.player->movement.position[0], (libc_double_t)lowest);
    CHECK_EQ_U64(rig.player->interaction.hand_count, 0u);
    CHECK_EQ_U64(shown, FW_BRIDGE);
    /* A straight line with no gap, and nothing anywhere else. */
    for (int32_t x = 1; x <= FW_BRIDGE; x++) {
        if (!CHECK_EQ_U64(fw_block(&rig, x, 0, FW_LEVEL), BLOCK_ID_OAK_PLANKS)) {
            printf("  gap at x = %d\n", x);
        }
    }
    CHECK_EQ_U64(fw_placed_anywhere(&rig), FW_BRIDGE);
    /* It never fell in. */
    CHECK(lowest > (float64_t)(FW_LEVEL + 1) - 1.0e-6f64);
    fw_end(&rig);
}

TEST(walking_a_bridge_and_placing_ahead_every_few_steps_builds_a_straight_line_with_no_gap) {
    fw_bridge_walk(t, false, 3u);
    fw_bridge_walk(t, false, 1u);
}

TEST(sneaking_stops_at_the_edge_and_still_places_ahead) {
    fw_bridge_walk(t, true, 2u);
    fw_bridge_walk(t, true, 7u);
}

/* At the platform's edge, looking east, at each pitch from straight down to straight up: level and below may place ahead, above never. */
TEST(looking_up_places_nothing_and_level_or_down_places_ahead) {
    fw_rig_t rig;
    REQUIRE(fw_begin(t, &rig));
    uint32_t ahead = 0u, above = 0u;
    for (int32_t step = -15; step <= 15; step++) {
        const float32_t pitch = (float32_t)step * 0.1f32;
        player_interaction_hand_set(&rig.player->interaction, BLOCK_ID_OAK_PLANKS, 8u);
        fw_tick(&rig, 0.0f32, pitch, 0, false, false);
        fw_tick(&rig, 0.0f32, pitch, 0, false, false);
        player_place_plan_t plan;
        const player_place_status_t status = player_place_preview(rig.player, rig.world.map, &plan);
        const bool missed = rig.player->target.hit.result == RAYCAST_RESULT_MISS;
        fw_tick(&rig, 0.0f32, pitch, 0, false, true);
        const bool placed = rig.player->interaction.hand_count == 7u;
        CHECK_EQ_U64(placed, status == PLAYER_PLACE_OK);
        if (pitch > 0.0f32) {
            /* Up: the aim finds nothing and nothing goes anywhere. */
            if (!CHECK(missed) || !CHECK_EQ_U64(status, PLAYER_PLACE_NONE)) {
                printf("  pitch %.1f\n", (libc_double_t)pitch);
            }
            above++;
        } else if (missed) {
            if (!CHECK_EQ_U64(status, PLAYER_PLACE_OK) || !CHECK_EQ_I64(plan.cells[0][0], 1) ||
                !CHECK_EQ_I64(plan.cells[0][2], FW_LEVEL)) {
                printf("  pitch %.1f\n", (libc_double_t)pitch);
            }
            ahead++;
        }
        if (placed) {
            (void)world_edit_block_set(&rig.world.world->edit, plan.cells[0][0], plan.cells[0][1], plan.cells[0][2],
                                       BLOCK_ID_WATER);
        }
        for (uint32_t i = 0u; i < 4u; i++) {
            fw_tick(&rig, 0.0f32, pitch, 0, false, false);
        }
    }
    printf("  %u pitches placed ahead, %u above level placed nothing\n", ahead, above);
    /* Level itself is one of those that place ahead. */
    CHECK(ahead >= 5u);
    CHECK_EQ_U64(above, 15u);
    CHECK_EQ_U64(fw_placed_anywhere(&rig), 0u);
    fw_end(&rig);
}

/* ---- The plan alone, on a target set by hand ---------------------------------------------------------------------------------- */

typedef struct fw_hands_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
} fw_hands_t;
static_assert(sizeof(fw_hands_t) == 1384);

static bool fw_hands_begin(test_context_t *t, fw_hands_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    memset(&rig->target, 0, sizeof(rig->target));
    player_interaction_start(&rig->hands, 5u);
    player_movement_start(&rig->movement, 0.5f64, 0.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    return true;
}

static uint16_t fw_hands_block(fw_hands_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t voxel = UINT16_MAX;
    (void)world_map_voxel_get(rig->world.map, x, y, z, &voxel);
    return voxel;
}

static player_interaction_input_t fw_input(fw_hands_t *rig, uint8_t look_facing, bool place) {
    return (player_interaction_input_t){.borrowed_target = &rig->target,
                                        .borrowed_movement = &rig->movement,
                                        .place = place ? 1u : 0u,
                                        .look_facing = look_facing,
                                        .look_ahead = 1u,
                                        ._pad0 = {0}};
}

static void fw_aim(fw_hands_t *rig, int32_t x, int32_t y, int32_t z, uint8_t axis, int8_t sign) {
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    (void)world_map_block_get(rig->world.map, x, y, z, &rig->target.hit.block);
    rig->target.hit.axis = axis;
    rig->target.hit.normal_sign = sign;
}

/* Every face of a block in reach, every look: with an aim, looking ahead or not makes no difference at all. */
TEST(with_a_block_in_the_aim_it_is_used_and_not_the_cell_ahead) {
    fw_hands_t rig;
    REQUIRE(fw_hands_begin(t, &rig));
    /* Standing on the east edge of one block, with air ahead, and a block to aim at up and to the north. */
    test_world_fill(t, &rig.world, 0, 0, -1, 0, 0, -1, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 2, 2, 1, 2, 2, 1, BLOCK_ID_STONE);
    uint32_t compared = 0u, moved = 0u;
    for (uint8_t axis = 0u; axis < 3u; axis++) {
        for (int8_t sign = -1; sign <= 1; sign += 2) {
            for (uint8_t look = 0u; look < 4u; look++) {
                player_interaction_hand_set(&rig.hands, BLOCK_ID_OAK_PLANKS, 8u);
                fw_aim(&rig, 2, 2, 1, axis, sign);
                player_interaction_input_t with = fw_input(&rig, look, false);
                player_interaction_input_t without = with;
                without.look_ahead = 0u;
                player_place_plan_t a, b;
                (void)player_interaction_place_plan(&rig.hands, rig.world.map, &with, &a);
                (void)player_interaction_place_plan(&rig.hands, rig.world.map, &without, &b);
                CHECK_MEM_EQ(&a, &b, sizeof(a));
                compared++;
                /* The click too: where the aim says, never ahead. */
                const player_interaction_input_t click = fw_input(&rig, look, true);
                player_interaction_tick(&rig.hands, &rig.world.world->edit, &click);
                if (a.status == PLAYER_PLACE_OK) {
                    CHECK_EQ_U64(fw_hands_block(&rig, a.cells[0][0], a.cells[0][1], a.cells[0][2]),
                                 BLOCK_ID_OAK_PLANKS);
                    (void)world_edit_block_set(&rig.world.world->edit, a.cells[0][0], a.cells[0][1], a.cells[0][2],
                                               BLOCK_ID_AIR);
                    moved++;
                }
                CHECK_EQ_U64(fw_hands_block(&rig, block_facing_step_x(look), block_facing_step_y(look), -1),
                             BLOCK_ID_AIR);
                rig.hands.place_delay = 0u;
            }
        }
    }
    printf("  %u aims compared, %u placed where aimed\n", compared, moved);
    CHECK(moved > 0u);
    /* And with the aim gone, the same look places ahead. */
    memset(&rig.target, 0, sizeof(rig.target));
    player_place_plan_t plan;
    const player_interaction_input_t look = fw_input(&rig, BLOCK_FACING_EAST, false);
    CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look, &plan), PLAYER_PLACE_OK);
    CHECK_EQ_I64(plan.cells[0][0], 1);
    CHECK_EQ_I64(plan.cells[0][1], 0);
    CHECK_EQ_I64(plan.cells[0][2], -1);
    /* An aim into a column not loaded is not a miss: nothing ahead either. */
    rig.target.hit.result = RAYCAST_RESULT_NOT_LOADED;
    CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look, &plan), PLAYER_PLACE_NONE);
    test_world_end(&rig.world);
}

/* Whether a whole block in this cell and the body overlap: touching is not. */
static bool fw_overlaps_body(const player_movement_t *movement, const int32_t cell[COLLISION_AXES]) {
    const float64_t half = PLAYER_MOVEMENT_WIDTH * 0.5f64;
    const float64_t height =
        movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
    const float64_t low[COLLISION_AXES] = {movement->position[0] - half, movement->position[1] - half,
                                           movement->position[2]};
    const float64_t high[COLLISION_AXES] = {movement->position[0] + half, movement->position[1] + half,
                                            movement->position[2] + height};
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        if (high[a] <= (float64_t)cell[a] || low[a] >= (float64_t)cell[a] + 1.0f64) {
            return false;
        }
    }
    return true;
}

/* On a cube and on a slab, standing and crouched, from the middle of the block to past its edge, looking each way: an OK plan never
 * has the body in its cell, and a click there places it. On the slab the cell ahead reaches up into the body near the edge, and that
 * is red, and the click places nothing. */
TEST(the_cell_ahead_is_never_inside_the_body) {
    fw_hands_t rig;
    REQUIRE(fw_hands_begin(t, &rig));
    uint32_t ok = 0u, blocked = 0u;
    for (uint32_t floor = 0u; floor < 2u; floor++) {
        const uint16_t under = floor == 0u ? BLOCK_ID_STONE : BLOCK_ID_OAK_SLAB;
        const float64_t feet = floor == 0u ? 0.0f64 : -0.5f64;
        test_world_fill(t, &rig.world, 0, 0, -1, 0, 0, -1, under);
        for (uint32_t crouched = 0u; crouched < 2u; crouched++) {
            for (uint8_t look = 0u; look < 4u; look++) {
                const int32_t step_x = block_facing_step_x(look), step_y = block_facing_step_y(look);
                for (uint32_t i = 0u; i <= 39u; i++) {
                    /* Out from the middle towards the edge the look faces, to where the feet are all but off. */
                    const float64_t out = (float64_t)i * 0.02f64;
                    player_movement_start(&rig.movement, 0.5f64 + out * (float64_t)step_x,
                                          0.5f64 + out * (float64_t)step_y, feet);
                    rig.movement.on_ground = 1u;
                    rig.movement.crouched = (uint8_t)crouched;
                    memset(&rig.target, 0, sizeof(rig.target));
                    player_interaction_hand_set(&rig.hands, BLOCK_ID_OAK_PLANKS, 8u);
                    rig.hands.place_delay = 0u;
                    const player_interaction_input_t look_in = fw_input(&rig, look, false);
                    player_place_plan_t plan;
                    const player_place_status_t status =
                        player_interaction_place_plan(&rig.hands, rig.world.map, &look_in, &plan);
                    if (!CHECK(status != PLAYER_PLACE_NONE)) {
                        printf("  floor %u, look %u, out %.2f\n", floor, (uint32_t)look, (libc_double_t)out);
                        continue;
                    }
                    CHECK_EQ_I64(plan.cells[0][0], step_x);
                    CHECK_EQ_I64(plan.cells[0][1], step_y);
                    CHECK_EQ_I64(plan.cells[0][2], -1);
                    const bool inside = fw_overlaps_body(&rig.movement, plan.cells[0]);
                    CHECK_EQ_U64(status == PLAYER_PLACE_BLOCKED, inside);
                    const player_interaction_input_t click = fw_input(&rig, look, true);
                    player_interaction_tick(&rig.hands, &rig.world.world->edit, &click);
                    const uint16_t now = fw_hands_block(&rig, step_x, step_y, -1);
                    if (status == PLAYER_PLACE_OK) {
                        ok++;
                        CHECK(!inside);
                        CHECK_EQ_U64(now, BLOCK_ID_OAK_PLANKS);
                        (void)world_edit_block_set(&rig.world.world->edit, step_x, step_y, -1, BLOCK_ID_AIR);
                    } else {
                        blocked++;
                        CHECK_EQ_U64(now, BLOCK_ID_AIR);
                        CHECK_EQ_U64(rig.hands.hand_count, 8u);
                    }
                }
            }
        }
    }
    printf("  %u placed ahead, %u red with the body in the way\n", ok, blocked);
    CHECK(ok > 0u);
    CHECK(blocked > 0u);
    test_world_end(&rig.world);
}

/* In the air over a block, in water, and on nothing at all: nothing to show and nothing placed, looking any way. On a wide floor, with
 * ground ahead, nothing either: only an edge has a cell ahead. */
TEST(nothing_is_placed_ahead_with_nothing_under_the_player_or_ground_ahead) {
    fw_hands_t rig;
    REQUIRE(fw_hands_begin(t, &rig));
    test_world_fill(t, &rig.world, 0, 0, -1, 0, 0, -1, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 10, 0, -1, 10, 0, -1, BLOCK_ID_WATER);
    test_world_fill(t, &rig.world, 18, -2, -1, 22, 2, -1, BLOCK_ID_STONE);
    /* Over the stone in a jump; in water; over air; on the wide floor. */
    static const float64_t PLACES[][3] = {
        {0.5f64, 0.5f64, 0.6f64},  {0.5f64, 0.5f64, 1.2f64},  {10.5f64, 0.5f64, 0.0f64},
        {30.5f64, 0.5f64, 0.0f64}, {20.5f64, 0.5f64, 0.0f64}, {20.9f64, 0.1f64, 0.0f64},
    };
    for (uint32_t p = 0u; p < sizeof(PLACES) / sizeof(PLACES[0]); p++) {
        for (uint8_t look = 0u; look < 4u; look++) {
            player_movement_start(&rig.movement, PLACES[p][0], PLACES[p][1], PLACES[p][2]);
            memset(&rig.target, 0, sizeof(rig.target));
            player_interaction_hand_set(&rig.hands, BLOCK_ID_OAK_PLANKS, 8u);
            rig.hands.place_delay = 0u;
            const player_interaction_input_t look_in = fw_input(&rig, look, false);
            player_place_plan_t plan;
            if (!CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look_in, &plan),
                              PLAYER_PLACE_NONE)) {
                printf("  place %u, look %u: cell %d %d %d\n", p, (uint32_t)look, plan.cells[0][0], plan.cells[0][1],
                       plan.cells[0][2]);
            }
            const player_interaction_input_t click = fw_input(&rig, look, true);
            player_interaction_tick(&rig.hands, &rig.world.world->edit, &click);
            CHECK_EQ_U64(rig.hands.hand_count, 8u);
        }
    }
    test_world_end(&rig.world);
}

/* What goes down ahead goes down as it would aimed at the side of the block underfoot: stairs face the way the player looks, turned
 * by R, a ladder lies on that block's side, and a torch hangs from it. */
TEST(what_goes_ahead_faces_as_anything_placed_does) {
    fw_hands_t rig;
    REQUIRE(fw_hands_begin(t, &rig));
    test_world_fill(t, &rig.world, 0, 0, -1, 0, 0, -1, BLOCK_ID_STONE);
    for (uint8_t look = 0u; look < 4u; look++) {
        const int32_t step_x = block_facing_step_x(look), step_y = block_facing_step_y(look);
        for (uint32_t turns = 0u; turns < 4u; turns++) {
            memset(&rig.target, 0, sizeof(rig.target));
            player_interaction_hand_set(&rig.hands, BLOCK_ID_OAK_STAIRS, 8u);
            for (uint32_t i = 0u; i < turns; i++) {
                player_interaction_turn(&rig.hands);
            }
            const player_interaction_input_t look_in = fw_input(&rig, look, false);
            player_place_plan_t ahead;
            CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look_in, &ahead), PLAYER_PLACE_OK);
            /* The same look aimed at that side of the same block. */
            fw_aim(&rig, 0, 0, -1, step_x != 0 ? (uint8_t)COLLISION_AXIS_X : (uint8_t)COLLISION_AXIS_Y,
                   (int8_t)(step_x != 0 ? step_x : step_y));
            player_place_plan_t aimed;
            CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look_in, &aimed), PLAYER_PLACE_OK);
            CHECK_MEM_EQ(&ahead, &aimed, sizeof(ahead));
            /* Unturned, it faces the way the player looks; R turns it from there, as the aimed plan above does. */
            if (turns == 0u) {
                CHECK_EQ_U64(ahead.voxels[0], block_state_placed(BLOCK_ID_OAK_STAIRS, look));
                CHECK_EQ_U64(ahead.facing, look);
            }
        }
        /* A torch hangs from the block underfoot; a ladder lies on its side. */
        memset(&rig.target, 0, sizeof(rig.target));
        player_interaction_hand_set(&rig.hands, BLOCK_ID_TORCH, 8u);
        const player_interaction_input_t look_in = fw_input(&rig, look, false);
        player_place_plan_t torch;
        CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look_in, &torch), PLAYER_PLACE_OK);
        CHECK_EQ_U64(torch.voxels[0], wall_torch_of(step_x, step_y));
        player_interaction_hand_set(&rig.hands, BLOCK_ID_LADDER, 8u);
        player_place_plan_t ladder;
        CHECK_EQ_U64(player_interaction_place_plan(&rig.hands, rig.world.map, &look_in, &ladder), PLAYER_PLACE_OK);
        CHECK_EQ_U64(ladder.facing, block_facing_of_step(-step_x, -step_y));
    }
    test_world_end(&rig.world);
}

TEST_MAIN()
