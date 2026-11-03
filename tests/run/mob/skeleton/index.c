/* ctest run_mob_skeleton. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "display/index.h"
#include "entity/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "item/stack/index.h"
#include "job/index.h"
#include "mob/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define MSK_EXACT 1.0e-9f64
#define MSK_TARGET_X 8.5f64
#define MSK_TARGET_Y 8.5f64
/* Well above any floor, for arrows that should meet only what a test puts there. */
#define MSK_HIGH 40.0f64
#define MSK_FAST 3.0f64

enum : int32_t {
    /* The floor the fixture's world is given: its corners are all loaded, or the columns are skipped. */
    MSK_FLOOR_MIN = -48,
    MSK_FLOOR_MAX = 63,
    MSK_HIGH_CELL = 40,
};
enum : uint32_t {
    MSK_DROP_ROLLS = 1000u,
    MSK_CROWD = 70u,
    MSK_SPEED_TICKS = 400u,
    MSK_SAME_TICKS = 600u,
};

typedef struct msk_rig_t {
    test_world_t world;                       /* 0 */
    entity_t *entity;                         /* 24 */
    mob_t *mob;                               /* 32 */
    entity_store_t *store;                    /* 40 */
    projectile_store_t *arrows;               /* 48 */
    mob_skeleton_t *skeleton;                 /* 56 */
    mob_skeleton_view_t view;                 /* 64 */
    float64_t last_direction[COLLISION_AXES]; /* 112: of the last hit on the target */
    uint64_t hits;                            /* 136 */
    float32_t hurt;                           /* 144: what the target has taken */
    float32_t last_amount;                    /* 148 */
} msk_rig_t;
static_assert(sizeof(msk_rig_t) == 152);

static bool msk_begin(test_context_t *t, msk_rig_t *rig, uint64_t seed) {
    memset(rig, 0, sizeof(*rig));
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics)) || !CHECK(mob_init(rig->mob, nullptr))) {
        return false;
    }
    entity_open(rig->entity, seed);
    mob_open(rig->mob, rig->world.world, seed);
    mob_skeleton_seed(&rig->mob->skeleton, seed);
    rig->store = &rig->entity->store;
    rig->arrows = &rig->entity->projectiles;
    rig->skeleton = &rig->mob->skeleton;
    rig->view = (mob_skeleton_view_t){
        .position = {MSK_TARGET_X, MSK_TARGET_Y, 0.0f64},
        .eye_height = 1.62f64,
        .height = 1.8f64,
        .alive = 1u,
    };
    return true;
}

static void msk_end(msk_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

/* A floor over every loaded column, whose top is at z = 0. */
static void msk_floor(test_context_t *t, msk_rig_t *rig) {
    for (int32_t x = MSK_FLOOR_MIN; x <= MSK_FLOOR_MAX; x++) {
        for (int32_t y = MSK_FLOOR_MIN; y <= MSK_FLOOR_MAX; y++) {
            column_t *const column = world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y));
            if (column != nullptr) {
                CHECK_EQ_U64(column_block_set(column, &rig->world.world->section_pool, column_local_of(x),
                                              column_local_of(y), -1, BLOCK_ID_STONE),
                             COLUMN_STATUS_OK);
            }
        }
    }
}

static void msk_put(test_context_t *t, msk_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    test_world_fill(t, &rig->world, x, y, z, x, y, z, block);
}

static entity_handle_t msk_body(msk_rig_t *rig, mob_id_t kind, float64_t x, float64_t y, float64_t z) {
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

static void msk_hurt(void *borrowed_context, float32_t amount, const float64_t direction[COLLISION_AXES]) {
    msk_rig_t *const rig = borrowed_context;
    rig->hits++;
    rig->hurt += amount;
    rig->last_amount = amount;
    memcpy(rig->last_direction, direction, sizeof(rig->last_direction));
}

static void msk_target_box(const msk_rig_t *rig, collision_box_t *out) {
    const float64_t *const at = rig->view.position;
    *out = (collision_box_t){
        .min = {at[0] - 0.3f64, at[1] - 0.3f64, at[2]},
        .max = {at[0] + 0.3f64, at[1] + 0.3f64, at[2] + rig->view.height},
    };
}

/* What a tick of the game does with all this, in its order. */
static void msk_tick(msk_rig_t *rig) {
    const world_map_t *const map = rig->world.map;
    mob_tick(rig->mob);
    mob_skeleton_tick(rig->skeleton, &rig->mob->path, rig->entity, map, &rig->view);
    entity_tick(rig->entity, map);
    collision_box_t box;
    msk_target_box(rig, &box);
    projectile_tick(rig->arrows, rig->store, &rig->entity->grid, map, rig->view.alive != 0u ? &box : nullptr, msk_hurt,
                    rig);
}

static void msk_ticks(msk_rig_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        msk_tick(rig);
    }
}

/* Only what is flying, with no skeleton thinking. */
static void msk_arrows_tick(msk_rig_t *rig, const collision_box_t *target) {
    entity_grid_build(&rig->entity->grid, rig->store);
    projectile_tick(rig->arrows, rig->store, &rig->entity->grid, rig->world.map, target, msk_hurt, rig);
}

static void msk_fire(msk_rig_t *rig, entity_handle_t shooter, float64_t x, float64_t y, float64_t z, float64_t speed) {
    projectile_fire(rig->arrows, shooter, (const float64_t[COLLISION_AXES]){x, y, z},
                    (const float64_t[COLLISION_AXES]){1.0f64, 0.0f64, 0.0f64}, speed, 0.0f64);
}

static uint64_t msk_arrow_books(const projectile_store_t *arrows) {
    return arrows->body_hits + arrows->target_hits + arrows->expired_count + arrows->burned_count + arrows->lost_count +
           arrows->count;
}

static float64_t msk_reach(const msk_rig_t *rig, entity_handle_t handle) {
    uint32_t index = 0u;
    if (!entity_store_index_of(rig->store, handle, &index)) {
        return -1.0f64;
    }
    const float64_t x = rig->store->borrowed_position[0][index] - rig->view.position[0];
    const float64_t y = rig->store->borrowed_position[1][index] - rig->view.position[1];
    const float64_t z = rig->store->borrowed_position[2][index] - rig->view.position[2];
    return sqrt(x * x + y * y + z * z);
}

/* ---- Flying -------------------------------------------------------------------------------------------------------- */

