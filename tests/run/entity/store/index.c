/* ctest run_entity_store. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "heap/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"
#include "time/index.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* What a number worked out by hand may differ by: the arithmetic of a few moves. */
#define ES_EXACT 1.0e-9f64
#define ES_NEAR_F32 1.0e-5f32
#define ES_PI 3.14159265358979f64

#define ES_WIDTH 0.3f32
#define ES_HEIGHT 1.95f32

enum : uint32_t {
    ES_RANDOM_STEPS = 200000u,
    ES_RANDOM_BODIES = 64u,
    ES_RANDOM_TICKS = 2000u,
    ES_QUERIES = 2000u,
    ES_TEXTURE_WIDTH = 64u,
    ES_TEXTURE_HEIGHT = 32u,
};

typedef struct es_rig_t {
    test_world_t world;    /* 0 */
    entity_t *entity;      /* 24 */
    entity_store_t *store; /* 32 */
    entity_grid_t *grid;   /* 40 */
} es_rig_t;
static_assert(sizeof(es_rig_t) == 48);

static bool es_begin(test_context_t *t, es_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics))) {
        return false;
    }
    entity_open(rig->entity, 77u);
    rig->store = &rig->entity->store;
    rig->grid = &rig->entity->grid;
    return true;
}

static void es_end(es_rig_t *rig) {
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

static entity_handle_t es_spawn(entity_store_t *store, float64_t x, float64_t y, float64_t z) {
    const entity_store_spawn_t spawn = {
        .position = {x, y, z},
        .half_width = ES_WIDTH,
        .height = ES_HEIGHT,
        .yaw = 0.0f32,
        .health = 20.0f32,
        .kind = 1u,
    };
    return entity_store_spawn(store, &spawn);
}

/* The index of a body that has to be alive. */
static uint32_t es_at(test_context_t *t, const entity_store_t *store, entity_handle_t handle) {
    uint32_t index = 0u;
    CHECK(entity_store_index_of(store, handle, &index));
    return index;
}

static void es_floor(test_context_t *t, es_rig_t *rig, uint16_t block) {
    test_world_fill(t, &rig->world, -16, -16, -1, 31, 31, -1, block);
}

static void es_ticks(es_rig_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        entity_store_tick(rig->store, rig->world.physics, rig->world.map);
    }
}

static bool es_has_flag(const entity_store_t *store, uint32_t index, uint8_t flag) {
    return (store->borrowed_flags[index] & flag) != 0u;
}

/* ---- The store ------------------------------------------------------------------------------------------------------ */

TEST(a_new_body_is_as_asked_and_a_handle_finds_it) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    const entity_store_spawn_t spawn = {
        .position = {1.5f64, -2.5f64, 3.0f64},
        .half_width = 0.25f32,
        .height = 1.5f32,
        .yaw = 0.5f32,
        .health = 7.0f32,
        .kind = 9u,
    };
    const entity_handle_t handle = entity_store_spawn(rig.store, &spawn);
    CHECK(handle != ENTITY_HANDLE_NONE);
    const uint32_t i = es_at(t, rig.store, handle);
    CHECK_EQ_U64(i, 0u);
    CHECK_EQ_U64(rig.store->count, 1u);
    CHECK_EQ_U64(rig.store->borrowed_kind[i], 9u);
    CHECK_NEAR(rig.store->borrowed_health[i], 7.0f32, ES_NEAR_F32);
    CHECK_NEAR(rig.store->borrowed_yaw[i], 0.5f32, ES_NEAR_F32);
    collision_box_t box;
    entity_store_box(rig.store, i, &box);
    CHECK_NEAR(box.min[0], 1.25f64, ES_EXACT);
    CHECK_NEAR(box.max[0], 1.75f64, ES_EXACT);
    CHECK_NEAR(box.min[1], -2.75f64, ES_EXACT);
    CHECK_NEAR(box.max[1], -2.25f64, ES_EXACT);
    CHECK_NEAR(box.min[2], 3.0f64, ES_EXACT);
    CHECK_NEAR(box.max[2], 4.5f64, ES_EXACT);
    /* Nothing, and a handle to nothing, is not a body. */
    uint32_t ignored = 0u;
    CHECK(!entity_store_index_of(rig.store, ENTITY_HANDLE_NONE, &ignored));
    CHECK(!entity_store_remove(rig.store, ENTITY_HANDLE_NONE));
    es_end(&rig);
}

TEST(removing_moves_every_array_of_the_last_body_and_its_handle_still_finds_it) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    const entity_handle_t first = es_spawn(s, 1.0f64, 1.0f64, 1.0f64);
    const entity_handle_t gone = es_spawn(s, 2.0f64, 2.0f64, 2.0f64);
    const entity_handle_t last = es_spawn(s, 3.0f64, 4.0f64, 5.0f64);
    const uint32_t j = es_at(t, s, last);
    /* Every array gets a number of its own, so that one left behind shows. */
    float64_t value = 10.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        s->borrowed_previous[a][j] = value++;
        s->borrowed_velocity[a][j] = value++;
    }
    s->borrowed_wish[0][j] = value++;
    s->borrowed_wish[1][j] = value++;
    s->borrowed_fall_distance[j] = value++;
    s->borrowed_landed_fall[j] = value++;
    s->borrowed_half_width[j] = 0.75f32;
    s->borrowed_height[j] = 1.25f32;
    s->borrowed_yaw[j] = 2.0f32;
    s->borrowed_health[j] = 3.0f32;
    s->borrowed_limb_phase[j] = 4.0f32;
    s->borrowed_limb_amount[j] = 5.0f32;
    s->borrowed_head_yaw[j] = 6.0f32;
    s->borrowed_head_pitch[j] = 7.0f32;
    s->borrowed_aim[j] = 8.0f32;
    s->borrowed_swell[j] = 9.0f32;
    s->borrowed_age[j] = 123u;
    s->borrowed_kind[j] = 45u;
    s->borrowed_flags[j] = ENTITY_STORE_FLAG_IN_LAVA;
    s->borrowed_swing_ticks[j] = 3u;

    CHECK(entity_store_remove(s, gone));
    CHECK(!entity_store_remove(s, gone));
    CHECK_EQ_U64(s->count, 2u);
    CHECK_EQ_U64(es_at(t, s, first), 0u);
    const uint32_t i = es_at(t, s, last);
    CHECK_EQ_U64(i, 1u);
    uint32_t ignored = 0u;
    CHECK(!entity_store_index_of(s, gone, &ignored));
    CHECK_NEAR(s->borrowed_position[0][i], 3.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_position[1][i], 4.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_position[2][i], 5.0f64, ES_EXACT);
    value = 10.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        CHECK_NEAR(s->borrowed_previous[a][i], value, ES_EXACT);
        value += 1.0f64;
        CHECK_NEAR(s->borrowed_velocity[a][i], value, ES_EXACT);
        value += 1.0f64;
    }
    CHECK_NEAR(s->borrowed_wish[0][i], value, ES_EXACT);
    CHECK_NEAR(s->borrowed_wish[1][i], value + 1.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_fall_distance[i], value + 2.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_landed_fall[i], value + 3.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_half_width[i], 0.75f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_height[i], 1.25f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_yaw[i], 2.0f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_health[i], 3.0f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_limb_phase[i], 4.0f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_limb_amount[i], 5.0f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_head_yaw[i], 6.0f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_head_pitch[i], 7.0f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_aim[i], 8.0f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_swell[i], 9.0f32, ES_NEAR_F32);
    CHECK_EQ_U64(s->borrowed_age[i], 123u);
    CHECK_EQ_U64(s->borrowed_kind[i], 45u);
    CHECK_EQ_U64(s->borrowed_flags[i], ENTITY_STORE_FLAG_IN_LAVA);
    CHECK_EQ_U64(s->borrowed_swing_ticks[i], 3u);
    CHECK_EQ_U64(s->borrowed_handle[i], last);
    CHECK_EQ_U64(s->spawned_count, 3u);
    CHECK_EQ_U64(s->removed_count, 1u);
    es_end(&rig);
}

