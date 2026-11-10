/* ctest run_mob_combat. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "display/index.h"
#include "entity/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/equipment/index.h"
#include "item/index.h"
#include "item/stack/index.h"
#include "job/index.h"
#include "mob/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/mobs/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <linux/input-event-codes.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* The player stands here, on a floor whose top is at z = 0, looking along +x unless told otherwise. */
#define MCO_X 8.5f64
#define MCO_Y 8.5f64
#define MCO_EYE 1.62f64
/* Enough to kill anything here at a stroke. */
#define MCO_HUGE 1000.0f32
#define MCO_NEAR 1.0e-4f64

enum : int32_t {
    MCO_FLOOR_MIN = -40,
    MCO_FLOOR_MAX = 55,
};

typedef struct mco_rig_t {
    test_world_t world;    /* 0 */
    entity_t *entity;      /* 24 */
    mob_t *mob;            /* 32 */
    player_t *player;      /* 40 */
    entity_store_t *store; /* 48 */
    int64_t now_ns;        /* 56 */
} mco_rig_t;
static_assert(sizeof(mco_rig_t) == 64);

static bool mco_begin(test_context_t *t, mco_rig_t *rig, uint64_t seed) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    rig->player = test_world_aligned(t, sizeof(player_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics)) || !CHECK(mob_init(rig->mob, nullptr))) {
        return false;
    }
    entity_open(rig->entity, seed);
    mob_open(rig->mob, rig->world.world, seed);
    player_init(rig->player, rig->world.physics);
    player_spawn(rig->player, MCO_X, MCO_Y, 0.0f64);
    rig->store = &rig->entity->store;
    rig->now_ns = 1000000000;
    /* A floor over every loaded column, for whatever walks or falls. */
    for (int32_t x = MCO_FLOOR_MIN; x <= MCO_FLOOR_MAX; x++) {
        for (int32_t y = MCO_FLOOR_MIN; y <= MCO_FLOOR_MAX; y++) {
            column_t *const column = world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y));
            if (column != nullptr) {
                CHECK_EQ_U64(column_block_set(column, &rig->world.world->section_pool, column_local_of(x),
                                              column_local_of(y), -1, BLOCK_ID_STONE),
                             COLUMN_STATUS_OK);
            }
        }
    }
    return true;
}

static void mco_end(mco_rig_t *rig) {
    player_deinit(rig->player);
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

static entity_handle_t mco_body(mco_rig_t *rig, mob_id_t kind, float64_t x, float64_t y, float64_t z) {
    const mob_def_t *const def = &mob_defs[kind];
    const entity_store_spawn_t body = {
        .position = {x, y, z},
        .half_width = def->half_width,
        .height = def->height,
        .health = def->health,
        .kind = (uint16_t)kind,
    };
    return entity_store_spawn(rig->store, &body);
}

static uint32_t mco_index(test_context_t *t, const mco_rig_t *rig, entity_handle_t handle) {
    uint32_t index = 0u;
    CHECK(entity_store_index_of(rig->store, handle, &index));
    return index;
}

static player_combat_weapon_t mco_weapon(uint16_t item) {
    const item_stack_t stack = item_stack_make(item, 1u);
    return (player_combat_weapon_t){
        .damage = equipment_attack_damage(&stack),
        .speed = player_combat_attack_speed(item),
        .item = item,
        ._pad0 = {0},
    };
}

/* What main sends each frame: looking this way, and the left button so. */
static void mco_send(mco_rig_t *rig, float32_t yaw, float32_t pitch, bool attack, bool sprint) {
    rig->now_ns += 50000000;
    const player_intent_t intent = {.yaw = yaw, .sprint = sprint ? 1u : 0u};
    player_send(rig->player, &intent, rig->now_ns);
    player_look_send(rig->player, yaw, pitch);
    player_hands_send(rig->player, attack, false);
}

/* What player_interact_tick would have taken, in a test of the hand alone: the next tick sees only what is sent for it. */
static void mco_latch_clear(mco_rig_t *rig) {
    atomic_store_explicit(&rig->player->hands_latch, 0u, memory_order_relaxed);
}

/* One tick of the hand alone, looking along +x. True if it swung. */
static bool mco_hand(mco_rig_t *rig, const player_combat_weapon_t *weapon, bool attack, player_combat_swing_t *swing) {
    mco_send(rig, 0.0f32, 0.0f32, attack, false);
    const bool swung = player_attack_tick(rig->player, rig->store, rig->world.map, weapon, rig->now_ns, swing);
    mco_latch_clear(rig);
    return swung;
}

/* Ticks of the hand with the button up, so that a swing comes back. */
static void mco_rest(mco_rig_t *rig, const player_combat_weapon_t *weapon, uint32_t ticks) {
    player_combat_swing_t swing;
    for (uint32_t i = 0u; i < ticks; i++) {
        (void)mco_hand(rig, weapon, false, &swing);
    }
}

/* A body standing on the ground, with its flags as ticks leave them. Two: on its first it has no speed yet, so it meets nothing. */
static entity_handle_t mco_settled(mco_rig_t *rig, mob_id_t kind, float64_t x, float64_t y) {
    const entity_handle_t handle = mco_body(rig, kind, x, y, 0.0f64);
    entity_store_tick(rig->store, rig->world.physics, rig->world.map);
    entity_store_tick(rig->store, rig->world.physics, rig->world.map);
    return handle;
}

static void mco_minds(mco_rig_t *rig, bool player_alive) {
    const player_movement_t *const movement = &rig->player->movement;
    mob_tick(rig->mob);
    const mob_zombie_view_t zombie_view = {
        .position = {movement->position[0], movement->position[1], movement->position[2]},
        .eye = movement->eye_height,
        .half_width = PLAYER_MOVEMENT_WIDTH * 0.5f64,
        .height = PLAYER_MOVEMENT_HEIGHT_STANDING,
        .alive = player_alive ? 1u : 0u,
    };
    (void)mob_minds_tick(rig->mob, rig->entity, &zombie_view);
    const mob_skeleton_view_t skeleton_view = {
        .position = {movement->position[0], movement->position[1], movement->position[2]},
        .eye_height = movement->eye_height,
        .height = PLAYER_MOVEMENT_HEIGHT_STANDING,
        .alive = player_alive ? 1u : 0u,
    };
    mob_skeleton_tick(&rig->mob->skeleton, &rig->mob->path, rig->entity, rig->world.map, &skeleton_view);
    const mob_creeper_scene_t scene = {
        .borrowed_entity = rig->entity,
        .borrowed_path = &rig->mob->path,
        .borrowed_edit = &rig->world.world->edit,
        .borrowed_movement = player_alive ? &rig->player->movement : nullptr,
        .borrowed_survival = player_alive ? &rig->player->survival : nullptr,
    };
    mob_creeper_tick(&rig->mob->creeper, &scene);
}

/* The tick's second half, in the game's order. */
static void mco_bodies(mco_rig_t *rig) {
    entity_tick(rig->entity, rig->world.map);
    mob_combat_tick(&rig->mob->combat, rig->store);
}

static mob_combat_blow_t mco_blow(float32_t damage, float64_t from_x) {
    return (mob_combat_blow_t){
        .from = {from_x, MCO_Y},
        .facing = {1.0f64, 0.0f64},
        .damage = damage,
        .knockback = 0.0f32,
    };
}

/* ---- What a swing is -------------------------------------------------------------------------------------------------- */

TEST(every_tool_swings_at_its_published_speed) {
    typedef struct row_t {
        float32_t speed;  /* 0 */
        uint16_t item;    /* 4 */
        uint8_t _pad0[2]; /* 6 */
    } row_t;
    static_assert(sizeof(row_t) == 8);
    const row_t rows[] = {
        {1.6f32, ITEM_ID_WOOD_SWORD, {0}},      {1.6f32, ITEM_ID_GOLD_SWORD, {0}},
        {1.6f32, ITEM_ID_STONE_SWORD, {0}},     {1.6f32, ITEM_ID_IRON_SWORD, {0}},
        {1.6f32, ITEM_ID_DIAMOND_SWORD, {0}},   {1.0f32, ITEM_ID_WOOD_SHOVEL, {0}},
        {1.0f32, ITEM_ID_DIAMOND_SHOVEL, {0}},  {1.2f32, ITEM_ID_WOOD_PICKAXE, {0}},
        {1.2f32, ITEM_ID_DIAMOND_PICKAXE, {0}}, {0.8f32, ITEM_ID_WOOD_AXE, {0}},
        {0.8f32, ITEM_ID_STONE_AXE, {0}},       {0.9f32, ITEM_ID_IRON_AXE, {0}},
        {1.0f32, ITEM_ID_GOLD_AXE, {0}},        {1.0f32, ITEM_ID_DIAMOND_AXE, {0}},
        {1.0f32, ITEM_ID_WOOD_HOE, {0}},        {1.0f32, ITEM_ID_GOLD_HOE, {0}},
        {2.0f32, ITEM_ID_STONE_HOE, {0}},       {3.0f32, ITEM_ID_IRON_HOE, {0}},
        {4.0f32, ITEM_ID_DIAMOND_HOE, {0}},     {4.0f32, ITEM_ID_NONE, {0}},
        {4.0f32, ITEM_ID_STICK, {0}},
    };
    for (uint32_t i = 0u; i < sizeof(rows) / sizeof(rows[0]); i++) {
        CHECK(player_combat_attack_speed(rows[i].item) == rows[i].speed);
    }
    /* Whatever is in the tables, however many more come. */
    for (uint32_t item = 0u; item < ITEM_COUNT; item++) {
        CHECK(player_combat_attack_speed((uint16_t)item) > 0.0f32);
    }
}

TEST(a_swing_comes_back_as_published) {
    /* A sword: 20 / 1.6 = 12.5 ticks. */
    CHECK_NEAR(player_combat_strength(0u, 1.6f32), 0.04f32, 1.0e-5f32);
    CHECK_NEAR(player_combat_strength(6u, 1.6f32), 0.52f32, 1.0e-5f32);
    CHECK(player_combat_strength(11u, 1.6f32) < 1.0f32);
    CHECK(player_combat_strength(12u, 1.6f32) == 1.0f32);
    CHECK(player_combat_strength(60000u, 1.6f32) == 1.0f32);
    /* A hand: 5 ticks. */
    CHECK(player_combat_strength(3u, 4.0f32) < 1.0f32);
    CHECK(player_combat_strength(5u, 4.0f32) == 1.0f32);
}

/* ---- What it aims at ------------------------------------------------------------------------------------------------- */

TEST(it_aims_at_the_nearest_body_in_reach_and_in_front_of_any_block) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 1u));
    const float64_t eye[COLLISION_AXES] = {MCO_X, MCO_Y, MCO_EYE};
    const float64_t along_x[COLLISION_AXES] = {1.0f64, 0.0f64, 0.0f64};
    player_combat_aim_t aim;

    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);
    CHECK_EQ_U64(aim.blocked, 0u);

    /* A zombie's half width is 0.3, so its near face is 1.7 from an eye 2 from its middle. */
    const entity_handle_t far = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.8f64, MCO_Y, 0.0f64);
    const entity_handle_t near = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, near);
    CHECK_NEAR(aim.distance, 1.7f64, 1.0e-6f64);
    (void)entity_store_remove(rig.store, near);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, far);
    CHECK_NEAR(aim.distance, 2.5f64, 1.0e-6f64);

    /* Reach is 3, to its near face. */
    (void)entity_store_remove(rig.store, far);
    const entity_handle_t in_reach = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 3.29f64, MCO_Y, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, in_reach);
    (void)entity_store_remove(rig.store, in_reach);
    (void)mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 3.31f64, MCO_Y, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);
    entity_store_remove(rig.store, rig.store->borrowed_handle[0]);

    /* To one side, and over its head. */
    const entity_handle_t beside = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y + 0.31f64, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);
    (void)entity_store_remove(rig.store, beside);
    const float64_t high_eye[COLLISION_AXES] = {MCO_X, MCO_Y, 2.0f64};
    const entity_handle_t under = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, high_eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);

    /* Down at 45 degrees: into the near face 0.7 along, which is 0.92 up it. */
    const float64_t down[COLLISION_AXES] = {sqrt(0.5f64), 0.0f64, -sqrt(0.5f64)};
    const entity_handle_t short_of = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 1.0f64, MCO_Y, 0.0f64);
    (void)entity_store_remove(rig.store, under);
    player_combat_aim(rig.world.map, rig.store, eye, down, &aim);
    CHECK_EQ_U64(aim.handle, short_of);
    CHECK_NEAR(aim.distance, 0.7f64 / sqrt(0.5f64), 1.0e-6f64);

    /* An eye inside a box hits it at once. */
    (void)entity_store_remove(rig.store, short_of);
    const entity_handle_t around = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X, MCO_Y, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, around);
    CHECK_NEAR(aim.distance, 0.0f64, 1.0e-9f64);
    (void)entity_store_remove(rig.store, around);
    mco_end(&rig);
}