TEST(an_arrow_flies_by_the_published_numbers) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 1u));
    msk_fire(&rig, ENTITY_HANDLE_NONE, -30.5f64, 8.5f64, MSK_HIGH, 1.6f64);
    float64_t x = -30.5f64, z = MSK_HIGH, across = 1.6f64, rise = 0.0f64;
    for (uint32_t i = 0u; i < 30u; i++) {
        msk_arrows_tick(&rig, nullptr);
        x += across;
        z += rise;
        across *= 0.99f64;
        rise = rise * 0.99f64 - 0.05f64;
        const projectile_t *const arrow = &rig.arrows->borrowed_items[0];
        CHECK_NEAR(arrow->position[0], x, MSK_EXACT);
        CHECK_NEAR(arrow->position[2], z, MSK_EXACT);
        CHECK_NEAR(arrow->velocity[0], across, MSK_EXACT);
        CHECK_NEAR(arrow->velocity[2], rise, MSK_EXACT);
    }
    CHECK_EQ_U64(rig.arrows->borrowed_items[0].age, 30u);
    msk_end(&rig);
}

TEST(a_shot_goes_astray_by_no_more_than_its_inaccuracy) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 2u));
    const float64_t reach = 6.0f64 * PROJECTILE_SPREAD;
    float64_t furthest = 0.0f64;
    for (uint32_t i = 0u; i < 400u; i++) {
        projectile_fire(rig.arrows, ENTITY_HANDLE_NONE, (const float64_t[COLLISION_AXES]){0.5f64, 0.5f64, MSK_HIGH},
                        (const float64_t[COLLISION_AXES]){2.0f64, 0.0f64, 0.0f64}, 1.0f64, 6.0f64);
        const projectile_t *const arrow = &rig.arrows->borrowed_items[rig.arrows->count - 1u];
        CHECK(fabs(arrow->velocity[0] - 1.0f64) <= reach);
        CHECK(fabs(arrow->velocity[1]) <= reach);
        CHECK(fabs(arrow->velocity[2]) <= reach);
        furthest = fabs(arrow->velocity[1]) > furthest ? fabs(arrow->velocity[1]) : furthest;
    }
    CHECK(furthest > reach * 0.5f64);
    /* No inaccuracy: exactly along it. */
    msk_fire(&rig, ENTITY_HANDLE_NONE, 0.5f64, 0.5f64, MSK_HIGH, 1.6f64);
    const projectile_t *const straight = &rig.arrows->borrowed_items[rig.arrows->count - 1u];
    CHECK_NEAR(straight->velocity[0], 1.6f64, MSK_EXACT);
    CHECK_NEAR(straight->velocity[1], 0.0f64, MSK_EXACT);
    msk_end(&rig);
}

/* ---- Blocks -------------------------------------------------------------------------------------------------------- */

TEST(it_sticks_just_short_of_what_it_hits) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 3u));
    msk_put(t, &rig, 10, 8, MSK_HIGH_CELL, BLOCK_ID_STONE);
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.5f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    REQUIRE(rig.arrows->count == 1u);
    const projectile_t *const arrow = &rig.arrows->borrowed_items[0];
    CHECK_EQ_U64(arrow->stuck, 1u);
    CHECK_NEAR(arrow->position[0], 10.0f64 - PROJECTILE_BACK_OFF, MSK_EXACT);
    CHECK_NEAR(arrow->position[2], MSK_HIGH + 0.5f64, MSK_EXACT);
    CHECK_EQ_I64(arrow->stuck_cell[0], 10);
    CHECK_EQ_I64(arrow->stuck_cell[1], 8);
    CHECK_EQ_I64(arrow->stuck_cell[2], MSK_HIGH_CELL);
    CHECK_NEAR(arrow->velocity[0], 0.0f64, MSK_EXACT);
    CHECK_EQ_U64(rig.arrows->stuck_count, 1u);
    /* It stays put. */
    const float64_t x = arrow->position[0];
    for (uint32_t i = 0u; i < 20u; i++) {
        msk_arrows_tick(&rig, nullptr);
    }
    CHECK_NEAR(rig.arrows->borrowed_items[0].position[0], x, 0.0f64);
    msk_end(&rig);
}

TEST(nothing_gets_through_however_fast_or_however_slanted) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 4u));
    /* A wall one block thick, right across where any of these go. */
    test_world_fill(t, &rig.world, 20, -16, 20, 20, 32, 60, BLOCK_ID_STONE);
    const float64_t speeds[3] = {0.7f64, 7.3f64, 19.0f64};
    for (uint32_t s = 0u; s < 3u; s++) {
        for (uint32_t i = 0u; i < 100u; i++) {
            const float64_t y = (float64_t)i * 0.13f64 - 6.0f64;
            const float64_t slant = (float64_t)i * 0.007f64 - 0.35f64;
            projectile_fire(rig.arrows, ENTITY_HANDLE_NONE, (const float64_t[COLLISION_AXES]){8.25f64, y, MSK_HIGH},
                            (const float64_t[COLLISION_AXES]){1.0f64, slant, slant}, speeds[s], 0.0f64);
        }
    }
    for (uint32_t i = 0u; i < 40u; i++) {
        msk_arrows_tick(&rig, nullptr);
    }
    CHECK_EQ_U64(rig.arrows->count, 300u);
    for (uint32_t i = 0u; i < rig.arrows->count; i++) {
        const projectile_t *const arrow = &rig.arrows->borrowed_items[i];
        CHECK_EQ_U64(arrow->stuck, 1u);
        CHECK(arrow->position[0] < 20.0f64);
        CHECK_EQ_I64(arrow->stuck_cell[0], 20);
    }
    msk_end(&rig);
}

TEST(a_slab_stops_only_what_meets_its_half_and_a_plant_nothing) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 5u));
    msk_put(t, &rig, 10, 8, MSK_HIGH_CELL, BLOCK_ID_STONE_SLAB);
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.25f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->borrowed_items[0].stuck, 1u);
    rig.arrows->count = 0u;
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.75f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->borrowed_items[0].stuck, 0u);
    rig.arrows->count = 0u;
    msk_put(t, &rig, 10, 8, MSK_HIGH_CELL, BLOCK_ID_TALL_GRASS);
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.25f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->borrowed_items[0].stuck, 0u);
    CHECK_NEAR(rig.arrows->borrowed_items[0].position[0], 8.5f64 + MSK_FAST, MSK_EXACT);
    msk_end(&rig);
}

