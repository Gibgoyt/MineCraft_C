/* ctest run_mob_zombie. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "heap/index.h"
#include "item/stack/index.h"
#include "job/index.h"
#include "main/index.h"
#include "mob/index.h"
#include "player/survival/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/rng/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    MZ_NOTICE_TICKS = 400u, /* a chance of 1 in 10 per tick misses this many with odds of about 1 in 10^18 */
    MZ_DEATHS = 6000u,
    MZ_LINES = 400u,
    MZ_MANY = 100u,
    MZ_SPEED_TICKS = 400u,
    MZ_PROGRAM_TIMEOUT_MS = 200000u,
};
enum : int32_t {
    MZ_FLOOR_MIN = -48,
    MZ_FLOOR_MAX = 63,
};

typedef struct mz_rig_t {
    test_world_t world;       /* 0 */
    entity_t *entity;         /* 24 */
    mob_t *mob;               /* 32 */
    entity_store_t *store;    /* 40 */
    mob_zombie_t *zombie;     /* 48 */
    item_drop_store_t *drops; /* 56 */
} mz_rig_t;
static_assert(sizeof(mz_rig_t) == 64);

static bool mz_begin_with(test_context_t *t, mz_rig_t *rig, uint64_t seed, job_system_t *job) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics)) || !CHECK(mob_init(rig->mob, job))) {
        return false;
    }
    entity_open(rig->entity, seed);
    mob_open(rig->mob, rig->world.world, seed);
    rig->store = &rig->entity->store;
    rig->zombie = &rig->mob->zombie;
    rig->drops = &rig->entity->item_drops;
    return true;
}

static bool mz_begin(test_context_t *t, mz_rig_t *rig, uint64_t seed) {
    return mz_begin_with(t, rig, seed, nullptr);
}

static void mz_end(mz_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

/* A floor whose top is at z = 0, under every column loaded. */
static void mz_floor(test_context_t *t, mz_rig_t *rig) {
    for (int32_t x = MZ_FLOOR_MIN; x <= MZ_FLOOR_MAX; x++) {
        for (int32_t y = MZ_FLOOR_MIN; y <= MZ_FLOOR_MAX; y++) {
            column_t *const column = world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y));
            if (column != nullptr) {
                CHECK_EQ_U64(column_block_set(column, &rig->world.world->section_pool, column_local_of(x),
                                              column_local_of(y), -1, BLOCK_ID_STONE),
                             COLUMN_STATUS_OK);
            }
        }
    }
}

static mob_zombie_view_t mz_view(float64_t x, float64_t y, float64_t z) {
    return (mob_zombie_view_t){
        .position = {x, y, z},
        .eye = 1.62f64,
        .half_width = 0.3f64,
        .height = 1.8f64,
        .alive = 1u,
    };
}

static entity_handle_t mz_spawn_kind(test_context_t *t, mz_rig_t *rig, mob_id_t kind, float64_t x, float64_t y,
                                     float64_t z) {
    const mob_def_t *const def = &mob_defs[kind];
    const entity_store_spawn_t body = {
        .position = {x, y, z},
        .half_width = def->half_width,
        .height = def->height,
        .health = def->health,
        .kind = (uint16_t)kind,
    };
    const entity_handle_t handle = entity_store_spawn(rig->store, &body);
    CHECK(handle != ENTITY_HANDLE_NONE);
    return handle;
}

static entity_handle_t mz_spawn(test_context_t *t, mz_rig_t *rig, float64_t x, float64_t y, float64_t z) {
    return mz_spawn_kind(t, rig, MOB_ID_ZOMBIE, x, y, z);
}

/* A tick as the game runs one. */
static float32_t mz_step(mz_rig_t *rig, const mob_zombie_view_t *view) {
    mob_tick(rig->mob);
    const float32_t blow = mob_minds_tick(rig->mob, rig->entity, view);
    entity_tick(rig->entity, rig->world.map);
    return blow;
}

static const mob_zombie_body_t *mz_body(const mz_rig_t *rig, entity_handle_t handle) {
    return &rig->zombie->borrowed_bodies[entity_handle_slot(handle)];
}

static uint32_t mz_index(test_context_t *t, const mz_rig_t *rig, entity_handle_t handle) {
    uint32_t index = 0u;
    CHECK(entity_store_index_of(rig->store, handle, &index));
    return index;
}

/* Thinks without moving, until this zombie has noticed. */
static bool mz_notice(mz_rig_t *rig, entity_handle_t handle, const mob_zombie_view_t *view) {
    float32_t ignored = 0.0f32;
    for (uint32_t i = 0u; i < MZ_NOTICE_TICKS && mz_body(rig, handle)->chasing == 0u; i++) {
        ignored = mob_minds_tick(rig->mob, rig->entity, view);
    }
    (void)ignored;
    return mz_body(rig, handle)->chasing != 0u;
}

static float64_t mz_across(test_context_t *t, const mz_rig_t *rig, entity_handle_t handle,
                           const mob_zombie_view_t *view) {
    const uint32_t index = mz_index(t, rig, handle);
    const float64_t dx = rig->store->borrowed_position[0][index] - view->position[0];
    const float64_t dy = rig->store->borrowed_position[1][index] - view->position[1];
    return sqrt(dx * dx + dy * dy);
}

/* ---- Sight ------------------------------------------------------------------------------------------------------------ */

static bool mz_clear(const mz_rig_t *rig, float64_t x0, float64_t y0, float64_t z0, float64_t x1, float64_t y1,
                     float64_t z1) {
    const float64_t from[3] = {x0, y0, z0}, to[3] = {x1, y1, z1};
    return mob_zombie_line_is_clear(rig->world.map, from, to);
}