TEST(a_full_store_refuses_and_a_freed_slot_comes_back_last_with_a_new_handle) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    entity_handle_t *const handles = test_alloc(t, ENTITY_STORE_CAPACITY * sizeof(entity_handle_t));
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        handles[i] = es_spawn(s, (float64_t)i * 0.01f64, 0.5f64, 1.0f64);
        CHECK(handles[i] != ENTITY_HANDLE_NONE);
        CHECK_EQ_U64(entity_handle_slot(handles[i]), i);
    }
    CHECK_EQ_U64(es_spawn(s, 0.0f64, 0.0f64, 0.0f64), ENTITY_HANDLE_NONE);
    CHECK_EQ_U64(s->refused_count, 1u);
    CHECK_EQ_U64(s->count, ENTITY_STORE_CAPACITY);
    /* Take two out: the ones given back come round in the order they were given. */
    CHECK(entity_store_remove(s, handles[5]));
    CHECK(entity_store_remove(s, handles[900]));
    const entity_handle_t again_a = es_spawn(s, 0.5f64, 0.5f64, 1.0f64);
    const entity_handle_t again_b = es_spawn(s, 0.5f64, 0.5f64, 1.0f64);
    CHECK_EQ_U64(entity_handle_slot(again_a), 5u);
    CHECK_EQ_U64(entity_handle_slot(again_b), 900u);
    CHECK(again_a != handles[5]);
    CHECK(again_b != handles[900]);
    uint32_t ignored = 0u;
    CHECK(!entity_store_index_of(s, handles[5], &ignored));
    CHECK(!entity_store_remove(s, handles[900]));
    CHECK_EQ_U64(es_spawn(s, 0.0f64, 0.0f64, 0.0f64), ENTITY_HANDLE_NONE);
    es_end(&rig);
}

TEST(a_generation_wraps_past_its_limit_and_never_to_the_handle_of_nothing) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    s->borrowed_slot_generation[0] = ENTITY_STORE_GENERATION_LIMIT - 1u;
    const entity_handle_t oldest = es_spawn(s, 0.5f64, 0.5f64, 1.0f64);
    CHECK_EQ_U64(entity_handle_slot(oldest), 0u);
    CHECK_EQ_U64(oldest >> ENTITY_STORE_SLOT_BITS, ENTITY_STORE_GENERATION_LIMIT - 1u);
    CHECK(entity_store_remove(s, oldest));
    CHECK_EQ_U64(s->borrowed_slot_generation[0], 1u);
    es_end(&rig);
}

TEST(a_long_random_life_keeps_every_handle_true) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    /* The model: by slot, the handle alive there, and what that body was given. */
    entity_handle_t *const alive = test_alloc(t, ENTITY_STORE_CAPACITY * sizeof(entity_handle_t));
    float64_t *const marks = test_alloc(t, ENTITY_STORE_CAPACITY * sizeof(float64_t));
    entity_handle_t stale = ENTITY_HANDLE_NONE;
    terrain_rng_t random = terrain_rng_start(5u);
    uint64_t failures = 0u;
    for (uint32_t step = 0u; step < ES_RANDOM_STEPS; step++) {
        const uint32_t count = s->count;
        /* Up and down through the store's whole size, so that it is full at times and empty at times. */
        const bool grow = (step / 3000u) % 2u == 0u;
        if (terrain_rng_next_below(&random, 4u) != 0u ? grow : !grow) {
            const float64_t mark = (float64_t)step;
            const entity_handle_t handle = es_spawn(s, mark, 0.5f64, 1.0f64);
            if (count == ENTITY_STORE_CAPACITY) {
                failures += handle == ENTITY_HANDLE_NONE ? 0u : 1u;
            } else {
                const uint32_t slot = entity_handle_slot(handle);
                failures += alive[slot] == ENTITY_HANDLE_NONE ? 0u : 1u;
                alive[slot] = handle;
                marks[slot] = mark;
            }
        } else if (count != 0u) {
            const entity_handle_t handle = s->borrowed_handle[terrain_rng_next_below(&random, count)];
            failures += entity_store_remove(s, handle) ? 0u : 1u;
            alive[entity_handle_slot(handle)] = ENTITY_HANDLE_NONE;
            stale = handle;
        }
        /* Checking every body each step would be quadratic: one at random, and everything now and then. */
        uint32_t index = 0u;
        if (stale != ENTITY_HANDLE_NONE && entity_store_index_of(s, stale, &index)) {
            failures++;
        }
        const bool all = step % 5000u == 0u;
        for (uint32_t slot = all ? 0u : terrain_rng_next_below(&random, ENTITY_STORE_CAPACITY);
             slot < ENTITY_STORE_CAPACITY; slot += all ? 1u : ENTITY_STORE_CAPACITY) {
            if (alive[slot] != ENTITY_HANDLE_NONE) {
                if (!entity_store_index_of(s, alive[slot], &index) || s->borrowed_position[0][index] != marks[slot]) {
                    failures++;
                }
            }
        }
    }
    CHECK_EQ_U64(failures, 0u);
    CHECK_EQ_U64(s->spawned_count - s->removed_count, s->count);
    CHECK(s->refused_count > 0u);
    es_end(&rig);
}

/* ---- Moving through the world --------------------------------------------------------------------------------------- */

TEST(a_fall_by_hand_and_a_landing_with_what_it_adds_up_to) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    const entity_handle_t handle = es_spawn(s, 0.5f64, 0.5f64, 20.0f64);
    const uint32_t i = es_at(t, s, handle);
    /* Tick 1 moves by 0, and leaves (0 - 0.08) * 0.98, and so on. */
    float64_t velocity = 0.0f64, height = 20.0f64;
    for (uint32_t tick = 0u; tick < 3u; tick++) {
        es_ticks(&rig, 1u);
        height += velocity;
        velocity = (velocity - 0.08f64) * 0.98f64;
        CHECK_NEAR(s->borrowed_position[2][i], height, ES_EXACT);
        CHECK_NEAR(s->borrowed_velocity[2][i], velocity, ES_EXACT);
        CHECK_NEAR(s->borrowed_previous[2][i], height - (tick == 0u ? 0.0f64 : (velocity / 0.98f64 + 0.08f64)),
                   ES_EXACT);
        CHECK(!es_has_flag(s, i, ENTITY_STORE_FLAG_ON_GROUND));
    }
    CHECK_NEAR(s->borrowed_fall_distance[i], 20.0f64 - height, ES_EXACT);
    float64_t landed = 0.0f64;
    for (uint32_t tick = 0u; tick < 200u; tick++) {
        es_ticks(&rig, 1u);
        landed = s->borrowed_landed_fall[i] != 0.0f64 ? s->borrowed_landed_fall[i] : landed;
    }
    CHECK_NEAR(landed, 20.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_position[2][i], 0.0f64, ES_EXACT);
    CHECK(es_has_flag(s, i, ENTITY_STORE_FLAG_ON_GROUND));
    CHECK_NEAR(s->borrowed_landed_fall[i], 0.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_fall_distance[i], 0.0f64, ES_EXACT);
    CHECK_EQ_U64(s->borrowed_age[i], 203u);
    es_end(&rig);
}

TEST(a_wish_is_the_speed_it_comes_to_on_plain_ground_and_on_ice_it_slides_on) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    const uint32_t i = es_at(t, s, es_spawn(s, -10.0f64, 0.5f64, 0.0f64));
    es_ticks(&rig, 2u);
    entity_store_wish_set(s, i, 0.1f64, 0.0f64);
    es_ticks(&rig, 60u);
    const float64_t before = s->borrowed_position[0][i];
    es_ticks(&rig, 1u);
    CHECK_NEAR(s->borrowed_position[0][i] - before, 0.1f64, 1.0e-6f64);
    CHECK_NEAR(s->borrowed_position[1][i], 0.5f64, ES_EXACT);
    /* Stopped, it stops within a few ticks on stone. */
    entity_store_wish_set(s, i, 0.0f64, 0.0f64);
    es_ticks(&rig, 40u);
    const float64_t stone_stop = s->borrowed_position[0][i];
    es_ticks(&rig, 1u);
    CHECK_NEAR(s->borrowed_position[0][i], stone_stop, ES_EXACT);
    CHECK_NEAR(s->borrowed_velocity[0][i], 0.0f64, ES_EXACT);

    /* The same push, on ice: by the sum of what each tick keeps, it goes a long way further. */
    es_floor(t, &rig, BLOCK_ID_ICE);
    s->borrowed_position[0][i] = -14.0f64;
    s->borrowed_velocity[0][i] = 0.2f64;
    es_ticks(&rig, 200u);
    const float64_t kept = 0.98f64 * 0.91f64;
    float64_t slid = 0.0f64;
    for (float64_t speed = 0.2f64; speed >= 0.003f64; speed *= kept) {
        slid += speed; /* under 0.003 it stops, and the last move is the one before */
    }
    CHECK_NEAR(s->borrowed_position[0][i] - -14.0f64, slid, ES_EXACT);
    CHECK(slid > 1.8f64 * 0.2f64 / (1.0f64 - 0.546f64));
    es_end(&rig);
}