TEST(water_slows_it_and_lava_ends_it) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 6u));
    msk_put(t, &rig, 8, 8, MSK_HIGH_CELL, BLOCK_ID_WATER);
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.5f64, 0.1f64);
    msk_arrows_tick(&rig, nullptr);
    CHECK_NEAR(rig.arrows->borrowed_items[0].velocity[0], 0.1f64 * PROJECTILE_WATER_DRAG, MSK_EXACT);
    rig.arrows->count = 0u;
    msk_put(t, &rig, 8, 8, MSK_HIGH_CELL, BLOCK_ID_LAVA);
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.5f64, 0.1f64);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->count, 0u);
    CHECK_EQ_U64(rig.arrows->burned_count, 1u);
    msk_end(&rig);
}

TEST(a_stuck_arrow_lasts_its_time_and_falls_if_its_block_goes) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 7u));
    msk_put(t, &rig, 10, 8, MSK_HIGH_CELL, BLOCK_ID_STONE);
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.5f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    REQUIRE(rig.arrows->borrowed_items[0].stuck == 1u);
    for (uint32_t i = 0u; i + 1u < PROJECTILE_STUCK_TICKS; i++) {
        msk_arrows_tick(&rig, nullptr);
    }
    CHECK_EQ_U64(rig.arrows->count, 1u);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->count, 0u);
    CHECK_EQ_U64(rig.arrows->expired_count, 1u);

    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 0.5f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    const float64_t z = rig.arrows->borrowed_items[0].position[2];
    msk_put(t, &rig, 10, 8, MSK_HIGH_CELL, BLOCK_ID_AIR);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->borrowed_items[0].stuck, 0u);
    for (uint32_t i = 0u; i < 5u; i++) {
        msk_arrows_tick(&rig, nullptr);
    }
    CHECK(rig.arrows->borrowed_items[0].position[2] < z);
    CHECK_EQ_U64(msk_arrow_books(rig.arrows), rig.arrows->fired_count);
    msk_end(&rig);
}

TEST(one_that_flies_too_long_is_gone) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 8u));
    /* Slow, and nothing under it for a long way: then it would come down on the bottom of the world, so give it up there. */
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH, 0.001f64);
    rig.arrows->borrowed_items[0].age = PROJECTILE_FLIGHT_TICKS - 2u;
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->count, 1u);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->count, 0u);
    CHECK_EQ_U64(rig.arrows->expired_count, 1u);
    msk_end(&rig);
}

TEST(it_waits_where_a_column_is_not_loaded) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 9u));
    /* In one. */
    msk_fire(&rig, ENTITY_HANDLE_NONE, 200.5f64, 8.5f64, MSK_HIGH, 1.6f64);
    /* About to enter one: 64 and on is not loaded along x, at this y. */
    msk_fire(&rig, ENTITY_HANDLE_NONE, 62.5f64, 8.5f64, MSK_HIGH, 5.0f64);
    REQUIRE(world_map_column_at(rig.world.map, column_chunk_of(62), column_chunk_of(8)) != nullptr);
    REQUIRE(world_map_column_at(rig.world.map, column_chunk_of(66), column_chunk_of(8)) == nullptr);
    for (uint32_t i = 0u; i < 10u; i++) {
        msk_arrows_tick(&rig, nullptr);
    }
    CHECK_EQ_U64(rig.arrows->count, 2u);
    for (uint32_t i = 0u; i < 2u; i++) {
        const projectile_t *const arrow = &rig.arrows->borrowed_items[i];
        CHECK_EQ_U64(arrow->age, 0u);
        CHECK(arrow->position[0] == 200.5f64 || arrow->position[0] == 62.5f64);
        CHECK_NEAR(arrow->position[2], MSK_HIGH, 0.0f64);
    }
    msk_end(&rig);
}

/* ---- What it hits -------------------------------------------------------------------------------------------------- */

TEST(a_hit_on_a_body_hurts_and_knocks_it_back) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 10u));
    const entity_handle_t body = msk_body(&rig, MOB_ID_ZOMBIE, 10.5f64, 8.5f64, MSK_HIGH);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    const float32_t health = rig.store->borrowed_health[index];
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, 1.6f64);
    msk_arrows_tick(&rig, nullptr);
    /* 1.6 away less the margin and half its width: it is inside this tick's reach, so the second tick cannot be needed. */
    CHECK_EQ_U64(rig.arrows->count, 0u);
    CHECK_EQ_U64(rig.arrows->body_hits, 1u);
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK_NEAR(rig.store->borrowed_health[index], health - 4.0f32, 1.0e-3f32);
    CHECK_NEAR(rig.store->borrowed_velocity[0][index], PROJECTILE_KNOCKBACK, MSK_EXACT);
    CHECK_NEAR(rig.store->borrowed_velocity[1][index], 0.0f64, MSK_EXACT);
    msk_end(&rig);
}

TEST(a_slower_arrow_hurts_less) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 11u));
    const entity_handle_t body = msk_body(&rig, MOB_ID_ZOMBIE, 9.5f64, 8.5f64, MSK_HIGH);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    const float32_t health = rig.store->borrowed_health[index];
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, 0.6f64);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->count, 0u);
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK_NEAR(rig.store->borrowed_health[index], health - 2.0f32, 1.0e-3f32);
    msk_end(&rig);
}

TEST(what_it_can_hit_has_a_margin_and_a_shooter_is_no_part_of_it) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 12u));
    /* A skeleton's half width is 0.3. So 0.3 + 0.3 away from its middle is the edge of what meets it. */
    const entity_handle_t body = msk_body(&rig, MOB_ID_ZOMBIE, 10.5f64, 8.5f64, MSK_HIGH);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    const float64_t half = (float64_t)rig.store->borrowed_half_width[index];
    const float32_t health = rig.store->borrowed_health[index];
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64 + half + PROJECTILE_BODY_MARGIN + 0.01f64, MSK_HIGH + 1.0f64,
             MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->body_hits, 0u);
    rig.arrows->count = 0u;
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64 + half + PROJECTILE_BODY_MARGIN - 0.01f64, MSK_HIGH + 1.0f64,
             MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->body_hits, 1u);
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK(rig.store->borrowed_health[index] < health);
    const float32_t after = rig.store->borrowed_health[index];

    /* Its own arrow goes straight through it. */
    msk_fire(&rig, body, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->count, 1u);
    CHECK_EQ_U64(rig.arrows->body_hits, 1u);
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK_NEAR(rig.store->borrowed_health[index], after, 0.0f32);
    rig.arrows->count = 0u;

    /* And nobody's goes through it once it has no health, as it is not there. */
    rig.store->borrowed_health[index] = 0.0f32;
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, MSK_FAST);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.arrows->count, 1u);
    CHECK_EQ_U64(rig.arrows->body_hits, 1u);
    msk_end(&rig);
}

