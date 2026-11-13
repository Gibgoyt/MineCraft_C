/* ctest run_player_survival. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "main/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PS_CLOSE 1.0e-4f32

/* The sun's height on the equator at the equinox at a time of day, as the sim tells it since plan 004 phase 02: cos(2 pi (f - 0.5)). */
static float64_t ps_equator_sun(float64_t day) {
    return cos(2.0f64 * 3.14159265358979323846f64 * (day - 0.5f64));
}

/* ---- A body on a floor, and its ticks in game_sim_tick's order ------------------------------------------------------------ */

typedef struct ps_keys_t {
    int8_t forward;   /* 0 */
    uint8_t jump;     /* 1 */
    uint8_t sneak;    /* 2 */
    uint8_t sprint;   /* 3 */
    uint8_t attack;   /* 4 */
    uint8_t place;    /* 5 */
    uint8_t _pad0[2]; /* 6 */
} ps_keys_t;
static_assert(sizeof(ps_keys_t) == 8);

typedef struct ps_rig_t {
    test_world_t world; /* 0 */
    player_t *player;   /* 24 */
    int64_t now_ns;     /* 32 */
    float32_t yaw;      /* 40 */
    float32_t pitch;    /* 44 */
} ps_rig_t;
static_assert(sizeof(ps_rig_t) == 48);

static const ps_keys_t PS_NO_KEYS = {
    .forward = 0, .jump = 0u, .sneak = 0u, .sprint = 0u, .attack = 0u, .place = 0u, ._pad0 = {0}};

/* Stone with its top at z = 0, with a body standing on it at (0.5, 0.5), looking up at nothing. */
static bool ps_begin(test_context_t *t, ps_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    test_world_fill(t, &rig->world, -32, -32, -1, 47, 47, -1, BLOCK_ID_STONE);
    rig->player = test_world_aligned(t, sizeof(player_t));
    player_init(rig->player, rig->world.physics);
    player_spawn(rig->player, 0.5f64, 0.5f64, 0.0f64);
    rig->now_ns = 1000000000;
    rig->yaw = 0.0f32;
    rig->pitch = 1.5f32;
    return true;
}

static void ps_end(ps_rig_t *rig) {
    player_deinit(rig->player);
    test_world_end(&rig->world);
}

static void ps_tick(ps_rig_t *rig, const ps_keys_t *keys) {
    const player_intent_t intent = {.yaw = rig->yaw,
                                    .forward = keys->forward,
                                    .right = 0,
                                    .jump = keys->jump,
                                    .sneak = keys->sneak,
                                    .sprint = keys->sprint,
                                    ._pad0 = {0}};
    rig->now_ns += PLAYER_TICK_NS;
    player_send(rig->player, &intent, rig->now_ns);
    player_look_send(rig->player, rig->yaw, rig->pitch);
    player_hands_send(rig->player, keys->attack != 0u, keys->place != 0u);
    player_interact_tick(rig->player, &rig->world.world->edit, rig->now_ns);
    player_tick(rig->player, rig->world.map, rig->now_ns);
    player_survive_tick(rig->player, rig->world.map);
}

static void ps_idle(ps_rig_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        ps_tick(rig, &PS_NO_KEYS);
    }
}

/* One tick with this button down and the next with it up. */
static void ps_click_place(ps_rig_t *rig) {
    ps_keys_t keys = PS_NO_KEYS;
    keys.place = 1u;
    ps_tick(rig, &keys);
    ps_idle(rig, 1u);
}

static void ps_press_jump(ps_rig_t *rig) {
    ps_keys_t keys = PS_NO_KEYS;
    keys.jump = 1u;
    ps_tick(rig, &keys);
}

/* Neither heals nor tires by itself until it is given a reason: under what heals, with nothing to spare. */
static void ps_no_healing(player_survival_t *survival) {
    survival->food = PLAYER_SURVIVAL_FOOD_HEALS - 1u;
    survival->saturation = 0.0f32;
}

/* The body back to its feet at this place, whatever it was doing. */
static void ps_put(ps_rig_t *rig, float64_t x, float64_t y, float64_t z) {
    player_movement_start(&rig->player->movement, x, y, z);
}

/* Looks at the middle of a bed's top. */
static void ps_aim_at_top(ps_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    const player_movement_t *const body = &rig->player->movement;
    const float64_t dx = (float64_t)x + 0.5f64 - body->position[0], dy = (float64_t)y + 0.5f64 - body->position[1];
    const float64_t dz = (float64_t)z + 0.5f64 - (body->position[2] + body->eye_height);
    rig->yaw = (float32_t)atan2(dy, dx);
    rig->pitch = (float32_t)atan2(dz, sqrt(dx * dx + dy * dy));
}

static uint16_t ps_block(const ps_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = UINT16_MAX;
    (void)world_map_block_get(rig->world.map, x, y, z, &block);
    return block;
}

/* ---- The sums -------------------------------------------------------------------------------------------------------------- */

TEST(a_body_starts_whole_and_main_can_see_it) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    const player_survival_t *const survival = &rig.player->survival;
    CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    CHECK_EQ_U64(survival->food, 20u);
    CHECK_NEAR(survival->saturation, 5.0f32, 0.0f32);
    CHECK_NEAR(survival->exhaustion, 0.0f32, 0.0f32);
    CHECK_EQ_I64(survival->air, 300);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    player_vitals_t vitals = player_vitals_read(rig.player);
    CHECK_NEAR(vitals.health, 20.0f32, 0.0f32);
    CHECK_EQ_U64(vitals.food, 20u);
    CHECK_EQ_U64(vitals.air, 300u);
    CHECK_EQ_U64(vitals.state, PLAYER_SURVIVAL_ALIVE);
    /* A hit is there for main after the next tick. */
    (void)player_survival_hurt(&rig.player->survival, 7.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 1u);
    vitals = player_vitals_read(rig.player);
    CHECK_NEAR(vitals.health, 13.0f32, 0.0f32);
    ps_end(&rig);
}

TEST(armor_falls_and_night_by_hand) {
    /* max(points / 5, points - hit / 2), held to 20, and 4 % a point off. */
    CHECK_NEAR(player_survival_after_armor(10.0f32, 0.0f32, 0.0f32), 10.0f32, PS_CLOSE);
    CHECK_NEAR(player_survival_after_armor(10.0f32, 20.0f32, 0.0f32), 4.0f32, PS_CLOSE);
    CHECK_NEAR(player_survival_after_armor(20.0f32, 20.0f32, 0.0f32), 12.0f32, PS_CLOSE);
    CHECK_NEAR(player_survival_after_armor(100.0f32, 5.0f32, 0.0f32), 96.0f32, PS_CLOSE);
    CHECK_NEAR(player_survival_after_armor(10.0f32, 40.0f32, 0.0f32), 2.0f32, PS_CLOSE);

    CHECK_NEAR(player_survival_fall_damage(0.0f64, false), 0.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(3.0f64, false), 0.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(3.0000000001f64, false), 0.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(3.01f64, false), 1.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(4.0f64, false), 1.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(4.5f64, false), 2.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(23.0f64, false), 20.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(3.5f64, true), 1.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(5.0f64, true), 1.0f32, 0.0f32);
    CHECK_NEAR(player_survival_fall_damage(7.0f64, true), 2.0f32, 0.0f32);

    /* By the sun's height (plan 004 phase 02): night once it is more than 8.13 degrees down. */
    static const float64_t night_sun[] = {-0.141418f64, -0.5f64, -1.0f64};
    static const float64_t day_sun[] = {-0.141417f64, 0.0f64, 0.5f64, 1.0f64};
    for (uint32_t i = 0u; i < sizeof(night_sun) / sizeof(night_sun[0]); i++) {
        CHECK(player_survival_is_night(night_sun[i]));
    }
    for (uint32_t i = 0u; i < sizeof(day_sun) / sizeof(day_sun[0]); i++) {
        CHECK(!player_survival_is_night(day_sun[i]));
    }
    /* On the equator at the equinox, today's times of day give today's answers: 12542 and 23459 ticks after a sunrise at 0.25, of
     * 24000. */
    static const float64_t night[] = {0.7726f64, 0.9f64, 0.0f64, 0.1f64, 0.2274f64, 1.1f64, -0.1f64};
    static const float64_t day[] = {0.2275f64, 0.25f64, 0.35f64, 0.5f64, 0.75f64, 0.7725f64, 1.5f64};
    for (uint32_t i = 0u; i < sizeof(night) / sizeof(night[0]); i++) {
        CHECK(player_survival_is_night(ps_equator_sun(night[i])));
        CHECK(!player_survival_is_night(ps_equator_sun(day[i])));
    }
    /* And at every time of day against the old two fractions. They were not quite symmetric about noon: 0.772583 is
     * 0.5 + 0.272583, but 0.227458 is 0.5 - 0.272542. So the old dawn ended 0.000041 of a day (one tick of 24000) later than
     * the sun's threshold does, and that one tick is the only place the two may differ (phase 02 file, Issues found 6). */
    uint64_t differ = 0u, differ_outside = 0u;
    for (uint32_t i = 0u; i < 1000000u; i++) {
        const float64_t f = (float64_t)i / 1000000.0f64;
        const bool old_night = f >= 0.772583f64 || f < 0.227458f64;
        if (player_survival_is_night(ps_equator_sun(f)) != old_night) {
            differ++;
            /* And 0.772583 itself, where the old rule said >= and the sun's height there is the threshold exactly, which is < . */
            differ_outside += (f >= 0.227416f64 && f < 0.227458f64) || f == 0.772583f64 ? 0u : 1u;
        }
    }
    printf("  %" PRIu64 " of 1000000 times of day differ from the old fractions, all in the last tick of dawn\n",
           differ);
    CHECK(differ <= 43u);
    CHECK_EQ_U64(differ_outside, 0u);
}