TEST(sight_stops_at_what_is_solid_and_at_nothing_else) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 1u));
    mz_floor(t, &rig);
    CHECK(mz_clear(&rig, 0.5f64, 0.5f64, 1.5f64, 20.5f64, 7.5f64, 3.5f64));
    CHECK(mz_clear(&rig, 0.5f64, 0.5f64, 1.5f64, 0.5f64, 0.5f64, 1.5f64));      /* to itself */
    CHECK(!mz_clear(&rig, 0.5f64, 0.5f64, 1.5f64, 0.5f64, 0.5f64, -3.5f64));    /* the floor */
    CHECK(mz_clear(&rig, -20.5f64, -7.5f64, 1.5f64, -2.5f64, -1.5f64, 1.5f64)); /* the negative side */

    test_world_fill(t, &rig.world, 10, 0, 0, 10, 0, 2, BLOCK_ID_STONE);
    CHECK(!mz_clear(&rig, 5.5f64, 0.5f64, 1.5f64, 15.5f64, 0.5f64, 1.5f64));
    CHECK(!mz_clear(&rig, 15.5f64, 0.5f64, 1.5f64, 5.5f64, 0.5f64, 1.5f64));
    CHECK(mz_clear(&rig, 5.5f64, 0.5f64, 3.5f64, 15.5f64, 0.5f64, 3.5f64)); /* over it */
    CHECK(mz_clear(&rig, 5.5f64, 1.5f64, 1.5f64, 15.5f64, 1.5f64, 1.5f64)); /* past it */

    /* A plant, and water, hide nothing. */
    test_world_fill(t, &rig.world, 3, 8, 0, 3, 8, 0, BLOCK_ID_TALL_GRASS);
    test_world_fill(t, &rig.world, 5, 8, 0, 5, 8, 0, BLOCK_ID_WATER);
    CHECK(mz_clear(&rig, 0.5f64, 8.5f64, 0.5f64, 8.5f64, 8.5f64, 0.5f64));

    /* A slab hides what its half does. */
    test_world_fill(t, &rig.world, 3, 12, 0, 3, 12, 0, BLOCK_ID_STONE_SLAB);
    CHECK(!mz_clear(&rig, 0.5f64, 12.5f64, 0.25f64, 6.5f64, 12.5f64, 0.25f64));
    CHECK(mz_clear(&rig, 0.5f64, 12.5f64, 0.75f64, 6.5f64, 12.5f64, 0.75f64));

    /* Nothing can be said of what is not there. */
    CHECK(!mz_clear(&rig, 0.5f64, 0.5f64, 1.5f64, 500.5f64, 0.5f64, 1.5f64));
    mz_end(&rig);
}

/* Whether the line meets a cube, by every cube around it: it knows nothing of the walk it is checking. */
static bool mz_line_meets_any(const uint8_t *solid, int32_t size, const float64_t from[3], const float64_t to[3]) {
    for (int32_t x = 0; x < size; x++) {
        for (int32_t y = 0; y < size; y++) {
            for (int32_t z = 0; z < size; z++) {
                if (solid[(x * size + y) * size + z] == 0u) {
                    continue;
                }
                const int32_t low[3] = {x, y, z};
                float64_t enter = 0.0f64, leave = 1.0f64;
                for (uint32_t axis = 0u; axis < 3u; axis++) {
                    const float64_t d = to[axis] - from[axis];
                    const float64_t a = ((float64_t)low[axis] - from[axis]) / d;
                    const float64_t c = ((float64_t)low[axis] + 1.0f64 - from[axis]) / d;
                    enter = fmax(enter, fmin(a, c));
                    leave = fmin(leave, fmax(a, c));
                }
                if (enter < leave) {
                    return true;
                }
            }
        }
    }
    return false;
}

TEST(sight_agrees_with_every_cube_tried_by_hand) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 2u));
    enum : int32_t { SIZE = 12 };
    uint8_t *const solid = test_alloc(t, (uint64_t)SIZE * SIZE * SIZE);
    terrain_rng_t random = terrain_rng_start(77u);
    for (int32_t i = 0; i < SIZE * SIZE * SIZE; i++) {
        if (terrain_rng_next_below(&random, 12u) == 0u) {
            solid[i] = 1u;
            const int32_t x = i / (SIZE * SIZE), y = (i / SIZE) % SIZE, z = i % SIZE;
            test_world_fill(t, &rig.world, x, y, z, x, y, z, BLOCK_ID_STONE);
        }
    }
    uint32_t blocked = 0u, clear = 0u;
    for (uint32_t line = 0u; line < MZ_LINES; line++) {
        float64_t from[3], to[3];
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            from[axis] = terrain_rng_next_f64(&random) * (float64_t)SIZE;
            to[axis] = terrain_rng_next_f64(&random) * (float64_t)SIZE;
        }
        const bool meets = mz_line_meets_any(solid, SIZE, from, to);
        CHECK_EQ_U64(mob_zombie_line_is_clear(rig.world.map, from, to) ? 0u : 1u, meets ? 1u : 0u);
        blocked += meets ? 1u : 0u;
        clear += meets ? 0u : 1u;
    }
    CHECK(blocked > 50u);
    CHECK(clear > 50u);
    mz_end(&rig);
}

/* ---- Reach ------------------------------------------------------------------------------------------------------------ */

TEST(reach_is_its_box_widened_sideways_and_not_up) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 3u));
    mz_floor(t, &rig);
    const uint32_t index = mz_index(t, &rig, mz_spawn(t, &rig, 0.5f64, 0.5f64, 0.0f64));
    /* 0.3 of its own, 0.828 of its arm and 0.3 of the other's. */
    mob_zombie_view_t view = mz_view(0.5f64 + 1.427f64, 0.5f64, 0.0f64);
    CHECK(mob_zombie_in_reach(rig.store, index, &view));
    view = mz_view(0.5f64 + 1.429f64, 0.5f64, 0.0f64);
    CHECK(!mob_zombie_in_reach(rig.store, index, &view));
    view = mz_view(0.5f64 - 1.427f64, 0.5f64, 0.0f64);
    CHECK(mob_zombie_in_reach(rig.store, index, &view));
    view = mz_view(0.5f64, 0.5f64 + 1.427f64, 0.0f64);
    CHECK(mob_zombie_in_reach(rig.store, index, &view));
    view = mz_view(0.5f64, 0.5f64 - 1.429f64, 0.0f64);
    CHECK(!mob_zombie_in_reach(rig.store, index, &view));
    /* A corner counts, as boxes have them. */
    view = mz_view(0.5f64 + 1.4f64, 0.5f64 + 1.4f64, 0.0f64);
    CHECK(mob_zombie_in_reach(rig.store, index, &view));
    /* Up: its own 1.95. Down: the other's 1.8. */
    view = mz_view(0.5f64, 0.5f64, 1.94f64);
    CHECK(mob_zombie_in_reach(rig.store, index, &view));
    view = mz_view(0.5f64, 0.5f64, 1.96f64);
    CHECK(!mob_zombie_in_reach(rig.store, index, &view));
    view = mz_view(0.5f64, 0.5f64, -1.79f64);
    CHECK(mob_zombie_in_reach(rig.store, index, &view));
    view = mz_view(0.5f64, 0.5f64, -1.81f64);
    CHECK(!mob_zombie_in_reach(rig.store, index, &view));
    mz_end(&rig);
}