TEST(the_first_thing_along_its_way_is_what_it_hits) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 13u));
    const entity_handle_t body = msk_body(&rig, MOB_ID_ZOMBIE, 13.5f64, 8.5f64, MSK_HIGH);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    const float32_t health = rig.store->borrowed_health[index];
    const collision_box_t past = {.min = {15.0f64, 8.0f64, MSK_HIGH}, .max = {16.0f64, 9.0f64, MSK_HIGH + 2.0f64}};
    const collision_box_t before = {.min = {11.0f64, 8.0f64, MSK_HIGH}, .max = {12.0f64, 9.0f64, MSK_HIGH + 2.0f64}};

    /* The body before the target. */
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, 8.0f64);
    msk_arrows_tick(&rig, &past);
    CHECK_EQ_U64(rig.arrows->body_hits, 1u);
    CHECK_EQ_U64(rig.hits, 0u);

    /* The target before the body. */
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    rig.store->borrowed_health[index] = health;
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, 8.0f64);
    msk_arrows_tick(&rig, &before);
    CHECK_EQ_U64(rig.hits, 1u);
    CHECK_EQ_U64(rig.arrows->target_hits, 1u);
    CHECK_NEAR(rig.last_amount, 16.0f32, 1.0e-3f32);
    CHECK_NEAR(rig.last_direction[0], 1.0f64, MSK_EXACT);
    CHECK_NEAR(rig.last_direction[1], 0.0f64, MSK_EXACT);
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK_NEAR(rig.store->borrowed_health[index], health, 0.0f32);

    /* A wall before them all. */
    msk_put(t, &rig, 10, 8, MSK_HIGH_CELL + 1, BLOCK_ID_STONE);
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, 8.0f64);
    msk_arrows_tick(&rig, &before);
    CHECK_EQ_U64(rig.hits, 1u);
    CHECK_EQ_U64(rig.arrows->count, 1u);
    CHECK_EQ_U64(rig.arrows->borrowed_items[0].stuck, 1u);
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK_NEAR(rig.store->borrowed_health[index], health, 0.0f32);
    CHECK_EQ_U64(msk_arrow_books(rig.arrows), rig.arrows->fired_count);
    msk_end(&rig);
}

TEST(with_no_target_nothing_is_told) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 14u));
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH + 1.0f64, 8.0f64);
    msk_arrows_tick(&rig, nullptr);
    CHECK_EQ_U64(rig.hits, 0u);
    CHECK_EQ_U64(rig.arrows->count, 1u);
    msk_end(&rig);
}

TEST(knockback_is_the_published_sum) {
    const float64_t direction[COLLISION_AXES] = {0.6f64, 0.8f64, 0.0f64};
    float64_t on_ground[COLLISION_AXES] = {1.0f64, -1.0f64, 0.0f64};
    projectile_knockback(on_ground, direction, true);
    CHECK_NEAR(on_ground[0], 0.5f64 + 0.24f64, MSK_EXACT);
    CHECK_NEAR(on_ground[1], -0.5f64 + 0.32f64, MSK_EXACT);
    CHECK_NEAR(on_ground[2], 0.4f64, MSK_EXACT);
    float64_t rising[COLLISION_AXES] = {0.0f64, 0.0f64, 2.0f64};
    projectile_knockback(rising, direction, true);
    CHECK_NEAR(rising[2], 0.4f64, MSK_EXACT);
    float64_t in_air[COLLISION_AXES] = {0.0f64, 0.0f64, -0.5f64};
    projectile_knockback(in_air, direction, false);
    CHECK_NEAR(in_air[2], -0.5f64, MSK_EXACT);
}

TEST(a_full_store_loses_what_has_lain_longest_and_else_what_has_flown_longest) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 15u));
    for (uint32_t i = 0u; i < PROJECTILE_CAPACITY; i++) {
        msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH, 0.01f64);
        rig.arrows->borrowed_items[i].age = i == 100u ? 900u : 1u;
    }
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH, 0.01f64);
    CHECK_EQ_U64(rig.arrows->count, PROJECTILE_CAPACITY);
    CHECK_EQ_U64(rig.arrows->lost_count, 1u);
    for (uint32_t i = 0u; i < rig.arrows->count; i++) {
        CHECK(rig.arrows->borrowed_items[i].age < 900u);
    }
    rig.arrows->borrowed_items[300].stuck = 1u;
    msk_fire(&rig, ENTITY_HANDLE_NONE, 8.5f64, 8.5f64, MSK_HIGH, 0.01f64);
    CHECK_EQ_U64(rig.arrows->lost_count, 2u);
    for (uint32_t i = 0u; i < rig.arrows->count; i++) {
        CHECK_EQ_U64(rig.arrows->borrowed_items[i].stuck, 0u);
    }
    CHECK_EQ_U64(msk_arrow_books(rig.arrows), rig.arrows->fired_count);
    msk_end(&rig);
}

TEST(a_line_is_clear_of_what_has_no_box_and_of_nothing_else) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 16u));
    const float64_t from[COLLISION_AXES] = {8.5f64, 8.5f64, MSK_HIGH + 0.5f64};
    const float64_t to[COLLISION_AXES] = {20.5f64, 8.5f64, MSK_HIGH + 0.5f64};
    CHECK(projectile_line_is_clear(rig.world.map, from, to));
    msk_put(t, &rig, 12, 8, MSK_HIGH_CELL, BLOCK_ID_TALL_GRASS);
    msk_put(t, &rig, 13, 8, MSK_HIGH_CELL, BLOCK_ID_WATER);
    CHECK(projectile_line_is_clear(rig.world.map, from, to));
    msk_put(t, &rig, 14, 8, MSK_HIGH_CELL, BLOCK_ID_GLASS);
    CHECK(!projectile_line_is_clear(rig.world.map, from, to));
    msk_put(t, &rig, 14, 8, MSK_HIGH_CELL, BLOCK_ID_AIR);
    CHECK(projectile_line_is_clear(rig.world.map, from, to));
    const float64_t far[COLLISION_AXES] = {200.5f64, 8.5f64, MSK_HIGH + 0.5f64};
    CHECK(!projectile_line_is_clear(rig.world.map, from, far));
    msk_end(&rig);
}

/* ---- Keeping its distance ------------------------------------------------------------------------------------------ */