TEST(for_ten_ticks_after_a_hit_only_what_is_more_counts_and_only_the_difference) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    CHECK_NEAR(player_survival_hurt(survival, 4.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 4.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(player_survival_hurt(survival, 3.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 0.0f32, 0.0f32);
    CHECK_NEAR(player_survival_hurt(survival, 4.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 0.0f32, 0.0f32);
    CHECK_NEAR(survival->health, 16.0f32, 0.0f32);
    CHECK_NEAR(player_survival_hurt(survival, 6.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 2.0f32, 0.0f32);
    CHECK_NEAR(survival->health, 14.0f32, 0.0f32);
    CHECK_EQ_U64(survival->hurt_count, 2u);
    /* The bigger hit does not put off the end, 9 ticks away by now. */
    ps_idle(&rig, 8u);
    CHECK_NEAR(player_survival_hurt(survival, 1.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 0.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(player_survival_hurt(survival, 1.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 1.0f32, 0.0f32);
    CHECK_NEAR(survival->health, 13.0f32, 0.0f32);
    /* Nothing is not a hit. */
    ps_idle(&rig, 10u);
    const uint64_t hurts = survival->hurt_count;
    CHECK_NEAR(player_survival_hurt(survival, 0.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 0.0f32, 0.0f32);
    CHECK_EQ_U64(survival->hurt_count, hurts);
    CHECK_EQ_U64(survival->invulnerable, 0u);
    ps_end(&rig);
}

/* ---- What the world does to a body ------------------------------------------------------------------------------------------ */

TEST(a_real_fall_hurts_by_its_height_less_three) {
    static const float64_t heights[] = {2.0f64, 3.0f64, 4.0f64, 5.0f64, 13.0f64};
    static const float32_t hurts[] = {0.0f32, 0.0f32, 1.0f32, 2.0f32, 10.0f32};
    for (uint32_t i = 0u; i < 5u; i++) {
        ps_rig_t rig;
        REQUIRE(ps_begin(t, &rig));
        ps_no_healing(&rig.player->survival);
        ps_put(&rig, 0.5f64, 0.5f64, heights[i]);
        for (uint32_t tick = 0u; tick < 100u && rig.player->movement.on_ground == 0u; tick++) {
            ps_idle(&rig, 1u);
        }
        CHECK_EQ_U64(rig.player->movement.on_ground, 1u);
        CHECK_NEAR(rig.player->survival.health, 20.0f32 - hurts[i], 0.0f32);
        /* A walk and a jump after that do no more. */
        ps_press_jump(&rig);
        ps_idle(&rig, 30u);
        CHECK_NEAR(rig.player->survival.health, 20.0f32 - hurts[i], 0.0f32);
        ps_end(&rig);
    }
}

TEST(a_long_fall_kills_water_and_a_ladder_break_it_and_a_bed_halves_it) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    /* Past 23 blocks. */
    ps_put(&rig, 0.5f64, 0.5f64, 30.0f64);
    ps_idle(&rig, 100u);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    CHECK_EQ_U64(survival->killed_by, PLAYER_SURVIVAL_CAUSE_FALL);
    CHECK_NEAR(survival->health, 0.0f32, 0.0f32);
    ps_end(&rig);

    REQUIRE(ps_begin(t, &rig));
    ps_no_healing(&rig.player->survival);
    test_world_fill(t, &rig.world, -1, -1, 0, 1, 1, 1, BLOCK_ID_WATER);
    ps_put(&rig, 0.5f64, 0.5f64, 20.0f64);
    ps_idle(&rig, 100u);
    CHECK_NEAR(rig.player->survival.health, 20.0f32, 0.0f32);
    ps_end(&rig);

    REQUIRE(ps_begin(t, &rig));
    ps_no_healing(&rig.player->survival);
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 2, BLOCK_ID_LADDER);
    ps_put(&rig, 0.5f64, 0.5f64, 20.0f64);
    ps_idle(&rig, 100u);
    CHECK_NEAR(rig.player->survival.health, 20.0f32, 0.0f32);
    ps_end(&rig);

    /* 7 blocks on to a bed, whose top is at 0.5: 4 past the safe 3, and half of that. */
    REQUIRE(ps_begin(t, &rig));
    ps_no_healing(&rig.player->survival);
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 0, BLOCK_ID_BED);
    ps_put(&rig, 0.5f64, 0.5f64, 7.5f64);
    ps_idle(&rig, 100u);
    CHECK_NEAR(rig.player->movement.position[2], 0.5f64, 1.0e-6f64);
    CHECK_NEAR(rig.player->survival.health, 18.0f32, 0.0f32);
    ps_end(&rig);
}

TEST(lava_hurts_every_ten_ticks_and_kills_and_water_puts_out_what_it_lit) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 1, BLOCK_ID_LAVA);
    /* A hit on the first tick and every tenth after: 5 of 4, so dead on the 41st. */
    for (uint32_t tick = 1u; tick <= 40u; tick++) {
        ps_idle(&rig, 1u);
        const uint32_t hits = (tick + 9u) / 10u;
        CHECK_NEAR(survival->health, 20.0f32 - 4.0f32 * (float32_t)hits, 0.0f32);
        CHECK_EQ_U64(survival->hurt_count, hits);
        CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    }
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    CHECK_EQ_U64(survival->killed_by, PLAYER_SURVIVAL_CAUSE_LAVA);
    CHECK_EQ_U64(survival->died_count, 1u);
    ps_end(&rig);

    /* A touch of lava, then out: 1 every 20 ticks for 15 s, and then it has gone out. */
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const lit = &rig.player->survival;
    ps_no_healing(lit);
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 1, BLOCK_ID_LAVA);
    ps_idle(&rig, 1u);
    CHECK_NEAR(lit->health, 16.0f32, 0.0f32);
    CHECK_EQ_U64(lit->burn_ticks, PLAYER_SURVIVAL_BURN_TICKS);
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 1, BLOCK_ID_AIR);
    ps_idle(&rig, 19u);
    CHECK_NEAR(lit->health, 16.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(lit->health, 15.0f32, 0.0f32);
    ps_idle(&rig, 280u);
    CHECK_EQ_U64(lit->burn_ticks, 0u);
    CHECK_NEAR(lit->health, 1.0f32, 0.0f32);
    ps_idle(&rig, 100u);
    CHECK_NEAR(lit->health, 1.0f32, 0.0f32);
    ps_end(&rig);

    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const doused = &rig.player->survival;
    ps_no_healing(doused);
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 1, BLOCK_ID_LAVA);
    ps_idle(&rig, 1u);
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 1, BLOCK_ID_WATER);
    ps_idle(&rig, 40u);
    CHECK_EQ_U64(doused->burn_ticks, 0u);
    CHECK_NEAR(doused->health, 16.0f32, 0.0f32);
    ps_end(&rig);
}

TEST(a_cactus_hurts_what_touches_it_and_nothing_else) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    test_world_fill(t, &rig.world, 2, -1, 0, 2, 1, 2, BLOCK_ID_CACTUS);
    /* A block away from it, nothing. */
    ps_idle(&rig, 30u);
    CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    ps_keys_t keys = PS_NO_KEYS;
    keys.forward = 1;
    while (survival->hurt_count == 0u) {
        const uint64_t ticks = rig.player->tick_count;
        ps_tick(&rig, &keys);
        REQUIRE(ticks < 200u);
    }
    /* Stopped at its face, within a hair. */
    CHECK_NEAR(rig.player->movement.position[0], 2.0f64 - PLAYER_MOVEMENT_WIDTH / 2.0f64, 0.02f64);
    CHECK_NEAR(survival->health, 19.0f32, 0.0f32);
    for (uint32_t i = 0u; i < 9u; i++) {
        ps_tick(&rig, &keys);
    }
    CHECK_NEAR(survival->health, 19.0f32, 0.0f32);
    ps_tick(&rig, &keys);
    CHECK_NEAR(survival->health, 18.0f32, 0.0f32);
    /* Armor counts here. */
    player_survival_armor_set(survival, 20.0f32, 0.0f32);
    for (uint32_t i = 0u; i < 10u; i++) {
        ps_tick(&rig, &keys);
    }
    CHECK_NEAR(survival->health, 18.0f32 - player_survival_after_armor(1.0f32, 20.0f32, 0.0f32), PS_CLOSE);
    ps_end(&rig);

    /* Standing on one. */
    REQUIRE(ps_begin(t, &rig));
    ps_no_healing(&rig.player->survival);
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 0, BLOCK_ID_CACTUS);
    ps_put(&rig, 0.5f64, 0.5f64, 1.0f64);
    ps_idle(&rig, 5u);
    CHECK_NEAR(rig.player->survival.health, 19.0f32, 0.0f32);
    ps_end(&rig);
}

TEST(under_water_the_air_lasts_300_ticks_then_it_drowns_and_comes_back_at_4_a_tick) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 5, BLOCK_ID_WATER);
    /* It stands on the bottom, with its eyes under from the first tick that looks. */
    ps_idle(&rig, 1u);
    REQUIRE(rig.player->movement.eyes_under_water == 1u);
    int32_t expected = survival->air;
    for (uint32_t i = 0u; i < 100u; i++) {
        ps_idle(&rig, 1u);
        expected--;
        CHECK_EQ_I64(survival->air, expected);
    }
    /* All of it gone, and 20 ticks past that a gasp of 2. */
    while (survival->air > -19) {
        ps_idle(&rig, 1u);
        CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    }
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->health, 18.0f32, 0.0f32);
    CHECK_EQ_I64(survival->air, 0);
    CHECK_EQ_U64(player_vitals_read(rig.player).air, 0u);
    ps_idle(&rig, 19u);
    CHECK_NEAR(survival->health, 18.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->health, 16.0f32, 0.0f32);
    CHECK_EQ_U64(survival->killed_by, PLAYER_SURVIVAL_CAUSE_NONE);
    /* Air again. */
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 5, BLOCK_ID_AIR);
    ps_idle(&rig, 1u);
    CHECK_EQ_I64(survival->air, 4);
    ps_idle(&rig, 9u);
    CHECK_EQ_I64(survival->air, 40);
    ps_idle(&rig, 100u);
    CHECK_EQ_I64(survival->air, 300);
    CHECK_NEAR(survival->health, 16.0f32, 0.0f32);
    ps_end(&rig);
}

/* Through the same call the HUD makes, on a game with nothing in it but the body: that is all this call reads that matters here. */
static ui_hud_state_t ps_bar(const game_t *game) {
    ui_hud_state_t state;
    game_hud_state_read(game, &state);
    return state;
}

TEST(the_bar_counts_down_with_the_air_and_is_empty_when_the_hearts_start_to_go) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    game_t *const game = test_world_aligned(t, sizeof(game_t));
    memset(game, 0, sizeof(*game));
    game->borrowed_player = rig.player;

    /* On land with all its air, it is not up. */
    ps_idle(&rig, 1u);
    REQUIRE(rig.player->movement.eyes_under_water == 0u);
    CHECK_EQ_U64(ps_bar(game).bubbles_shown, 0u);

    /* In over its head, one bubble goes each 30 ticks, and no heart while there is one. */
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 5, BLOCK_ID_WATER);
    uint32_t under = 0u;
    while (survival->air > 0 && under < 400u) {
        ps_idle(&rig, 1u);
        under++;
        const ui_hud_state_t state = ps_bar(game);
        CHECK_EQ_U64(state.bubbles_shown, 1u);
        CHECK_EQ_I64(survival->air, 300 - (int32_t)under);
        CHECK_EQ_U64(state.bubbles, (300u - under + 29u) / 30u);
        CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    }
    /* 15 seconds, to the tick, with the last bubble going on the last of them. */
    CHECK_EQ_U64(under, 300u);
    CHECK_EQ_U64(ps_bar(game).bubbles, 0u);

    /* The row stays empty for the one second to the first gasp, which comes when the sim has always had it. */
    uint32_t past = 0u;
    while (survival->health >= 20.0f32 && past < 100u) {
        CHECK_EQ_U64(ps_bar(game).bubbles, 0u);
        ps_idle(&rig, 1u);
        past++;
    }
    CHECK_EQ_U64(past, 20u);
    CHECK_NEAR(survival->health, 18.0f32, 0.0f32);
    /* And while it drowns the row stays up and empty, though the sim's own number has gone back to 0 and below. */
    for (uint32_t i = 0u; i < 25u; i++) {
        const ui_hud_state_t state = ps_bar(game);
        CHECK_EQ_U64(state.bubbles_shown, 1u);
        CHECK_EQ_U64(state.bubbles, 0u);
        ps_idle(&rig, 1u);
    }

    /* Out of the water they come back at 4 ticks' worth a tick, and go only when all are back. */
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 5, BLOCK_ID_AIR);
    ps_idle(&rig, 1u);
    CHECK_EQ_I64(survival->air, 4);
    CHECK_EQ_U64(ps_bar(game).bubbles, 1u);
    CHECK_EQ_U64(ps_bar(game).bubbles_shown, 1u);
    ps_idle(&rig, 73u);
    CHECK_EQ_I64(survival->air, 296);
    CHECK_EQ_U64(ps_bar(game).bubbles, 10u);
    CHECK_EQ_U64(ps_bar(game).bubbles_shown, 1u);
    ps_idle(&rig, 1u);
    CHECK_EQ_I64(survival->air, 300);
    CHECK_EQ_U64(ps_bar(game).bubbles_shown, 0u);

    /* Dead, under water or not, nothing about air is up. */
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 5, BLOCK_ID_WATER);
    ps_idle(&rig, 5u);
    CHECK_EQ_U64(ps_bar(game).bubbles_shown, 1u);
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    REQUIRE(survival->state == PLAYER_SURVIVAL_DEAD);
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(ps_bar(game).bubbles_shown, 0u);
    ps_end(&rig);
}