/* ---- Noticing --------------------------------------------------------------------------------------------------------- */

TEST(it_notices_what_is_in_sight_about_one_tick_in_ten) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 4u));
    mz_floor(t, &rig);
    enum : uint32_t { BODIES = 400u };
    entity_handle_t *const handles = test_alloc(t, BODIES * sizeof(entity_handle_t));
    for (uint32_t i = 0u; i < BODIES; i++) {
        handles[i] = mz_spawn(t, &rig, -10.5f64 + (float64_t)(i % 20u), -10.5f64 + (float64_t)(i / 20u), 0.0f64);
    }
    const mob_zombie_view_t view = mz_view(0.5f64, 20.5f64, 0.0f64);
    (void)mob_minds_tick(rig.mob, rig.entity, &view);
    uint32_t chasing = 0u;
    for (uint32_t i = 0u; i < BODIES; i++) {
        chasing += mz_body(&rig, handles[i])->chasing;
    }
    CHECK(chasing >= 20u && chasing <= 65u);
    mz_end(&rig);
}

typedef struct mz_notice_row_t {
    float64_t x;      /* 0 */
    float64_t y;      /* 8 */
    float64_t z;      /* 16 */
    uint8_t alive;    /* 24 */
    uint8_t noticed;  /* 25 */
    uint8_t _pad0[6]; /* 26 */
} mz_notice_row_t;
static_assert(sizeof(mz_notice_row_t) == 32);

TEST(it_does_not_notice_what_it_should_not) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 5u));
    mz_floor(t, &rig);
    test_world_fill(t, &rig.world, 2, -8, 0, 2, 8, 4, BLOCK_ID_STONE);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 0.5f64, 0.0f64);
    /* What is tried, and whether it counts. */
    const mz_notice_row_t rows[] = {
        {.x = 4.5f64, .y = 0.5f64, .z = 0.0f64, .alive = 1u, .noticed = 0u},   /* behind the wall */
        {.x = -35.6f64, .y = 0.5f64, .z = 0.0f64, .alive = 1u, .noticed = 0u}, /* past its range */
        {.x = -5.5f64, .y = 0.5f64, .z = 5.0f64, .alive = 1u, .noticed = 0u},  /* too far above */
        {.x = -5.5f64, .y = 0.5f64, .z = -5.0f64, .alive = 1u, .noticed = 0u}, /* too far below */
        {.x = -5.5f64, .y = 0.5f64, .z = 0.0f64, .alive = 0u, .noticed = 0u},  /* dead */
        {.x = -5.5f64, .y = 0.5f64, .z = 3.5f64, .alive = 1u, .noticed = 1u},
        {.x = -34.5f64, .y = 0.5f64, .z = 0.0f64, .alive = 1u, .noticed = 1u},
    };
    for (uint32_t row = 0u; row < sizeof(rows) / sizeof(rows[0]); row++) {
        mob_zombie_view_t view = mz_view(rows[row].x, rows[row].y, rows[row].z);
        view.alive = rows[row].alive;
        CHECK_EQ_U64(mz_notice(&rig, handle, &view) ? 1u : 0u, rows[row].noticed);
        rig.zombie->borrowed_bodies[entity_handle_slot(handle)].chasing = 0u;
    }
    mz_end(&rig);
}

/* ---- The chase -------------------------------------------------------------------------------------------------------- */

TEST(it_walks_up_to_what_it_chases_at_its_speed_and_stops_short) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 6u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t view = mz_view(20.5f64, 8.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &view));
    uint32_t ticks = 0u;
    while (mz_step(&rig, &view) == 0.0f32 && ticks < 400u) {
        ticks++;
    }
    /* It stops when its arms reach, and it took about as long as the walk's length over its speed. */
    const float64_t walked = 20.0f64 - 1.428f64;
    const float64_t expected = walked / (float64_t)mob_defs[MOB_ID_ZOMBIE].speed;
    CHECK((float64_t)ticks > expected * 0.85f64);
    CHECK((float64_t)ticks < expected * 1.3f64);
    CHECK_EQ_U64(rig.zombie->struck_count, 1u);

    /* It stands there, and does not walk into what it hits. */
    for (uint32_t i = 0u; i < 60u; i++) {
        (void)mz_step(&rig, &view);
    }
    const uint32_t index = mz_index(t, &rig, handle);
    CHECK(rig.store->borrowed_wish[0][index] == 0.0f64);
    CHECK(rig.store->borrowed_wish[1][index] == 0.0f64);
    const float64_t across = mz_across(t, &rig, handle, &view);
    CHECK(across > 1.0f64 && across < 1.428f64);
    /* Facing it: it is straight on along x. */
    CHECK_NEAR((float64_t)rig.store->borrowed_yaw[index], 0.0f64, 0.05f64);
    CHECK_NEAR((float64_t)rig.store->borrowed_head_yaw[index], 0.0f64, 0.05f64);
    mz_end(&rig);
}