TEST(too_near_it_steps_back_and_stays_back) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 20u));
    msk_floor(t, &rig);
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 3.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 40u);
    CHECK(msk_reach(&rig, body) > 3.5f64);
    float64_t nearest_after = 100.0f64;
    float64_t furthest_after = 0.0f64;
    msk_ticks(&rig, 60u);
    for (uint32_t i = 0u; i < 400u; i++) {
        msk_tick(&rig);
        const float64_t reach = msk_reach(&rig, body);
        nearest_after = reach < nearest_after ? reach : nearest_after;
        furthest_after = reach > furthest_after ? reach : furthest_after;
    }
    /* Inside near it goes back, and circling only ever takes it out a little. */
    CHECK(nearest_after >= MOB_SKELETON_NEAR - 0.5f64);
    CHECK(furthest_after <= MOB_SKELETON_FAR + 1.0f64);
    msk_end(&rig);
}

TEST(backing_off_it_goes_directly_away_at_its_whole_speed) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 21u));
    msk_floor(t, &rig);
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 3.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 1u);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    const float64_t x = rig.store->borrowed_wish[0][index], y = rig.store->borrowed_wish[1][index];
    CHECK_NEAR(sqrt(x * x + y * y), (float64_t)mob_defs[MOB_ID_SKELETON].speed, 1.0e-6f64);
    CHECK(x > 0.0f64);
    CHECK(fabs(y) < x);
    /* It looks where it shoots, at the target. */
    CHECK_NEAR(rig.store->borrowed_yaw[index], 3.14159265f32, 1.0e-4f32);
    msk_end(&rig);
}

TEST(between_near_and_far_it_circles_at_half_speed) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 22u));
    msk_floor(t, &rig);
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 8.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 1u);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    const float64_t x = rig.store->borrowed_wish[0][index], y = rig.store->borrowed_wish[1][index];
    CHECK_NEAR(x, 0.0f64, 1.0e-9f64);
    CHECK_NEAR(fabs(y), (float64_t)mob_defs[MOB_ID_SKELETON].speed * MOB_SKELETON_CIRCLE_SPEED, 1.0e-6f64);
    msk_end(&rig);
}

TEST(outside_far_it_comes_on) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 23u));
    msk_floor(t, &rig);
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 14.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 200u);
    const float64_t reach = msk_reach(&rig, body);
    CHECK(reach < 12.5f64);
    CHECK(reach > MOB_SKELETON_NEAR);
    msk_end(&rig);
}

TEST(past_its_range_it_walks_a_path_and_then_shoots) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 24u));
    msk_floor(t, &rig);
    /* Seen at 15.5: past its range and inside its sight. */
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 15.5f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 300u);
    const float64_t reach = msk_reach(&rig, body);
    CHECK(reach < MOB_SKELETON_RANGE);
    CHECK(rig.skeleton->shot_count > 0u);
    msk_end(&rig);
}

TEST(it_will_not_back_off_a_cliff) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 25u));
    /* A small platform with the target on it. No floor around it. */
    const int32_t z = MSK_HIGH_CELL - 1;
    test_world_fill(t, &rig.world, 8, 6, z, 13, 10, z, BLOCK_ID_STONE);
    rig.view.position[2] = MSK_HIGH;
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, 11.5f64, MSK_TARGET_Y, MSK_HIGH);
    for (uint32_t i = 0u; i < 400u; i++) {
        msk_tick(&rig);
        uint32_t index = 0u;
        REQUIRE(entity_store_index_of(rig.store, body, &index));
        CHECK(rig.store->borrowed_position[2][index] > MSK_HIGH - 0.01f64);
        CHECK(rig.store->borrowed_position[0][index] < 14.0f64 && rig.store->borrowed_position[0][index] > 8.0f64);
    }
    /* Cornered, it goes on shooting. */
    CHECK(rig.skeleton->shot_count > 5u);
    msk_end(&rig);
}

TEST(it_will_not_back_into_lava_or_water) {
    const uint16_t fluids[2] = {BLOCK_ID_LAVA, BLOCK_ID_WATER};
    for (uint32_t f = 0u; f < 2u; f++) {
        msk_rig_t rig;
        REQUIRE(msk_begin(t, &rig, 26u));
        msk_floor(t, &rig);
        /* A ring of it round where it stands. */
        for (int32_t x = 8; x <= 15; x++) {
            for (int32_t y = 5; y <= 12; y++) {
                if (x == 8 || x == 15 || y == 5 || y == 12) {
                    if (x != 8) {
                        msk_put(t, &rig, x, y, 0, fluids[f]);
                    }
                }
            }
        }
        const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, 11.5f64, MSK_TARGET_Y, 0.0f64);
        for (uint32_t i = 0u; i < 300u; i++) {
            msk_tick(&rig);
            uint32_t index = 0u;
            REQUIRE(entity_store_index_of(rig.store, body, &index));
            CHECK((rig.store->borrowed_flags[index] & (ENTITY_STORE_FLAG_IN_LAVA | ENTITY_STORE_FLAG_IN_WATER)) == 0u);
        }
        msk_end(&rig);
    }
}

TEST(a_wall_behind_it_stops_it_and_not_its_shooting) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 27u));
    msk_floor(t, &rig);
    test_world_fill(t, &rig.world, 12, -16, 0, 12, 32, 3, BLOCK_ID_STONE);
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, 11.5f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 100u);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK(rig.store->borrowed_position[0][index] < 12.0f64);
    CHECK_EQ_U64(rig.skeleton->shot_count >= 2u, 1u);
    msk_end(&rig);
}

/* ---- The bow ------------------------------------------------------------------------------------------------------- */

TEST(the_bow_takes_a_second_to_draw_and_then_shoots_every_two) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 30u));
    msk_floor(t, &rig);
    (void)msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 8.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, MOB_SKELETON_DRAW_TICKS - 1u);
    CHECK_EQ_U64(rig.skeleton->shot_count, 0u);
    msk_tick(&rig);
    CHECK_EQ_U64(rig.skeleton->shot_count, 1u);
    msk_ticks(&rig, MOB_SKELETON_DRAW_TICKS + MOB_SKELETON_REST_TICKS - 1u);
    CHECK_EQ_U64(rig.skeleton->shot_count, 1u);
    msk_tick(&rig);
    CHECK_EQ_U64(rig.skeleton->shot_count, 2u);
    CHECK_EQ_U64(rig.arrows->fired_count, 2u);
    msk_end(&rig);
}