TEST(kelp_and_seagrass_in_the_sea_give_no_air_and_kelp_on_land_takes_none) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    game_t *const game = test_world_aligned(t, sizeof(game_t));
    memset(game, 0, sizeof(*game));
    game->borrowed_player = rig.player;
    const player_movement_t *const movement = &rig.player->movement;

    /* The eye is in the cell at z = 1. Each of them there, with the sea going on over it: the air goes on going, one a tick, and it swims. */
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 5, BLOCK_ID_WATER);
    ps_idle(&rig, 10u);
    CHECK_EQ_I64(survival->air, 290);
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 1, BLOCK_ID_KELP);
    ps_idle(&rig, 10u);
    CHECK_EQ_U64(movement->eyes_under_water, 1u);
    CHECK_EQ_I64(survival->air, 280);
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 1, BLOCK_ID_SEAGRASS);
    ps_idle(&rig, 10u);
    CHECK_EQ_U64(movement->eyes_under_water, 1u);
    CHECK_EQ_I64(survival->air, 270);
    CHECK_EQ_U64(ps_bar(game).bubbles_shown, 1u);
    CHECK_EQ_U64(ps_bar(game).bubbles, 9u);

    /* A whole stalk, feet to over the head, and nothing but it around the body: still the sea. */
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 4, BLOCK_ID_KELP);
    ps_idle(&rig, 10u);
    CHECK_EQ_U64(movement->mode, PLAYER_MOVEMENT_MODE_WATER);
    CHECK_EQ_I64(survival->air, 260);

    /* The sea gone from over and around it: kelp on land is a plant, and the air comes back. */
    test_world_fill(t, &rig.world, -3, -3, 0, 3, 3, 5, BLOCK_ID_AIR);
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 2, BLOCK_ID_KELP);
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(movement->eyes_under_water, 0u);
    CHECK_EQ_I64(survival->air, 264);
    CHECK(movement->mode != PLAYER_MOVEMENT_MODE_WATER);
    ps_end(&rig);
}

TEST(a_full_opaque_cube_at_the_eyes_smothers_and_glass_and_leaves_do_not) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    /* The eye is at 1.62 above the feet at z = 0. */
    test_world_fill(t, &rig.world, 0, 0, 1, 0, 0, 1, BLOCK_ID_GLASS);
    ps_idle(&rig, 11u);
    CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    test_world_fill(t, &rig.world, 0, 0, 1, 0, 0, 1, BLOCK_ID_OAK_LEAVES);
    ps_idle(&rig, 11u);
    CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    test_world_fill(t, &rig.world, 0, 0, 1, 0, 0, 1, BLOCK_ID_STONE);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->health, 19.0f32, 0.0f32);
    ps_idle(&rig, 9u);
    CHECK_NEAR(survival->health, 19.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->health, 18.0f32, 0.0f32);
    /* Armor does not help with this. */
    player_survival_armor_set(survival, 20.0f32, 0.0f32);
    ps_idle(&rig, 10u);
    CHECK_NEAR(survival->health, 17.0f32, 0.0f32);
    ps_end(&rig);
}

/* ---- Food --------------------------------------------------------------------------------------------------------------------- */

TEST(sprinting_jumping_swimming_and_mining_tire_by_the_published_numbers_and_walking_does_not) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    player_movement_t *const body = &rig.player->movement;
    /* Nothing. */
    ps_idle(&rig, 3u);
    CHECK_NEAR(survival->exhaustion, 0.0f32, 0.0f32);
    /* A walk. */
    ps_keys_t keys = PS_NO_KEYS;
    keys.forward = 1;
    for (uint32_t i = 0u; i < 20u; i++) {
        ps_tick(&rig, &keys);
    }
    CHECK(body->position[0] > 2.0f64);
    CHECK_NEAR(survival->exhaustion, 0.0f32, 0.0f32);
    /* A sprint, a block at a time. */
    keys.sprint = 1u;
    const float64_t from = body->position[0];
    for (uint32_t i = 0u; i < 20u; i++) {
        ps_tick(&rig, &keys);
    }
    CHECK(body->sprinting == 1u);
    CHECK_NEAR(survival->exhaustion, (float32_t)((body->position[0] - from) * 0.1f64), PS_CLOSE);
    /* It stops, and a jump is 0.05. */
    ps_idle(&rig, 30u);
    survival->exhaustion = 0.0f32;
    ps_press_jump(&rig);
    CHECK_NEAR(survival->exhaustion, 0.05f32, PS_CLOSE);
    ps_idle(&rig, 30u);
    CHECK_NEAR(survival->exhaustion, 0.05f32, PS_CLOSE);
    /* Walking off an edge is not a jump. */
    ps_end(&rig);

    /* A sprint's jump is 0.2 for the jump and the ground before it. */
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const runner = &rig.player->survival;
    ps_keys_t run = PS_NO_KEYS;
    run.forward = 1;
    run.sprint = 1u;
    for (uint32_t i = 0u; i < 10u; i++) {
        ps_tick(&rig, &run);
    }
    runner->exhaustion = 0.0f32;
    run.jump = 1u;
    ps_tick(&rig, &run);
    CHECK_NEAR(runner->exhaustion, 0.2f32, PS_CLOSE);
    ps_end(&rig);

    /* A swim: 0.01 a block, in all three directions. */
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const swimmer = &rig.player->survival;
    test_world_fill(t, &rig.world, -32, -32, 0, 47, 47, 5, BLOCK_ID_WATER);
    ps_keys_t swim = PS_NO_KEYS;
    swim.forward = 1;
    ps_idle(&rig, 20u);
    swimmer->exhaustion = 0.0f32;
    float64_t swum = 0.0f64;
    for (uint32_t i = 0u; i < 40u; i++) {
        ps_tick(&rig, &swim);
        const player_movement_t *const in_water = &rig.player->movement;
        const float64_t dx = in_water->position[0] - in_water->previous[0];
        const float64_t dy = in_water->position[1] - in_water->previous[1];
        const float64_t dz = in_water->position[2] - in_water->previous[2];
        swum += sqrt(dx * dx + dy * dy + dz * dz);
    }
    CHECK(swum > 1.0f64);
    CHECK_NEAR(swimmer->exhaustion, (float32_t)(swum * 0.01f64), PS_CLOSE);
    ps_end(&rig);

    /* A block mined is 0.005, by the hands' own count. */
    REQUIRE(ps_begin(t, &rig));
    rig.player->interaction.mined_count = 3u;
    ps_idle(&rig, 1u);
    CHECK_NEAR(rig.player->survival.exhaustion, 0.015f32, PS_CLOSE);
    ps_idle(&rig, 1u);
    CHECK_NEAR(rig.player->survival.exhaustion, 0.015f32, PS_CLOSE);
    ps_end(&rig);
}

TEST(tiredness_takes_saturation_then_food_and_stops_at_nothing) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    survival->saturation = 1.5f32;
    /* Exactly 4 is not past it. */
    player_survival_exhaust(survival, 4.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->exhaustion, 4.0f32, 0.0f32);
    CHECK_NEAR(survival->saturation, 1.5f32, 0.0f32);
    player_survival_exhaust(survival, 1.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->exhaustion, 1.0f32, PS_CLOSE);
    CHECK_NEAR(survival->saturation, 0.5f32, PS_CLOSE);
    CHECK_EQ_U64(survival->food, 20u);
    /* What is left of it goes, and then food. */
    player_survival_exhaust(survival, 4.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->saturation, 0.0f32, 0.0f32);
    CHECK_EQ_U64(survival->food, 20u);
    player_survival_exhaust(survival, 4.0f32);
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(survival->food, 19u);
    /* Only one step goes in a tick, and all the tiredness there can be is 40. */
    player_survival_exhaust(survival, 100.0f32);
    CHECK_NEAR(survival->exhaustion, 40.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->exhaustion, 36.0f32, PS_CLOSE);
    CHECK_EQ_U64(survival->food, 18u);
    survival->food = 0u;
    ps_idle(&rig, 3u);
    CHECK_EQ_U64(survival->food, 0u);
    ps_end(&rig);
}

TEST(it_heals_fast_when_full_slowly_when_fed_and_starves_down_to_one) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    /* 5 saturation's worth at the tenth tick: 5/6 of a health and 5 tiredness. The next tick takes 4 of that and one saturation. */
    survival->health = 10.0f32;
    ps_idle(&rig, 9u);
    CHECK_NEAR(survival->health, 10.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->health, 10.0f32 + 5.0f32 / 6.0f32, PS_CLOSE);
    CHECK_NEAR(survival->exhaustion, 5.0f32, PS_CLOSE);
    ps_idle(&rig, 1u);
    CHECK_NEAR(survival->saturation, 4.0f32, PS_CLOSE);
    CHECK_NEAR(survival->exhaustion, 1.0f32, PS_CLOSE);
    ps_idle(&rig, 9u);
    CHECK_NEAR(survival->health, 10.0f32 + 5.0f32 / 6.0f32 + 4.0f32 / 6.0f32, PS_CLOSE);
    ps_end(&rig);

    /* At 18, with no saturation: 1 every 80 ticks, which costs 6 tiredness, which next tick takes a food, so that it stops at 17. */
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const fed = &rig.player->survival;
    fed->food = 18u;
    fed->saturation = 0.0f32;
    fed->health = 10.0f32;
    ps_idle(&rig, 79u);
    CHECK_NEAR(fed->health, 10.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(fed->health, 11.0f32, 0.0f32);
    CHECK_NEAR(fed->exhaustion, 6.0f32, PS_CLOSE);
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(fed->food, 17u);
    ps_idle(&rig, 200u);
    CHECK_NEAR(fed->health, 11.0f32, 0.0f32);
    ps_end(&rig);

    /* Not full and not hurt does nothing to the timer, or to anything else. */
    REQUIRE(ps_begin(t, &rig));
    ps_idle(&rig, 200u);
    CHECK_NEAR(rig.player->survival.health, 20.0f32, 0.0f32);
    CHECK_EQ_U64(rig.player->survival.food_timer, 0u);
    CHECK_NEAR(rig.player->survival.saturation, 5.0f32, 0.0f32);
    ps_end(&rig);

    /* Nothing to eat: 1 every 80 ticks, down to 1, and no further. */
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const starving = &rig.player->survival;
    starving->food = 0u;
    starving->saturation = 0.0f32;
    starving->health = 3.0f32;
    ps_idle(&rig, 79u);
    CHECK_NEAR(starving->health, 3.0f32, 0.0f32);
    ps_idle(&rig, 1u);
    CHECK_NEAR(starving->health, 2.0f32, 0.0f32);
    ps_idle(&rig, 80u);
    CHECK_NEAR(starving->health, 1.0f32, 0.0f32);
    ps_idle(&rig, 800u);
    CHECK_NEAR(starving->health, 1.0f32, 0.0f32);
    CHECK_EQ_U64(starving->state, PLAYER_SURVIVAL_ALIVE);
    ps_end(&rig);
}