TEST(a_wall_stops_it_and_says_so_a_slab_is_walked_up_and_a_jump_clears_a_block) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    /* A wall two high, at x = 5. */
    test_world_fill(t, &rig.world, 5, -8, 0, 5, 8, 1, BLOCK_ID_STONE);
    const uint32_t w = es_at(t, s, es_spawn(s, 3.5f64, 0.5f64, 0.0f64));
    es_ticks(&rig, 2u);
    entity_store_wish_set(s, w, 0.2f64, 0.0f64);
    es_ticks(&rig, 40u);
    CHECK_NEAR(s->borrowed_position[0][w], 5.0f64 - (float64_t)ES_WIDTH, ES_EXACT);
    CHECK_NEAR(s->borrowed_position[2][w], 0.0f64, ES_EXACT);
    CHECK(es_has_flag(s, w, ENTITY_STORE_FLAG_HIT_WALL));
    CHECK_NEAR(s->borrowed_velocity[0][w], 0.0f64, ES_EXACT);
    CHECK(entity_store_remove(s, s->borrowed_handle[w]));

    /* A slab, half high: walked up with no jump. */
    test_world_fill(t, &rig.world, 0, 10, 0, 3, 10, 0, BLOCK_ID_STONE_SLAB);
    const uint32_t l = es_at(t, s, es_spawn(s, 1.5f64, 8.5f64, 0.0f64));
    es_ticks(&rig, 2u);
    entity_store_wish_set(s, l, 0.0f64, 0.2f64);
    es_ticks(&rig, 12u);
    CHECK_NEAR(s->borrowed_position[2][l], 0.5f64, ES_EXACT);
    CHECK(s->borrowed_position[1][l] > 10.0f64);
    CHECK(es_has_flag(s, l, ENTITY_STORE_FLAG_ON_GROUND));
    CHECK(entity_store_remove(s, s->borrowed_handle[l]));

    /* A block, whole: no. And with a jump, yes: 0.42 is good for about 1.25. */
    test_world_fill(t, &rig.world, -8, -6, 0, -4, 8, 0, BLOCK_ID_STONE);
    const uint32_t b = es_at(t, s, es_spawn(s, -6.0f64, -8.5f64, 0.0f64));
    es_ticks(&rig, 2u);
    entity_store_wish_set(s, b, 0.0f64, 0.2f64);
    es_ticks(&rig, 30u);
    CHECK_NEAR(s->borrowed_position[1][b], -6.0f64 - (float64_t)ES_WIDTH, ES_EXACT);
    CHECK_NEAR(s->borrowed_position[2][b], 0.0f64, ES_EXACT);
    entity_store_jump(s, b);
    es_ticks(&rig, 1u);
    CHECK(!es_has_flag(s, b, ENTITY_STORE_FLAG_JUMP));
    CHECK(s->borrowed_position[2][b] > 0.4f64);
    es_ticks(&rig, 20u);
    CHECK_NEAR(s->borrowed_position[2][b], 1.0f64, ES_EXACT);
    CHECK(s->borrowed_position[1][b] > -5.0f64);
    es_end(&rig);
}

TEST(a_jump_is_used_up_in_the_air_with_no_second_one) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    const uint32_t i = es_at(t, s, es_spawn(s, 0.5f64, 0.5f64, 10.0f64));
    es_ticks(&rig, 3u);
    const float64_t velocity = s->borrowed_velocity[2][i];
    entity_store_jump(s, i);
    es_ticks(&rig, 1u);
    CHECK(!es_has_flag(s, i, ENTITY_STORE_FLAG_JUMP));
    CHECK_NEAR(s->borrowed_velocity[2][i], (velocity - 0.08f64) * 0.98f64, ES_EXACT);
    es_end(&rig);
}

TEST(water_breaks_a_fall_and_slows_lava_more_and_a_bank_is_hopped_out_of) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 0, 0, 0, 8, 8, 9, BLOCK_ID_WATER);
    test_world_fill(t, &rig.world, 16, 16, 0, 24, 24, 9, BLOCK_ID_LAVA);
    entity_store_t *const s = rig.store;
    const uint32_t i = es_at(t, s, es_spawn(s, 4.5f64, 4.5f64, 40.0f64));
    es_ticks(&rig, 70u);
    CHECK(es_has_flag(s, i, ENTITY_STORE_FLAG_IN_WATER));
    CHECK_NEAR(s->borrowed_fall_distance[i], 0.0f64, ES_EXACT);
    es_ticks(&rig, 200u);
    /* It has sunk to the bottom, and has never landed as a fall. */
    CHECK_NEAR(s->borrowed_position[2][i], 0.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_landed_fall[i], 0.0f64, ES_EXACT);
    /* Its sinking speed, once it has one: 0.02 a tick, with 0.8 kept. */
    s->borrowed_position[2][i] = 5.0f64;
    s->borrowed_velocity[2][i] = 0.0f64;
    es_ticks(&rig, 1u);
    CHECK_NEAR(s->borrowed_velocity[2][i], -0.02f64, ES_EXACT);
    es_ticks(&rig, 1u);
    CHECK_NEAR(s->borrowed_velocity[2][i], -0.02f64 * 0.8f64 - 0.02f64, ES_EXACT);
    /* Rising, by jumping every tick: 0.04 up against that. */
    for (uint32_t tick = 0u; tick < 30u; tick++) {
        entity_store_jump(s, i);
        es_ticks(&rig, 1u);
    }
    CHECK(s->borrowed_position[2][i] > 5.0f64);

    const uint32_t l = es_at(t, s, es_spawn(s, 20.5f64, 20.5f64, 8.0f64));
    es_ticks(&rig, 2u);
    CHECK(es_has_flag(s, l, ENTITY_STORE_FLAG_IN_LAVA));
    CHECK(!es_has_flag(s, l, ENTITY_STORE_FLAG_IN_WATER));
    s->borrowed_velocity[0][l] = 1.0f64;
    es_ticks(&rig, 1u);
    CHECK_NEAR(s->borrowed_velocity[0][l], 0.5f64, ES_EXACT);
    CHECK(entity_store_remove(s, s->borrowed_handle[l]));

    /* In a pool one deep, rising by jumping and swimming at the wall that ends at the top of it. At the bottom the wall is still in
     * the way, and near the top it is hopped over. */
    test_world_fill(t, &rig.world, -8, 0, 0, -3, 4, 0, BLOCK_ID_WATER);
    test_world_fill(t, &rig.world, -2, 0, 0, -2, 4, 0, BLOCK_ID_STONE);
    const uint32_t h = es_at(t, s, es_spawn(s, -4.0f64, 2.5f64, 0.0f64));
    entity_store_wish_set(s, h, 0.5f64, 0.0f64);
    es_ticks(&rig, 20u);
    CHECK_NEAR(s->borrowed_position[0][h], -2.0f64 - (float64_t)ES_WIDTH, ES_EXACT);
    CHECK_NEAR(s->borrowed_position[2][h], 0.0f64, ES_EXACT);
    float64_t highest = 0.0f64;
    for (uint32_t tick = 0u; tick < 40u; tick++) {
        entity_store_jump(s, h);
        es_ticks(&rig, 1u);
        highest = s->borrowed_position[2][h] > highest ? s->borrowed_position[2][h] : highest;
    }
    CHECK(highest >= 1.0f64);
    CHECK(s->borrowed_position[0][h] > -2.0f64);
    es_end(&rig);
}

TEST(a_ladder_holds_a_fall_and_climbs_when_walked_into) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    test_world_fill(t, &rig.world, 2, 2, 0, 2, 2, 19, BLOCK_ID_LADDER);
    const uint32_t i = es_at(t, s, es_spawn(s, 2.5f64, 2.5f64, 15.0f64));
    s->borrowed_velocity[2][i] = -2.0f64;
    es_ticks(&rig, 1u);
    CHECK(es_has_flag(s, i, ENTITY_STORE_FLAG_CLIMBING));
    CHECK_NEAR(s->borrowed_position[2][i], 15.0f64 - 0.15f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_fall_distance[i], 0.0f64, ES_EXACT);
    /* With a wall to walk into, it goes up. */
    test_world_fill(t, &rig.world, 3, 2, 0, 3, 2, 30, BLOCK_ID_STONE);
    const float64_t start = s->borrowed_position[2][i];
    entity_store_wish_set(s, i, 0.2f64, 0.0f64);
    es_ticks(&rig, 20u);
    CHECK(s->borrowed_position[2][i] > start + 1.0f64);
    es_end(&rig);
}