TEST(its_arrows_reach_a_target_standing_in_the_open_and_hurt_it) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 31u));
    msk_floor(t, &rig);
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 10.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 1200u);
    CHECK(rig.skeleton->shot_count >= 25u);
    /* Most of a hit at normal difficulty, from around ten blocks. */
    CHECK(rig.hits * 2u >= rig.skeleton->shot_count);
    CHECK(rig.hurt >= (float32_t)rig.hits * 2.0f32);
    CHECK(rig.hurt <= (float32_t)rig.hits * 4.0f32);
    CHECK_EQ_U64(rig.arrows->target_hits, rig.hits);
    CHECK_EQ_U64(msk_arrow_books(rig.arrows), rig.arrows->fired_count);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    /* Never by its own. */
    CHECK_NEAR(rig.store->borrowed_health[index], mob_defs[MOB_ID_SKELETON].health, 0.0f32);
    msk_end(&rig);
}

TEST(it_does_not_shoot_what_it_cannot_see_or_what_is_too_far) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 32u));
    msk_floor(t, &rig);
    /* Boxed in, so it does not walk round. */
    test_world_fill(t, &rig.world, 12, -48, 0, 12, 63, 8, BLOCK_ID_STONE);
    (void)msk_body(&rig, MOB_ID_SKELETON, 16.5f64, MSK_TARGET_Y, 0.0f64);
    (void)msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X, MSK_TARGET_Y + 30.0f64, 0.0f64);
    msk_ticks(&rig, 200u);
    CHECK_EQ_U64(rig.skeleton->shot_count, 0u);
    msk_end(&rig);
}

TEST(it_forgets_what_stays_out_of_sight_and_lets_its_arms_down) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 33u));
    msk_floor(t, &rig);
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 8.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 10u);
    const mob_skeleton_body_t *const mind = &rig.skeleton->borrowed_bodies[entity_handle_slot(body)];
    CHECK_EQ_U64(mind->engaged, 1u);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK(rig.store->borrowed_aim[index] > 0.9f32);
    /* The target goes, and nothing moves out of this line of sight by itself: this is out of sight and within reach. */
    test_world_fill(t, &rig.world, 11, -48, 0, 11, 63, 8, BLOCK_ID_STONE);
    const uint64_t shots = rig.skeleton->shot_count;
    msk_ticks(&rig, MOB_SKELETON_FORGET_TICKS - 1u);
    CHECK_EQ_U64(mind->engaged, 1u);
    msk_ticks(&rig, 1u);
    CHECK_EQ_U64(mind->engaged, 0u);
    CHECK_EQ_U64(rig.skeleton->shot_count, shots);
    msk_ticks(&rig, 40u);
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK(rig.store->borrowed_aim[index] < 0.1f32);
    msk_end(&rig);
}

TEST(with_nothing_to_be_after_it_strolls_and_never_draws) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 34u));
    msk_floor(t, &rig);
    rig.view.alive = 0u;
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X, MSK_TARGET_Y, 0.0f64);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    const float64_t x = rig.store->borrowed_position[0][index], y = rig.store->borrowed_position[1][index];
    float64_t furthest = 0.0f64;
    for (uint32_t i = 0u; i < 2000u; i++) {
        msk_tick(&rig);
        REQUIRE(entity_store_index_of(rig.store, body, &index));
        const float64_t dx = rig.store->borrowed_position[0][index] - x,
                        dy = rig.store->borrowed_position[1][index] - y;
        const float64_t gone = sqrt(dx * dx + dy * dy);
        furthest = gone > furthest ? gone : furthest;
    }
    CHECK(furthest > 1.0f64);
    CHECK_EQ_U64(rig.skeleton->shot_count, 0u);
    CHECK_NEAR(rig.store->borrowed_aim[index], 0.0f32, 0.0f32);
    msk_end(&rig);
}

TEST(a_body_waiting_for_its_column_thinks_about_nothing) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 35u));
    rig.view.position[0] = 200.5f64;
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, 204.5f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 100u);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK((rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_WAITING) != 0u);
    CHECK_EQ_U64(rig.skeleton->shot_count, 0u);
    msk_end(&rig);
}

TEST(in_water_it_swims_up) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 36u));
    msk_floor(t, &rig);
    test_world_fill(t, &rig.world, 0, 0, 0, 15, 15, 4, BLOCK_ID_WATER);
    rig.view.alive = 0u;
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, 8.5f64, 8.5f64, 0.0f64);
    msk_ticks(&rig, 60u);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    CHECK(rig.store->borrowed_position[2][index] > 1.0f64);
    msk_end(&rig);
}

/* ---- Dying --------------------------------------------------------------------------------------------------------- */

TEST(what_it_drops_is_what_the_table_says) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 40u));
    const mob_def_t *const def = &mob_defs[MOB_ID_SKELETON];
    const float64_t at[COLLISION_AXES] = {MSK_TARGET_X, MSK_TARGET_Y, MSK_HIGH};
    item_drop_store_t *const drops = &rig.entity->item_drops;
    uint64_t total = 0u, bones = 0u, arrows = 0u;
    const uint16_t bone_row = item_stack_drop_row(ITEM_ID_BONE);
    const uint16_t arrow_row = item_stack_drop_row(ITEM_ID_ARROW);
    for (uint32_t i = 0u; i < MSK_DROP_ROLLS; i++) {
        const uint32_t before = drops->count;
        const uint32_t count = mob_skeleton_drops(rig.skeleton, drops, def, at);
        CHECK(count <= 4u);
        total += count;
        uint32_t here = 0u;
        for (uint32_t s = before; s < drops->count; s++) {
            const item_drop_t *const drop = &drops->borrowed_items[s];
            CHECK(drop->item == bone_row || drop->item == arrow_row);
            CHECK(drop->count >= 1u && drop->count <= 2u);
            bones += drop->item == bone_row ? drop->count : 0u;
            arrows += drop->item == arrow_row ? drop->count : 0u;
            here += drop->count;
        }
        CHECK_EQ_U64(here, count);
    }
    CHECK_EQ_U64(rig.skeleton->dropped_count, total);
    CHECK_EQ_U64(item_drop_on_ground(drops), total);
    /* 0, 1 or 2 of each, evenly: a mean of 1 each, with a spread far inside this. */
    CHECK(bones > MSK_DROP_ROLLS * 9u / 10u && bones < MSK_DROP_ROLLS * 11u / 10u);
    CHECK(arrows > MSK_DROP_ROLLS * 9u / 10u && arrows < MSK_DROP_ROLLS * 11u / 10u);
    msk_end(&rig);
}

