/* ctest run_player_command. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "physics/index.h"
#include "player/index.h"
#include "tables/blocks/index.h"
#include "world/index.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

enum : uint32_t {
    PC_ARRIVE_TICKS = 400u, /* more than any fall here takes: 300 blocks is about 140 ticks */
};

/* A body in a small world of stone with its top at z = 0, 61 blocks square,, with the hands and the health ticking as the sim's do. */
typedef struct pc_rig_t {
    test_world_t w;    /* 0 */
    player_t *player;  /* 24 */
    int64_t tick_ns;   /* 32 */
    uint8_t _pad0[24]; /* 40 */
} pc_rig_t;
static_assert(sizeof(pc_rig_t) == 64);

static bool pc_begin(test_context_t *t, pc_rig_t *rig) {
    memset(rig, 0, sizeof(*rig));
    if (!test_world_begin(t, &rig->w)) {
        return false;
    }
    test_world_fill(t, &rig->w, -30, -30, -1, 30, 30, -1, BLOCK_ID_STONE);
    rig->player = test_world_aligned(t, sizeof(player_t));
    player_init(rig->player, rig->w.physics);
    player_spawn(rig->player, 0.5f64, 0.5f64, 0.0f64);
    rig->tick_ns = PLAYER_TICK_NS;
    return true;
}

static void pc_end(pc_rig_t *rig) {
    player_deinit(rig->player);
    test_world_end(&rig->w);
}

/* As game_sim_tick does it for a body: what main sent, then the body, then its health. Keys as given, sent fresh each tick. */
static player_command_kind_t pc_tick(pc_rig_t *rig, int8_t forward, bool jump, bool sneak) {
    const player_intent_t intent = {.yaw = 0.0f32,
                                    .forward = forward,
                                    .right = 0,
                                    .jump = jump ? 1u : 0u,
                                    .sneak = sneak ? 1u : 0u,
                                    .sprint = 0u,
                                    ._pad0 = {0}};
    player_send(rig->player, &intent, rig->tick_ns);
    const player_command_kind_t done = player_command_take(rig->player);
    player_tick(rig->player, rig->w.map, rig->tick_ns);
    player_survive_tick(rig->player, rig->w.map);
    rig->tick_ns += PLAYER_TICK_NS;
    return done;
}

static void pc_idle(pc_rig_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        (void)pc_tick(rig, 0, false, false);
    }
}

/* The body has room where it stands and something under its feet. */
static bool pc_standing(const pc_rig_t *rig) {
    const player_movement_t *const body = &rig->player->movement;
    collision_box_t box;
    player_movement_box(body->position, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    return physics_box_is_clear(rig->w.physics, rig->w.map, &box) && body->on_ground != 0u;
}

static const float64_t PC_ZERO[3] = {0.0f64, 0.0f64, 0.0f64};

/* ---- Teleports ---------------------------------------------------------------------------------------------------------- */

TEST(tp_up_keeps_x_and_y) {
    pc_rig_t rig;
    REQUIRE(pc_begin(t, &rig));
    pc_idle(&rig, 5u);
    REQUIRE(pc_standing(&rig));
    const float64_t before[3] = {rig.player->movement.position[0], rig.player->movement.position[1],
                                 rig.player->movement.position[2]};
    const float64_t up[3] = {0.0f64, 0.0f64, 300.0f64};
    const uint32_t all = PLAYER_COMMAND_RELATIVE_X | PLAYER_COMMAND_RELATIVE_Y | PLAYER_COMMAND_RELATIVE_Z;
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, up, all, 0u));
    /* A mailbox of one: a second waits for the sim. */
    CHECK(!player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, up, all, 0u));
    CHECK_EQ_U64(pc_tick(&rig, 0, false, false), PLAYER_COMMAND_TP_EXACT);
    CHECK_EQ_U64(atomic_load(&rig.player->command_word), 0u); /* the mailbox is empty again */
    const player_movement_t *const body = &rig.player->movement;
    CHECK(memcmp(&body->position[0], &before[0], sizeof(float64_t)) == 0);
    CHECK(memcmp(&body->position[1], &before[1], sizeof(float64_t)) == 0);
    /* The tick it was taken in it stood where it was put, and fell from there after: within a tick's fall of 300 up. */
    CHECK(body->position[2] > before[2] + 299.0f64 && body->position[2] <= before[2] + 300.0f64);
    CHECK_EQ_U64(rig.player->teleports, 1u);
    /* 300 blocks down, and no fall hurts it: a teleport's drop is not a fall. */
    pc_idle(&rig, PC_ARRIVE_TICKS);
    CHECK(pc_standing(&rig));
    CHECK_NEAR(body->position[2], 0.0f64, 1.0e-9f64);
    CHECK_EQ_U64(rig.player->survival.hurt_count, 0u);
    CHECK_NEAR((float64_t)rig.player->survival.health, 20.0f64, 0.0f64);
    CHECK_EQ_U64(rig.player->survival.arriving, PLAYER_SURVIVAL_ARRIVING_NONE);
    /* And a real fall still hurts, once it has landed. */
    const float64_t high[3] = {0.0f64, 0.0f64, 30.0f64};
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, high, all, 0u));
    pc_idle(&rig, PC_ARRIVE_TICKS);
    CHECK_EQ_U64(rig.player->survival.hurt_count, 0u);
    rig.player->movement.position[2] = 10.0f64; /* lifted by hand, not by a teleport */
    rig.player->movement.on_ground = 0u;
    pc_idle(&rig, 100u);
    CHECK_EQ_U64(rig.player->survival.hurt_count, 1u);
    pc_end(&rig);
}