TEST(it_walks_round_a_wall_it_cannot_see_through) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 7u));
    mz_floor(t, &rig);
    test_world_fill(t, &rig.world, 10, 6, 0, 10, 11, 3, BLOCK_ID_STONE);
    const entity_handle_t handle = mz_spawn(t, &rig, 8.5f64, 8.5f64, 0.0f64);
    /* It notices something off to the side, which then goes behind the wall. */
    const mob_zombie_view_t side = mz_view(12.5f64, 1.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &side));
    const mob_zombie_view_t view = mz_view(12.5f64, 8.5f64, 0.0f64);
    bool went_round = false;
    float32_t blow = 0.0f32;
    for (uint32_t i = 0u; i < 300u && blow == 0.0f32; i++) {
        blow = mz_step(&rig, &view);
        const uint32_t index = mz_index(t, &rig, handle);
        const float64_t y = rig.store->borrowed_position[1][index];
        went_round = went_round || y < 5.9f64 || y > 12.1f64;
    }
    CHECK(blow == MOB_ZOMBIE_DAMAGE);
    CHECK(went_round);
    /* From its own side of the wall, no blow can have reached: it had to come through. */
    const uint32_t index = mz_index(t, &rig, handle);
    CHECK(rig.store->borrowed_position[0][index] > 10.0f64);
    mz_end(&rig);
}

TEST(it_climbs_blocks_after_what_it_chases) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 8u));
    mz_floor(t, &rig);
    test_world_fill(t, &rig.world, 10, 0, 0, 20, 16, 0, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 12, 0, 1, 20, 16, 1, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 14, 0, 2, 20, 16, 2, BLOCK_ID_STONE);
    const entity_handle_t handle = mz_spawn(t, &rig, 5.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t view = mz_view(16.5f64, 8.5f64, 3.0f64);
    REQUIRE(mz_notice(&rig, handle, &view));
    float32_t blow = 0.0f32;
    for (uint32_t i = 0u; i < 400u && blow == 0.0f32; i++) {
        blow = mz_step(&rig, &view);
    }
    CHECK(blow == MOB_ZOMBIE_DAMAGE);
    const uint32_t index = mz_index(t, &rig, handle);
    CHECK(rig.store->borrowed_position[2][index] > 1.0f64);
    mz_end(&rig);
}

TEST(it_catches_what_walks_away_slower_than_it_walks) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 9u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, -30.5f64, 8.5f64, 0.0f64);
    mob_zombie_view_t view = mz_view(-20.5f64, 8.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &view));
    float32_t blow = 0.0f32;
    uint32_t ticks = 0u;
    while (blow == 0.0f32 && ticks < 900u) {
        view.position[0] += 0.07f64;
        blow = mz_step(&rig, &view);
        ticks++;
    }
    CHECK(blow == MOB_ZOMBIE_DAMAGE);
    CHECK(view.position[0] > -10.0f64); /* it took a while: it was not there already */
    mob_path_stats_t stats;
    mob_path_stats_read(&rig.mob->path, &stats);
    /* It asked again as it went, and not every tick. */
    CHECK(stats.requests > 8u);
    CHECK(stats.requests < ticks / 3u);
    mz_end(&rig);
}

TEST(a_wall_built_across_its_way_sends_it_round) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 10u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t view = mz_view(14.5f64, 8.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &view));
    for (uint32_t i = 0u; i < 40u; i++) {
        (void)mz_step(&rig, &view);
    }
    /* Too high to climb, and short enough that it comes back into sight before it gives up. */
    world_edit_t *const edit = &rig.world.world->edit;
    for (int32_t y = 6; y <= 11; y++) {
        for (int32_t z = 0; z <= 3; z++) {
            CHECK_EQ_U64(world_edit_block_set(edit, 8, y, z, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED);
        }
    }
    float32_t blow = 0.0f32;
    for (uint32_t i = 0u; i < 500u && blow == 0.0f32; i++) {
        blow = mz_step(&rig, &view);
    }
    CHECK(blow == MOB_ZOMBIE_DAMAGE);
    CHECK(mz_across(t, &rig, handle, &view) < 1.5f64);
    mz_end(&rig);
}

/* ---- Giving up -------------------------------------------------------------------------------------------------------- */

TEST(it_gives_up_on_what_has_gone_too_far_or_died) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 11u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    mob_zombie_view_t view = mz_view(10.5f64, 8.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &view));
    for (uint32_t i = 0u; i < 30u; i++) {
        (void)mz_step(&rig, &view);
    }
    uint32_t index = mz_index(t, &rig, handle);
    CHECK(rig.store->borrowed_wish[0][index] != 0.0f64);

    view.alive = 0u;
    (void)mob_minds_tick(rig.mob, rig.entity, &view);
    CHECK_EQ_U64(mz_body(&rig, handle)->chasing, 0u);
    CHECK(rig.store->borrowed_wish[0][index] == 0.0f64);
    CHECK((float64_t)rig.store->borrowed_head_yaw[index] == 0.0f64);

    view.alive = 1u;
    REQUIRE(mz_notice(&rig, handle, &view));
    view = mz_view(rig.store->borrowed_position[0][index] + 36.0f64, 8.5f64, 0.0f64);
    (void)mob_minds_tick(rig.mob, rig.entity, &view);
    CHECK_EQ_U64(mz_body(&rig, handle)->chasing, 0u);
    mz_end(&rig);
}

TEST(it_gives_up_on_what_stays_out_of_sight_and_not_before) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 12u));
    mz_floor(t, &rig);
    /* A whole wall: the far side cannot be seen, or got to, from this side. */
    test_world_fill(t, &rig.world, 2, -32, 0, 2, 47, 40, BLOCK_ID_STONE);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 0.5f64, 0.0f64);
    const mob_zombie_view_t seen = mz_view(-5.5f64, 0.5f64, 0.0f64);
    const mob_zombie_view_t hidden = mz_view(6.5f64, 0.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &seen));

    /* Out of sight for a while, and back: the count starts over. */
    for (uint32_t round = 0u; round < 3u; round++) {
        for (uint32_t i = 0u; i < 50u; i++) {
            rig.store->borrowed_age[0]++;
            (void)mob_minds_tick(rig.mob, rig.entity, &hidden);
        }
        CHECK_EQ_U64(mz_body(&rig, handle)->chasing, 1u);
        for (uint32_t i = 0u; i < MOB_ZOMBIE_SIGHT_EVERY; i++) {
            rig.store->borrowed_age[0]++;
            (void)mob_minds_tick(rig.mob, rig.entity, &seen);
        }
        CHECK_EQ_U64(mz_body(&rig, handle)->unseen, 0u);
    }

    uint32_t ticks = 0u;
    while (mz_body(&rig, handle)->chasing != 0u && ticks < 200u) {
        rig.store->borrowed_age[0]++;
        (void)mob_minds_tick(rig.mob, rig.entity, &hidden);
        ticks++;
    }
    CHECK(ticks > MOB_ZOMBIE_UNSEEN_TICKS);
    CHECK(ticks <= MOB_ZOMBIE_UNSEEN_TICKS + 2u * MOB_ZOMBIE_SIGHT_EVERY);
    mz_end(&rig);
}