TEST(one_with_no_health_leaves_its_drops_and_is_gone) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 41u));
    msk_floor(t, &rig);
    rig.view.alive = 0u;
    item_drop_store_t *const drops = &rig.entity->item_drops;
    for (uint32_t i = 0u; i < 40u; i++) {
        const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, -20.5f64 + (float64_t)i, MSK_TARGET_Y, 0.0f64);
        uint32_t index = 0u;
        REQUIRE(entity_store_index_of(rig.store, body, &index));
        rig.store->borrowed_health[index] = -1.0f32;
    }
    mob_skeleton_tick(rig.skeleton, &rig.mob->path, rig.entity, rig.world.map, &rig.view);
    CHECK_EQ_U64(rig.store->count, 0u);
    CHECK_EQ_U64(rig.skeleton->died_count, 40u);
    CHECK(rig.skeleton->dropped_count > 0u);
    CHECK_EQ_U64(item_drop_on_ground(drops), rig.skeleton->dropped_count);
    /* They lie where it stood. */
    for (uint32_t i = 0u; i < drops->count; i++) {
        CHECK(fabs(drops->borrowed_items[i].position[1] - MSK_TARGET_Y) < 0.3f64);
        CHECK(drops->borrowed_items[i].position[2] > 0.0f64);
    }
    msk_end(&rig);
}

TEST(an_arrow_can_kill_one_and_it_drops_then) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 42u));
    msk_floor(t, &rig);
    rig.view.alive = 0u;
    const entity_handle_t body = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 2.0f64, MSK_TARGET_Y, 0.0f64);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, body, &index));
    rig.store->borrowed_health[index] = 1.0f32;
    msk_fire(&rig, ENTITY_HANDLE_NONE, MSK_TARGET_X, MSK_TARGET_Y, 1.0f64, MSK_FAST);
    msk_tick(&rig);
    CHECK_EQ_U64(rig.arrows->body_hits, 1u);
    CHECK_EQ_U64(rig.skeleton->died_count, 0u);
    msk_tick(&rig);
    CHECK_EQ_U64(rig.skeleton->died_count, 1u);
    CHECK(!entity_store_index_of(rig.store, body, &index));
    msk_end(&rig);
}

TEST(other_kinds_of_body_are_left_alone) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 43u));
    msk_floor(t, &rig);
    const entity_handle_t zombie = msk_body(&rig, MOB_ID_ZOMBIE, MSK_TARGET_X + 3.0f64, MSK_TARGET_Y, 0.0f64);
    const entity_handle_t creeper = msk_body(&rig, MOB_ID_CREEPER, MSK_TARGET_X - 3.0f64, MSK_TARGET_Y, 0.0f64);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, creeper, &index));
    rig.store->borrowed_health[index] = 0.0f32;
    msk_ticks(&rig, 100u);
    CHECK_EQ_U64(rig.store->count, 2u);
    CHECK_EQ_U64(rig.skeleton->shot_count, 0u);
    CHECK_EQ_U64(rig.skeleton->died_count, 0u);
    REQUIRE(entity_store_index_of(rig.store, zombie, &index));
    CHECK_NEAR(rig.store->borrowed_wish[0][index], 0.0f64, 0.0f64);
    CHECK_NEAR(rig.store->borrowed_aim[index], 0.0f32, 0.0f32);
    msk_end(&rig);
}

TEST(a_slot_used_again_starts_over) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 44u));
    msk_floor(t, &rig);
    const entity_handle_t first = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 8.0f64, MSK_TARGET_Y, 0.0f64);
    msk_ticks(&rig, 10u);
    const uint32_t slot = entity_handle_slot(first);
    CHECK_EQ_U64(rig.skeleton->borrowed_bodies[slot].engaged, 1u);
    CHECK_EQ_U64(rig.skeleton->borrowed_bodies[slot].draw, 10u);
    REQUIRE(entity_store_remove(rig.store, first));
    entity_handle_t again = ENTITY_HANDLE_NONE;
    for (uint32_t i = 0u; i <= ENTITY_STORE_CAPACITY; i++) {
        again = msk_body(&rig, MOB_ID_SKELETON, MSK_TARGET_X + 8.0f64, MSK_TARGET_Y, 0.0f64);
        if (entity_handle_slot(again) == slot) {
            break;
        }
        REQUIRE(entity_store_remove(rig.store, again));
    }
    REQUIRE(entity_handle_slot(again) == slot);
    CHECK(again != first);
    rig.view.alive = 0u;
    msk_ticks(&rig, 1u);
    CHECK_EQ_U64(rig.skeleton->borrowed_bodies[slot].owner, again);
    CHECK_EQ_U64(rig.skeleton->borrowed_bodies[slot].engaged, 0u);
    CHECK_EQ_U64(rig.skeleton->borrowed_bodies[slot].draw, 0u);
    msk_end(&rig);
}

/* ---- The Owners and the cost --------------------------------------------------------------------------------------- */

static void msk_crowd(test_context_t *t, msk_rig_t *rig, uint32_t count) {
    msk_floor(t, rig);
    for (uint32_t i = 0u; i < count; i++) {
        const float64_t turn = (float64_t)i * 6.283185307f64 / (float64_t)count;
        const float64_t reach = 4.0f64 + (float64_t)(i % 12u);
        (void)msk_body(rig, MOB_ID_SKELETON, MSK_TARGET_X + cos(turn) * reach, MSK_TARGET_Y + sin(turn) * reach,
                       0.0f64);
    }
}

TEST(the_same_calls_give_the_same_bytes_and_another_seed_others) {
    float64_t first_x[MSK_CROWD], first_arrow = 0.0f64;
    uint64_t first_hits = 0u;
    for (uint32_t run = 0u; run < 3u; run++) {
        msk_rig_t rig;
        REQUIRE(msk_begin(t, &rig, run == 2u ? 51u : 50u));
        msk_crowd(t, &rig, MSK_CROWD);
        msk_ticks(&rig, MSK_SAME_TICKS);
        REQUIRE(rig.store->count > 0u);
        const uint32_t count = rig.store->count < MSK_CROWD ? rig.store->count : MSK_CROWD;
        REQUIRE(rig.arrows->count > 0u);
        if (run == 0u) {
            memcpy(first_x, rig.store->borrowed_position[0], (uint64_t)count * sizeof(float64_t));
            first_arrow = rig.arrows->borrowed_items[0].position[0];
            first_hits = rig.arrows->target_hits;
        } else if (run == 1u) {
            CHECK_MEM_EQ(first_x, rig.store->borrowed_position[0], (uint64_t)count * sizeof(float64_t));
            CHECK_NEAR(first_arrow, rig.arrows->borrowed_items[0].position[0], 0.0f64);
            CHECK_EQ_U64(first_hits, rig.arrows->target_hits);
        } else {
            CHECK(memcmp(first_x, rig.store->borrowed_position[0], (uint64_t)count * sizeof(float64_t)) != 0);
        }
        msk_end(&rig);
    }
}