TEST(teleport_there_and_back_lands_safely) {
    pc_rig_t rig;
    REQUIRE(pc_begin(t, &rig));
    pc_idle(&rig, 5u);
    const float64_t home[3] = {rig.player->movement.position[0], rig.player->movement.position[1],
                               rig.player->movement.position[2]};
    /* Far past what is loaded: it waits there, held and unharmed, for its ground. */
    const float64_t far[3] = {3000.0f64, -2000.0f64, 0.0f64};
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_GROUND, far, PLAYER_COMMAND_RELATIVE_Z, 0u));
    CHECK_EQ_U64(pc_tick(&rig, 1, true, false), PLAYER_COMMAND_TP_GROUND);
    const player_movement_t *const body = &rig.player->movement;
    CHECK_NEAR(body->position[0], 3000.0f64, 0.0f64);
    CHECK_NEAR(body->position[1], -2000.0f64, 0.0f64);
    for (uint32_t i = 0u; i < 50u; i++) {
        (void)pc_tick(&rig, 1, true, false); /* keys count for nothing while it waits */
    }
    CHECK_EQ_U64(rig.player->survival.arriving, PLAYER_SURVIVAL_ARRIVING_WAIT);
    CHECK(player_survival_holds_body(&rig.player->survival));
    CHECK_NEAR(body->position[0], 3000.0f64, 0.0f64);
    CHECK_NEAR(body->position[2], 0.0f64, 0.0f64);
    CHECK_NEAR((float64_t)player_survival_hurt(&rig.player->survival, 5.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 0.0f64,
               0.0f64);
    /* The ground comes: stone up to z = 40 there, a hill. It stands on top, with room. */
    world_map_set_center(rig.w.map, column_chunk_of(3000), column_chunk_of(-2000));
    test_world_stream_all(t, &rig.w);
    test_world_fill(t, &rig.w, 2990, -2010, -1, 3010, -1990, 40, BLOCK_ID_STONE);
    pc_idle(&rig, PC_ARRIVE_TICKS);
    CHECK(pc_standing(&rig));
    CHECK_NEAR(body->position[2], 41.0f64, 1.0e-9f64);
    CHECK_EQ_U64(rig.player->survival.arriving, PLAYER_SURVIVAL_ARRIVING_NONE);
    /* And back home, as exact feet, which a column loaded at home already holds. */
    world_map_set_center(rig.w.map, 0, 0);
    test_world_stream_all(t, &rig.w);
    test_world_fill(t, &rig.w, -30, -30, -1, 30, 30, -1, BLOCK_ID_STONE);
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, home, 0u, 0u));
    pc_idle(&rig, PC_ARRIVE_TICKS);
    CHECK(pc_standing(&rig));
    CHECK(memcmp(body->position, home, sizeof(home)) == 0);
    CHECK_EQ_U64(rig.player->survival.hurt_count, 0u);
    CHECK_NEAR((float64_t)rig.player->survival.health, 20.0f64, 0.0f64);
    CHECK_EQ_U64(rig.player->teleports, 2u);
    pc_end(&rig);
}

TEST(a_teleport_into_rock_climbs_out_and_one_to_the_ground_stands_on_top) {
    pc_rig_t rig;
    REQUIRE(pc_begin(t, &rig));
    test_world_fill(t, &rig.w, 10, 10, 0, 12, 12, 20, BLOCK_ID_STONE);
    /* Feet at 10.5, 10.5, 5: inside the stone, so out to the first room above, at 21. */
    const float64_t inside[3] = {10.5f64, 11.5f64, 5.0f64};
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, inside, 0u, 0u));
    pc_idle(&rig, 10u);
    CHECK(pc_standing(&rig));
    CHECK_NEAR(rig.player->movement.position[2], 21.0f64, 1.0e-9f64);
    /* To the ground at x, y: from the world's top down, onto the stone, whatever height it is asked from. */
    const float64_t ground[3] = {11.5f64, 10.5f64, -100.0f64};
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_GROUND, ground, 0u, 0u));
    pc_idle(&rig, PC_ARRIVE_TICKS);
    CHECK(pc_standing(&rig));
    CHECK_NEAR(rig.player->movement.position[2], 21.0f64, 1.0e-9f64);
    CHECK_EQ_U64(rig.player->survival.hurt_count, 0u);
    pc_end(&rig);
}