TEST(with_nothing_to_chase_it_strolls_about_near_where_it_was_and_seldom_asks) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 24u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, 8.5f64, 8.5f64, 0.0f64);
    mob_zombie_view_t view = mz_view(8.5f64, 8.5f64, 0.0f64);
    view.alive = 0u;
    const uint32_t index = mz_index(t, &rig, handle);
    enum : uint32_t { TICKS = 6000u };
    uint32_t moving = 0u, stopped = 0u;
    float64_t furthest = 0.0f64;
    for (uint32_t i = 0u; i < TICKS; i++) {
        CHECK(mz_step(&rig, &view) == 0.0f32);
        const float64_t dx = rig.store->borrowed_position[0][index] - rig.store->borrowed_previous[0][index];
        const float64_t dy = rig.store->borrowed_position[1][index] - rig.store->borrowed_previous[1][index];
        moving += dx * dx + dy * dy > 1.0e-6f64 ? 1u : 0u;
        stopped += dx * dx + dy * dy > 1.0e-6f64 ? 0u : 1u;
        furthest = fmax(furthest, mz_across(t, &rig, handle, &view));
    }
    CHECK(moving > TICKS / 10u);
    CHECK(stopped > TICKS / 10u);
    CHECK(furthest > 3.0f64);
    /* At a fair walk it could have been 690 blocks off. A walk of 10 at a time over this many goes as drunks go, and stays far nearer. */
    CHECK(furthest < 200.0f64);
    CHECK_EQ_U64(mz_body(&rig, handle)->chasing, 0u);
    mob_path_stats_t stats;
    mob_path_stats_read(&rig.mob->path, &stats);
    CHECK(stats.requests > TICKS / 400u);
    CHECK(stats.requests < TICKS / 60u);
    mz_end(&rig);
}

TEST(a_stroll_in_a_box_it_cannot_leave_ends_and_it_can_still_notice) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 25u));
    mz_floor(t, &rig);
    /* Solid all round, to the head: nowhere to go, and nothing asked of it can last. */
    test_world_fill(t, &rig.world, 7, 7, 0, 9, 9, 3, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 8, 8, 0, 8, 8, 2, BLOCK_ID_AIR);
    const entity_handle_t handle = mz_spawn(t, &rig, 8.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t hidden = mz_view(20.5f64, 8.5f64, 0.0f64);
    uint32_t strolls = 0u, was = 0u;
    for (uint32_t i = 0u; i < 4000u; i++) {
        CHECK(mz_step(&rig, &hidden) == 0.0f32);
        const uint32_t stroll = mz_body(&rig, handle)->stroll;
        strolls += stroll > was ? 1u : 0u;
        was = stroll;
        CHECK(stroll <= MOB_ZOMBIE_STROLL_TICKS);
    }
    CHECK(strolls > 3u);
    CHECK_NEAR(mz_across(t, &rig, handle, &hidden), 12.0f64, 0.5f64);
    /* The roof comes off, and it sees over the wall to something standing higher. */
    test_world_fill(t, &rig.world, 7, 7, 1, 9, 9, 3, BLOCK_ID_AIR);
    const mob_zombie_view_t above = mz_view(20.5f64, 8.5f64, 3.0f64);
    CHECK(mz_notice(&rig, handle, &above));
    CHECK_EQ_U64(mz_body(&rig, handle)->stroll, 0u);
    mz_end(&rig);
}

TEST(what_it_cannot_get_to_is_walked_towards_and_asked_for_seldom) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 13u));
    mz_floor(t, &rig);
    /* On a pillar, in plain sight, and too high to reach. */
    test_world_fill(t, &rig.world, 12, 8, 0, 12, 8, 2, BLOCK_ID_STONE);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t view = mz_view(12.5f64, 8.5f64, 3.0f64);
    REQUIRE(mz_notice(&rig, handle, &view));
    float32_t blows = 0.0f32;
    enum : uint32_t { TICKS = 400u };
    for (uint32_t i = 0u; i < TICKS; i++) {
        blows += mz_step(&rig, &view);
    }
    CHECK(mz_across(t, &rig, handle, &view) < 3.0f64);
    /* Its arms do not reach 3 up, past its 1.95. */
    CHECK(blows == 0.0f32);
    CHECK_EQ_U64(mz_body(&rig, handle)->chasing, 1u);
    mob_path_stats_t stats;
    mob_path_stats_read(&rig.mob->path, &stats);
    CHECK(stats.requests < TICKS / 5u);
    mz_end(&rig);
}

/* ---- The blow --------------------------------------------------------------------------------------------------------- */

TEST(its_blows_come_at_once_then_twenty_ticks_apart) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 14u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t far = mz_view(4.5f64, 8.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &far));
    const mob_zombie_view_t view = mz_view(1.5f64, 8.5f64, 0.0f64);
    /* Its first, the moment it is in reach. */
    CHECK(mob_minds_tick(rig.mob, rig.entity, &view) == MOB_ZOMBIE_DAMAGE);
    CHECK(MOB_ZOMBIE_DAMAGE == 3.0f32);
    const uint32_t index = mz_index(t, &rig, handle);
    CHECK_EQ_U64(rig.store->borrowed_swing_ticks[index], ENTITY_ANIMATION_SWING_TICKS);
    for (uint32_t blow = 0u; blow < 4u; blow++) {
        for (uint32_t i = 0u; i + 1u < MOB_ZOMBIE_ATTACK_TICKS; i++) {
            CHECK(mob_minds_tick(rig.mob, rig.entity, &view) == 0.0f32);
        }
        CHECK(mob_minds_tick(rig.mob, rig.entity, &view) == MOB_ZOMBIE_DAMAGE);
    }
    CHECK_EQ_U64(rig.zombie->struck_count, 5u);
    mz_end(&rig);
}