TEST(a_block_shields_a_body_and_a_body_shields_a_block) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 2u));
    const float64_t eye[COLLISION_AXES] = {MCO_X, MCO_Y, MCO_EYE};
    const float64_t along_x[COLLISION_AXES] = {1.0f64, 0.0f64, 0.0f64};
    player_combat_aim_t aim;
    test_world_fill(t, &rig.world, 10, 8, 1, 10, 8, 1, BLOCK_ID_STONE);
    /* The wall's face is 1.5 off. */
    const entity_handle_t before = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 1.0f64, MCO_Y, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, before);
    CHECK_EQ_U64(aim.blocked, 1u);
    (void)entity_store_remove(rig.store, before);
    (void)mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.5f64, MCO_Y, 0.0f64);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);
    CHECK_EQ_U64(aim.blocked, 1u);
    /* Tall grass can be aimed at, so it stands in the way. */
    test_world_fill(t, &rig.world, 10, 8, 1, 10, 8, 1, BLOCK_ID_AIR);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK(aim.handle != ENTITY_HANDLE_NONE);
    test_world_fill(t, &rig.world, 9, 8, 1, 9, 8, 1, BLOCK_ID_TALL_GRASS);
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);
    CHECK_EQ_U64(aim.blocked, 1u);
    /* A column not loaded stops the ray, and says so. */
    const float64_t edge[COLLISION_AXES] = {63.9f64, MCO_Y, MCO_EYE};
    player_combat_aim(rig.world.map, rig.store, edge, along_x, &aim);
    CHECK_EQ_U64(aim.blocked, 1u);
    mco_end(&rig);
}

TEST(a_dead_body_and_one_waiting_for_its_column_are_not_there) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 3u));
    const float64_t eye[COLLISION_AXES] = {MCO_X, MCO_Y, MCO_EYE};
    const float64_t along_x[COLLISION_AXES] = {1.0f64, 0.0f64, 0.0f64};
    player_combat_aim_t aim;
    const entity_handle_t handle = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    const uint32_t index = mco_index(t, &rig, handle);
    rig.store->borrowed_health[index] = 0.0f32;
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);
    rig.store->borrowed_health[index] = 5.0f32;
    rig.store->borrowed_flags[index] = ENTITY_STORE_FLAG_WAITING;
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, ENTITY_HANDLE_NONE);
    rig.store->borrowed_flags[index] = 0u;
    player_combat_aim(rig.world.map, rig.store, eye, along_x, &aim);
    CHECK_EQ_U64(aim.handle, handle);
    mco_end(&rig);
}

/* ---- What the button does --------------------------------------------------------------------------------------------- */

TEST(a_press_swings_once_and_a_hold_does_not_swing_again) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 4u));
    const entity_handle_t handle = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    const player_combat_weapon_t sword = mco_weapon(ITEM_ID_DIAMOND_SWORD);
    player_combat_swing_t swing;
    /* Its first tick with this in hand starts it over, so a whole swing's rest and one. */
    mco_rest(&rig, &sword, 13u);
    REQUIRE(mco_hand(&rig, &sword, true, &swing));
    CHECK_EQ_U64(swing.handle, handle);
    CHECK(swing.damage == 7.0f32);
    CHECK(swing.strength == 1.0f32);
    CHECK_EQ_U64(swing.critical, 0u);
    CHECK(swing.knockback == 0.0f32);
    CHECK_NEAR(swing.facing[0], 1.0f64, 1.0e-9f64);
    CHECK_NEAR(swing.facing[1], 0.0f64, 1.0e-9f64);
    CHECK_EQ_U64(rig.player->combat.aiming_body, 1u);
    for (uint32_t i = 0u; i < 40u; i++) {
        CHECK(!mco_hand(&rig, &sword, true, &swing));
        CHECK_EQ_U64(rig.player->combat.aiming_body, 1u);
    }
    CHECK(!mco_hand(&rig, &sword, false, &swing));
    CHECK_EQ_U64(rig.player->combat.aiming_body, 0u);
    CHECK(mco_hand(&rig, &sword, true, &swing));
    mco_end(&rig);
}

TEST(a_click_between_two_ticks_still_counts_and_nobody_at_the_controls_does_nothing) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 5u));
    (void)mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    const player_combat_weapon_t hand = mco_weapon(ITEM_ID_NONE);
    player_combat_swing_t swing;
    mco_rest(&rig, &hand, 10u);
    rig.now_ns += 50000000;
    const player_intent_t intent = {.yaw = 0.0f32};
    player_send(rig.player, &intent, rig.now_ns);
    player_look_send(rig.player, 0.0f32, 0.0f32);
    player_hands_send(rig.player, true, false);
    player_hands_send(rig.player, false, false);
    CHECK(player_attack_tick(rig.player, rig.store, rig.world.map, &hand, rig.now_ns, &swing));
    mco_latch_clear(&rig);
    mco_rest(&rig, &hand, 10u);
    /* The button is down, and what said so is too old to believe. */
    player_hands_send(rig.player, true, false);
    CHECK(!player_attack_tick(rig.player, rig.store, rig.world.map, &hand,
                              rig.now_ns + (int64_t)PLAYER_INTENT_STALE_NS + 1, &swing));
    CHECK_EQ_U64(rig.player->combat.aiming_body, 0u);
    mco_end(&rig);
}