TEST(with_six_food_or_less_a_body_does_not_sprint) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    ps_keys_t run = PS_NO_KEYS;
    run.forward = 1;
    run.sprint = 1u;
    player_survival_t *const survival = &rig.player->survival;
    survival->saturation = 0.0f32;
    survival->food = PLAYER_SURVIVAL_FOOD_NO_SPRINT + 1u;
    ps_tick(&rig, &run);
    CHECK_EQ_U64(rig.player->movement.sprinting, 1u);
    survival->food = PLAYER_SURVIVAL_FOOD_NO_SPRINT;
    ps_tick(&rig, &run);
    CHECK_EQ_U64(rig.player->movement.sprinting, 0u);
    ps_end(&rig);
}

TEST(food_is_eaten_in_32_ticks_of_the_button_held) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    survival->food = 10u;
    survival->saturation = 0.0f32;
    player_survival_hold_food(survival, ITEM_ID_APPLE, 4u, 2.4f32);
    ps_keys_t hold = PS_NO_KEYS;
    hold.place = 1u;
    for (uint32_t i = 0u; i < 31u; i++) {
        ps_tick(&rig, &hold);
    }
    CHECK_EQ_U64(survival->eaten_count, 0u);
    CHECK_EQ_U64(survival->food, 10u);
    /* No sprinting with a mouth full. */
    CHECK(!player_survival_can_sprint(survival));
    ps_tick(&rig, &hold);
    CHECK_EQ_U64(survival->eaten_count, 1u);
    CHECK_EQ_U64(survival->food, 14u);
    CHECK_NEAR(survival->saturation, 2.4f32, PS_CLOSE);
    CHECK_EQ_U64(survival->eat_ticks, 0u);
    /* Held on, it goes on. */
    for (uint32_t i = 0u; i < 32u; i++) {
        ps_tick(&rig, &hold);
    }
    CHECK_EQ_U64(survival->eaten_count, 2u);
    CHECK_EQ_U64(survival->food, 18u);
    /* Letting go, even for one tick, starts over. */
    for (uint32_t i = 0u; i < 31u; i++) {
        ps_tick(&rig, &hold);
    }
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(survival->eat_ticks, 0u);
    for (uint32_t i = 0u; i < 31u; i++) {
        ps_tick(&rig, &hold);
    }
    CHECK_EQ_U64(survival->eaten_count, 2u);
    /* So does changing what is in hand, and changing it back. */
    player_survival_hold_food(survival, ITEM_ID_BREAD, 5u, 6.0f32);
    CHECK_EQ_U64(survival->eat_ticks, 0u);
    player_survival_hold_food(survival, ITEM_ID_APPLE, 4u, 2.4f32);
    for (uint32_t i = 0u; i < 31u; i++) {
        ps_tick(&rig, &hold);
    }
    CHECK_EQ_U64(survival->eaten_count, 2u);
    ps_tick(&rig, &hold);
    CHECK_EQ_U64(survival->eaten_count, 3u);
    /* Full, it will not eat at all. */
    CHECK_EQ_U64(survival->food, 20u);
    for (uint32_t i = 0u; i < 40u; i++) {
        ps_tick(&rig, &hold);
    }
    CHECK_EQ_U64(survival->eaten_count, 3u);
    CHECK_EQ_U64(survival->eat_ticks, 0u);
    /* What is not food is not eaten. */
    survival->food = 5u;
    player_survival_hold_food(survival, ITEM_ID_STONE, 0u, 0.0f32);
    for (uint32_t i = 0u; i < 40u; i++) {
        ps_tick(&rig, &hold);
    }
    CHECK_EQ_U64(survival->eaten_count, 3u);
    ps_end(&rig);
}

TEST(every_food_in_config_items_gives_what_its_row_says_up_to_the_limits) {
    uint32_t foods = 0u;
    for (uint32_t i = 0u; i < ITEM_COUNT; i++) {
        const item_def_t *const def = &item_defs[i];
        if (def->food == 0u) {
            continue;
        }
        foods++;
        player_survival_t survival;
        player_survival_start(&survival, 0.0f64, 0.0f64, 0.0f64);
        survival.food = 2u;
        survival.saturation = 0.0f32;
        player_survival_eat(&survival, def->food, def->saturation);
        CHECK_EQ_U64(survival.food, 2u + (uint32_t)def->food);
        const float32_t cap = (float32_t)survival.food;
        CHECK_NEAR(survival.saturation, def->saturation < cap ? def->saturation : cap, PS_CLOSE);
        CHECK(survival.saturation <= cap);
        survival.food = 19u;
        player_survival_eat(&survival, def->food, def->saturation);
        CHECK_EQ_U64(survival.food, 20u);
    }
    /* What config/items/ holds today: a new one needs no change here, and one lost should be noticed. */
    CHECK(foods >= 13u);
    CHECK_EQ_U64(item_defs[ITEM_ID_APPLE].food, 4u);
    CHECK_NEAR(item_defs[ITEM_ID_APPLE].saturation, 2.4f32, PS_CLOSE);
    CHECK_EQ_U64(item_defs[ITEM_ID_PUMPKIN_PIE].food, 8u);
    CHECK_NEAR(item_defs[ITEM_ID_PUMPKIN_PIE].saturation, 4.8f32, PS_CLOSE);
    /* The animals' food, raw and cooked: the published numbers, and goat as mutton, which it is most like. */
    static const uint16_t meats[] = {
        ITEM_ID_BEEF,      ITEM_ID_COOKED_BEEF,      ITEM_ID_PORK,    ITEM_ID_COOKED_PORK,
        ITEM_ID_MUTTON,    ITEM_ID_COOKED_MUTTON,    ITEM_ID_CHICKEN, ITEM_ID_COOKED_CHICKEN,
        ITEM_ID_GOAT_MEAT, ITEM_ID_COOKED_GOAT_MEAT, ITEM_ID_EGG,     ITEM_ID_MILK_BUCKET,
    };
    static const uint8_t meat_food[] = {3u, 8u, 3u, 8u, 2u, 6u, 2u, 6u, 2u, 6u, 2u, 4u};
    static const float32_t meat_saturation[] = {1.8f32, 12.8f32, 1.8f32, 12.8f32, 1.2f32, 9.6f32,
                                                1.2f32, 7.2f32,  1.2f32, 9.6f32,  1.2f32, 2.4f32};
    static_assert(sizeof(meats) / sizeof(meats[0]) == sizeof(meat_food));
    static_assert(sizeof(meat_food) == sizeof(meat_saturation) / sizeof(meat_saturation[0]));
    for (uint32_t i = 0u; i < sizeof(meat_food); i++) {
        CHECK_EQ_U64(item_defs[meats[i]].food, meat_food[i]);
        CHECK_NEAR(item_defs[meats[i]].saturation, meat_saturation[i], PS_CLOSE);
    }
    CHECK_EQ_U64(item_defs[ITEM_ID_COW_HIDE].food, 0u);
    /* Saturation stops at food. */
    player_survival_t low;
    player_survival_start(&low, 0.0f64, 0.0f64, 0.0f64);
    low.food = 0u;
    low.saturation = 0.0f32;
    player_survival_eat(&low, 1u, 7.0f32);
    CHECK_NEAR(low.saturation, 1.0f32, 0.0f32);
}