TEST(a_teleport_is_held_inside_the_world_and_refused_by_the_dead) {
    pc_rig_t rig;
    REQUIRE(pc_begin(t, &rig));
    const float64_t high[3] = {0.5f64, 0.5f64, 99999.0f64};
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, high, 0u, 0u));
    (void)pc_tick(&rig, 0, false, false);
    CHECK(rig.player->movement.position[2] <= (float64_t)COLUMN_Z_GAMEPLAY_MAX);
    const float64_t far[3] = {1.0e12f64, -1.0e12f64, -99999.0f64};
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, far, 0u, 0u));
    (void)pc_tick(&rig, 0, false, false);
    CHECK_NEAR(rig.player->movement.position[0], PLAYER_COMMAND_PLACE_MOST, 0.0f64);
    CHECK_NEAR(rig.player->movement.position[1], -PLAYER_COMMAND_PLACE_MOST, 0.0f64);
    CHECK_NEAR(rig.player->movement.position[2], (float64_t)COLUMN_Z_GAMEPLAY_MIN, 0.0f64);
    /* A step a long way off is held the same. */
    const float64_t step[3] = {4.0e9f64, 0.0f64, 0.0f64};
    REQUIRE(player_command_send(rig.player, PLAYER_COMMAND_TP_EXACT, step, PLAYER_COMMAND_RELATIVE_X, 0u));
    (void)pc_tick(&rig, 0, false, false);
    CHECK_NEAR(rig.player->movement.position[0], PLAYER_COMMAND_PLACE_MOST, 0.0f64);
    /* Dead, nothing moves it. */
    player_t *const player = rig.player;
    player_spawn(player, 0.5f64, 0.5f64, 0.0f64);
    (void)player_survival_hurt(&player->survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    REQUIRE(player->survival.state == PLAYER_SURVIVAL_DEAD);
    REQUIRE(player_command_send(player, PLAYER_COMMAND_TP_EXACT, PC_ZERO, 0u, 0u));
    CHECK_EQ_U64(pc_tick(&rig, 0, false, false), PLAYER_COMMAND_NONE);
    CHECK_EQ_U64(player->teleports, 0u);
    pc_end(&rig);
}

/* ---- Fly ---------------------------------------------------------------------------------------------------------------- */