TEST(no_blow_goes_through_a_wall_built_in_the_way) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 15u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t view = mz_view(1.9f64, 8.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, handle, &view));
    /* Noticing it was not a blow's business: within reach, it has struck on that same tick. */
    CHECK_EQ_U64(rig.zombie->struck_count, 1u);
    /* A wall between them, hiding it at once though its mind has not caught up. */
    test_world_fill(t, &rig.world, 1, 0, 0, 1, 16, 4, BLOCK_ID_STONE);
    for (uint32_t i = 0u; i < 3u * MOB_ZOMBIE_ATTACK_TICKS; i++) {
        CHECK(mob_minds_tick(rig.mob, rig.entity, &view) == 0.0f32);
    }
    CHECK_EQ_U64(rig.zombie->struck_count, 1u);
    mz_end(&rig);
}

TEST(two_blows_in_a_tick_are_one_and_the_body_takes_them_as_any_hit) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 16u));
    mz_floor(t, &rig);
    const entity_handle_t one = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    const entity_handle_t two = mz_spawn(t, &rig, 2.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t view = mz_view(1.5f64, 8.5f64, 0.0f64);
    /* Both know of it, and both have struck their first. */
    REQUIRE(mz_notice(&rig, one, &view));
    REQUIRE(mz_notice(&rig, two, &view));
    for (uint32_t i = 0u; i < MOB_ZOMBIE_ATTACK_TICKS; i++) {
        (void)mob_minds_tick(rig.mob, rig.entity, &view);
    }
    rig.zombie->borrowed_bodies[entity_handle_slot(one)].cooldown = 0u;
    rig.zombie->borrowed_bodies[entity_handle_slot(two)].cooldown = 0u;
    const uint64_t struck = rig.zombie->struck_count;
    const float32_t blow = mob_minds_tick(rig.mob, rig.entity, &view);
    CHECK(blow == MOB_ZOMBIE_DAMAGE);
    CHECK_EQ_U64(rig.zombie->struck_count, struck + 2u);

    /* What the game does with one. Seven kill. Armor takes some, as a hit from outside should. */
    player_survival_t survival;
    player_survival_start(&survival, 0.0f64, 0.0f64, 0.0f64);
    for (uint32_t i = 0u; i < 6u; i++) {
        CHECK(player_survival_hurt(&survival, blow, PLAYER_SURVIVAL_CAUSE_ATTACK) == blow);
        survival.invulnerable = 0u;
        CHECK_EQ_U64(survival.state, PLAYER_SURVIVAL_ALIVE);
    }
    (void)player_survival_hurt(&survival, blow, PLAYER_SURVIVAL_CAUSE_ATTACK);
    CHECK_EQ_U64(survival.state, PLAYER_SURVIVAL_DEAD);
    CHECK_EQ_U64(survival.killed_by, PLAYER_SURVIVAL_CAUSE_ATTACK);

    player_survival_start(&survival, 0.0f64, 0.0f64, 0.0f64);
    player_survival_armor_set(&survival, 10.0f32, 0.0f32);
    CHECK(player_survival_hurt(&survival, blow, PLAYER_SURVIVAL_CAUSE_ATTACK) < blow);
    mz_end(&rig);
}

/* ---- What it leaves --------------------------------------------------------------------------------------------------- */

TEST(what_it_carries_lies_as_the_items_the_file_names) {
    /* Each by the row a drop keeps: the game turns that back into an id at the other end. */
    const mob_def_t *const def = &mob_defs[MOB_ID_ZOMBIE];
    REQUIRE(def->drop_count == 4u);
    for (uint32_t row = 0u; row < def->drop_count; row++) {
        const uint16_t item = mob_drops[def->first_drop + row].item;
        CHECK_EQ_U64(block_drop_items[item_stack_drop_row(item)].item, item);
    }
}

TEST(a_dead_zombie_leaves_what_the_file_says_how_often_it_says) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 17u));
    mz_floor(t, &rig);
    const mob_zombie_view_t view = mz_view(30.5f64, 30.5f64, 0.0f64);
    uint32_t flesh_by_count[3] = {0u, 0u, 0u}, iron = 0u, carrot = 0u, potato = 0u, other = 0u;
    uint64_t counted = 0u;
    const uint32_t row_flesh = item_stack_drop_row(ITEM_ID_ROTTEN_FLESH);
    const uint32_t row_iron = item_stack_drop_row(ITEM_ID_IRON_INGOT);
    const uint32_t row_carrot = item_stack_drop_row(ITEM_ID_CARROT);
    const uint32_t row_potato = item_stack_drop_row(ITEM_ID_POTATO);
    for (uint32_t death = 0u; death < MZ_DEATHS; death++) {
        const entity_handle_t handle = mz_spawn(t, &rig, 5.5f64, 8.5f64, 0.0f64);
        rig.store->borrowed_health[mz_index(t, &rig, handle)] = 0.0f32;
        (void)mob_minds_tick(rig.mob, rig.entity, &view);
        CHECK_EQ_U64(rig.store->count, 0u);
        uint32_t flesh = 0u;
        for (uint32_t i = 0u; i < rig.drops->count; i++) {
            const item_drop_t *const drop = &rig.drops->borrowed_items[i];
            CHECK_NEAR(drop->position[0], 5.5f64, 1.0f64);
            CHECK_NEAR(drop->position[1], 8.5f64, 1.0f64);
            CHECK(drop->position[2] > 0.0f64);
            counted += drop->count;
            if (drop->item == row_flesh) {
                flesh += drop->count;
            } else if (drop->item == row_iron) {
                iron += drop->count;
            } else if (drop->item == row_carrot) {
                carrot += drop->count;
            } else if (drop->item == row_potato) {
                potato += drop->count;
            } else {
                other++;
            }
        }
        REQUIRE(flesh <= 2u);
        flesh_by_count[flesh]++;
        rig.drops->count = 0u;
    }
    CHECK_EQ_U64(rig.zombie->died_count, MZ_DEATHS);
    CHECK_EQ_U64(rig.zombie->dropped_count, counted);
    CHECK_EQ_U64(other, 0u);
    /* 0, 1 or 2 alike: a third each, within 4 standard deviations and a bit. */
    for (uint32_t i = 0u; i < 3u; i++) {
        CHECK(flesh_by_count[i] > MZ_DEATHS / 3u - 170u && flesh_by_count[i] < MZ_DEATHS / 3u + 170u);
    }
    /* 0.83% of 6000 is 50, with a standard deviation of 7. */
    CHECK(iron > 20u && iron < 80u);
    CHECK(carrot > 20u && carrot < 80u);
    CHECK(potato > 20u && potato < 80u);
    mz_end(&rig);
}