TEST(a_body_over_a_column_not_loaded_waits_exactly_as_it_was) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    /* Radius 3 reaches up to 64 along an axis: past it is not there. */
    const uint32_t i = es_at(t, s, es_spawn(s, 200.5f64, 0.5f64, 40.0f64));
    s->borrowed_velocity[0][i] = 1.0f64;
    s->borrowed_velocity[2][i] = -1.0f64;
    entity_store_jump(s, i);
    es_ticks(&rig, 50u);
    CHECK_NEAR(s->borrowed_position[0][i], 200.5f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_position[2][i], 40.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_velocity[0][i], 1.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_velocity[2][i], -1.0f64, ES_EXACT);
    CHECK_EQ_U64(s->borrowed_age[i], 0u);
    CHECK(es_has_flag(s, i, ENTITY_STORE_FLAG_WAITING));
    CHECK(es_has_flag(s, i, ENTITY_STORE_FLAG_JUMP));
    /* Half over the edge is not over what is loaded either, and one walking to the edge stops at it. */
    CHECK(entity_store_remove(s, s->borrowed_handle[i]));
    es_floor(t, &rig, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 32, 0, -1, 63, 0, -1, BLOCK_ID_STONE);
    const uint32_t w = es_at(t, s, es_spawn(s, 62.5f64, 0.5f64, 0.0f64));
    es_ticks(&rig, 2u);
    entity_store_wish_set(s, w, 0.2f64, 0.0f64);
    es_ticks(&rig, 40u);
    CHECK_NEAR(s->borrowed_position[0][w], 64.0f64 - (float64_t)ES_WIDTH, ES_EXACT);
    CHECK(!es_has_flag(s, w, ENTITY_STORE_FLAG_WAITING));
    es_end(&rig);
}

TEST(no_body_ever_ends_a_tick_inside_anything_solid) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    terrain_rng_t random = terrain_rng_start(11u);
    /* Walls, pillars, slabs and pools, at random. */
    for (uint32_t i = 0u; i < 300u; i++) {
        const int32_t x = (int32_t)terrain_rng_next_below(&random, 48u) - 16;
        const int32_t y = (int32_t)terrain_rng_next_below(&random, 48u) - 16;
        const int32_t tall = (int32_t)terrain_rng_next_below(&random, 3u);
        const uint32_t pick = terrain_rng_next_below(&random, 4u);
        const uint16_t block = pick == 0u ? BLOCK_ID_STONE_SLAB : pick == 1u ? BLOCK_ID_WATER : BLOCK_ID_STONE;
        test_world_fill(t, &rig.world, x, y, 0, x, y, block == BLOCK_ID_STONE ? tall : 0, block);
    }
    for (uint32_t i = 0u; i < ES_RANDOM_BODIES; i++) {
        const float64_t x = -14.0f64 + terrain_rng_next_f64(&random) * 44.0f64;
        const float64_t y = -14.0f64 + terrain_rng_next_f64(&random) * 44.0f64;
        (void)es_spawn(s, x, y, 4.0f64);
    }
    uint64_t inside = 0u;
    for (uint32_t tick = 0u; tick < ES_RANDOM_TICKS; tick++) {
        for (uint32_t i = 0u; i < s->count; i++) {
            if (tick % 20u == 0u) {
                const float64_t angle = terrain_rng_next_f64(&random) * 2.0f64 * ES_PI;
                entity_store_wish_set(s, i, cos(angle) * 0.3f64, sin(angle) * 0.3f64);
            }
            if ((s->borrowed_flags[i] & ENTITY_STORE_FLAG_HIT_WALL) != 0u) {
                entity_store_jump(s, i);
            }
        }
        entity_tick(rig.entity, rig.world.map);
        for (uint32_t i = 0u; i < s->count; i++) {
            collision_box_t box;
            entity_store_box(s, i, &box);
            inside += physics_box_is_clear(rig.world.physics, rig.world.map, &box) ? 0u : 1u;
        }
    }
    CHECK_EQ_U64(inside, 0u);
    /* And they did get somewhere. */
    uint32_t moved = 0u;
    for (uint32_t i = 0u; i < s->count; i++) {
        moved += s->borrowed_age[i] == ES_RANDOM_TICKS && s->borrowed_limb_phase[i] != 0.0f32 ? 1u : 0u;
    }
    CHECK(moved > ES_RANDOM_BODIES / 2u);
    es_end(&rig);
}

/* ---- The grid ------------------------------------------------------------------------------------------------------- */

static uint32_t es_query_box(es_rig_t *rig, float64_t x0, float64_t y0, float64_t z0, float64_t x1, float64_t y1,
                             float64_t z1, entity_handle_t *out, uint32_t max) {
    const collision_box_t box = {.min = {x0, y0, z0}, .max = {x1, y1, z1}};
    return entity_grid_query(rig->grid, rig->store, &box, out, max);
}

TEST(a_query_finds_what_overlaps_and_not_what_only_touches_in_any_cell) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    const entity_handle_t a = es_spawn(s, 0.5f64, 0.5f64, 0.0f64);
    const entity_handle_t b = es_spawn(s, -7.5f64, -7.5f64, -3.0f64);
    entity_grid_build(rig.grid, s);
    entity_handle_t out[8];
    /* Into it from each side, then just off it. */
    CHECK_EQ_U64(es_query_box(&rig, 0.7f64, 0.5f64, 0.5f64, 2.0f64, 0.6f64, 0.6f64, out, 8u), 1u);
    CHECK_EQ_U64(out[0], a);
    const float64_t edge = 0.5f64 + (float64_t)ES_WIDTH, other_edge = 0.5f64 - (float64_t)ES_WIDTH;
    CHECK_EQ_U64(es_query_box(&rig, edge, 0.5f64, 0.5f64, 2.0f64, 0.6f64, 0.6f64, out, 8u), 0u);
    CHECK_EQ_U64(es_query_box(&rig, -3.0f64, 0.5f64, 0.5f64, other_edge, 0.6f64, 0.6f64, out, 8u), 0u);
    CHECK_EQ_U64(es_query_box(&rig, -3.0f64, 0.5f64, 0.5f64, 0.3f64, 0.6f64, 0.6f64, out, 8u), 1u);
    /* Exactly at its top is touching, whatever cell that is. */
    const float64_t top = (float64_t)ES_HEIGHT;
    CHECK_EQ_U64(es_query_box(&rig, 0.4f64, 0.4f64, top, 0.6f64, 0.6f64, 9.0f64, out, 8u), 0u);
    CHECK_EQ_U64(es_query_box(&rig, 0.4f64, 0.4f64, top - 0.01f64, 0.6f64, 0.6f64, 9.0f64, out, 8u), 1u);
    /* In the negative cells. */
    CHECK_EQ_U64(es_query_box(&rig, -8.0f64, -8.0f64, -3.0f64, -7.0f64, -7.0f64, -2.0f64, out, 8u), 1u);
    CHECK_EQ_U64(out[0], b);
    /* Both, and a list too short for both says how many there were. */
    CHECK_EQ_U64(es_query_box(&rig, -9.0f64, -9.0f64, -9.0f64, 9.0f64, 9.0f64, 9.0f64, out, 8u), 2u);
    entity_handle_t only[1] = {ENTITY_HANDLE_NONE};
    CHECK_EQ_U64(es_query_box(&rig, -9.0f64, -9.0f64, -9.0f64, 9.0f64, 9.0f64, 9.0f64, only, 1u), 2u);
    CHECK(only[0] == a || only[0] == b);
    es_end(&rig);
}