TEST(armor_counts_against_what_it_should_and_nothing_else) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_no_healing(survival);
    player_survival_armor_set(survival, 20.0f32, 0.0f32);
    CHECK_NEAR(player_survival_hurt(survival, 4.0f32, PLAYER_SURVIVAL_CAUSE_LAVA), 4.0f32 * 0.28f32, PS_CLOSE);
    ps_idle(&rig, 10u);
    CHECK_NEAR(player_survival_hurt(survival, 4.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 4.0f32 * 0.28f32, PS_CLOSE);
    ps_idle(&rig, 10u);
    CHECK_NEAR(player_survival_hurt(survival, 4.0f32, PLAYER_SURVIVAL_CAUSE_FALL), 4.0f32, PS_CLOSE);
    ps_idle(&rig, 10u);
    CHECK_NEAR(player_survival_hurt(survival, 2.0f32, PLAYER_SURVIVAL_CAUSE_DROWN), 2.0f32, PS_CLOSE);
    /* What tires and what does not: a lava or attack hit 0.1, a fall nothing. */
    ps_idle(&rig, 10u);
    survival->exhaustion = 0.0f32;
    (void)player_survival_hurt(survival, 1.0f32, PLAYER_SURVIVAL_CAUSE_FALL);
    CHECK_NEAR(survival->exhaustion, 0.0f32, 0.0f32);
    ps_idle(&rig, 10u);
    (void)player_survival_hurt(survival, 1.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    CHECK_NEAR(survival->exhaustion, 0.1f32, PS_CLOSE);
    ps_end(&rig);
}

/* ---- Death and coming back ------------------------------------------------------------------------------------------------- */

TEST(a_dead_body_falls_and_nothing_moves_it_then_a_press_brings_it_back_whole) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    player_movement_t *const body = &rig.player->movement;
    survival->food = 3u;
    survival->saturation = 0.0f32;
    survival->exhaustion = 2.0f32;
    survival->burn_ticks = 100u;
    /* In the air, with grass where it looks. */
    test_world_fill(t, &rig.world, 8, 8, 0, 8, 8, 0, BLOCK_ID_TALL_GRASS);
    ps_put(&rig, 8.5f64, 7.0f64, 2.0f64);
    ps_aim_at_top(&rig, 8, 8, 0);
    ps_idle(&rig, 1u);
    CHECK_NEAR(player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 100.0f32, PS_CLOSE);
    CHECK_NEAR(survival->health, 0.0f32, 0.0f32);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    CHECK_EQ_U64(survival->killed_by, PLAYER_SURVIVAL_CAUSE_ATTACK);
    CHECK_EQ_U64(survival->died_count, 1u);
    CHECK(player_survival_died_take(survival));
    CHECK(!player_survival_died_take(survival));
    /* The button goes down at once and stays: not a press it can use, however long it is held. */
    ps_keys_t all = PS_NO_KEYS;
    all.forward = 1;
    all.sprint = 1u;
    all.attack = 1u;
    const float64_t x = body->position[0], y = body->position[1];
    for (uint32_t i = 0u; i < 60u; i++) {
        ps_tick(&rig, &all);
    }
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    CHECK_NEAR(body->position[0], x, 0.0f64);
    CHECK_NEAR(body->position[1], y, 0.0f64);
    CHECK_NEAR(body->position[2], 0.0f64, 1.0e-6f64);
    CHECK_EQ_U64(rig.player->interaction.mined_count, 0u);
    CHECK_EQ_U64(ps_block(&rig, 8, 8, 0), BLOCK_ID_TALL_GRASS);
    /* The view is down, and more hits do nothing. */
    CHECK_NEAR(body->eye_height, PLAYER_SURVIVAL_EYE_LYING, 1.0e-9f64);
    CHECK_NEAR(player_survival_hurt(survival, 5.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 0.0f32, 0.0f32);
    CHECK_EQ_U64(survival->died_count, 1u);
    /* Let go, and a press. */
    ps_idle(&rig, 1u);
    all.forward = 0;
    all.sprint = 0u;
    ps_tick(&rig, &all);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->respawned_count, 1u);
    CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    CHECK_EQ_U64(survival->food, 20u);
    CHECK_NEAR(survival->saturation, 5.0f32, 0.0f32);
    CHECK_NEAR(survival->exhaustion, 0.0f32, 0.0f32);
    CHECK_EQ_I64(survival->air, 300);
    CHECK_EQ_U64(survival->burn_ticks, 0u);
    CHECK_EQ_U64(survival->killed_by, PLAYER_SURVIVAL_CAUSE_ATTACK);
    CHECK_NEAR(body->position[0], 0.5f64, 0.0f64);
    CHECK_NEAR(body->position[1], 0.5f64, 0.0f64);
    CHECK_NEAR(body->position[2], 0.0f64, 1.0e-9f64);
    /* It moves again, and the click that brought it back has not mined anything. */
    ps_keys_t walk = PS_NO_KEYS;
    walk.forward = 1;
    for (uint32_t i = 0u; i < 10u; i++) {
        ps_tick(&rig, &walk);
    }
    CHECK(body->position[0] != 0.5f64 || body->position[1] != 0.5f64);
    ps_end(&rig);
}

TEST(a_press_before_twenty_ticks_is_too_soon) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 19u);
    /* The press is taken before that tick counts, so 19 are done. */
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->respawned_count, 1u);
    /* Nobody at the controls cannot press. */
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 30u);
    player_hands_send(rig.player, true, true);
    rig.now_ns += PLAYER_INTENT_STALE_NS + PLAYER_TICK_NS;
    player_interact_tick(rig.player, &rig.world.world->edit, rig.now_ns);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    ps_end(&rig);
}

TEST(coming_back_the_body_goes_above_what_was_built_at_the_place) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_put(&rig, 8.5f64, 8.5f64, 0.0f64);
    test_world_fill(t, &rig.world, 0, 0, 0, 0, 0, 3, BLOCK_ID_STONE);
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 25u);
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->placing, 0u);
    CHECK_NEAR(rig.player->movement.position[2], 4.0f64, 1.0e-6f64);
    ps_end(&rig);
}

TEST(coming_back_to_ground_not_loaded_it_holds_still_until_there_is_some) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    player_movement_t *const body = &rig.player->movement;
    /* Far past what the test's world has loaded, which is 3 chunks round the middle. */
    survival->spawn[0] = 200.5f64;
    survival->spawn[1] = 0.5f64;
    survival->spawn[2] = 5.0f64;
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 25u);
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->respawned_count, 1u);
    CHECK_EQ_U64(survival->placing, 1u);
    CHECK(player_survival_holds_body(survival));
    ps_keys_t walk = PS_NO_KEYS;
    walk.forward = 1;
    walk.attack = 1u;
    for (uint32_t i = 0u; i < 10u; i++) {
        ps_tick(&rig, &walk);
    }
    CHECK_NEAR(body->position[0], 200.5f64, 0.0f64);
    CHECK_NEAR(body->position[2], 5.0f64, 0.0f64);
    CHECK_EQ_U64(survival->placing, 1u);
    /* Nothing can hurt it yet. */
    CHECK_NEAR(player_survival_hurt(survival, 5.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK), 0.0f32, 0.0f32);
    /* The world follows its eye there, and what it comes back into is stone with its top at z = 7. */
    world_map_set_center(rig.world.map, 12, 0);
    test_world_stream_all(t, &rig.world);
    test_world_fill(t, &rig.world, 200, 0, 0, 200, 0, 6, BLOCK_ID_STONE);
    ps_idle(&rig, 1u);
    CHECK_EQ_U64(survival->placing, 0u);
    CHECK_NEAR(body->position[2], 7.0f64, 1.0e-6f64);
    CHECK(!player_survival_holds_body(survival));
    ps_end(&rig);
}

/* ---- The bed ------------------------------------------------------------------------------------------------------------------ */

/* A bed at (2, 0, 0), looked at from (0.5, 0.5, 0), and the aim taken. */
static void ps_bed_begin(test_context_t *t, ps_rig_t *rig) {
    test_world_fill(t, &rig->world, 2, 0, 0, 2, 0, 0, BLOCK_ID_BED);
    ps_aim_at_top(rig, 2, 0, 0);
    ps_idle(rig, 2u);
    ps_aim_at_top(rig, 2, 0, 0);
    ps_idle(rig, 1u);
    CHECK_EQ_U64(rig->player->target.hit.block, BLOCK_ID_BED);
}

TEST(by_day_a_bed_is_only_where_a_body_comes_back_to) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    ps_bed_begin(t, &rig);
    player_survival_sun_set(survival, ps_equator_sun(0.5f64));
    /* It holds dirt, which goes nowhere: the bed takes the click. */
    player_interaction_hand_give(&rig.player->interaction, BLOCK_ID_DIRT, 4u);
    ps_click_place(&rig);
    CHECK_EQ_U64(survival->bed_answer, PLAYER_SURVIVAL_BED_NOT_NIGHT);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->has_bed, 1u);
    CHECK_EQ_I64(survival->bed_cell[0], 2);
    CHECK_EQ_I64(survival->bed_cell[1], 0);
    CHECK_EQ_I64(survival->bed_cell[2], 0);
    CHECK_EQ_U64(rig.player->interaction.placed_count, 0u);
    CHECK_EQ_U64(rig.player->interaction.hand_count, 4u);
    /* The left button still breaks it, as any block. */
    ps_keys_t mine = PS_NO_KEYS;
    mine.attack = 1u;
    for (uint32_t i = 0u; i < 40u; i++) {
        ps_tick(&rig, &mine);
    }
    CHECK_EQ_U64(ps_block(&rig, 2, 0, 0), BLOCK_ID_AIR);
    player_drop_t drop;
    REQUIRE(player_interaction_drop_next(&rig.player->interaction, &drop));
    CHECK_STR_EQ(block_drop_items[drop.item].name, "bed");
    ps_end(&rig);
}

TEST(a_night_in_bed_takes_100_ticks_and_ends_in_the_morning_beside_it) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    player_movement_t *const body = &rig.player->movement;
    ps_bed_begin(t, &rig);
    player_survival_sun_set(survival, ps_equator_sun(0.9f64));
    /* Held down from here to the end: a hold does not wake it. */
    ps_keys_t hold = PS_NO_KEYS;
    hold.place = 1u;
    ps_tick(&rig, &hold);
    CHECK_EQ_U64(survival->bed_answer, PLAYER_SURVIVAL_BED_ASLEEP);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_SLEEPING);
    CHECK_EQ_U64(player_vitals_read(rig.player).state, PLAYER_SURVIVAL_SLEEPING);
    CHECK_NEAR(body->position[0], 2.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[1], 0.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[2], 0.5f64, 1.0e-9f64);
    for (uint32_t i = 2u; i < PLAYER_SURVIVAL_SLEEP_TICKS; i++) {
        ps_tick(&rig, &hold);
        CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_SLEEPING);
        CHECK(!player_survival_morning_take(survival));
    }
    /* Lying on it, however it got here. */
    CHECK_NEAR(body->position[2], 0.5f64, 1.0e-9f64);
    CHECK_NEAR(body->eye_height, PLAYER_SURVIVAL_EYE_LYING, 1.0e-9f64);
    /* It went on being tired and whole while it slept, and what it held was not used. */
    CHECK_EQ_U64(rig.player->interaction.placed_count, 0u);
    ps_tick(&rig, &hold);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->slept_count, 1u);
    CHECK(player_survival_morning_take(survival));
    CHECK(!player_survival_morning_take(survival));
    /* Beside it, on the first side there is room, standing and on the ground. */
    CHECK_NEAR(body->position[0], 3.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[1], 0.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[2], 0.0f64, 1.0e-9f64);
    CHECK_NEAR(body->eye_height, PLAYER_MOVEMENT_EYE_STANDING, 1.0e-9f64);
    ps_idle(&rig, 3u);
    CHECK_EQ_U64(body->on_ground, 1u);
    ps_end(&rig);
}