TEST(a_body_held_down_or_a_screen_up_does_not_hit_and_a_press_that_got_it_up_is_not_a_new_press) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 6u));
    (void)mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    const player_combat_weapon_t hand = mco_weapon(ITEM_ID_NONE);
    player_combat_swing_t swing;
    mco_rest(&rig, &hand, 10u);
    rig.player->survival.state = PLAYER_SURVIVAL_DEAD;
    CHECK(!mco_hand(&rig, &hand, true, &swing));
    CHECK_EQ_U64(rig.player->combat.aiming_body, 0u);
    rig.player->survival.state = PLAYER_SURVIVAL_ALIVE;
    CHECK(!mco_hand(&rig, &hand, true, &swing));
    CHECK_EQ_U64(rig.player->combat.aiming_body, 1u);
    CHECK(!mco_hand(&rig, &hand, false, &swing));
    rig.player->hands_held_off = 1u;
    CHECK(!mco_hand(&rig, &hand, true, &swing));
    CHECK_EQ_U64(rig.player->combat.aiming_body, 0u);
    rig.player->hands_held_off = 0u;
    CHECK(mco_hand(&rig, &hand, true, &swing));
    mco_end(&rig);
}

TEST(a_weak_swing_does_what_the_published_sum_says_and_a_miss_or_a_change_of_hand_costs_the_swing) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 7u));
    const entity_handle_t handle = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    const player_combat_weapon_t sword = mco_weapon(ITEM_ID_DIAMOND_SWORD);
    player_combat_swing_t swing;
    mco_rest(&rig, &sword, 30u);
    REQUIRE(mco_hand(&rig, &sword, true, &swing));
    /* Six rests and the press itself. */
    mco_rest(&rig, &sword, 6u);
    REQUIRE(mco_hand(&rig, &sword, true, &swing));
    const float32_t strength = (7.5f32) / 12.5f32;
    CHECK_NEAR(swing.strength, strength, 1.0e-5f32);
    CHECK_NEAR(swing.damage, 7.0f32 * (0.2f32 + 0.8f32 * strength * strength), 1.0e-4f32);
    CHECK_EQ_U64(swing.critical, 0u);
    CHECK(!mco_hand(&rig, &sword, false, &swing));

    /* Now at the air: the body is gone, and so is what the rest had got back. */
    uint32_t index = mco_index(t, &rig, handle);
    const float64_t held_at = rig.store->borrowed_position[0][index];
    rig.store->borrowed_position[0][index] = held_at + 20.0f64;
    mco_rest(&rig, &sword, 30u);
    CHECK(!mco_hand(&rig, &sword, true, &swing));
    CHECK_EQ_U64(rig.player->combat.since_swing, 0u);
    CHECK(!mco_hand(&rig, &sword, false, &swing));

    /* At a block it is mining's, and costs nothing. */
    test_world_fill(t, &rig.world, 10, 8, 1, 10, 8, 1, BLOCK_ID_DIRT);
    mco_rest(&rig, &sword, 30u);
    const uint16_t rested = rig.player->combat.since_swing;
    CHECK(!mco_hand(&rig, &sword, true, &swing));
    CHECK_EQ_U64(rig.player->combat.since_swing, rested + 1u);
    CHECK(!mco_hand(&rig, &sword, false, &swing));
    test_world_fill(t, &rig.world, 10, 8, 1, 10, 8, 1, BLOCK_ID_AIR);

    /* Another thing in the hand starts over. */
    const player_combat_weapon_t axe = mco_weapon(ITEM_ID_DIAMOND_AXE);
    CHECK(!mco_hand(&rig, &axe, false, &swing));
    CHECK_EQ_U64(rig.player->combat.since_swing, 0u);
    rig.store->borrowed_position[0][index] = held_at;
    REQUIRE(mco_hand(&rig, &axe, true, &swing));
    CHECK_NEAR(swing.strength, 1.5f32 / 20.0f32, 1.0e-5f32);
    mco_end(&rig);
}

TEST(a_critical_hit_wants_a_full_swing_a_fall_and_nothing_to_hold_on_to) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 8u));
    (void)mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    const player_combat_weapon_t sword = mco_weapon(ITEM_ID_DIAMOND_SWORD);
    player_movement_t *const movement = &rig.player->movement;
    player_combat_swing_t swing;
    mco_rest(&rig, &sword, 13u);
    movement->on_ground = 0u;
    movement->mode = PLAYER_MOVEMENT_MODE_AIR;
    movement->fall_distance = 0.5f64;
    REQUIRE(mco_hand(&rig, &sword, true, &swing));
    CHECK_EQ_U64(swing.critical, 1u);
    CHECK(swing.damage == 10.5f32);
    CHECK(!mco_hand(&rig, &sword, false, &swing));
    /* Too soon after the last: it is a weak swing, and no more. */
    REQUIRE(mco_hand(&rig, &sword, true, &swing));
    CHECK_EQ_U64(swing.critical, 0u);
    CHECK(!mco_hand(&rig, &sword, false, &swing));

    typedef struct case_t {
        float64_t fall;   /* 0 */
        uint8_t ground;   /* 8 */
        uint8_t mode;     /* 9 */
        uint8_t sprint;   /* 10 */
        uint8_t _pad0[5]; /* 11 */
    } case_t;
    static_assert(sizeof(case_t) == 16);
    const case_t none[] = {
        {0.0f64, 0u, PLAYER_MOVEMENT_MODE_AIR, 0u, {0}},    /* rising, or at the top of a jump */
        {0.5f64, 1u, PLAYER_MOVEMENT_MODE_GROUND, 0u, {0}}, /* landed */
        {0.5f64, 0u, PLAYER_MOVEMENT_MODE_CLIMB, 0u, {0}},  {0.5f64, 0u, PLAYER_MOVEMENT_MODE_WATER, 0u, {0}},
        {0.5f64, 0u, PLAYER_MOVEMENT_MODE_AIR, 1u, {0}},
    };
    for (uint32_t i = 0u; i < sizeof(none) / sizeof(none[0]); i++) {
        mco_rest(&rig, &sword, 13u);
        movement->fall_distance = none[i].fall;
        movement->on_ground = none[i].ground;
        movement->mode = none[i].mode;
        movement->sprinting = none[i].sprint;
        REQUIRE(mco_hand(&rig, &sword, true, &swing));
        CHECK_EQ_U64(swing.critical, 0u);
        CHECK(swing.damage == 7.0f32);
        CHECK(!mco_hand(&rig, &sword, false, &swing));
    }
    mco_end(&rig);
}

TEST(a_full_swing_at_a_sprint_pushes_more_and_stops_the_sprint) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 9u));
    (void)mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
    const player_combat_weapon_t sword = mco_weapon(ITEM_ID_DIAMOND_SWORD);
    player_movement_t *const movement = &rig.player->movement;
    player_combat_swing_t swing;
    mco_rest(&rig, &sword, 13u);
    movement->sprinting = 1u;
    movement->velocity[0] = 0.2f64;
    movement->velocity[1] = -0.1f64;
    movement->velocity[2] = 0.3f64;
    REQUIRE(mco_hand(&rig, &sword, true, &swing));
    CHECK(swing.knockback == 0.5f32);
    const float32_t exhaustion = rig.player->survival.exhaustion;
    player_attack_landed(rig.player, &swing);
    CHECK_EQ_U64(movement->sprinting, 0u);
    CHECK_NEAR(movement->velocity[0], 0.12f64, 1.0e-12f64);
    CHECK_NEAR(movement->velocity[1], -0.06f64, 1.0e-12f64);
    CHECK_NEAR(movement->velocity[2], 0.3f64, 1.0e-12f64);
    CHECK_NEAR(rig.player->survival.exhaustion - exhaustion, 0.1f32, 1.0e-6f32);
    CHECK(!mco_hand(&rig, &sword, false, &swing));

    /* A weak swing at a sprint is only a weak swing. */
    movement->sprinting = 1u;
    REQUIRE(mco_hand(&rig, &sword, true, &swing));
    CHECK(swing.knockback == 0.0f32);
    const float64_t speed = movement->velocity[0];
    player_attack_landed(rig.player, &swing);
    CHECK_EQ_U64(movement->sprinting, 1u);
    CHECK_NEAR(movement->velocity[0], speed, 1.0e-12f64);
    mco_end(&rig);
}

TEST(the_button_on_a_body_mines_nothing_behind_it_and_once_the_body_is_gone_it_mines) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 10u));
    test_world_fill(t, &rig.world, 11, 8, 1, 11, 8, 1, BLOCK_ID_DIRT);
    const entity_handle_t handle = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 1.5f64, MCO_Y, 0.0f64);
    const player_combat_weapon_t hand = mco_weapon(ITEM_ID_NONE);
    player_t *const player = rig.player;
    player_combat_swing_t swing;
    bool swung = false;
    for (uint32_t i = 0u; i < 60u; i++) {
        mco_send(&rig, 0.0f32, 0.0f32, true, false);
        swung = player_attack_tick(player, rig.store, rig.world.map, &hand, rig.now_ns, &swing) || swung;
        player_interact_tick(player, &rig.world.world->edit, rig.now_ns);
        player_tick(player, rig.world.map, rig.now_ns);
        CHECK_EQ_U64(player->interaction.mining, 0u);
    }
    CHECK(swung);
    CHECK_EQ_U64(player->interaction.mined_count, 0u);
    (void)entity_store_remove(rig.store, handle);
    /* The same button, still down. */
    for (uint32_t i = 0u; i < 60u && player->interaction.mined_count == 0u; i++) {
        mco_send(&rig, 0.0f32, 0.0f32, true, false);
        CHECK(!player_attack_tick(player, rig.store, rig.world.map, &hand, rig.now_ns, &swing));
        player_interact_tick(player, &rig.world.world->edit, rig.now_ns);
        player_tick(player, rig.world.map, rig.now_ns);
    }
    CHECK_EQ_U64(player->interaction.mined_count, 1u);
    mco_end(&rig);
}