TEST(a_body_is_found_from_a_cell_its_box_reaches_into_but_its_middle_is_not_in) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    /* 8 across, its middle at x = 2, so it reaches from -2 to 6, three cells from its own. */
    const entity_store_spawn_t wide = {
        .position = {2.0f64, 2.0f64, 0.0f64},
        .half_width = 4.0f32,
        .height = 8.0f32,
        .health = 1.0f32,
    };
    const entity_handle_t a = entity_store_spawn(s, &wide);
    (void)es_spawn(s, 40.0f64, 40.0f64, 0.0f64); /* far off, and with it the cells near it are few */
    entity_grid_build(rig.grid, s);
    entity_handle_t out[4];
    CHECK_EQ_U64(es_query_box(&rig, 5.5f64, 5.5f64, 7.5f64, 5.9f64, 5.9f64, 7.9f64, out, 4u), 1u);
    CHECK_EQ_U64(out[0], a);
    CHECK_EQ_U64(es_query_box(&rig, -1.9f64, -1.9f64, 0.1f64, -1.8f64, -1.8f64, 0.2f64, out, 4u), 1u);
    CHECK_EQ_U64(es_query_box(&rig, 6.1f64, 2.0f64, 1.0f64, 6.2f64, 2.1f64, 1.1f64, out, 4u), 0u);
    es_end(&rig);
}

TEST(what_it_answers_is_of_when_it_was_built_and_the_dead_are_not_in_it) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    const entity_handle_t a = es_spawn(s, 0.5f64, 0.5f64, 0.0f64);
    const entity_handle_t b = es_spawn(s, 1.5f64, 0.5f64, 0.0f64);
    entity_grid_build(rig.grid, s);
    /* a moves away, and it is still found where it was, not where it is; and one new one is not found. */
    s->borrowed_position[0][es_at(t, s, a)] = 90.0f64;
    (void)es_spawn(s, 0.5f64, 1.5f64, 0.0f64);
    entity_handle_t out[4];
    CHECK_EQ_U64(es_query_box(&rig, 0.0f64, 0.0f64, 0.0f64, 3.0f64, 3.0f64, 3.0f64, out, 4u), 2u);
    CHECK_EQ_U64(es_query_box(&rig, 89.0f64, 0.0f64, 0.0f64, 91.0f64, 3.0f64, 3.0f64, out, 4u), 0u);
    /* The removed one is gone, and the last, moved to its index, is still right. */
    CHECK(entity_store_remove(s, a));
    CHECK_EQ_U64(es_query_box(&rig, 0.0f64, 0.0f64, 0.0f64, 3.0f64, 3.0f64, 3.0f64, out, 4u), 1u);
    CHECK_EQ_U64(out[0], b);
    /* Built again, it has all of them where they are. */
    entity_grid_build(rig.grid, s);
    CHECK_EQ_U64(es_query_box(&rig, 0.0f64, 0.0f64, 0.0f64, 3.0f64, 3.0f64, 3.0f64, out, 4u), 2u);
    es_end(&rig);
}

/* The sort of any two lists, to compare them. */
static void es_sort(entity_handle_t *list, uint32_t count) {
    for (uint32_t i = 1u; i < count; i++) {
        const entity_handle_t value = list[i];
        uint32_t at = i;
        while (at > 0u && list[at - 1u] > value) {
            list[at] = list[at - 1u];
            at--;
        }
        list[at] = value;
    }
}

TEST(queries_agree_with_looking_at_every_body_at_every_size) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    terrain_rng_t random = terrain_rng_start(21u);
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        const entity_store_spawn_t spawn = {
            .position = {-60.0f64 + terrain_rng_next_f64(&random) * 120.0f64,
                         -60.0f64 + terrain_rng_next_f64(&random) * 120.0f64,
                         -20.0f64 + terrain_rng_next_f64(&random) * 40.0f64},
            .half_width = 0.1f32 + terrain_rng_next_f32(&random) * 1.5f32,
            .height = 0.1f32 + terrain_rng_next_f32(&random) * 4.0f32,
            .health = 1.0f32,
        };
        (void)entity_store_spawn(s, &spawn);
    }
    for (uint32_t i = 0u; i < 100u; i++) {
        CHECK(entity_store_remove(s, s->borrowed_handle[terrain_rng_next_below(&random, s->count)]));
    }
    entity_grid_build(rig.grid, s);
    for (uint32_t i = 0u; i < 100u; i++) {
        (void)es_spawn(s, 0.0f64, 0.0f64, 0.0f64); /* not built yet, so not found */
        CHECK(entity_store_remove(s, s->borrowed_handle[s->count - 1u]));
    }
    entity_handle_t *const got = test_alloc(t, ENTITY_STORE_CAPACITY * sizeof(entity_handle_t));
    entity_handle_t *const want = test_alloc(t, ENTITY_STORE_CAPACITY * sizeof(entity_handle_t));
    uint64_t found_total = 0u;
    for (uint32_t q = 0u; q < ES_QUERIES; q++) {
        const float64_t size = q % 10u == 0u ? 300.0f64 : (q % 2u == 0u ? 30.0f64 : 3.0f64);
        collision_box_t box;
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            const float64_t middle = -70.0f64 + terrain_rng_next_f64(&random) * 140.0f64;
            const float64_t half = terrain_rng_next_f64(&random) * size * 0.5f64;
            box.min[a] = middle - half;
            box.max[a] = middle + half;
        }
        const uint32_t got_count = entity_grid_query(rig.grid, s, &box, got, ENTITY_STORE_CAPACITY);
        uint32_t want_count = 0u;
        for (uint32_t i = 0u; i < s->count; i++) {
            collision_box_t other;
            entity_store_box(s, i, &other);
            if (collision_overlaps_any(&other, 1u, &box)) {
                want[want_count++] = s->borrowed_handle[i];
            }
        }
        CHECK_EQ_U64(got_count, want_count);
        if (got_count == want_count) {
            es_sort(got, got_count);
            es_sort(want, want_count);
            CHECK_MEM_EQ(got, want, (uint64_t)got_count * sizeof(entity_handle_t));
        }
        found_total += got_count;
    }
    /* All of it was really tested: both ways of answering, and answers with something in them. */
    CHECK(rig.grid->cell_walks > 100u);
    CHECK(rig.grid->list_walks > 100u);
    CHECK(found_total > ES_QUERIES);
    es_end(&rig);
}

TEST(a_box_of_any_size_gets_an_answer) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    (void)es_spawn(s, 0.5f64, 0.5f64, 0.0f64);
    entity_grid_build(rig.grid, s);
    entity_handle_t out[2];
    const float64_t huge = 1.0e300f64;
    CHECK_EQ_U64(es_query_box(&rig, -huge, -huge, -huge, huge, huge, huge, out, 2u), 1u);
    CHECK_EQ_U64(es_query_box(&rig, -(float64_t)INFINITY, 0.0f64, 0.0f64, (float64_t)INFINITY, 1.0f64, 1.0f64, out, 2u),
                 1u);
    CHECK_EQ_U64(es_query_box(&rig, (float64_t)NAN, 0.0f64, 0.0f64, 1.0f64, 1.0f64, 1.0f64, out, 2u), 1u);
    /* A long thin box at the far end of what a key holds, with nothing there. */
    CHECK_EQ_U64(es_query_box(&rig, 1.0e8f64, 1.0e8f64, 0.0f64, 1.0e8f64 + 1.0f64, 1.0e8f64 + 1.0f64, 1.0f64, out, 2u),
                 0u);
    es_end(&rig);
}

/* ---- Pushing apart -------------------------------------------------------------------------------------------------- */

TEST(two_that_overlap_are_pushed_apart_by_the_published_shove_and_two_that_do_not_are_not) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    const uint32_t a = es_at(t, s, es_spawn(s, 0.5f64, 0.5f64, 0.0f64));
    const uint32_t b = es_at(t, s, es_spawn(s, 0.75f64, 0.5f64, 0.0f64));
    const uint32_t c = es_at(t, s, es_spawn(s, 9.5f64, 9.5f64, 0.0f64));
    const uint32_t d = es_at(t, s, es_spawn(s, 9.5f64 + 2.0f64 * (float64_t)ES_WIDTH, 9.5f64, 0.0f64)); /* touching */
    entity_grid_build(rig.grid, s);
    entity_grid_push_apart(rig.grid, s);
    /* By hand: 0.25 apart, so the root is 0.5, the way is 0.25 / 0.5 = 0.5, and it is not past a block: 0.5 * 0.05. */
    CHECK_NEAR(s->borrowed_velocity[0][b], 0.025f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_velocity[0][a], -0.025f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_velocity[1][a], 0.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_velocity[0][c], 0.0f64, ES_EXACT);
    CHECK_NEAR(s->borrowed_velocity[0][d], 0.0f64, ES_EXACT);
    CHECK_EQ_U64(rig.grid->pushes, 1u);
    es_end(&rig);
}