TEST(a_press_a_hit_and_the_bed_going_each_end_a_night_early_with_no_morning) {
    ps_rig_t rig;
    /* A press. */
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *survival = &rig.player->survival;
    ps_bed_begin(t, &rig);
    player_survival_sun_set(survival, ps_equator_sun(0.1f64));
    ps_click_place(&rig);
    REQUIRE(survival->state == PLAYER_SURVIVAL_SLEEPING);
    ps_keys_t walk = PS_NO_KEYS;
    walk.forward = 1;
    for (uint32_t i = 0u; i < 10u; i++) {
        ps_tick(&rig, &walk);
    }
    /* Walking keys are no press, and do nothing to it. */
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_SLEEPING);
    CHECK_NEAR(rig.player->movement.position[0], 2.5f64, 1.0e-9f64);
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->slept_count, 0u);
    CHECK(!player_survival_morning_take(survival));
    CHECK_NEAR(rig.player->movement.position[0], 3.5f64, 1.0e-9f64);
    ps_end(&rig);

    /* A hit: it stays on the bed. */
    REQUIRE(ps_begin(t, &rig));
    survival = &rig.player->survival;
    ps_bed_begin(t, &rig);
    player_survival_sun_set(survival, ps_equator_sun(0.9f64));
    ps_click_place(&rig);
    REQUIRE(survival->state == PLAYER_SURVIVAL_SLEEPING);
    CHECK(player_survival_hurt(survival, 2.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK) > 0.0f32);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    ps_idle(&rig, 3u);
    CHECK_NEAR(rig.player->movement.position[0], 2.5f64, 1.0e-9f64);
    CHECK_NEAR(rig.player->movement.position[2], 0.5f64, 1.0e-9f64);
    CHECK(!player_survival_morning_take(survival));
    ps_end(&rig);

    /* The bed going: it falls to the floor. */
    REQUIRE(ps_begin(t, &rig));
    survival = &rig.player->survival;
    ps_bed_begin(t, &rig);
    player_survival_sun_set(survival, ps_equator_sun(0.9f64));
    ps_click_place(&rig);
    REQUIRE(survival->state == PLAYER_SURVIVAL_SLEEPING);
    REQUIRE(world_edit_block_set(&rig.world.world->edit, 2, 0, 0, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    ps_idle(&rig, 20u);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_NEAR(rig.player->movement.position[2], 0.0f64, 1.0e-6f64);
    CHECK(!player_survival_morning_take(survival));
    ps_end(&rig);
}

TEST(a_sneak_places_against_a_bed_and_one_too_far_off_says_so) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *survival = &rig.player->survival;
    ps_bed_begin(t, &rig);
    player_survival_sun_set(survival, ps_equator_sun(0.9f64));
    player_interaction_hand_give(&rig.player->interaction, BLOCK_ID_DIRT, 4u);
    ps_keys_t sneak = PS_NO_KEYS;
    sneak.sneak = 1u;
    /* The eye is lower with a crouch, so the aim is taken again. */
    for (uint32_t i = 0u; i < 6u; i++) {
        ps_tick(&rig, &sneak);
    }
    ps_aim_at_top(&rig, 2, 0, 0);
    ps_tick(&rig, &sneak);
    REQUIRE(rig.player->target.hit.block == BLOCK_ID_BED);
    sneak.place = 1u;
    ps_tick(&rig, &sneak);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->has_bed, 0u);
    CHECK_EQ_U64(rig.player->interaction.placed_count, 1u);
    ps_end(&rig);

    /* Within what a hand reaches, and past what a bed allows: 4 away across its middle. */
    REQUIRE(ps_begin(t, &rig));
    survival = &rig.player->survival;
    test_world_fill(t, &rig.world, 4, 0, 0, 4, 0, 0, BLOCK_ID_BED);
    ps_aim_at_top(&rig, 4, 0, 0);
    ps_idle(&rig, 2u);
    REQUIRE(rig.player->target.hit.block == BLOCK_ID_BED);
    player_survival_sun_set(survival, ps_equator_sun(0.9f64));
    ps_click_place(&rig);
    CHECK_EQ_U64(survival->bed_answer, PLAYER_SURVIVAL_BED_TOO_FAR);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->has_bed, 0u);
    ps_end(&rig);
}

TEST(death_comes_back_to_the_bed_unless_it_has_gone_and_on_it_if_it_is_walled_in) {
    ps_rig_t rig;
    REQUIRE(ps_begin(t, &rig));
    player_survival_t *const survival = &rig.player->survival;
    player_movement_t *const body = &rig.player->movement;
    ps_bed_begin(t, &rig);
    player_survival_sun_set(survival, ps_equator_sun(0.5f64));
    ps_click_place(&rig);
    REQUIRE(survival->has_bed == 1u);
    ps_put(&rig, 20.5f64, 20.5f64, 0.0f64);
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 25u);
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_NEAR(body->position[0], 3.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[1], 0.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[2], 0.0f64, 1.0e-9f64);

    /* Walled in at both heights on all four sides: on it. */
    ps_put(&rig, 20.5f64, 20.5f64, 0.0f64);
    test_world_fill(t, &rig.world, 1, -1, 0, 3, 1, 2, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 2, 0, 0, 2, 0, 2, BLOCK_ID_AIR);
    test_world_fill(t, &rig.world, 2, 0, 0, 2, 0, 0, BLOCK_ID_BED);
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 25u);
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_NEAR(body->position[0], 2.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[2], 0.5f64, 1.0e-9f64);

    /* The bed gone: the world's place, and from then on no bed. */
    ps_put(&rig, 20.5f64, 20.5f64, 0.0f64);
    test_world_fill(t, &rig.world, 2, 0, 0, 2, 0, 0, BLOCK_ID_AIR);
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    ps_idle(&rig, 25u);
    ps_press_jump(&rig);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->has_bed, 0u);
    CHECK_NEAR(body->position[0], 0.5f64, 1.0e-9f64);
    CHECK_NEAR(body->position[1], 0.5f64, 1.0e-9f64);
    ps_end(&rig);
}

/* ---- The same bytes, and no heap --------------------------------------------------------------------------------------------- */

/* A bit of everything, always in the same order. */
static void ps_a_long_day(test_context_t *t, ps_rig_t *rig) {
    player_survival_t *const survival = &rig->player->survival;
    test_world_fill(t, &rig->world, 10, -8, 0, 12, 8, 1, BLOCK_ID_WATER);
    test_world_fill(t, &rig->world, 20, -8, 0, 22, 8, 1, BLOCK_ID_LAVA);
    test_world_fill(t, &rig->world, 30, -8, 0, 30, 8, 2, BLOCK_ID_CACTUS);
    player_survival_hold_food(survival, ITEM_ID_APPLE, 4u, 2.4f32);
    ps_keys_t keys = PS_NO_KEYS;
    keys.forward = 1;
    for (uint32_t i = 0u; i < 1500u; i++) {
        keys.sprint = i % 300u < 200u ? 1u : 0u;
        keys.jump = i % 17u == 0u ? 1u : 0u;
        keys.attack = survival->state == PLAYER_SURVIVAL_DEAD && i % 40u == 0u ? 1u : 0u;
        keys.place = survival->food < 18u && i % 100u < 40u ? 1u : 0u;
        rig->yaw = (float32_t)((i / 200u) % 2u) * 3.14159265f32;
        ps_tick(rig, &keys);
    }
}

TEST(the_same_world_and_buttons_give_the_same_bytes_with_nothing_from_the_heap) {
    player_survival_t *const first = test_alloc(t, sizeof(player_survival_t));
    player_movement_t *const first_body = test_alloc(t, sizeof(player_movement_t));
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        ps_rig_t rig;
        REQUIRE(ps_begin(t, &rig));
        heap_counters_t before, after;
        heap_counters_read(&before);
        ps_a_long_day(t, &rig);
        heap_counters_read(&after);
        CHECK_MEM_EQ(&before, &after, sizeof(before));
        const player_survival_t *const survival = &rig.player->survival;
        /* It did do something. */
        CHECK(survival->hurt_count > 0u);
        CHECK(survival->exhaustion > 0.0f32 || survival->saturation < PLAYER_SURVIVAL_SATURATION_START);
        if (pass == 0u) {
            memcpy(first, survival, sizeof(*first));
            memcpy(first_body, &rig.player->movement, sizeof(*first_body));
        } else {
            CHECK_MEM_EQ(first, survival, sizeof(*first));
            CHECK_MEM_EQ(first_body, &rig.player->movement, sizeof(*first_body));
        }
        ps_end(&rig);
    }
}

/* ---- The real game --------------------------------------------------------------------------------------------------------- */

enum : uint32_t {
    PS_WIDTH = 320u,
    PS_HEIGHT = 180u,
    PS_PROGRAM_TIMEOUT_MS = 120000u,
};

typedef struct ps_game_rig_t {
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
    entity_t *entity;            /* 80 */
    item_t *item;                /* 88 */
} ps_game_rig_t;
static_assert(sizeof(ps_game_rig_t) == 96);