TEST(a_run_that_never_asks_about_bodies_has_hands_that_only_mine) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 11u));
    test_world_fill(t, &rig.world, 11, 8, 1, 11, 8, 1, BLOCK_ID_DIRT);
    (void)mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 1.5f64, MCO_Y, 0.0f64);
    player_t *const player = rig.player;
    for (uint32_t i = 0u; i < 60u; i++) {
        mco_send(&rig, 0.0f32, 0.0f32, true, false);
        player_interact_tick(player, &rig.world.world->edit, rig.now_ns);
        player_tick(player, rig.world.map, rig.now_ns);
    }
    CHECK_EQ_U64(player->interaction.mined_count, 1u);
    mco_end(&rig);
}

/* ---- What a hit does ----------------------------------------------------------------------------------------------- */

TEST(a_hit_takes_its_size_and_the_next_ten_ticks_only_what_is_more) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 12u));
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y);
    const uint32_t index = mco_index(t, &rig, handle);
    mob_combat_t *const combat = &rig.mob->combat;
    float32_t *const health = &rig.store->borrowed_health[index];
    REQUIRE(*health == 20.0f32);

    mob_combat_outcome_t outcome = mob_combat_hurt(combat, rig.store, index, 5.0f32);
    CHECK(outcome.taken == 5.0f32);
    CHECK_EQ_U64(outcome.landed, 1u);
    CHECK_EQ_U64(outcome.fresh, 1u);
    CHECK_EQ_U64(outcome.killed, 0u);
    CHECK(*health == 15.0f32);

    /* No more than the last: nothing. */
    outcome = mob_combat_hurt(combat, rig.store, index, 5.0f32);
    CHECK_EQ_U64(outcome.landed, 0u);
    CHECK(outcome.taken == 0.0f32);
    outcome = mob_combat_hurt(combat, rig.store, index, 2.0f32);
    CHECK_EQ_U64(outcome.landed, 0u);
    CHECK(*health == 15.0f32);

    /* More: the difference, and the size to beat goes up, but the time does not start over. */
    outcome = mob_combat_hurt(combat, rig.store, index, 8.0f32);
    CHECK_EQ_U64(outcome.landed, 1u);
    CHECK_EQ_U64(outcome.fresh, 0u);
    CHECK(outcome.taken == 3.0f32);
    CHECK(*health == 12.0f32);
    outcome = mob_combat_hurt(combat, rig.store, index, 7.0f32);
    CHECK_EQ_U64(outcome.landed, 0u);

    /* Nine ticks are not enough, and the tenth is. */
    for (uint32_t i = 0u; i < MOB_COMBAT_INVULNERABLE_TICKS - 1u; i++) {
        mob_combat_tick(combat, rig.store);
    }
    outcome = mob_combat_hurt(combat, rig.store, index, 1.0f32);
    CHECK_EQ_U64(outcome.landed, 0u);
    mob_combat_tick(combat, rig.store);
    outcome = mob_combat_hurt(combat, rig.store, index, 1.0f32);
    CHECK_EQ_U64(outcome.fresh, 1u);
    CHECK(*health == 11.0f32);

    /* Nothing is nothing, and takes no time. */
    for (uint32_t i = 0u; i < MOB_COMBAT_INVULNERABLE_TICKS; i++) {
        mob_combat_tick(combat, rig.store);
    }
    outcome = mob_combat_hurt(combat, rig.store, index, 0.0f32);
    CHECK_EQ_U64(outcome.landed, 0u);
    outcome = mob_combat_hurt(combat, rig.store, index, -3.0f32);
    CHECK_EQ_U64(outcome.landed, 0u);
    CHECK(*health == 11.0f32);
    CHECK_EQ_U64(combat->landed_count, 3u);
    mco_end(&rig);
}

TEST(health_stops_at_nothing_and_a_dead_body_feels_no_more) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 13u));
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y);
    const uint32_t index = mco_index(t, &rig, handle);
    mob_combat_t *const combat = &rig.mob->combat;
    mob_combat_outcome_t outcome = mob_combat_hurt(combat, rig.store, index, 25.0f32);
    CHECK_EQ_U64(outcome.landed, 1u);
    CHECK_EQ_U64(outcome.killed, 1u);
    CHECK(outcome.taken == 20.0f32);
    CHECK(rig.store->borrowed_health[index] == 0.0f32);
    CHECK_EQ_U64(combat->killed_count, 1u);
    outcome = mob_combat_hurt(combat, rig.store, index, 90.0f32);
    CHECK_EQ_U64(outcome.landed, 0u);
    CHECK_EQ_U64(outcome.killed, 0u);
    CHECK_EQ_U64(combat->killed_count, 1u);
    CHECK_EQ_U64(combat->landed_count, 1u);
    mco_end(&rig);
}

TEST(a_push_is_the_published_sum) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 14u));
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y);
    const uint32_t index = mco_index(t, &rig, handle);
    mob_combat_t *const combat = &rig.mob->combat;
    float64_t *const vx = &rig.store->borrowed_velocity[0][index];
    float64_t *const vy = &rig.store->borrowed_velocity[1][index];
    float64_t *const vz = &rig.store->borrowed_velocity[2][index];

    /* On the ground: half of what it had, and up. */
    rig.store->borrowed_flags[index] = ENTITY_STORE_FLAG_ON_GROUND;
    *vx = 0.2f64;
    *vy = -0.1f64;
    *vz = -0.08f64;
    mob_combat_knockback(combat, rig.store, index, 0.4f64, 3.0f64, 4.0f64);
    CHECK_NEAR(*vx, 0.1f64 + 0.24f64, 1.0e-12f64);
    CHECK_NEAR(*vy, -0.05f64 + 0.32f64, 1.0e-12f64);
    CHECK_NEAR(*vz, 0.36f64, 1.0e-12f64);

    /* However much it is pushed, it goes up no more than 0.4. */
    *vz = 0.3f64;
    mob_combat_knockback(combat, rig.store, index, 0.4f64, 1.0f64, 0.0f64);
    CHECK_NEAR(*vz, 0.4f64, 1.0e-12f64);

    /* In the air it is not lifted. */
    rig.store->borrowed_flags[index] = 0u;
    *vx = 0.0f64;
    *vy = 0.0f64;
    *vz = -0.5f64;
    mob_combat_knockback(combat, rig.store, index, 0.4f64, 0.0f64, -2.0f64);
    CHECK_NEAR(*vx, 0.0f64, 1.0e-12f64);
    CHECK_NEAR(*vy, -0.4f64, 1.0e-12f64);
    CHECK_NEAR(*vz, -0.5f64, 1.0e-12f64);

    /* No strength is no push at all. */
    const uint64_t pushes = combat->knocked_count;
    mob_combat_knockback(combat, rig.store, index, 0.0f64, 1.0f64, 0.0f64);
    CHECK_NEAR(*vy, -0.4f64, 1.0e-12f64);
    CHECK_EQ_U64(combat->knocked_count, pushes);

    /* No direction only slows it. */
    mob_combat_knockback(combat, rig.store, index, 0.4f64, 0.0f64, 0.0f64);
    CHECK_NEAR(*vy, -0.2f64, 1.0e-12f64);
    mco_end(&rig);
}

TEST(a_blow_pushes_away_from_the_striker_and_only_a_whole_hit_does) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 15u));
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y);
    const uint32_t index = mco_index(t, &rig, handle);
    mob_combat_t *const combat = &rig.mob->combat;
    float64_t *const vx = &rig.store->borrowed_velocity[0][index];
    float64_t *const vz = &rig.store->borrowed_velocity[2][index];
    rig.store->borrowed_flags[index] = ENTITY_STORE_FLAG_ON_GROUND;
    *vx = 0.0f64;
    *vz = 0.0f64;
    mob_combat_outcome_t outcome;

    mob_combat_blow_t blow = mco_blow(7.0f32, MCO_X);
    REQUIRE(mob_combat_strike(combat, rig.store, handle, &blow, &outcome));
    CHECK_NEAR(*vx, 0.4f64, 1.0e-12f64);
    CHECK_NEAR(*vz, 0.4f64, 1.0e-12f64);
    CHECK(rig.store->borrowed_health[index] == 13.0f32);

    /* The same again at once: nothing, and no push. */
    *vx = 0.0f64;
    CHECK(!mob_combat_strike(combat, rig.store, handle, &blow, &outcome));
    CHECK_NEAR(*vx, 0.0f64, 1.0e-12f64);

    /* A harder one takes the difference, and does not push. */
    blow.damage = 8.0f32;
    REQUIRE(mob_combat_strike(combat, rig.store, handle, &blow, &outcome));
    CHECK_NEAR(*vx, 0.0f64, 1.0e-12f64);
    CHECK(rig.store->borrowed_health[index] == 12.0f32);

    /* With a sprint's push, that one goes all the same. */
    blow.damage = 9.0f32;
    blow.knockback = 0.5f32;
    REQUIRE(mob_combat_strike(combat, rig.store, handle, &blow, &outcome));
    CHECK_NEAR(*vx, 0.5f64, 1.0e-12f64);

    /* From the other side, when the time is up: a whole hit, and both pushes, one on the other, as published. */
    for (uint32_t i = 0u; i < MOB_COMBAT_INVULNERABLE_TICKS; i++) {
        mob_combat_tick(combat, rig.store);
    }
    *vx = 0.0f64;
    *vz = 0.0f64;
    rig.store->borrowed_flags[index] = ENTITY_STORE_FLAG_ON_GROUND;
    blow = mco_blow(1.0f32, MCO_X + 5.0f64);
    blow.facing[0] = -1.0f64;
    blow.knockback = 0.5f32;
    REQUIRE(mob_combat_strike(combat, rig.store, handle, &blow, &outcome));
    CHECK_NEAR(*vx, -0.2f64 - 0.5f64, 1.0e-12f64);
    CHECK_NEAR(*vz, 0.4f64, 1.0e-12f64);

    /* Exactly on top of it, the blow's own way. */
    for (uint32_t i = 0u; i < MOB_COMBAT_INVULNERABLE_TICKS; i++) {
        mob_combat_tick(combat, rig.store);
    }
    *vx = 0.0f64;
    blow = mco_blow(1.0f32, rig.store->borrowed_position[0][index]);
    blow.from[1] = rig.store->borrowed_position[1][index];
    blow.facing[0] = 0.0f64;
    blow.facing[1] = 1.0f64;
    float64_t *const vy = &rig.store->borrowed_velocity[1][index];
    *vy = 0.0f64;
    REQUIRE(mob_combat_strike(combat, rig.store, handle, &blow, &outcome));
    CHECK_NEAR(*vy, 0.4f64, 1.0e-12f64);

    /* One that has gone is not hit. */
    (void)entity_store_remove(rig.store, handle);
    outcome.landed = 9u;
    CHECK(!mob_combat_strike(combat, rig.store, handle, &blow, &outcome));
    CHECK_EQ_U64(outcome.landed, 0u);
    mco_end(&rig);
}