TEST(only_a_dead_zombie_goes_and_nothing_else_is_touched) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 18u));
    mz_floor(t, &rig);
    enum : uint32_t { BODIES = 12u };
    entity_handle_t handles[BODIES];
    for (uint32_t i = 0u; i < BODIES; i++) {
        handles[i] = mz_spawn(t, &rig, -20.5f64 + 2.0f64 * (float64_t)i, 8.5f64, 0.0f64);
    }
    const entity_handle_t skeleton = mz_spawn_kind(t, &rig, MOB_ID_SKELETON, -20.5f64, 12.5f64, 0.0f64);
    const entity_handle_t creeper = mz_spawn_kind(t, &rig, MOB_ID_CREEPER, -20.5f64, 14.5f64, 0.0f64);
    /* Somebody's dying is not for here, nor a body that is not a mob. */
    rig.store->borrowed_health[mz_index(t, &rig, skeleton)] = 0.0f32;
    const entity_store_spawn_t plain = {
        .position = {-20.5f64, 16.5f64, 0.0f64},
        .half_width = 0.3f32,
        .height = 1.0f32,
        .health = 0.0f32,
        .kind = 0u,
    };
    const entity_handle_t none = entity_store_spawn(rig.store, &plain);
    const mob_zombie_view_t view = mz_view(30.5f64, 8.5f64, 0.0f64);

    /* Those in the middle and at either end, all on one tick. */
    rig.store->borrowed_health[mz_index(t, &rig, handles[0])] = -1.0f32;
    rig.store->borrowed_health[mz_index(t, &rig, handles[5])] = 0.0f32;
    rig.store->borrowed_health[mz_index(t, &rig, handles[11])] = 0.0f32;
    (void)mob_minds_tick(rig.mob, rig.entity, &view);
    CHECK_EQ_U64(rig.zombie->died_count, 3u);
    uint32_t index = 0u;
    for (uint32_t i = 0u; i < BODIES; i++) {
        CHECK_EQ_U64(entity_store_index_of(rig.store, handles[i], &index) ? 1u : 0u,
                     i == 0u || i == 5u || i == 11u ? 0u : 1u);
    }
    CHECK(entity_store_index_of(rig.store, skeleton, &index));
    CHECK(entity_store_index_of(rig.store, creeper, &index));
    CHECK(entity_store_index_of(rig.store, none, &index));
    CHECK(rig.store->borrowed_wish[0][index] == 0.0f64);

    /* One taken away by anything else leaves nothing. */
    const uint64_t dropped = rig.zombie->dropped_count;
    REQUIRE(entity_store_remove(rig.store, handles[1]));
    (void)mob_minds_tick(rig.mob, rig.entity, &view);
    CHECK_EQ_U64(rig.zombie->dropped_count, dropped);
    CHECK_EQ_U64(rig.zombie->died_count, 3u);
    mz_end(&rig);
}

TEST(a_slot_used_again_knows_nothing_of_the_last_one) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 19u));
    mz_floor(t, &rig);
    const mob_zombie_view_t view = mz_view(1.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t away = mz_view(40.5f64, 8.5f64, 0.0f64);
    const entity_handle_t first = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    REQUIRE(mz_notice(&rig, first, &view));
    const uint32_t slot = entity_handle_slot(first);
    REQUIRE(rig.zombie->borrowed_bodies[slot].cooldown != 0u);
    REQUIRE(entity_store_remove(rig.store, first));
    entity_handle_t again = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY && entity_handle_slot(again) != slot; i++) {
        REQUIRE(entity_store_remove(rig.store, again));
        again = mz_spawn(t, &rig, 0.5f64, 8.5f64, 0.0f64);
    }
    REQUIRE(entity_handle_slot(again) == slot && again != first);
    (void)mob_minds_tick(rig.mob, rig.entity, &away);
    CHECK_EQ_U64(rig.zombie->borrowed_bodies[slot].owner, again);
    CHECK_EQ_U64(rig.zombie->borrowed_bodies[slot].chasing, 0u);
    CHECK_EQ_U64(rig.zombie->borrowed_bodies[slot].cooldown, 0u);
    mz_end(&rig);
}

TEST(a_zombie_waiting_for_its_column_does_nothing) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 20u));
    mz_floor(t, &rig);
    const entity_handle_t handle = mz_spawn(t, &rig, 500.5f64, 8.5f64, 0.0f64);
    const mob_zombie_view_t view = mz_view(501.5f64, 8.5f64, 0.0f64);
    entity_tick(rig.entity, rig.world.map);
    const uint32_t index = mz_index(t, &rig, handle);
    REQUIRE((rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_WAITING) != 0u);
    for (uint32_t i = 0u; i < 200u; i++) {
        CHECK(mz_step(&rig, &view) == 0.0f32);
    }
    CHECK_EQ_U64(mz_body(&rig, handle)->chasing, 0u);
    CHECK_EQ_U64(rig.zombie->struck_count, 0u);
    mz_end(&rig);
}

/* ---- The same bytes, and what it costs -------------------------------------------------------------------------------- */