TEST(a_tick_takes_no_heap_and_no_arena_and_little_time) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 52u));
    msk_crowd(t, &rig, MSK_CROWD);
    msk_ticks(&rig, 40u);
    heap_counters_t before, after;
    heap_counters_read(&before);
    const uint64_t mob_used = rig.mob->arena_world.used, entity_used = rig.entity->arena_world.used;
    const int64_t started = time_now_ns();
    msk_ticks(&rig, MSK_SPEED_TICKS);
    const int64_t took = time_now_ns() - started;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(rig.mob->arena_world.used, mob_used);
    CHECK_EQ_U64(rig.entity->arena_world.used, entity_used);
    /* The phase's budget is 5 ms a tick for everything, at the debug flags and with the sanitizers too: this is a floor under it. */
    CHECK(took / (int64_t)MSK_SPEED_TICKS < 5000000);
    msk_end(&rig);
}

TEST(a_world_opened_again_starts_empty) {
    msk_rig_t rig;
    REQUIRE(msk_begin(t, &rig, 53u));
    msk_crowd(t, &rig, 8u);
    msk_ticks(&rig, 200u);
    REQUIRE(rig.arrows->fired_count > 0u);
    mob_close(rig.mob);
    entity_close(rig.entity);
    CHECK_EQ_U64(rig.arrows->count, 0u);
    CHECK_EQ_U64(rig.arrows->fired_count, 0u);
    entity_open(rig.entity, 53u);
    mob_open(rig.mob, rig.world.world, 53u);
    CHECK_EQ_U64(rig.arrows->count, 0u);
    CHECK_EQ_U64(rig.skeleton->shot_count, 0u);
    CHECK_EQ_U64(rig.skeleton->borrowed_bodies[0].owner, ENTITY_HANDLE_NONE);
    msk_end(&rig);
}

/* ---- In a real game ------------------------------------------------------------------------------------------------ */

typedef struct msk_game_rig_t {
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
} msk_game_rig_t;
static_assert(sizeof(msk_game_rig_t) == 104);

static bool msk_game_begin(test_context_t *t, msk_game_rig_t *rig) {
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

static void msk_game_end(test_context_t *t, msk_game_rig_t *rig) {
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

static bool msk_game_ticks(display_headless_scene_t *scene, display_script_state_t *state, uint32_t *tick,
                           uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        (*tick)++;
        const display_headless_tick_t now = {.events = nullptr, .state = state, .event_count = 0u, .tick = *tick};
        if (!scene->tick(scene->borrowed_context, &now)) {
            return false;
        }
    }
    return true;
}

/* All of the wiring in run/src/game/, on its own tick: the skeleton thinks, its arrow is drawn and flies, a real player is hurt and
 * knocked about, and a dead skeleton leaves its drops for that player's world. */
TEST(in_a_real_game_a_skeleton_shoots_the_player_and_leaves_its_drops) {
    msk_game_rig_t rig;
    REQUIRE(msk_game_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = 5u, .width = 320u, .height = 180u, ._pad0 = {0}};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    display_headless_scene_t scene;
    display_script_state_t *const state = test_alloc(t, sizeof(display_script_state_t));
    game_headless_scene(game, &scene);
    uint32_t tick = 0u;
    /* The ground the seed starts on falls away steeply, so a floor of its own, with the sky over it. */
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
    REQUIRE(msk_game_ticks(&scene, state, &tick, 10u));
    const float64_t *const feet = rig.player->movement.position;
    const mob_def_t *const def = &mob_defs[MOB_ID_SKELETON];
    const entity_store_spawn_t body = {
        .position = {feet[0] + 8.0f64, feet[1], (float64_t)pad_z},
        .half_width = def->half_width,
        .height = def->height,
        .health = def->health,
        .kind = (uint16_t)MOB_ID_SKELETON,
    };
    const entity_handle_t handle = entity_store_spawn(&rig.entity->store, &body);
    REQUIRE(handle != ENTITY_HANDLE_NONE);

    /* The first arrow is in the air before it can have got there, and it is drawn. */
    REQUIRE(msk_game_ticks(&scene, state, &tick, MOB_SKELETON_DRAW_TICKS + 1u));
    CHECK_EQ_U64(rig.mob->skeleton.shot_count, 1u);
    REQUIRE(rig.entity->projectiles.count == 1u);
    CHECK_EQ_U64(rig.entity->item_drops.count, 0u);
    /* The arrow, and the six parts of what shot it. */
    uint32_t drawn = 0u;
    for (uint32_t i = 0u; i < RENDER_EXCHANGE_SLOTS; i++) {
        const uint32_t count = rig.render->borrowed_entities->slots[i].count;
        drawn = count > drawn ? count : drawn;
    }
    CHECK_EQ_U64(drawn, 1u + MOB_SKELETON_PART_COUNT);

    const float32_t health = rig.player->survival.health;
    const uint64_t hurts = rig.player->survival.hurt_count;
    REQUIRE(msk_game_ticks(&scene, state, &tick, 200u));
    CHECK(rig.entity->projectiles.target_hits > 0u);
    CHECK(rig.player->survival.hurt_count > hurts);
    CHECK(rig.player->survival.health < health || rig.player->survival.died_count > 0u);

    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(&rig.entity->store, handle, &index));
    /* What was on the ground by now may be the player's: it can have died, and spilled. */
    const uint64_t dropped_before = rig.entity->item_drops.dropped_count;
    rig.entity->store.borrowed_health[index] = 0.0f32;
    REQUIRE(msk_game_ticks(&scene, state, &tick, 1u));
    CHECK(!entity_store_index_of(&rig.entity->store, handle, &index));
    CHECK_EQ_U64(rig.mob->skeleton.died_count, 1u);
    CHECK_EQ_U64(rig.entity->item_drops.dropped_count - dropped_before, rig.mob->skeleton.dropped_count);

    game_close(game);
    msk_game_end(t, &rig);
}

TEST_MAIN()