TEST(a_real_body_flies_and_lands_where_a_real_push_sends_it_and_a_wall_stops_it) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 16u));
    const float64_t start = MCO_X + 2.0f64;
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_ZOMBIE, start, MCO_Y);
    const uint32_t index = mco_index(t, &rig, handle);
    REQUIRE((rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_ON_GROUND) != 0u);
    mob_combat_outcome_t outcome;
    const mob_combat_blow_t blow = mco_blow(7.0f32, MCO_X);
    REQUIRE(mob_combat_strike(&rig.mob->combat, rig.store, handle, &blow, &outcome));
    float64_t top = 0.0f64;
    for (uint32_t i = 0u; i < 60u; i++) {
        mco_bodies(&rig);
        const float64_t z = rig.store->borrowed_position[2][index];
        top = z > top ? z : top;
    }
    /* Off the ground at 0.4 with gravity's 0.08 a tick, it goes up about 0.8 blocks, and in the air it keeps 0.91 of its speed. */
    CHECK(top > 0.6f64 && top < 1.2f64);
    const float64_t went = rig.store->borrowed_position[0][index] - start;
    CHECK(went > 1.5f64 && went < 3.5f64);
    CHECK_NEAR(rig.store->borrowed_position[1][index], MCO_Y, 1.0e-9f64);
    CHECK_NEAR(rig.store->borrowed_position[2][index], 0.0f64, 1.0e-9f64);
    CHECK((rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_ON_GROUND) != 0u);
    /* A landing that short costs it nothing more. */
    CHECK(rig.store->borrowed_health[index] == 13.0f32);

    /* A wall on its way. Its face is 1 off, and its side 0.3 before that. */
    const float64_t second = rig.store->borrowed_position[0][index];
    const int32_t wall = (int32_t)floor(second) + 2;
    test_world_fill(t, &rig.world, wall, 0, 0, wall, 16, 3, BLOCK_ID_STONE);
    REQUIRE(mob_combat_strike(&rig.mob->combat, rig.store, handle, &blow, &outcome));
    for (uint32_t i = 0u; i < 60u; i++) {
        mco_bodies(&rig);
    }
    CHECK(rig.store->borrowed_position[0][index] <= (float64_t)wall - 0.3f64 + 1.0e-6f64);
    mco_end(&rig);
}

TEST(a_chasing_zombie_is_knocked_back_and_comes_on_again) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 17u));
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y);
    uint32_t index = mco_index(t, &rig, handle);
    for (uint32_t i = 0u; i < 200u; i++) {
        mco_minds(&rig, true);
        mco_bodies(&rig);
        index = mco_index(t, &rig, handle);
        if (rig.mob->zombie.borrowed_bodies[entity_handle_slot(handle)].chasing != 0u &&
            mob_zombie_in_reach(
                rig.store, index,
                &(mob_zombie_view_t){.position = {MCO_X, MCO_Y, 0.0f64}, .half_width = 0.3f64, .height = 1.8f64})) {
            break;
        }
    }
    REQUIRE(rig.mob->zombie.borrowed_bodies[entity_handle_slot(handle)].chasing != 0u);
    const float64_t close = rig.store->borrowed_position[0][index];
    mob_combat_outcome_t outcome;
    const mob_combat_blow_t blow = mco_blow(7.0f32, MCO_X);
    REQUIRE(mob_combat_strike(&rig.mob->combat, rig.store, handle, &blow, &outcome));
    float64_t furthest = close;
    for (uint32_t i = 0u; i < 100u; i++) {
        mco_minds(&rig, true);
        mco_bodies(&rig);
        index = mco_index(t, &rig, handle);
        const float64_t x = rig.store->borrowed_position[0][index];
        furthest = x > furthest ? x : furthest;
    }
    CHECK(furthest > close + 1.0f64);
    CHECK(rig.store->borrowed_position[0][index] < furthest - 0.5f64);
    mco_end(&rig);
}

/* ---- A fall and lava -------------------------------------------------------------------------------------------------- */

TEST(a_fall_hurts_a_mob_by_the_players_sum_on_the_tick_it_lands) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 18u));
    typedef struct row_t {
        float64_t height; /* 0 */
        float32_t lost;   /* 8 */
        uint8_t _pad0[4]; /* 12 */
    } row_t;
    static_assert(sizeof(row_t) == 16);
    const row_t rows[] = {{1.0f64, 0.0f32, {0}},
                          {3.0f64, 0.0f32, {0}},
                          {4.0f64, 1.0f32, {0}},
                          {10.0f64, 7.0f32, {0}},
                          {22.0f64, 19.0f32, {0}}};
    for (uint32_t r = 0u; r < sizeof(rows) / sizeof(rows[0]); r++) {
        const entity_handle_t handle = mco_body(&rig, MOB_ID_CREEPER, MCO_X, MCO_Y + 2.0f64, rows[r].height);
        const uint32_t index = mco_index(t, &rig, handle);
        for (uint32_t i = 0u; i < 100u && (rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_ON_GROUND) == 0u; i++) {
            CHECK(rig.store->borrowed_health[index] == 20.0f32);
            mco_bodies(&rig);
        }
        CHECK((rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_ON_GROUND) != 0u);
        CHECK(rig.store->borrowed_health[index] == 20.0f32 - rows[r].lost);
        /* And only that tick. */
        for (uint32_t i = 0u; i < 20u; i++) {
            mco_bodies(&rig);
        }
        CHECK(rig.store->borrowed_health[index] == 20.0f32 - rows[r].lost);
        (void)entity_store_remove(rig.store, handle);
    }
    mco_end(&rig);
}

TEST(a_long_fall_kills_and_the_mind_takes_it_away_with_what_it_leaves) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 19u));
    const entity_handle_t handle = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X, MCO_Y + 4.0f64, 40.0f64);
    uint32_t index = 0u;
    for (uint32_t i = 0u; i < 200u && rig.mob->combat.killed_count == 0u; i++) {
        mco_bodies(&rig);
    }
    CHECK_EQ_U64(rig.mob->combat.killed_count, 1u);
    REQUIRE(entity_store_index_of(rig.store, handle, &index));
    CHECK(rig.store->borrowed_health[index] == 0.0f32);
    mco_minds(&rig, false);
    CHECK(!entity_store_index_of(rig.store, handle, &index));
    CHECK_EQ_U64(rig.mob->zombie.died_count, 1u);
    mco_end(&rig);
}

TEST(lava_hurts_every_ten_ticks_and_water_does_not) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 20u));
    test_world_fill(t, &rig.world, 8, 4, 0, 10, 6, 2, BLOCK_ID_LAVA);
    test_world_fill(t, &rig.world, 8, 12, 0, 10, 14, 2, BLOCK_ID_WATER);
    const entity_handle_t burnt = mco_body(&rig, MOB_ID_ZOMBIE, 9.5f64, 5.5f64, 0.0f64);
    const entity_handle_t wet = mco_body(&rig, MOB_ID_ZOMBIE, 9.5f64, 13.5f64, 0.0f64);
    const uint32_t burnt_index = mco_index(t, &rig, burnt);
    const uint32_t wet_index = mco_index(t, &rig, wet);
    uint32_t first = 0u, second = 0u;
    for (uint32_t i = 1u; i <= 40u && second == 0u; i++) {
        mco_bodies(&rig);
        const float32_t health = rig.store->borrowed_health[burnt_index];
        first = first == 0u && health == 16.0f32 ? i : first;
        second = health == 12.0f32 ? i : second;
    }
    REQUIRE(first != 0u);
    CHECK_EQ_U64(second, first + MOB_COMBAT_INVULNERABLE_TICKS);
    CHECK(rig.store->borrowed_health[wet_index] == 20.0f32);
    for (uint32_t i = 0u; i < 60u; i++) {
        mco_bodies(&rig);
    }
    /* Three more hits at most would finish it by then: nothing has stopped it at some health in between. */
    CHECK(rig.store->borrowed_health[burnt_index] == 0.0f32);
    CHECK(rig.store->borrowed_health[wet_index] == 20.0f32);
    CHECK_EQ_U64(rig.mob->combat.killed_count, 1u);
    mco_end(&rig);
}