TEST(two_in_the_very_same_place_have_a_way_apart_that_is_always_the_same) {
    float64_t first = 0.0f64;
    for (uint32_t run = 0u; run < 2u; run++) {
        es_rig_t rig;
        REQUIRE(es_begin(t, &rig));
        entity_store_t *const s = rig.store;
        const uint32_t a = es_at(t, s, es_spawn(s, 0.5f64, 0.5f64, 0.0f64));
        const uint32_t b = es_at(t, s, es_spawn(s, 0.5f64, 0.5f64, 0.0f64));
        entity_grid_build(rig.grid, s);
        entity_grid_push_apart(rig.grid, s);
        const float64_t x = s->borrowed_velocity[0][b], y = s->borrowed_velocity[1][b];
        CHECK_NEAR(sqrt(x * x + y * y), 0.05f64, ES_EXACT);
        CHECK_NEAR(s->borrowed_velocity[0][a], -x, ES_EXACT);
        CHECK_NEAR(s->borrowed_velocity[1][a], -y, ES_EXACT);
        if (run == 1u) {
            CHECK_NEAR(x, first, 0.0f64);
        }
        first = x;
        es_end(&rig);
    }
}

TEST(a_crowd_is_shoved_within_its_limit_and_one_waiting_is_left_alone) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    entity_store_t *const s = rig.store;
    for (uint32_t i = 0u; i < 500u; i++) {
        (void)es_spawn(s, 1.0f64 + (float64_t)i * 0.0005f64, 1.0f64, 0.0f64);
    }
    entity_grid_build(rig.grid, s);
    entity_grid_push_apart(rig.grid, s);
    /* Every pair would be 124750. Each of the first looks for no more than the limit, and those near the end have few left. */
    CHECK(rig.grid->pushes <= 500u * ENTITY_GRID_PUSHES_MAX);
    CHECK(rig.grid->pushes >= (500u - ENTITY_GRID_PUSHES_MAX) * ENTITY_GRID_PUSHES_MAX);
    /* Nothing pushed is lost or made: every shove is on two, opposite ways. */
    float64_t sum = 0.0f64;
    for (uint32_t i = 0u; i < s->count; i++) {
        sum += s->borrowed_velocity[0][i];
    }
    CHECK_NEAR(sum, 0.0f64, 1.0e-9f64);
    es_end(&rig);

    es_rig_t other;
    REQUIRE(es_begin(t, &other));
    entity_store_t *const o = other.store;
    const uint32_t a = es_at(t, o, es_spawn(o, 200.5f64, 0.5f64, 0.0f64));
    const uint32_t b = es_at(t, o, es_spawn(o, 200.6f64, 0.5f64, 0.0f64));
    es_ticks(&other, 1u);
    CHECK(es_has_flag(o, a, ENTITY_STORE_FLAG_WAITING));
    entity_grid_build(other.grid, o);
    entity_grid_push_apart(other.grid, o);
    CHECK_NEAR(o->borrowed_velocity[0][a], 0.0f64, ES_EXACT);
    CHECK_NEAR(o->borrowed_velocity[0][b], 0.0f64, ES_EXACT);
    es_end(&other);
}

TEST(a_pile_comes_apart_in_a_running_world) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    for (uint32_t i = 0u; i < 16u; i++) {
        (void)es_spawn(s, 8.5f64, 8.5f64, 0.0f64);
    }
    for (uint32_t tick = 0u; tick < 400u; tick++) {
        entity_tick(rig.entity, rig.world.map);
    }
    /* Count who still overlaps anyone: few, if any. A ring of them is not at rest, so a few may still be touching at a tick. */
    entity_handle_t near[16];
    uint32_t overlapping = 0u;
    entity_grid_build(rig.grid, s);
    for (uint32_t i = 0u; i < s->count; i++) {
        collision_box_t box;
        entity_store_box(s, i, &box);
        overlapping += entity_grid_query(rig.grid, s, &box, near, 16u) > 1u ? 1u : 0u;
        CHECK_NEAR(s->borrowed_position[2][i], 0.0f64, ES_EXACT);
    }
    CHECK(overlapping <= 4u);
    es_end(&rig);
}

/* ---- Animation ------------------------------------------------------------------------------------------------------ */

static void es_state_rest(entity_animation_state_t *state) {
    *state = (entity_animation_state_t){.yaw = 0.0f32};
}

/* The whole of a part's unwrap, as a box-mapped skin lays it out, is on its texture. */
TEST(every_part_of_every_rig_is_inside_its_texture_and_stands_on_the_ground) {
    for (uint32_t r = 0u; r < ENTITY_ANIMATION_RIG_COUNT; r++) {
        const entity_animation_rig_t *const rig = entity_animation_rig((entity_animation_rig_id_t)r);
        CHECK_EQ_U64(rig->texture_width, ES_TEXTURE_WIDTH);
        CHECK_EQ_U64(rig->texture_height, ES_TEXTURE_HEIGHT);
        CHECK(rig->part_count > 0u && rig->part_count <= ENTITY_ANIMATION_PARTS_MAX);
        float32_t lowest = 1000.0f32;
        for (uint32_t p = 0u; p < rig->part_count; p++) {
            const entity_animation_part_t *const part = &rig->borrowed_parts[p];
            const float32_t across = part->size[ENTITY_ANIMATION_LEFT];
            const float32_t deep = part->size[ENTITY_ANIMATION_FORWARD];
            const float32_t tall = part->size[ENTITY_ANIMATION_UP];
            CHECK((float32_t)part->texture_u + 2.0f32 * (across + deep) <= (float32_t)rig->texture_width);
            CHECK((float32_t)part->texture_v + deep + tall <= (float32_t)rig->texture_height);
            const float32_t bottom = part->pivot[2] + part->offset[2] - tall * 0.5f32;
            lowest = bottom < lowest ? bottom : lowest;
        }
        CHECK_NEAR(lowest, 0.0f32, ES_NEAR_F32);
    }
}

TEST(a_zombie_standing_still_by_hand) {
    entity_animation_state_t state;
    es_state_rest(&state);
    const float64_t at[COLLISION_AXES] = {10.0f64, 20.0f64, 30.0f64};
    entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
    CHECK_EQ_U64(entity_animation_pose(entity_animation_rig(ENTITY_ANIMATION_RIG_ZOMBIE), &state, at, poses), 6u);
    /* The head, from 24 to 32, up the middle. */
    CHECK_NEAR(poses[0].center[0], 10.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[0].center[1], 20.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[0].center[2], 30.0f64 + 28.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[0].half[0], 0.25f32, ES_NEAR_F32);
    /* The body, from 12 to 24. */
    CHECK_NEAR(poses[1].center[2], 30.0f64 + 18.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[1].half[ENTITY_ANIMATION_UP], 6.0f32 / 16.0f32, ES_NEAR_F32);
    CHECK_NEAR(poses[1].half[ENTITY_ANIMATION_FORWARD], 2.0f32 / 16.0f32, ES_NEAR_F32);
    /* An arm, straight out from its shoulder at height 22 and 5 + 1 to the left: 4 forward of it. */
    CHECK_NEAR(poses[2].center[0], 10.0f64 + 4.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[2].center[1], 20.0f64 + 6.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[2].center[2], 30.0f64 + 22.0f64 / 16.0f64, 1.0e-6f64);
    /* What is 12 tall, held level, reaches 6 forward and 2 up. */
    CHECK_NEAR(poses[2].reach[ENTITY_ANIMATION_FORWARD], 6.0f32 / 16.0f32, ES_NEAR_F32);
    CHECK_NEAR(poses[2].reach[ENTITY_ANIMATION_UP], 2.0f32 / 16.0f32, ES_NEAR_F32);
    CHECK_NEAR(poses[3].center[1], 20.0f64 - 6.0f64 / 16.0f64, 1.0e-6f64);
    /* A leg, from the ground to 12. */
    CHECK_NEAR(poses[4].center[2], 30.0f64 + 6.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[4].center[1], 20.0f64 + 1.9f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[4].pitch, 0.0f32, ES_NEAR_F32);
}