TEST(fly_has_no_gravity_and_no_fall_damage) {
    pc_rig_t rig;
    REQUIRE(pc_begin(t, &rig));
    pc_idle(&rig, 5u);
    player_t *const player = rig.player;
    const uint8_t food = player->survival.food;
    const float32_t saturation = player->survival.saturation;
    REQUIRE(player_command_send(player, PLAYER_COMMAND_FLY, nullptr, 0u, 0u));
    CHECK_EQ_U64(pc_tick(&rig, 0, false, false), PLAYER_COMMAND_FLY);
    CHECK_EQ_U64(player->movement.mode, PLAYER_MOVEMENT_MODE_FLY);
    bool flying = false;
    uint32_t speed = 0u, factor = 0u;
    player_flight_read(player, &flying, &speed, &factor);
    CHECK(flying);
    CHECK_EQ_U64(speed, 116u);
    CHECK_EQ_U64(factor, 1u);
    /* Space: up at 10 x 1.25^11 blocks a second, 5.82 a tick, to past 200. */
    while (player->movement.position[2] < 200.0f64) {
        (void)pc_tick(&rig, 0, true, false);
    }
    const float64_t hover = player->movement.position[2];
    CHECK_NEAR(hover, 35.0f64 * 116.415321826934814453125f64 / 20.0f64, 1.0e-6f64);
    /* 200 ticks with nothing held, and forward: not a hair lower. */
    pc_idle(&rig, 200u);
    CHECK(memcmp(&player->movement.position[2], &hover, sizeof(hover)) == 0);
    for (uint32_t i = 0u; i < 4u; i++) {
        (void)pc_tick(&rig, 1, false, false);
    }
    CHECK(memcmp(&player->movement.position[2], &hover, sizeof(hover)) == 0);
    CHECK_NEAR(player->movement.position[0], 0.5f64 + 4.0f64 * 116.415321826934814453125f64 / 20.0f64, 1.0e-6f64);
    /* The wheel: the slowest is 10 blocks a second, half a block a tick. */
    player_fly_wheel_send(player, -100);
    (void)pc_tick(&rig, 0, false, false);
    player_flight_read(player, &flying, &speed, &factor);
    CHECK_EQ_U64(speed, 10u);
    player_fly_wheel_send(player, 100);
    (void)pc_tick(&rig, 0, false, false);
    player_flight_read(player, &flying, &speed, &factor);
    CHECK_EQ_U64(speed, 1000u);
    /* Down onto the stone with Shift, which stops it there, still flying, and off a block over it: no harm. */
    player_fly_wheel_send(player, -100);
    while (player->movement.on_ground == 0u) {
        (void)pc_tick(&rig, 0, false, true);
    }
    CHECK_NEAR(player->movement.position[2], 0.0f64, 1.0e-9f64);
    const float64_t over[3] = {0.0f64, 0.0f64, 1.0f64};
    REQUIRE(player_command_send(player, PLAYER_COMMAND_TP_EXACT, over,
                                PLAYER_COMMAND_RELATIVE_X | PLAYER_COMMAND_RELATIVE_Y | PLAYER_COMMAND_RELATIVE_Z, 0u));
    pc_idle(&rig, 3u);
    CHECK_EQ_U64(player->movement.mode, PLAYER_MOVEMENT_MODE_FLY); /* a teleport keeps it flying */
    CHECK_NEAR(player->movement.position[2], 1.0f64, 1.0e-9f64);
    REQUIRE(player_command_send(player, PLAYER_COMMAND_FLY, nullptr, 0u, 0u));
    pc_idle(&rig, 40u);
    CHECK(pc_standing(&rig));
    CHECK(player->movement.mode != PLAYER_MOVEMENT_MODE_FLY);
    CHECK_EQ_U64(player->survival.hurt_count, 0u);
    CHECK_NEAR((float64_t)player->survival.health, 20.0f64, 0.0f64);
    /* No hunger drain in all that flying. */
    CHECK_EQ_U64(player->survival.food, food);
    CHECK_NEAR((float64_t)player->survival.saturation, (float64_t)saturation, 0.0f64);
    pc_end(&rig);
}

TEST(speed_multiplies_walking_and_flying) {
    pc_rig_t rig;
    REQUIRE(pc_begin(t, &rig));
    player_t *const player = rig.player;
    player_spawn(player, -25.5f64, 0.5f64, 0.0f64);
    REQUIRE(player_command_send(player, PLAYER_COMMAND_SPEED, nullptr, 0u, 5u));
    CHECK_EQ_U64(pc_tick(&rig, 0, false, false), PLAYER_COMMAND_SPEED);
    bool flying = false;
    uint32_t speed = 0u, factor = 0u;
    player_flight_read(player, &flying, &speed, &factor);
    CHECK_EQ_U64(factor, 5u);
    /* Walking: 4.317 blocks a second, five times. */
    for (uint32_t i = 0u; i < 20u; i++) {
        (void)pc_tick(&rig, 1, false, false);
    }
    const float64_t x = player->movement.position[0];
    for (uint32_t i = 0u; i < 4u; i++) {
        (void)pc_tick(&rig, 1, false, false);
    }
    CHECK_NEAR((player->movement.position[0] - x) * 20.0f64 / 4.0f64, 5.0f64 * 4.317f64, 0.05f64);
    /* Flying: 116 a second, five times, and a factor out of range changes nothing. */
    REQUIRE(player_command_send(player, PLAYER_COMMAND_FLY, nullptr, 0u, 0u));
    (void)pc_tick(&rig, 0, false, false);
    const float64_t z = player->movement.position[2];
    (void)pc_tick(&rig, 0, true, false);
    CHECK_NEAR((player->movement.position[2] - z) * 20.0f64, 5.0f64 * 116.415321826934814453125f64, 1.0e-6f64);
    REQUIRE(player_command_send(player, PLAYER_COMMAND_SPEED, nullptr, 0u, 11u));
    CHECK_EQ_U64(pc_tick(&rig, 0, false, false), PLAYER_COMMAND_NONE);
    player_flight_read(player, &flying, &speed, &factor);
    CHECK_EQ_U64(factor, 5u);
    /* It is the player's, not the body's: a teleport and a respawn keep it. */
    const float64_t up[3] = {0.0f64, 0.0f64, 10.0f64};
    REQUIRE(player_command_send(player, PLAYER_COMMAND_TP_EXACT, up,
                                PLAYER_COMMAND_RELATIVE_X | PLAYER_COMMAND_RELATIVE_Y | PLAYER_COMMAND_RELATIVE_Z, 0u));
    pc_idle(&rig, 2u);
    CHECK_EQ_U64(player->movement.speed_factor, 5u);
    pc_end(&rig);
}

TEST_MAIN()