TEST(a_body_waiting_for_its_column_is_not_hurt_and_no_time_passes_for_it) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 21u));
    const entity_handle_t handle = mco_body(&rig, MOB_ID_ZOMBIE, 1000.5f64, 1000.5f64, 0.0f64);
    const uint32_t index = mco_index(t, &rig, handle);
    mco_bodies(&rig);
    REQUIRE((rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_WAITING) != 0u);
    (void)mob_combat_hurt(&rig.mob->combat, rig.store, index, 5.0f32);
    rig.store->borrowed_landed_fall[index] = 50.0f64;
    for (uint32_t i = 0u; i < 30u; i++) {
        mob_combat_tick(&rig.mob->combat, rig.store);
    }
    CHECK(rig.store->borrowed_health[index] == 15.0f32);
    CHECK_EQ_U64(rig.mob->combat.borrowed_bodies[entity_handle_slot(handle)].invulnerable,
                 MOB_COMBAT_INVULNERABLE_TICKS);
    mco_end(&rig);
}

TEST(a_slot_used_again_starts_over) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 22u));
    const entity_handle_t first = mco_settled(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y);
    (void)mob_combat_hurt(&rig.mob->combat, rig.store, mco_index(t, &rig, first), 15.0f32);
    (void)entity_store_remove(rig.store, first);
    entity_handle_t again = ENTITY_HANDLE_NONE;
    for (uint32_t i = 0u; i <= ENTITY_STORE_CAPACITY; i++) {
        again = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y, 0.0f64);
        if (entity_handle_slot(again) == entity_handle_slot(first)) {
            break;
        }
        (void)entity_store_remove(rig.store, again);
    }
    REQUIRE(entity_handle_slot(again) == entity_handle_slot(first));
    REQUIRE(again != first);
    /* The body in this slot before was within its ten ticks, of 15. This one is not. */
    const mob_combat_outcome_t outcome =
        mob_combat_hurt(&rig.mob->combat, rig.store, mco_index(t, &rig, again), 2.0f32);
    CHECK_EQ_U64(outcome.fresh, 1u);
    CHECK(outcome.taken == 2.0f32);
    mco_end(&rig);
}

/* ---- Death, for each kind ------------------------------------------------------------------------------------------- */

/* Each kind, killed by a blow: gone on its mind's next tick, with its drops going where the books say. */
TEST(a_blow_kills_each_kind_and_its_own_mind_leaves_what_it_drops) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 23u));
    const uint64_t rounds = 200u;
    const mob_id_t kinds[] = {MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER};
    mob_combat_blow_t blow = mco_blow(MCO_HUGE, MCO_X);
    for (uint32_t k = 0u; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        const uint64_t on_ground = rig.entity->item_drops.dropped_count;
        for (uint64_t round = 0u; round < rounds; round++) {
            const entity_handle_t handle = mco_body(&rig, kinds[k], MCO_X + 2.0f64, MCO_Y, 0.0f64);
            mob_combat_outcome_t outcome;
            REQUIRE(mob_combat_strike(&rig.mob->combat, rig.store, handle, &blow, &outcome));
            CHECK_EQ_U64(outcome.killed, 1u);
            mco_minds(&rig, false);
            uint32_t index = 0u;
            CHECK(!entity_store_index_of(rig.store, handle, &index));
        }
        const uint64_t dropped = rig.entity->item_drops.dropped_count - on_ground;
        /* A zombie and a creeper have one row of 0 to 2, so one each on average. A skeleton has two such rows, so two each. */
        const uint64_t average = kinds[k] == MOB_ID_SKELETON ? 2u * rounds : rounds;
        CHECK(dropped > average * 3u / 4u && dropped < average * 5u / 4u + rounds / 10u);
    }
    CHECK_EQ_U64(rig.mob->zombie.died_count, rounds);
    CHECK_EQ_U64(rig.mob->skeleton.died_count, rounds);
    CHECK_EQ_U64(rig.mob->creeper.died_count, rounds);
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 0u);
    CHECK_EQ_U64(rig.mob->combat.killed_count, 3u * rounds);
    CHECK_EQ_U64(rig.store->count, 0u);
    mco_end(&rig);
}

TEST(a_creeper_killed_with_its_fuse_lit_does_not_burst) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 24u));
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_CREEPER, MCO_X + 2.0f64, MCO_Y);
    uint32_t index = 0u;
    const mob_creeper_body_t *const mind = &rig.mob->creeper.borrowed_bodies[entity_handle_slot(handle)];
    for (uint32_t i = 0u; i < 200u && mind->fuse < MOB_CREEPER_FUSE_TICKS / 2u; i++) {
        mco_minds(&rig, true);
        mco_bodies(&rig);
    }
    REQUIRE(entity_store_index_of(rig.store, handle, &index));
    REQUIRE(mind->fuse >= MOB_CREEPER_FUSE_TICKS / 2u);
    const uint64_t broken = rig.entity->explosion.broken_count;
    mob_combat_outcome_t outcome;
    const mob_combat_blow_t blow = mco_blow(MCO_HUGE, MCO_X);
    REQUIRE(mob_combat_strike(&rig.mob->combat, rig.store, handle, &blow, &outcome));
    mco_minds(&rig, true);
    CHECK(!entity_store_index_of(rig.store, handle, &index));
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 0u);
    CHECK_EQ_U64(rig.mob->creeper.died_count, 1u);
    CHECK_EQ_U64(rig.entity->explosion.broken_count, broken);
    CHECK(rig.player->survival.health == (float32_t)PLAYER_SURVIVAL_HEALTH_MAX);
    mco_end(&rig);
}

/* ---- The same bytes, no heap, and the cost -------------------------------------------------------------------------- */

/* A fight of a few ticks, of one seed, with what is left of it in out: every body's place, speed and health. */
static uint64_t mco_fight(test_context_t *t, uint64_t seed, float64_t *out, uint32_t out_count) {
    mco_rig_t rig;
    if (!mco_begin(t, &rig, seed)) {
        return 0u;
    }
    for (uint32_t i = 0u; i < 10u; i++) {
        (void)mco_body(&rig, i % 2u == 0u ? MOB_ID_ZOMBIE : MOB_ID_SKELETON, MCO_X + 2.0f64 + (float64_t)i * 0.3f64,
                       MCO_Y + 0.1f64 * (float64_t)i, 0.0f64);
    }
    const player_combat_weapon_t sword = mco_weapon(ITEM_ID_DIAMOND_SWORD);
    for (uint32_t tick = 0u; tick < 200u; tick++) {
        mco_send(&rig, 0.0f32, 0.0f32, tick % 13u == 0u, false);
        player_combat_swing_t swing;
        if (player_attack_tick(rig.player, rig.store, rig.world.map, &sword, rig.now_ns, &swing)) {
            const mob_combat_blow_t blow = {
                .from = {rig.player->movement.position[0], rig.player->movement.position[1]},
                .facing = {swing.facing[0], swing.facing[1]},
                .damage = swing.damage,
                .knockback = swing.knockback,
            };
            mob_combat_outcome_t outcome;
            if (mob_combat_strike(&rig.mob->combat, rig.store, swing.handle, &blow, &outcome)) {
                player_attack_landed(rig.player, &swing);
            }
        }
        mco_latch_clear(&rig);
        rig.player->combat.aiming_body = 0u;
        mco_minds(&rig, true);
        mco_bodies(&rig);
        player_tick(rig.player, rig.world.map, rig.now_ns);
    }
    memset(out, 0, out_count * sizeof(float64_t));
    for (uint32_t i = 0u; i < rig.store->count && 5u * i + 4u < out_count; i++) {
        out[5u * i + 0u] = rig.store->borrowed_position[0][i];
        out[5u * i + 1u] = rig.store->borrowed_position[1][i];
        out[5u * i + 2u] = rig.store->borrowed_position[2][i];
        out[5u * i + 3u] = rig.store->borrowed_velocity[0][i];
        out[5u * i + 4u] = (float64_t)rig.store->borrowed_health[i];
    }
    const uint64_t landed = rig.mob->combat.landed_count;
    mco_end(&rig);
    return landed;
}

TEST(the_same_fight_gives_the_same_bytes_and_a_different_seed_a_different_fight) {
    enum : uint32_t { MCO_RECORD = 64u };
    float64_t *const one = test_alloc(t, MCO_RECORD * sizeof(float64_t));
    float64_t *const two = test_alloc(t, MCO_RECORD * sizeof(float64_t));
    float64_t *const other = test_alloc(t, MCO_RECORD * sizeof(float64_t));
    const uint64_t landed = mco_fight(t, 31u, one, MCO_RECORD);
    CHECK(landed > 0u);
    CHECK_EQ_U64(mco_fight(t, 31u, two, MCO_RECORD), landed);
    CHECK_MEM_EQ(one, two, MCO_RECORD * sizeof(float64_t));
    (void)mco_fight(t, 32u, other, MCO_RECORD);
    CHECK(memcmp(one, other, MCO_RECORD * sizeof(float64_t)) != 0);
}