TEST(a_turn_turns_every_part_about_the_feet) {
    entity_animation_state_t state;
    es_state_rest(&state);
    state.yaw = (float32_t)(ES_PI * 0.5f64); /* facing +y, so that its left is -x */
    const float64_t at[COLLISION_AXES] = {0.0f64, 0.0f64, 0.0f64};
    entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
    (void)entity_animation_pose(entity_animation_rig(ENTITY_ANIMATION_RIG_ZOMBIE), &state, at, poses);
    /* The left arm: 4 forward is +y, and 6 left is -x. */
    CHECK_NEAR(poses[2].center[0], -6.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[2].center[1], 4.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[2].yaw, state.yaw, ES_NEAR_F32);
}

TEST(walking_swings_opposite_legs_opposite_ways_and_the_foot_goes_forward) {
    entity_animation_state_t state;
    es_state_rest(&state);
    state.limb_amount = 1.0f32;
    const float64_t at[COLLISION_AXES] = {0.0f64, 0.0f64, 0.0f64};
    entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
    (void)entity_animation_pose(entity_animation_rig(ENTITY_ANIMATION_RIG_ZOMBIE), &state, at, poses);
    /* At the phase's start, a full stride: the two legs at -1.4 and 1.4. */
    CHECK_NEAR(poses[4].pitch, -1.4f32, ES_NEAR_F32);
    CHECK_NEAR(poses[5].pitch, 1.4f32, ES_NEAR_F32);
    /* What is at a plus goes forward, by 6 pixels of its sine, and rises by 6 of its cosine's lack. */
    const float64_t forward = 6.0f64 * sin(1.4f64) / 16.0f64;
    CHECK_NEAR(poses[5].center[0], forward, 1.0e-6f64);
    CHECK_NEAR(poses[4].center[0], -forward, 1.0e-6f64);
    CHECK_NEAR(poses[5].center[2], (12.0f64 - 6.0f64 * cos(1.4f64)) / 16.0f64, 1.0e-6f64);
    /* A zombie's arms do not walk. */
    CHECK_NEAR(poses[2].pitch, (float32_t)(ES_PI * 0.5f64), ES_NEAR_F32);
    /* A skeleton's do, opposite its leg on that side. */
    (void)entity_animation_pose(entity_animation_rig(ENTITY_ANIMATION_RIG_SKELETON), &state, at, poses);
    CHECK_NEAR(poses[2].pitch, 1.0f32, ES_NEAR_F32);
    CHECK_NEAR(poses[4].pitch, -1.4f32, ES_NEAR_F32);
    /* A creeper's corners go in pairs across it. */
    (void)entity_animation_pose(entity_animation_rig(ENTITY_ANIMATION_RIG_CREEPER), &state, at, poses);
    CHECK_NEAR(poses[2].pitch, 1.4f32, ES_NEAR_F32);
    CHECK_NEAR(poses[3].pitch, -1.4f32, ES_NEAR_F32);
    CHECK_NEAR(poses[4].pitch, -1.4f32, ES_NEAR_F32);
    CHECK_NEAR(poses[5].pitch, 1.4f32, ES_NEAR_F32);
    /* A quarter of the way through a stride they pass each other. */
    state.limb_phase = (float32_t)(ES_PI * 0.5f64 / 0.6662f64);
    (void)entity_animation_pose(entity_animation_rig(ENTITY_ANIMATION_RIG_ZOMBIE), &state, at, poses);
    CHECK_NEAR(poses[4].pitch, 0.0f32, 1.0e-4f32);
}

TEST(the_head_looks_an_attack_lifts_aim_holds_and_a_swell_grows) {
    entity_animation_state_t state;
    es_state_rest(&state);
    const float64_t at[COLLISION_AXES] = {0.0f64, 0.0f64, 0.0f64};
    entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
    const entity_animation_rig_t *const zombie = entity_animation_rig(ENTITY_ANIMATION_RIG_ZOMBIE);
    const entity_animation_rig_t *const skeleton = entity_animation_rig(ENTITY_ANIMATION_RIG_SKELETON);

    /* Looking up by a quarter turn: the head goes over backward from its neck, and turns none of the rest. */
    state.head_pitch = (float32_t)(ES_PI * 0.5f64);
    (void)entity_animation_pose(zombie, &state, at, poses);
    CHECK_NEAR(poses[0].center[0], -4.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[0].center[2], 24.0f64 / 16.0f64, 1.0e-6f64);
    CHECK_NEAR(poses[1].pitch, 0.0f32, ES_NEAR_F32);
    state.head_pitch = 0.0f32;

    /* Looking left by a quarter turn: only its yaw changes, for a head in the middle. */
    state.head_yaw = (float32_t)(ES_PI * 0.5f64);
    (void)entity_animation_pose(zombie, &state, at, poses);
    CHECK_NEAR(poses[0].yaw, state.head_yaw, ES_NEAR_F32);
    CHECK_NEAR(poses[1].yaw, 0.0f32, ES_NEAR_F32);
    CHECK_NEAR(poses[0].center[1], 0.0f64, 1.0e-6f64);
    state.head_yaw = 0.0f32;

    /* An attack, half way through: at its height. */
    state.swing = 0.5f32;
    (void)entity_animation_pose(zombie, &state, at, poses);
    CHECK_NEAR(poses[2].pitch, (float32_t)(ES_PI * 0.5f64) + 1.2f32, ES_NEAR_F32);
    CHECK_NEAR(poses[4].pitch, 0.0f32, ES_NEAR_F32);
    state.swing = 0.0f32;

    /* Aiming takes the arms from a walk to level, whatever the stride. */
    state.limb_amount = 1.0f32;
    state.aim = 1.0f32;
    (void)entity_animation_pose(skeleton, &state, at, poses);
    CHECK_NEAR(poses[2].pitch, (float32_t)(ES_PI * 0.5f64), ES_NEAR_F32);
    CHECK_NEAR(poses[3].pitch, (float32_t)(ES_PI * 0.5f64), ES_NEAR_F32);
    state.aim = 0.0f32;
    state.limb_amount = 0.0f32;

    /* All swollen up: a fifth bigger, about the same middle. */
    state.swell = 1.0f32;
    (void)entity_animation_pose(entity_animation_rig(ENTITY_ANIMATION_RIG_CREEPER), &state, at, poses);
    CHECK_NEAR(poses[0].half[0], 0.25f32 * 1.2f32, ES_NEAR_F32);
    CHECK_NEAR(poses[0].center[2], 22.0f64 / 16.0f64, 1.0e-6f64);
}

TEST(a_part_tilted_any_way_stays_inside_what_its_reach_says) {
    entity_animation_state_t state;
    es_state_rest(&state);
    const float64_t at[COLLISION_AXES] = {0.0f64, 0.0f64, 0.0f64};
    entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
    terrain_rng_t random = terrain_rng_start(3u);
    const entity_animation_rig_t *const rig = entity_animation_rig(ENTITY_ANIMATION_RIG_ZOMBIE);
    for (uint32_t i = 0u; i < 1000u; i++) {
        state.limb_phase = terrain_rng_next_f32(&random) * 10.0f32;
        state.limb_amount = terrain_rng_next_f32(&random);
        (void)entity_animation_pose(rig, &state, at, poses);
        for (uint32_t p = 0u; p < rig->part_count; p++) {
            const entity_animation_pose_t *const pose = &poses[p];
            const float32_t c = cosf(pose->pitch), s = sinf(pose->pitch);
            for (uint32_t corner = 0u; corner < 4u; corner++) {
                const float32_t f = (corner & 1u) != 0u ? pose->half[0] : -pose->half[0];
                const float32_t u = (corner & 2u) != 0u ? pose->half[2] : -pose->half[2];
                CHECK(fabsf(f * c - u * s) <= pose->reach[0] + ES_NEAR_F32);
                CHECK(fabsf(f * s + u * c) <= pose->reach[2] + ES_NEAR_F32);
            }
        }
    }
}