static bool ps_game_begin(test_context_t *t, ps_game_rig_t *rig) {
    rig->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    rig->job = test_world_aligned(t, sizeof(job_system_t));
    rig->world = test_world_aligned(t, sizeof(world_t));
    rig->terrain = test_world_aligned(t, sizeof(terrain_t));
    rig->render = test_world_aligned(t, sizeof(render_t));
    rig->texture = test_world_aligned(t, sizeof(texture_t));
    rig->game = test_world_aligned(t, sizeof(game_t));
    rig->physics = test_world_aligned(t, sizeof(physics_t));
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->item = test_world_aligned(t, sizeof(item_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(entity_init(rig->entity, rig->physics)) || !CHECK(item_init(rig->item)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    game_entity_set(rig->game, rig->entity);
    game_item_set(rig->game, rig->item);
    return true;
}

static void ps_game_end(test_context_t *t, ps_game_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    item_deinit(rig->item);
    entity_deinit(rig->entity);
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static const game_config_t ps_small_game = {
    .render_distance = 5u, .width = PS_WIDTH, .height = PS_HEIGHT, .show_date = 0u};

/* The headless scene's ticks, as a script gives them. */
typedef struct ps_script_t {
    display_headless_scene_t scene; /* 0 */
    display_script_state_t state;   /* 24 */
    uint32_t tick;                  /* 176 */
    uint8_t _pad0[4];               /* 180 */
} ps_script_t;
static_assert(sizeof(ps_script_t) == 184);

static void ps_script_key(ps_script_t *script, uint32_t code, bool down) {
    const uint64_t bit = 1ull << (code % 64u);
    script->state.down[code / 64u] =
        down ? script->state.down[code / 64u] | bit : script->state.down[code / 64u] & ~bit;
}

static bool ps_script_ticks(ps_script_t *script, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        script->tick++;
        const display_headless_tick_t tick = {
            .events = nullptr, .state = &script->state, .event_count = 0u, .tick = script->tick};
        if (!script->scene.tick(script->scene.borrowed_context, &tick)) {
            return false;
        }
    }
    return true;
}

static uint32_t ps_on_ground(const entity_t *entity, uint16_t item) {
    const item_drop_store_t *const drops = &entity->item_drops;
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < drops->count; i++) {
        count += block_drop_items[drops->borrowed_items[i].item].item == item ? drops->borrowed_items[i].count : 0u;
    }
    return count;
}

TEST(in_a_real_game_a_script_eats_keeps_a_bowl_places_a_bed_sleeps_dies_spills_and_comes_back_to_pick_it_up) {
    ps_game_rig_t rig;
    REQUIRE(ps_game_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &ps_small_game));
    REQUIRE(game_settle(game));
    ps_script_t *const script = test_alloc(t, sizeof(ps_script_t));
    memset(script, 0, sizeof(*script));
    game_headless_scene(game, &script->scene);
    player_survival_t *const survival = &rig.player->survival;
    inventory_t *const inventory = &rig.item->inventory;
    /* The ground the seed starts on falls away steeply, so a floor of its own to work on, with the sky over it. */
    const int32_t pad_x = (int32_t)floor(rig.player->movement.position[0]);
    const int32_t pad_y = (int32_t)floor(rig.player->movement.position[1]);
    const int32_t pad_z = (int32_t)floor(rig.player->movement.position[2]);
    for (int32_t x = pad_x - 6; x <= pad_x + 6; x++) {
        for (int32_t y = pad_y - 6; y <= pad_y + 6; y++) {
            for (int32_t z = pad_z - 1; z <= pad_z + 8; z++) {
                const world_edit_status_t status =
                    world_edit_block_set(&rig.world->edit, x, y, z, z < pad_z ? BLOCK_ID_STONE : BLOCK_ID_AIR);
                REQUIRE(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
        }
    }
    REQUIRE(ps_script_ticks(script, 10u));

    /* Apples in the first slot, a bed in the second and a stew in the third. Looking at the sky. */
    CHECK_EQ_U64(inventory_add(inventory, ITEM_ID_APPLE, 3u), 3u);
    REQUIRE(inventory_slot_read(inventory, 0u)->item == ITEM_ID_APPLE);
    *inventory_slot(inventory, 1u) = item_stack_make(ITEM_ID_BED, 1u);
    *inventory_slot(inventory, 2u) = item_stack_make(ITEM_ID_MUSHROOM_STEW, 1u);
    inventory->added_count += 2u;
    game->camera.pitch = 1.5f32;
    survival->food = 8u;
    survival->saturation = 0.0f32;
    ps_script_key(script, BTN_RIGHT, true);
    REQUIRE(ps_script_ticks(script, 33u));
    ps_script_key(script, BTN_RIGHT, false);
    REQUIRE(ps_script_ticks(script, 1u));
    CHECK_EQ_U64(survival->eaten_count, 1u);
    CHECK_EQ_U64(survival->food, 12u);
    CHECK_EQ_U64(inventory_slot_read(inventory, 0u)->count, 2u);

    /* A stew leaves its bowl in the hand. */
    ps_script_key(script, KEY_3, true);
    REQUIRE(ps_script_ticks(script, 1u));
    ps_script_key(script, KEY_3, false);
    ps_script_key(script, BTN_RIGHT, true);
    REQUIRE(ps_script_ticks(script, 33u));
    ps_script_key(script, BTN_RIGHT, false);
    REQUIRE(ps_script_ticks(script, 1u));
    CHECK_EQ_U64(survival->eaten_count, 2u);
    CHECK_EQ_U64(survival->food, 18u);
    CHECK_EQ_U64(inventory_slot_read(inventory, 2u)->item, ITEM_ID_BOWL);
    CHECK_EQ_U64(inventory_slot_read(inventory, 2u)->count, 1u);
    CHECK_EQ_U64(inventory->added_count, inventory->removed_count + inventory_items_held(inventory));

    /* The bed goes down where it looks, and it costs the one in hand. */
    ps_script_key(script, KEY_2, true);
    REQUIRE(ps_script_ticks(script, 1u));
    ps_script_key(script, KEY_2, false);
    game->camera.pitch = -0.8f32;
    REQUIRE(ps_script_ticks(script, 3u));
    REQUIRE(rig.player->target.hit.result == RAYCAST_RESULT_HIT);
    uint64_t bytes = post_xrgb_bytes(PS_WIDTH, PS_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = PS_WIDTH * 4u,
                               .width = PS_WIDTH,
                               .height = PS_HEIGHT};
    const display_headless_tick_t draw_tick = {
        .events = nullptr, .state = &script->state, .event_count = 0u, .tick = 0u};
    REQUIRE(script->scene.draw(script->scene.borrowed_context, &draw_tick, &target));
    uint8_t *const before = test_alloc(t, bytes);
    memcpy(before, target.owned_map, bytes);
    ps_script_key(script, BTN_RIGHT, true);
    REQUIRE(ps_script_ticks(script, 1u));
    ps_script_key(script, BTN_RIGHT, false);
    REQUIRE(ps_script_ticks(script, 3u));
    CHECK_EQ_U64(rig.player->interaction.placed_count, 1u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_BED), 0u);
    REQUIRE(rig.player->target.hit.block == BLOCK_ID_BED);
    /* And it can be seen. */
    REQUIRE(script->scene.draw(script->scene.borrowed_context, &draw_tick, &target));
    CHECK(memcmp(before, target.owned_map, bytes) != 0);
    const int32_t bed[3] = {rig.player->target.hit.cell[0], rig.player->target.hit.cell[1],
                            rig.player->target.hit.cell[2]};

    /* Night, through game_day_set as a console would. The click sleeps, and the morning comes to the sky too. */
    game_day_set(game, 0.9f64);
    REQUIRE(ps_script_ticks(script, 1u));
    ps_script_key(script, BTN_RIGHT, true);
    REQUIRE(ps_script_ticks(script, 1u));
    ps_script_key(script, BTN_RIGHT, false);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_SLEEPING);
    CHECK_EQ_U64(survival->has_bed, 1u);
    CHECK_EQ_I64(survival->bed_cell[0], bed[0]);
    CHECK_EQ_I64(survival->bed_cell[1], bed[1]);
    CHECK_EQ_I64(survival->bed_cell[2], bed[2]);
    /* The click's own tick was the first. */
    REQUIRE(ps_script_ticks(script, PLAYER_SURVIVAL_SLEEP_TICKS - 2u));
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_SLEEPING);
    REQUIRE(ps_script_ticks(script, 1u));
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_EQ_U64(survival->slept_count, 1u);
    REQUIRE(ps_script_ticks(script, 1u));
    CHECK_NEAR(game->day_fraction, PLAYER_SURVIVAL_MORNING, 0.001f64);
    CHECK_NEAR(rig.player->movement.eye_height, PLAYER_MOVEMENT_EYE_STANDING, 0.01f64);

    /* A hit from outside kills it, and what it carried goes on the ground, and stays there while it is dead. */
    REQUIRE(ps_script_ticks(script, 10u));
    const uint64_t items_held = inventory_items_held(inventory);
    CHECK_EQ_U64(items_held, 3u);
    const uint64_t dropped_before = rig.entity->item_drops.dropped_count;
    const uint64_t picked_before = rig.entity->item_drops.picked_up_count;
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    REQUIRE(ps_script_ticks(script, 1u));
    CHECK_EQ_U64(inventory_items_held(inventory), 0u);
    CHECK_EQ_U64(inventory_stacks_held(inventory), 0u);
    CHECK_EQ_U64(rig.entity->item_drops.dropped_count, dropped_before + items_held);
    CHECK_EQ_U64(inventory->added_count, inventory->removed_count);
    REQUIRE(ps_script_ticks(script, PLAYER_SURVIVAL_RESPAWN_WAIT_TICKS + 20u));
    CHECK_EQ_U64(rig.entity->item_drops.picked_up_count, picked_before);
    CHECK_EQ_U64(survival->died_count, 1u);

    /* Space, and it is back by the bed, whole, with its things beside it for the picking up. */
    ps_script_key(script, KEY_SPACE, true);
    REQUIRE(ps_script_ticks(script, 1u));
    ps_script_key(script, KEY_SPACE, false);
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK_NEAR(survival->health, 20.0f32, 0.0f32);
    const float64_t across_x = rig.player->movement.position[0] - ((float64_t)bed[0] + 0.5f64);
    const float64_t across_y = rig.player->movement.position[1] - ((float64_t)bed[1] + 0.5f64);
    CHECK(fabs(across_x) <= 1.0f64 && fabs(across_y) <= 1.0f64);
    REQUIRE(ps_script_ticks(script, 60u));
    CHECK(inventory_items_held(inventory) > 0u);
    CHECK(rig.entity->item_drops.picked_up_count > picked_before);
    /* All of it is either back in hand or still lying there. Only these: leaves near the floor it made go on decaying and dropping. */
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_APPLE) + ps_on_ground(rig.entity, ITEM_ID_APPLE), 2u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_BOWL) + ps_on_ground(rig.entity, ITEM_ID_BOWL), 1u);
    game_close(game);
    ps_game_end(t, &rig);
}

TEST(in_a_real_game_milk_is_drunk_and_leaves_its_bucket_in_the_hand) {
    ps_game_rig_t rig;
    REQUIRE(ps_game_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &ps_small_game));
    REQUIRE(game_settle(game));
    ps_script_t *const script = test_alloc(t, sizeof(ps_script_t));
    memset(script, 0, sizeof(*script));
    game_headless_scene(game, &script->scene);
    player_survival_t *const survival = &rig.player->survival;
    inventory_t *const inventory = &rig.item->inventory;
    REQUIRE(ps_script_ticks(script, 10u));
    /* Milk in the first slot, looking at the sky so that the button only drinks. */
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_MILK_BUCKET, 1u);
    inventory->added_count++;
    game->camera.pitch = 1.5f32;
    survival->food = 8u;
    survival->saturation = 0.0f32;
    ps_script_key(script, KEY_1, true);
    REQUIRE(ps_script_ticks(script, 1u));
    ps_script_key(script, KEY_1, false);
    ps_script_key(script, BTN_RIGHT, true);
    REQUIRE(ps_script_ticks(script, 33u));
    ps_script_key(script, BTN_RIGHT, false);
    REQUIRE(ps_script_ticks(script, 1u));
    CHECK_EQ_U64(survival->eaten_count, 1u);
    CHECK_EQ_U64(survival->food, 8u + (uint32_t)item_defs[ITEM_ID_MILK_BUCKET].food);
    CHECK_EQ_U64(inventory_slot_read(inventory, 0u)->item, ITEM_ID_BUCKET);
    CHECK_EQ_U64(inventory_slot_read(inventory, 0u)->count, 1u);
    CHECK_EQ_U64(inventory->added_count, inventory->removed_count + inventory_items_held(inventory));
    game_close(game);
    ps_game_end(t, &rig);
}