TEST(a_fight_of_the_plans_hundred_touches_no_heap_or_arena_and_stays_within_the_budget) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 33u));
    for (uint32_t i = 0u; i < 100u; i++) {
        const float64_t x = MCO_X + 2.0f64 + 2.0f64 * (float64_t)(i % 10u);
        const float64_t y = MCO_Y - 10.0f64 + 2.0f64 * (float64_t)(i / 10u);
        (void)mco_body(&rig, MOB_ID_ZOMBIE, x, y, 0.0f64);
    }
    const player_combat_weapon_t sword = mco_weapon(ITEM_ID_DIAMOND_SWORD);
    heap_counters_t before, after;
    heap_counters_read(&before);
    const uint64_t used_entity = rig.entity->arena_world.used, used_mob = rig.mob->arena_world.used;
    int64_t ours = 0;
    uint64_t swings = 0u;
    const entity_handle_t target = mco_body(&rig, MOB_ID_ZOMBIE, MCO_X + 1.0f64, MCO_Y + 0.05f64, 0.0f64);
    for (uint32_t tick = 0u; tick < 200u; tick++) {
        /* Always one to hit, however the last blow left it. */
        const uint32_t index = mco_index(t, &rig, target);
        rig.store->borrowed_position[0][index] = MCO_X + 1.0f64;
        rig.store->borrowed_position[1][index] = MCO_Y + 0.05f64;
        rig.store->borrowed_health[index] = 20.0f32;
        mco_send(&rig, 0.0f32, 0.0f32, tick % 2u == 0u, false);
        const int64_t started = time_now_ns();
        player_combat_swing_t swing;
        if (player_attack_tick(rig.player, rig.store, rig.world.map, &sword, rig.now_ns, &swing)) {
            mob_combat_blow_t blow = mco_blow(swing.damage, rig.player->movement.position[0]);
            mob_combat_outcome_t outcome;
            (void)mob_combat_strike(&rig.mob->combat, rig.store, swing.handle, &blow, &outcome);
            swings++;
        }
        mob_combat_tick(&rig.mob->combat, rig.store);
        ours += time_now_ns() - started;
        mco_latch_clear(&rig);
        entity_tick(rig.entity, rig.world.map);
    }
    heap_counters_read(&after);
    CHECK(swings > 50u);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(rig.entity->arena_world.used, used_entity);
    CHECK_EQ_U64(rig.mob->arena_world.used, used_mob);
    /* The phase's budget is 5 ms a tick for everything. This takes a small part of it, at the debug flags and with sanitizers too. */
    CHECK(ours / 200 < 500000);
    mco_end(&rig);
}

TEST(a_world_opened_again_has_nobody_hurt_and_what_it_takes_is_counted) {
    mco_rig_t rig;
    REQUIRE(mco_begin(t, &rig, 34u));
    const entity_handle_t handle = mco_settled(&rig, MOB_ID_ZOMBIE, MCO_X + 2.0f64, MCO_Y);
    (void)mob_combat_hurt(&rig.mob->combat, rig.store, mco_index(t, &rig, handle), 5.0f32);
    CHECK_EQ_U64(rig.mob->combat.landed_count, 1u);
    CHECK(rig.mob->arena_world.used <= (uint64_t)MOB_PATH_BYTES + (uint64_t)MOB_ZOMBIE_BYTES +
                                           (uint64_t)MOB_SKELETON_BYTES + (uint64_t)MOB_CREEPER_BYTES +
                                           (uint64_t)MOB_COMBAT_BYTES + (uint64_t)MOB_PASSIVE_BYTES);
    mob_close(rig.mob);
    CHECK(rig.mob->combat.borrowed_bodies == nullptr);
    CHECK_EQ_U64(rig.mob->combat.landed_count, 0u);
    mob_open(rig.mob, rig.world.world, 34u);
    REQUIRE(rig.mob->combat.borrowed_bodies != nullptr);
    CHECK_EQ_U64(rig.mob->combat.landed_count, 0u);
    CHECK_EQ_U64(rig.mob->combat.borrowed_bodies[entity_handle_slot(handle)].owner, ENTITY_HANDLE_NONE);
    mco_end(&rig);
}

/* ---- In a real game ---------------------------------------------------------------------------------------------------- */

typedef struct mco_game_rig_t {
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
    mob_t *mob;                  /* 96 */
} mco_game_rig_t;
static_assert(sizeof(mco_game_rig_t) == 104);