TEST(what_a_body_does_drives_its_stride_by_hand) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    const uint32_t i = es_at(t, s, es_spawn(s, -10.0f64, 0.5f64, 0.0f64));
    /* By hand: a tenth of a block in a tick wants 0.4 of a pace, and takes 0.4 of the way there. */
    s->borrowed_previous[0][i] = -10.1f64;
    entity_animation_tick(s);
    CHECK_NEAR(s->borrowed_limb_amount[i], 0.16f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_limb_phase[i], 0.16f32, ES_NEAR_F32);
    s->borrowed_previous[0][i] = -11.0f64; /* a whole block is past a full pace */
    entity_animation_tick(s);
    CHECK_NEAR(s->borrowed_limb_amount[i], 0.16f32 + 0.84f32 * 0.4f32, ES_NEAR_F32);
    /* Standing still it fades. */
    s->borrowed_previous[0][i] = s->borrowed_position[0][i];
    const float32_t was = s->borrowed_limb_amount[i];
    entity_animation_tick(s);
    CHECK_NEAR(s->borrowed_limb_amount[i], was * 0.6f32, ES_NEAR_F32);

    /* The phase stays within a stride, for as long as it walks. */
    const float32_t period = (float32_t)(2.0f64 * ES_PI / 0.6662f64);
    s->borrowed_limb_amount[i] = 1.0f32;
    for (uint32_t tick = 0u; tick < 100000u; tick++) {
        s->borrowed_previous[0][i] = s->borrowed_position[0][i] - 1.0f64;
        entity_animation_tick(s);
        if (s->borrowed_limb_phase[i] < 0.0f32 || s->borrowed_limb_phase[i] >= period + ES_NEAR_F32) {
            CHECK(false);
            break;
        }
    }

    /* An attack, over its ticks. */
    entity_animation_swing(s, i);
    entity_animation_state_t state;
    entity_animation_state_read(s, i, &state);
    CHECK_NEAR(state.swing, 0.0f32, ES_NEAR_F32);
    for (uint32_t tick = 1u; tick < ENTITY_ANIMATION_SWING_TICKS; tick++) {
        entity_animation_tick(s);
        entity_animation_state_read(s, i, &state);
        CHECK_NEAR(state.swing, (float32_t)tick / (float32_t)ENTITY_ANIMATION_SWING_TICKS, ES_NEAR_F32);
    }
    entity_animation_tick(s);
    entity_animation_state_read(s, i, &state);
    CHECK_NEAR(state.swing, 0.0f32, ES_NEAR_F32);
    entity_animation_tick(s);
    CHECK_EQ_U64(s->borrowed_swing_ticks[i], 0u);

    /* Waiting for a column: it holds its stride. */
    const uint32_t w = es_at(t, s, es_spawn(s, 200.5f64, 0.5f64, 0.0f64));
    s->borrowed_limb_amount[w] = 0.5f32;
    s->borrowed_limb_phase[w] = 1.0f32;
    es_ticks(&rig, 1u);
    entity_animation_tick(s);
    CHECK_NEAR(s->borrowed_limb_amount[w], 0.5f32, ES_NEAR_F32);
    CHECK_NEAR(s->borrowed_limb_phase[w], 1.0f32, ES_NEAR_F32);
    es_end(&rig);
}

/* ---- The Owner ------------------------------------------------------------------------------------------------------ */

/* Everything in the store and the grid's answers, as one number. */
static uint64_t es_bytes_hash(const entity_store_t *s) {
    uint64_t hash = 0xCBF29CE484222325ull;
    const uint32_t count = s->count;
    for (uint32_t i = 0u; i < count; i++) {
        uint64_t word = 0u;
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            memcpy(&word, &s->borrowed_position[a][i], sizeof(word));
            hash = terrain_rng_mix(hash ^ word);
            memcpy(&word, &s->borrowed_velocity[a][i], sizeof(word));
            hash = terrain_rng_mix(hash ^ word);
        }
        uint32_t small = 0u;
        memcpy(&small, &s->borrowed_limb_phase[i], sizeof(small));
        hash = terrain_rng_mix(hash ^ small);
        hash = terrain_rng_mix(hash ^ s->borrowed_handle[i] ^ ((uint64_t)s->borrowed_age[i] << 32u) ^
                               ((uint64_t)s->borrowed_flags[i] << 24u));
    }
    return hash;
}

static uint64_t es_run_a_world(test_context_t *t, uint64_t seed) {
    es_rig_t rig;
    if (!es_begin(t, &rig)) {
        return 0u;
    }
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    terrain_rng_t random = terrain_rng_start(seed);
    for (uint32_t i = 0u; i < ES_RANDOM_BODIES; i++) {
        (void)es_spawn(s, terrain_rng_next_f64(&random) * 20.0f64, terrain_rng_next_f64(&random) * 20.0f64, 2.0f64);
    }
    for (uint32_t tick = 0u; tick < 500u; tick++) {
        const uint32_t pick = terrain_rng_next_below(&random, s->count);
        entity_store_wish_set(s, pick, terrain_rng_next_f64(&random) * 0.2f64, terrain_rng_next_f64(&random) * 0.2f64);
        if (tick % 50u == 25u) {
            CHECK(entity_store_remove(s, s->borrowed_handle[pick]));
            (void)es_spawn(s, terrain_rng_next_f64(&random) * 20.0f64, 5.0f64, 2.0f64);
        }
        entity_tick(rig.entity, rig.world.map);
    }
    const uint64_t hash = es_bytes_hash(s);
    es_end(&rig);
    return hash;
}

TEST(the_same_calls_give_the_same_bytes_and_others_give_others) {
    const uint64_t first = es_run_a_world(t, 1u);
    CHECK(first != 0u);
    CHECK_EQ_U64(es_run_a_world(t, 1u), first);
    CHECK(es_run_a_world(t, 2u) != first);
}

TEST(a_tick_takes_no_heap_keeps_to_its_arena_and_the_world_starts_again_empty) {
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    for (uint32_t i = 0u; i < 100u; i++) {
        (void)es_spawn(s, (float64_t)(i % 10u) * 2.0f64, (float64_t)(i / 10u) * 2.0f64, 0.0f64);
    }
    entity_tick(rig.entity, rig.world.map);
    const uint64_t peak = rig.entity->arena_world.used;
    const uint64_t allocs = rig.entity->arena_world.alloc_count;
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t tick = 0u; tick < 200u; tick++) {
        for (uint32_t i = 0u; i < s->count; i++) {
            entity_store_wish_set(s, i, 0.1f64, 0.05f64);
        }
        entity_tick(rig.entity, rig.world.map);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(rig.entity->arena_world.used, peak);
    CHECK_EQ_U64(rig.entity->arena_world.alloc_count, allocs);
    /* All that a world takes is what the Owner's assert counted on. */
    CHECK(rig.entity->arena_world.used <= (uint64_t)ITEM_DROP_STORE_BYTES + (uint64_t)ENTITY_STORE_BYTES +
                                              (uint64_t)ENTITY_GRID_BYTES + (uint64_t)EXPLOSION_BYTES);

    entity_close(rig.entity);
    CHECK_EQ_U64(rig.entity->store.count, 0u);
    entity_open(rig.entity, 78u);
    CHECK_EQ_U64(s->count, 0u);
    CHECK_EQ_U64(s->spawned_count, 0u);
    entity_tick(rig.entity, rig.world.map); /* with nobody in it */
    entity_handle_t out[1];
    CHECK_EQ_U64(es_query_box(&rig, -9.0f64, -9.0f64, -9.0f64, 9.0f64, 9.0f64, 9.0f64, out, 1u), 0u);
    es_end(&rig);
}

TEST(the_region_comes_back_and_a_full_store_ticks) {
    heap_counters_t before, after;
    es_rig_t rig;
    REQUIRE(es_begin(t, &rig));
    es_floor(t, &rig, BLOCK_ID_STONE);
    entity_store_t *const s = rig.store;
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        (void)es_spawn(s, (float64_t)(i % 32u) * 1.5f64 - 16.0f64 + 0.5f64,
                       (float64_t)(i / 32u) * 1.5f64 - 16.0f64 + 0.5f64, 0.0f64);
    }
    for (uint32_t tick = 0u; tick < 20u; tick++) {
        entity_tick(rig.entity, rig.world.map);
    }
    CHECK_EQ_U64(s->count, ENTITY_STORE_CAPACITY);
    /* Removing from the end, as the header says, gets every one. */
    for (uint32_t i = s->count; i-- > 0u;) {
        CHECK(entity_store_remove(s, s->borrowed_handle[i]));
    }
    CHECK_EQ_U64(s->count, 0u);
    heap_counters_read(&before);
    entity_close(rig.entity);
    entity_deinit(rig.entity);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.release_calls, before.release_calls + 1u);
    /* Deinit is still safe on what it has zeroed. */
    entity_deinit(rig.entity);
    test_world_end(&rig.world);
}

TEST_MAIN()