/* A crowd after something moving. */
static void mz_crowd_run(test_context_t *t, mz_rig_t *rig, float64_t *out, uint32_t ticks) {
    mz_floor(t, rig);
    test_world_fill(t, &rig->world, 0, 10, 0, 0, 14, 2, BLOCK_ID_STONE);
    for (uint32_t i = 0u; i < 16u; i++) {
        (void)mz_spawn(t, rig, -16.5f64 + (float64_t)(i % 4u) * 2.0f64, 2.5f64 + (float64_t)(i / 4u) * 2.0f64, 0.0f64);
    }
    mob_zombie_view_t view = mz_view(8.5f64, 8.5f64, 0.0f64);
    for (uint32_t i = 0u; i < ticks; i++) {
        view.position[1] = 8.5f64 + 6.0f64 * sin((float64_t)i * 0.02f64);
        (void)mz_step(rig, &view);
    }
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        memcpy(out + axis * 16u, rig->store->borrowed_position[axis], 16u * sizeof(float64_t));
    }
}

TEST(the_same_calls_give_the_same_bytes_with_workers_or_without_and_a_seed_others) {
    float64_t *const once = test_alloc(t, 48u * sizeof(float64_t));
    float64_t *const twice = test_alloc(t, 48u * sizeof(float64_t));
    float64_t *const workers = test_alloc(t, 48u * sizeof(float64_t));
    float64_t *const other = test_alloc(t, 48u * sizeof(float64_t));
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 21u));
    mz_crowd_run(t, &rig, once, 300u);
    const uint64_t struck = rig.zombie->struck_count;
    /* They did something for these bytes to be about. */
    CHECK(struck > 0u);
    mz_end(&rig);
    REQUIRE(mz_begin(t, &rig, 21u));
    mz_crowd_run(t, &rig, twice, 300u);
    CHECK_EQ_U64(rig.zombie->struck_count, struck);
    mz_end(&rig);
    CHECK_MEM_EQ(once, twice, 48u * sizeof(float64_t));

    job_system_t *const job = test_world_aligned(t, sizeof(job_system_t));
    thread_topology_t *const topology = test_world_aligned(t, sizeof(thread_topology_t));
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(job, topology));
    REQUIRE(mz_begin_with(t, &rig, 21u, job));
    mz_crowd_run(t, &rig, workers, 300u);
    mz_end(&rig);
    job_deinit(job);
    CHECK_MEM_EQ(once, workers, 48u * sizeof(float64_t));

    REQUIRE(mz_begin(t, &rig, 22u));
    mz_crowd_run(t, &rig, other, 300u);
    mz_end(&rig);
    CHECK(memcmp(once, other, 48u * sizeof(float64_t)) != 0);
}

TEST(a_hundred_chasing_take_no_heap_and_no_arena_and_little_time) {
    mz_rig_t rig;
    REQUIRE(mz_begin(t, &rig, 23u));
    mz_floor(t, &rig);
    test_world_fill(t, &rig.world, 10, 6, 0, 10, 12, 3, BLOCK_ID_STONE);
    for (uint32_t i = 0u; i < MZ_MANY; i++) {
        (void)mz_spawn(t, &rig, -9.5f64 + 2.0f64 * (float64_t)(i % 10u), -9.5f64 + 2.0f64 * (float64_t)(i / 10u),
                       0.0f64);
    }
    /* It goes back and forth, so that the cache keeps missing as it would. */
    mob_zombie_view_t view = mz_view(12.5f64, 8.5f64, 0.0f64);
    heap_counters_t before, after;
    heap_counters_read(&before);
    const uint64_t used = rig.mob->arena_world.used, allocs = rig.mob->arena_world.alloc_count;
    const int64_t started = time_now_ns();
    for (uint32_t i = 0u; i < MZ_SPEED_TICKS; i++) {
        view.position[1] = 8.5f64 + 12.0f64 * sin((float64_t)i * 0.05f64);
        (void)mz_step(&rig, &view);
    }
    const int64_t took = time_now_ns() - started;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(rig.mob->arena_world.used, used);
    CHECK_EQ_U64(rig.mob->arena_world.alloc_count, allocs);
    CHECK(rig.mob->arena_world.used <= (uint64_t)MOB_PATH_BYTES + (uint64_t)MOB_ZOMBIE_BYTES +
                                           (uint64_t)MOB_SKELETON_BYTES + (uint64_t)MOB_CREEPER_BYTES +
                                           (uint64_t)MOB_COMBAT_BYTES);
    uint32_t chasing = 0u;
    for (uint32_t i = 0u; i < rig.store->count; i++) {
        chasing += rig.zombie->borrowed_bodies[entity_handle_slot(rig.store->borrowed_handle[i])].chasing;
    }
    CHECK(chasing > MZ_MANY / 2u);
    printf("a hundred zombies: %.3f ms a tick, %" PRIu32 " chasing, %" PRIu64 " blows\n",
           (libc_double_t)((float64_t)took / (float64_t)MZ_SPEED_TICKS / 1.0e6f64), chasing, rig.zombie->struck_count);
    /* A floor under the phase's 5 ms a tick, which bench_mobs is for: this runs with the debug flags and on a shared machine. */
    CHECK(took / (int64_t)MZ_SPEED_TICKS < 50000000);
    mz_end(&rig);
}

/* ---- The program ------------------------------------------------------------------------------------------------------ */

TEST(the_program_at_midnight_with_nobody_moving_gets_its_player_hit) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--time-of-day",
                          (char *)"0",
                          (char *)"--no-day-cycle",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          (char *)"--ticks",
                          (char *)"12000",
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, MZ_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "zombies struck");
    CHECK_CONTAINS(run.output, "0 bytes live");
    /* Not "zombies struck 0 blows": something came, and the body felt it. */
    const char *const said = strstr(run.output, "zombies struck ");
    REQUIRE(said != nullptr);
    CHECK(said[strlen("zombies struck ")] != '0');
    const char *const hurt = strstr(run.output, "; hurt ");
    REQUIRE(hurt != nullptr);
    CHECK(hurt[strlen("; hurt ")] != '0');
    if (t->failure_count != 0u) {
        fputs(run.output, stdout);
    }
}

TEST_MAIN()