/* From another thread, as a real run has it: main sees it all through player_vitals_read. */
TEST(on_the_real_threads_it_eats_dies_in_real_lava_and_comes_back_to_a_real_bed) {
    ps_game_rig_t rig;
    REQUIRE(ps_game_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &ps_small_game));
    REQUIRE(game_settle(game));
    player_t *const player = rig.player;
    inventory_t *const inventory = &rig.item->inventory;
    CHECK_EQ_U64(inventory_add(inventory, ITEM_ID_APPLE, 2u), 2u);
    player->survival.food = 10u;
    player->survival.saturation = 0.0f32;
    game->camera.pitch = 1.5f32;
    const uint64_t bytes = post_xrgb_bytes(PS_WIDTH, PS_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = PS_WIDTH * 4u,
                               .width = PS_WIDTH,
                               .height = PS_HEIGHT};
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    REQUIRE(game_loop_start(game, nullptr));
    const int64_t fed_by = time_now_ns() + 30000000000;
    while (time_now_ns() < fed_by && player_vitals_read(player).food != 14u) {
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, true);
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    CHECK_EQ_U64(player_vitals_read(player).food, 14u);
    CHECK(game_loop_stop(game));
    CHECK(player->survival.eaten_count >= 1u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_APPLE), 2u - player->survival.eaten_count);

    /* With the threads stopped, this thread may change the world: lava where it stands, and a bed to come back to, far from where lava
     * runs. */
    const int32_t x = (int32_t)floor(player->movement.position[0]);
    const int32_t y = (int32_t)floor(player->movement.position[1]);
    const int32_t z = (int32_t)floor(player->movement.position[2]);
    world_edit_t *const edit = &rig.world->edit;
    const int32_t bed_x = x + 24;
    int32_t bed_z = COLUMN_Z_GAMEPLAY_MAX;
    uint16_t found = BLOCK_ID_AIR;
    while (bed_z > COLUMN_Z_GAMEPLAY_MIN) {
        REQUIRE(world_map_block_get(&rig.world->map, bed_x, y, bed_z, &found) == WORLD_MAP_STATUS_OK);
        if (found != BLOCK_ID_AIR) {
            break;
        }
        bed_z--;
    }
    bed_z++;
    REQUIRE(world_edit_block_set(edit, bed_x, y, bed_z, BLOCK_ID_BED) == WORLD_EDIT_STATUS_CHANGED);
    player->survival.bed_cell[0] = bed_x;
    player->survival.bed_cell[1] = y;
    player->survival.bed_cell[2] = bed_z;
    player->survival.has_bed = 1u;
    REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_LAVA) == WORLD_EDIT_STATUS_CHANGED);

    REQUIRE(game_loop_start(game, nullptr));
    const int64_t dead_by = time_now_ns() + 30000000000;
    while (time_now_ns() < dead_by && player_vitals_read(player).state != PLAYER_SURVIVAL_DEAD) {
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, false);
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    CHECK_EQ_U64(player_vitals_read(player).state, PLAYER_SURVIVAL_DEAD);
    /* Past the wait, for sure. */
    const int64_t waited_until = time_now_ns() + 1500000000;
    while (time_now_ns() < waited_until) {
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, false);
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    CHECK_EQ_U64(player_vitals_read(player).state, PLAYER_SURVIVAL_DEAD);
    /* A press that begins and ends between two ticks. */
    game_walk_send(game, &none, true, false, time_now_ns());
    game_walk_send(game, &none, false, false, time_now_ns());
    const int64_t back_by = time_now_ns() + 5000000000;
    while (time_now_ns() < back_by && player_vitals_read(player).state == PLAYER_SURVIVAL_DEAD) {
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, false);
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    CHECK_EQ_U64(player_vitals_read(player).state, PLAYER_SURVIVAL_ALIVE);
    CHECK(game_loop_stop(game));
    CHECK_EQ_U64(player->survival.died_count, 1u);
    CHECK_EQ_U64(player->survival.killed_by, PLAYER_SURVIVAL_CAUSE_LAVA);
    CHECK_EQ_U64(player->survival.respawned_count, 1u);
    CHECK(fabs(player->movement.position[0] - ((float64_t)bed_x + 0.5f64)) <= 1.5f64);
    CHECK_NEAR(player_vitals_read(player).health, 20.0f32, 0.0f32);
    game_close(game);
    ps_game_end(t, &rig);
}

/* ---- The built program ---------------------------------------------------------------------------------------------------------- */

typedef struct ps_temp_dir_t {
    char path[64]; /* 0 */
} ps_temp_dir_t;
static_assert(sizeof(ps_temp_dir_t) == 64);

static bool ps_temp_dir_make(ps_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_player_survival_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void ps_temp_dir_remove(const ps_temp_dir_t *dir) {
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

/* Sprints and jumps for 45 s along a dry run of land the test finds from seed 11's terrain, and goes to by /tp. What lay ahead of
 * where seed 11 starts is the land's, and it moves with each plan that changes it: a jungle's trunks since plan 006 phase 03, and
 * since plan 007 phase 03 water, where a body swims and does not sprint, so it spent 3 of its 5 saturation. So, as the dive finds
 * its sea (tests/cuda/under_water/), the run is on land the terrain says is dry and gentle all the way, facing the way a new
 * world first looks, and 45 s of sprinting and jumping there spends far more than the 5 saturation it starts with. */
enum : uint32_t {
    PS_RUN_SEED = 11u,
    PS_RUN_LENGTH = 480u,      /* blocks of dry land ahead: 45 s of sprint jumps covers about 320 */
    PS_RUN_STEP = 8u,          /* blocks between the columns looked at along it */
    PS_RUN_RISE = 5u,          /* at most this much up or down between them */
    PS_RUN_SEARCH_STEP = 64u,  /* blocks between the places tried, ring by ring round the world's middle */
    PS_RUN_SEARCH_RINGS = 64u, /* 4 km out */
    PS_RUN_TP_TICK = 20u,
    PS_RUN_LOADED_TICKS = 500u, /* a far jump's columns are drawn within 15 s (plan 006 phase 01) */
    PS_RUN_TICKS = 900u,
    PS_RUN_SCRIPT_BYTES = 4096u,
};
#define PS_RUN_YAW                                                                                                     \
    0.6f64 /* GAME_START_YAW: a new world's first look, which a /tp keeps; w walks along (cos, sin) of it */

/* Whether the run from x, y along the start's look is dry land, above the sea, rising or falling gently, all the way. */
static bool ps_run_dry(const terrain_t *terrain, int32_t x, int32_t y) {
    float32_t last = 0.0f32;
    for (uint32_t i = 0u; i * PS_RUN_STEP <= PS_RUN_LENGTH; i++) {
        const float64_t along = (float64_t)(i * PS_RUN_STEP);
        const int32_t cx = x + (int32_t)lround(cos(PS_RUN_YAW) * along);
        const int32_t cy = y + (int32_t)lround(sin(PS_RUN_YAW) * along);
        const terrain_hydrology_column_t column = terrain_hydrology_column_at(terrain->borrowed_hydrology, cx, cy);
        if (column.kind != TERRAIN_HYDROLOGY_KIND_LAND || column.ground < (float32_t)MC_WORLD_Z_SEA + 2.0f32 ||
            (i > 0u && fabsf(column.ground - last) > (float32_t)PS_RUN_RISE)) {
            return false;
        }
        last = column.ground;
    }
    return true;
}

/* The script: /tp to the nearest such run's start, a wait for the columns there, then 45 s of sprinting and jumping. */
static bool ps_run_script_write(test_context_t *t, const char *path) {
    terrain_t *const terrain = test_alloc(t, sizeof(terrain_t));
    if (!CHECK(terrain_init(terrain, PS_RUN_SEED))) {
        return false;
    }
    bool found = false;
    int32_t run_x = 0, run_y = 0;
    const int32_t step = (int32_t)PS_RUN_SEARCH_STEP;
    for (int32_t ring = 0; ring < (int32_t)PS_RUN_SEARCH_RINGS && !found; ring++) {
        for (int32_t dy = -ring; dy <= ring && !found; dy++) {
            for (int32_t dx = -ring; dx <= ring && !found; dx++) {
                if (dx != -ring && dx != ring && dy != -ring && dy != ring) {
                    continue;
                }
                const int32_t x = GAME_WORLD_MIDDLE + dx * step, y = GAME_WORLD_MIDDLE + dy * step;
                if (ps_run_dry(terrain, x, y)) {
                    run_x = x, run_y = y, found = true;
                }
            }
        }
    }
    terrain_deinit(terrain);
    if (!CHECK(found)) {
        return false;
    }
    printf("  the run starts at %" PRId32 ", %" PRId32 "\n", run_x, run_y);
    char *const text = test_alloc(t, PS_RUN_SCRIPT_BYTES);
    uint64_t length = 0u;
    uint32_t tick = PS_RUN_TP_TICK;
    char command[64];
    snprintf(command, sizeof(command), "/tp %" PRId32 " %" PRId32, run_x, run_y);
    for (const char *c = command; *c != '\0'; c++) {
        char name[16];
        if (*c == ' ') {
            snprintf(name, sizeof(name), "space");
        } else if (*c == '/') {
            snprintf(name, sizeof(name), "slash");
        } else if (*c == '-') {
            snprintf(name, sizeof(name), "minus");
        } else {
            snprintf(name, sizeof(name), "%c", *c);
        }
        tick += *c == '/' ? 1u : 2u;
        length += (uint64_t)snprintf(text + length, PS_RUN_SCRIPT_BYTES - length,
                                     "%" PRIu32 " key %s down\n%" PRIu32 " key %s up\n", tick, name, tick, name);
    }
    tick += 2u;
    length += (uint64_t)snprintf(text + length, PS_RUN_SCRIPT_BYTES - length,
                                 "%" PRIu32 " key enter down\n%" PRIu32 " key enter up\n", tick, tick);
    tick += PS_RUN_LOADED_TICKS;
    length += (uint64_t)snprintf(text + length, PS_RUN_SCRIPT_BYTES - length,
                                 "%" PRIu32 " key lctrl down\n%" PRIu32 " key w down\n%" PRIu32
                                 " key space down\n%" PRIu32 " end\n",
                                 tick, tick, tick, tick + PS_RUN_TICKS);
    return CHECK_EQ_U64(file_write_atomic(path, text, length), FILE_OK);
}

static void ps_program_run(test_context_t *t, const ps_temp_dir_t *dir, const char *script, test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--seed",
                          (char *)"11",
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
    test_process_run(t, argv, PS_PROGRAM_TIMEOUT_MS, out);
}

/* The report's one line about this, whole, out of the program's output. */
static void ps_report_line(const char *output, char *out, uint64_t capacity) {
    const char *const from = strstr(output, "the player has ");
    out[0] = '\0';
    if (from != nullptr) {
        const char *const to = strchr(from, '\n');
        const uint64_t length = to != nullptr ? (uint64_t)(to - from) : strlen(from);
        const uint64_t kept = length < capacity - 1u ? length : capacity - 1u;
        memcpy(out, from, kept);
        out[kept] = '\0';
    }
}

TEST(the_program_runs_itself_tired_the_same_each_time) {
    ps_temp_dir_t dir;
    REQUIRE(ps_temp_dir_make(&dir));
    char script[128];
    snprintf(script, sizeof(script), "%s/run.script", dir.path);
    REQUIRE(ps_run_script_write(t, script));
    test_process_t run;
    ps_program_run(t, &dir, script, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "0 bytes live");
    char first[256], second[256];
    ps_report_line(run.output, first, sizeof(first));
    float32_t health = 0.0f32, saturation = 0.0f32;
    uint32_t food = 0u;
    uint64_t hurt = 0u;
    {
        char *rest = strstr(first, "the player has ");
        REQUIRE(rest != nullptr);
        rest += strlen("the player has ");
        health = strtof(rest, nullptr);
        rest = strstr(rest, "health, ");
        REQUIRE(rest != nullptr);
        food = (uint32_t)strtoul(rest + strlen("health, "), nullptr, 10);
        rest = strstr(rest, "food and ");
        REQUIRE(rest != nullptr);
        saturation = strtof(rest + strlen("food and "), nullptr);
        rest = strstr(rest, "hurt ");
        REQUIRE(rest != nullptr);
        hurt = strtoull(rest + strlen("hurt "), nullptr, 10);
    }
    /* 45 s of it on dry land is far past what 5 saturation covers, so saturation has gone and food with it. Whether it falls off
     * anything on the way is the land's, so a hurt is not asked for; alive, it says so. */
    printf("  %s\n", first);
    CHECK(food < 20u);
    CHECK_NEAR(saturation, 0.0f32, 0.0f32);
    CHECK(health > 0.0f32);
    (void)hurt;
    ps_program_run(t, &dir, script, &run);
    REQUIRE(run.started != 0u);
    ps_report_line(run.output, second, sizeof(second));
    CHECK_STR_EQ(first, second);
    ps_temp_dir_remove(&dir);
}

TEST_MAIN()