static bool mco_game_begin(test_context_t *t, mco_game_rig_t *rig) {
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
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(entity_init(rig->entity, rig->physics)) || !CHECK(item_init(rig->item)) ||
        !CHECK(mob_init(rig->mob, rig->job)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    game_entity_set(rig->game, rig->entity);
    game_mob_set(rig->game, rig->mob);
    game_item_set(rig->game, rig->item);
    return true;
}

static void mco_game_end(test_context_t *t, mco_game_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    mob_deinit(rig->mob);
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

typedef struct mco_script_t {
    display_headless_scene_t scene; /* 0 */
    display_script_state_t state;   /* 24 */
    uint32_t tick;                  /* 176 */
    uint8_t _pad0[4];               /* 180 */
} mco_script_t;
static_assert(sizeof(mco_script_t) == 184);

static void mco_script_button(mco_script_t *script, bool down) {
    const uint32_t code = BTN_LEFT;
    const uint64_t bit = 1ull << (code % 64u);
    script->state.down[code / 64u] =
        down ? script->state.down[code / 64u] | bit : script->state.down[code / 64u] & ~bit;
}

static bool mco_script_ticks(mco_script_t *script, uint32_t ticks) {
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

/* Puts a body right in front of the eye, as a chase does, however the last blow sent it. */
static void mco_game_in_front(const mco_game_rig_t *rig, uint32_t index, float64_t feet_z) {
    const float64_t *const feet = rig->player->movement.position;
    const float32_t yaw = rig->game->camera.yaw;
    entity_store_t *const store = &rig->entity->store;
    store->borrowed_position[0][index] = feet[0] + 1.5f64 * cos((float64_t)yaw);
    store->borrowed_position[1][index] = feet[1] + 1.5f64 * sin((float64_t)yaw);
    store->borrowed_position[2][index] = feet_z;
}

/* How many boxes of this mob's skin are in the fullest snapshot, and whether any is near a place. */
static uint32_t mco_drawn_parts(const mco_game_rig_t *rig, uint16_t mob, const float64_t near[3], bool *out_near) {
    const render_entities_t *const entities = rig->render->borrowed_entities;
    uint32_t most = 0u;
    *out_near = false;
    for (uint32_t s = 0u; s < RENDER_EXCHANGE_SLOTS; s++) {
        const render_entities_slot_t *const slot = &entities->slots[s];
        uint32_t parts = 0u;
        for (uint32_t i = 0u; i < slot->count; i++) {
            const render_entities_item_t *const item = &slot->borrowed_items[i];
            if (item->skin_size[0] == 0u || item->texture != entities->mob_textures[mob]) {
                continue;
            }
            parts++;
            const float64_t x = item->center[0] - near[0], y = item->center[1] - near[1], z = item->center[2] - near[2];
            *out_near = *out_near || sqrt(x * x + y * y + z * z) < 3.0f64;
        }
        most = parts > most ? parts : most;
    }
    return most;
}

/* A zombie put down on known ground, through all of the wiring in run/src/game/: it is in what the frames draw, part by part, it notices,
 * walks over and strikes, and the blow lands on the body. */
TEST(in_a_real_game_a_zombie_walks_up_is_drawn_and_strikes) {
    mco_game_rig_t rig;
    REQUIRE(mco_game_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = 5u, .width = 320u, .height = 180u, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    mco_script_t *const script = test_alloc(t, sizeof(mco_script_t));
    memset(script, 0, sizeof(*script));
    game_headless_scene(game, &script->scene);
    const int32_t pad_x = (int32_t)floor(rig.player->movement.position[0]);
    const int32_t pad_y = (int32_t)floor(rig.player->movement.position[1]);
    const int32_t pad_z = (int32_t)floor(rig.player->movement.position[2]);
    for (int32_t x = pad_x - 14; x <= pad_x + 14; x++) {
        for (int32_t y = pad_y - 14; y <= pad_y + 14; y++) {
            for (int32_t z = pad_z - 1; z <= pad_z + 8; z++) {
                const world_edit_status_t status =
                    world_edit_block_set(&rig.world->edit, x, y, z, z < pad_z ? BLOCK_ID_STONE : BLOCK_ID_AIR);
                REQUIRE(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
        }
    }
    REQUIRE(mco_script_ticks(script, 20u));
    entity_store_t *const store = &rig.entity->store;
    while (store->count > 0u) {
        (void)entity_store_remove(store, store->borrowed_handle[store->count - 1u]);
    }
    const float64_t *const feet = rig.player->movement.position;
    const mob_def_t *const def = &mob_defs[MOB_ID_ZOMBIE];
    const entity_store_spawn_t body = {
        .position = {feet[0] + 9.0f64, feet[1], feet[2]},
        .half_width = def->half_width,
        .height = def->height,
        .health = def->health,
        .kind = (uint16_t)MOB_ID_ZOMBIE,
    };
    const entity_handle_t handle = entity_store_spawn(store, &body);
    REQUIRE(handle != ENTITY_HANDLE_NONE);
    REQUIRE(mco_script_ticks(script, 3u));
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(store, handle, &index));
    const float64_t place[3] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                store->borrowed_position[2][index]};
    bool near = false;
    const uint32_t parts = mco_drawn_parts(&rig, (uint16_t)MOB_ID_ZOMBIE, place, &near);
    CHECK(parts >= MOB_ZOMBIE_PART_COUNT);
    CHECK_EQ_U64(parts % MOB_ZOMBIE_PART_COUNT, 0u);
    CHECK(near);

    const uint64_t hurts = rig.player->survival.hurt_count;
    const float32_t health = rig.player->survival.health;
    const uint64_t struck = rig.mob->zombie.struck_count;
    uint32_t waited = 0u;
    for (; waited < 400u && rig.mob->zombie.struck_count == struck; waited++) {
        REQUIRE(mco_script_ticks(script, 1u));
    }
    printf("  struck after %" PRIu32 " ticks\n", waited);
    CHECK(rig.mob->zombie.struck_count > struck);
    /* It walked 8 blocks or so, at its speed, and no faster. */
    CHECK(waited >= 8.0f32 / def->speed * 0.8f32);
    REQUIRE(mco_script_ticks(script, 1u));
    CHECK(rig.player->survival.hurt_count > hurts);
    CHECK(rig.player->survival.health < health);
    REQUIRE(entity_store_index_of(store, handle, &index));
    CHECK(mob_zombie_in_reach(store, index,
                              &(mob_zombie_view_t){.position = {feet[0], feet[1], feet[2]},
                                                   .eye = rig.player->movement.eye_height,
                                                   .half_width = 0.3f64,
                                                   .height = 1.8f64,
                                                   .alive = 1u,
                                                   ._pad0 = {0}}));
    game_close(game);
    mco_game_end(t, &rig);
}

/* All of the wiring in run/src/game/, on its own tick: the script's button is a blow with the sword in hand, which wears it and tires the
 * body and pushes what it hit, the block behind it is left alone, and the third blow kills, and what it leaves is in the world. */
TEST(in_a_real_game_a_click_hits_with_the_sword_in_hand_and_three_kill_a_zombie) {
    mco_game_rig_t rig;
    REQUIRE(mco_game_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = 5u, .width = 320u, .height = 180u, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    mco_script_t *const script = test_alloc(t, sizeof(mco_script_t));
    memset(script, 0, sizeof(*script));
    game_headless_scene(game, &script->scene);
    const int32_t pad_x = (int32_t)floor(rig.player->movement.position[0]);
    const int32_t pad_y = (int32_t)floor(rig.player->movement.position[1]);
    const int32_t pad_z = (int32_t)floor(rig.player->movement.position[2]);
    for (int32_t x = pad_x - 14; x <= pad_x + 14; x++) {
        for (int32_t y = pad_y - 14; y <= pad_y + 14; y++) {
            for (int32_t z = pad_z - 1; z <= pad_z + 8; z++) {
                const world_edit_status_t status =
                    world_edit_block_set(&rig.world->edit, x, y, z, z < pad_z ? BLOCK_ID_STONE : BLOCK_ID_AIR);
                REQUIRE(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
        }
    }
    /* Looking level, along whatever way the world started it. */
    game->camera.pitch = 0.0f32;
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, inventory->selected) = item_stack_make(ITEM_ID_DIAMOND_SWORD, 1u);
    REQUIRE(mco_script_ticks(script, 20u));
    /* On top of the eye's line, and nobody else about. */
    entity_store_t *const store = &rig.entity->store;
    while (store->count > 0u) {
        (void)entity_store_remove(store, store->borrowed_handle[store->count - 1u]);
    }
    const float64_t *const feet = rig.player->movement.position;
    const mob_def_t *const def = &mob_defs[MOB_ID_ZOMBIE];
    const entity_store_spawn_t body = {
        .position = {feet[0], feet[1], feet[2]},
        .half_width = def->half_width,
        .height = def->height,
        .health = def->health,
        .kind = (uint16_t)MOB_ID_ZOMBIE,
    };
    const entity_handle_t handle = entity_store_spawn(store, &body);
    REQUIRE(handle != ENTITY_HANDLE_NONE);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(store, handle, &index));
    mco_game_in_front(&rig, index, feet[2]);
    /* Two ticks standing, as anything has that was not put there a moment ago, and back where it should be. */
    REQUIRE(mco_script_ticks(script, 2u));
    REQUIRE(entity_store_index_of(store, handle, &index));
    REQUIRE((store->borrowed_flags[index] & ENTITY_STORE_FLAG_ON_GROUND) != 0u);
    mco_game_in_front(&rig, index, feet[2]);
    /* A wall just behind it, in the line of the swing, which mining would be at. */
    const float64_t behind_x = feet[0] + 3.5f64 * cos((float64_t)game->camera.yaw);
    const float64_t behind_y = feet[1] + 3.5f64 * sin((float64_t)game->camera.yaw);
    const int32_t wall_x = (int32_t)floor(behind_x), wall_y = (int32_t)floor(behind_y);
    const int32_t eye_z = (int32_t)floor(feet[2] + 1.62f64);
    REQUIRE(world_edit_block_set(&rig.world->edit, wall_x, wall_y, eye_z, BLOCK_ID_DIRT) == WORLD_EDIT_STATUS_CHANGED);

    const uint64_t landed = rig.mob->combat.landed_count;
    const float32_t exhaustion = rig.player->survival.exhaustion;
    const uint16_t wear = inventory_selected(inventory)->wear;
    mco_script_button(script, true);
    REQUIRE(mco_script_ticks(script, 1u));
    CHECK_EQ_U64(rig.mob->combat.landed_count, landed + 1u);
    REQUIRE(entity_store_index_of(store, handle, &index));
    CHECK(store->borrowed_health[index] == 13.0f32);
    CHECK_EQ_U64(inventory_selected(inventory)->wear, (uint64_t)wear + equipment_wear_of_attack(ITEM_ID_DIAMOND_SWORD));
    CHECK(rig.player->survival.exhaustion > exhaustion);
    /* It has been sent on its way along the eye's line, and has had its tick of that. */
    const float64_t from_x = store->borrowed_position[0][index] - feet[0];
    const float64_t from_y = store->borrowed_position[1][index] - feet[1];
    CHECK(hypot(from_x, from_y) > 1.5f64);
    CHECK(store->borrowed_position[2][index] > feet[2]);
    CHECK_EQ_U64(rig.player->interaction.mining, 0u);
    /* Holding on hits no more, and does not start on the wall. */
    REQUIRE(entity_store_index_of(store, handle, &index));
    mco_game_in_front(&rig, index, feet[2]);
    REQUIRE(mco_script_ticks(script, 5u));
    REQUIRE(entity_store_index_of(store, handle, &index));
    CHECK_EQ_U64(rig.mob->combat.landed_count, landed + 1u);
    CHECK_EQ_U64(rig.player->interaction.mined_count, 0u);
    mco_script_button(script, false);

    /* Twice more with a full swing and past the ten ticks: the last takes what is left, and its mind takes it away in that same tick. */
    const uint64_t died = rig.mob->zombie.died_count;
    const uint64_t dropped = rig.entity->item_drops.dropped_count;
    const uint64_t zombie_dropped = rig.mob->zombie.dropped_count;
    for (uint32_t blow = 0u; blow < 2u; blow++) {
        REQUIRE(mco_script_ticks(script, 13u));
        REQUIRE(entity_store_index_of(store, handle, &index));
        mco_game_in_front(&rig, index, feet[2]);
        store->borrowed_velocity[0][index] = 0.0f64;
        store->borrowed_velocity[1][index] = 0.0f64;
        mco_script_button(script, true);
        REQUIRE(mco_script_ticks(script, 1u));
        mco_script_button(script, false);
    }
    CHECK_EQ_U64(rig.mob->combat.landed_count, landed + 3u);
    CHECK_EQ_U64(rig.mob->combat.killed_count, 1u);
    CHECK(!entity_store_index_of(store, handle, &index));
    CHECK_EQ_U64(rig.mob->zombie.died_count, died + 1u);
    CHECK_EQ_U64(rig.entity->item_drops.dropped_count - dropped, rig.mob->zombie.dropped_count - zombie_dropped);
    CHECK_EQ_U64(inventory_selected(inventory)->wear,
                 (uint64_t)wear + 3u * equipment_wear_of_attack(ITEM_ID_DIAMOND_SWORD));
    /* Now the line is clear, and the button is on the wall: it is only mining. */
    mco_script_button(script, true);
    const uint64_t mined = rig.player->interaction.mined_count;
    for (uint32_t i = 0u; i < 60u && rig.player->interaction.mined_count == mined; i++) {
        REQUIRE(mco_script_ticks(script, 1u));
    }
    CHECK_EQ_U64(rig.player->interaction.mined_count, mined + 1u);
    CHECK_EQ_U64(rig.mob->combat.landed_count, landed + 3u);
    mco_script_button(script, false);

    game_close(game);
    mco_game_end(t, &rig);
}

TEST_MAIN()
