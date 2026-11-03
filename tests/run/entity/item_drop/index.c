/* ctest run_entity_drop. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "file/index.h"
#include "heap/index.h"
#include "main/index.h"
#include "player/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"
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

/* ---- A world, a store, and stacks put down by hand ------------------------------------------------------------------- */

enum : uint32_t {
    DI_PROGRAM_TIMEOUT_MS = 120000u,
    DI_RANDOM_STACKS = 1500u,
    DI_RANDOM_ITEMS = 3u,
};

typedef struct di_rig_t {
    test_world_t world; /* 0 */
    entity_t *entity;   /* 24 */
} di_rig_t;
static_assert(sizeof(di_rig_t) == 32);

static bool di_begin(test_context_t *t, di_rig_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics))) {
        return false;
    }
    entity_open(rig->entity, 77u);
    return true;
}

static void di_end(di_rig_t *rig) {
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

static item_drop_store_t *di_store(di_rig_t *rig) {
    return &rig->entity->item_drops;
}

static void di_ticks(di_rig_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        entity_tick(rig->entity, rig->world.map);
    }
}

/* The drop table's row for what breaking this block gives first. */
static uint16_t di_item_of(uint16_t block) {
    return block_drops[block_defs[block].first_drop].item;
}

/* A stack at this very place, at rest, ready to be picked up. It is the last in the store until the next one. */
static item_drop_t *di_put(item_drop_store_t *store, uint16_t item, uint32_t count, float64_t x, float64_t y,
                           float64_t z) {
    const float64_t position[COLLISION_AXES] = {x, y, z};
    item_drop_spawn(store, item, count, position);
    item_drop_t *const drop = &store->borrowed_items[store->count - 1u];
    drop->position[0] = x;
    drop->position[1] = y;
    drop->position[2] = z;
    drop->velocity[0] = 0.0f64;
    drop->velocity[1] = 0.0f64;
    drop->velocity[2] = 0.0f64;
    drop->pickup_delay = 0u;
    return drop;
}

/* What the counts must always add up to. */
static void di_books_balance(test_context_t *t, const item_drop_store_t *store) {
    CHECK_EQ_U64(store->dropped_count, store->picked_up_count + store->despawned_count + store->destroyed_count +
                                           store->lost_count + item_drop_on_ground(store));
}

/* ---- Physics --------------------------------------------------------------------------------------------------------- */

TEST(it_falls_as_worked_out_by_hand) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const item_drop_t *const drop = di_put(store, di_item_of(BLOCK_ID_DIRT), 1u, 0.5f64, 0.5f64, 20.5f64);
    /* v -= 0.04, z += v, v *= 0.98. */
    di_ticks(&rig, 1u);
    CHECK_NEAR(drop->position[2], 20.46f64, 1.0e-9f64);
    CHECK_NEAR(drop->velocity[2], -0.0392f64, 1.0e-9f64);
    di_ticks(&rig, 1u);
    CHECK_NEAR(drop->position[2], 20.3808f64, 1.0e-9f64);
    CHECK_NEAR(drop->velocity[2], -0.077616f64, 1.0e-9f64);
    di_ticks(&rig, 1u);
    CHECK_NEAR(drop->position[2], 20.263184f64, 1.0e-9f64);
    CHECK_NEAR(drop->velocity[2], -0.11526368f64, 1.0e-9f64);
    /* A hundred ticks on, by the same rule. */
    float64_t z = drop->position[2], v = drop->velocity[2];
    di_ticks(&rig, 100u);
    for (uint32_t i = 0u; i < 100u; i++) {
        v -= 0.04f64;
        z += v;
        v *= 0.98f64;
    }
    CHECK_NEAR(drop->position[2], z, 1.0e-9f64);
    CHECK_NEAR(drop->velocity[2], v, 1.0e-9f64);
    CHECK_EQ_U64(drop->on_ground, 0u);
    di_end(&rig);
}

TEST(thrown_out_of_a_block_it_goes_up_stays_in_its_cell_and_waits_to_be_picked_up) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const float64_t middle[COLLISION_AXES] = {3.5f64, -4.5f64, 7.5f64};
    bool any_east = false, any_west = false;
    for (uint32_t i = 0u; i < 1000u; i++) {
        item_drop_spawn(store, di_item_of(BLOCK_ID_DIRT), 1u, middle);
        const item_drop_t *const drop = &store->borrowed_items[i];
        collision_box_t box;
        item_drop_box(drop->position, &box);
        CHECK(box.min[0] >= 3.0f64 && box.max[0] <= 4.0f64);
        CHECK(box.min[1] >= -5.0f64 && box.max[1] <= -4.0f64);
        CHECK(box.min[2] >= 7.0f64 && box.max[2] <= 8.0f64);
        CHECK(fabs(drop->velocity[0]) <= 0.1f64);
        CHECK(fabs(drop->velocity[1]) <= 0.1f64);
        CHECK_NEAR(drop->velocity[2], 0.2f64, 1.0e-12f64);
        CHECK_EQ_U64(drop->pickup_delay, 10u);
        CHECK_EQ_U64(drop->count, 1u);
        any_east = any_east || drop->velocity[0] > 0.05f64;
        any_west = any_west || drop->velocity[0] < -0.05f64;
    }
    CHECK(any_east && any_west);
    CHECK_EQ_U64(store->dropped_count, 1000u);
    di_end(&rig);
}

TEST(more_than_a_stack_comes_out_as_several) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const float64_t middle[COLLISION_AXES] = {0.5f64, 0.5f64, 0.5f64};
    item_drop_spawn(store, di_item_of(BLOCK_ID_DIRT), 130u, middle);
    CHECK_EQ_U64(store->count, 3u);
    CHECK_EQ_U64(store->borrowed_items[0].count, 64u);
    CHECK_EQ_U64(store->borrowed_items[1].count, 64u);
    CHECK_EQ_U64(store->borrowed_items[2].count, 2u);
    di_books_balance(t, store);
    di_end(&rig);
}

TEST(it_lands_and_lies_still_on_a_block_and_on_a_slab) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    item_drop_store_t *const store = di_store(&rig);
    const item_drop_t *const on_stone = di_put(store, di_item_of(BLOCK_ID_DIRT), 1u, 0.5f64, 0.5f64, 1.5f64);
    di_ticks(&rig, 60u);
    CHECK_NEAR(on_stone->position[2], 0.125f64, 1.0e-6f64);
    CHECK_EQ_U64(on_stone->on_ground, 1u);
    CHECK_NEAR(on_stone->velocity[2], 0.0f64, 1.0e-12f64);
    const float64_t rested = on_stone->position[2];
    di_ticks(&rig, 400u);
    CHECK_NEAR(on_stone->position[2], rested, 1.0e-12f64);
    CHECK_EQ_U64(on_stone->on_ground, 1u);

    test_world_fill(t, &rig.world, 4, 4, 0, 4, 4, 0, BLOCK_ID_STONE_SLAB);
    const item_drop_t *const on_slab = di_put(store, di_item_of(BLOCK_ID_COAL_ORE), 1u, 4.5f64, 4.5f64, 2.5f64);
    di_ticks(&rig, 60u);
    CHECK_NEAR(on_slab->position[2], 0.625f64, 1.0e-6f64);
    di_end(&rig);
}

TEST(it_slides_further_on_ice_and_stops_at_a_wall) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 30, -8, -1, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, -8, 8, -1, 30, 8, -1, BLOCK_ID_ICE);
    test_world_fill(t, &rig.world, -8, 10, -1, 30, 10, -1, BLOCK_ID_ICE);
    test_world_fill(t, &rig.world, 3, 10, 0, 3, 10, 2, BLOCK_ID_STONE);
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    item_drop_t *const on_stone = di_put(store, item, 1u, 0.5f64, -7.5f64, 0.125f64);
    on_stone->velocity[0] = 0.2f64;
    on_stone->on_ground = 1u;
    item_drop_t *const on_ice = di_put(store, item, 1u, 0.5f64, 8.5f64, 0.125f64);
    on_ice->velocity[0] = 0.2f64;
    on_ice->on_ground = 1u;
    item_drop_t *const at_wall = di_put(store, item, 1u, 0.5f64, 10.5f64, 0.125f64);
    at_wall->velocity[0] = 0.2f64;
    at_wall->on_ground = 1u;
    /* Each tick it goes as fast as it is going, and then loses a part: 0.2 / (1 - 0.6 * 0.98) in all, and 0.2 / (1 - 0.98 * 0.98). */
    di_ticks(&rig, 400u);
    CHECK_NEAR(on_stone->position[0] - 0.5f64, 0.2f64 / (1.0f64 - 0.588f64), 0.02f64);
    CHECK_NEAR(on_ice->position[0] - 0.5f64, 0.2f64 / (1.0f64 - 0.9604f64), 0.1f64);
    /* 0.2 / 0.0396 is over 5 blocks, and the wall's face is 2.5 away. */
    CHECK_NEAR(at_wall->position[0], 3.0f64 - ITEM_DROP_SIZE / 2.0f64, 1.0e-6f64);
    CHECK_NEAR(at_wall->velocity[0], 0.0f64, 1.0e-12f64);
    di_end(&rig);
}

TEST(when_what_it_lies_on_goes_it_falls_and_when_a_block_comes_down_on_it_it_leaves) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -2, 8, 8, -2, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_DIRT);
    item_drop_store_t *const store = di_store(&rig);
    const item_drop_t *const drop = di_put(store, di_item_of(BLOCK_ID_DIRT), 1u, 0.5f64, 0.5f64, 1.5f64);
    di_ticks(&rig, 60u);
    CHECK_NEAR(drop->position[2], 0.125f64, 1.0e-6f64);

    /* Gone from under it: it knows within the 4 ticks of something lying still. */
    test_world_fill(t, &rig.world, 0, 0, -1, 0, 0, -1, BLOCK_ID_AIR);
    di_ticks(&rig, ITEM_DROP_REST_EVERY_TICKS + 1u);
    CHECK(drop->position[2] < 0.125f64 - 1.0e-3f64);
    di_ticks(&rig, 40u);
    CHECK_NEAR(drop->position[2], -0.875f64, 1.0e-6f64);

    /* A diagonal neighbour only: its box sticks out of an open cell, and over one that was filled later. It moves inside. */
    item_drop_t *const edge = di_put(store, di_item_of(BLOCK_ID_COAL_ORE), 1u, 4.95f64, 4.95f64, 0.125f64);
    test_world_fill(t, &rig.world, 5, 5, 0, 5, 5, 0, BLOCK_ID_DIRT);
    di_ticks(&rig, 6u);
    collision_box_t edge_box;
    item_drop_box(edge->position, &edge_box);
    CHECK(physics_box_is_clear(rig.world.physics, rig.world.map, &edge_box));
    CHECK_NEAR(edge->position[0], 4.875f64, 1.0e-3f64);
    CHECK_NEAR(edge->position[1], 4.875f64, 1.0e-3f64);
    CHECK(edge->on_ground != 0u);
    store->borrowed_items[1] = store->borrowed_items[store->count - 1u]; /* out of the way of the rest */
    store->count--;
    store->dropped_count--;

    /* A block in its place: dirt is put in the hole, and it is out of it and on top within a few ticks, by whichever face. */
    test_world_fill(t, &rig.world, 0, 0, -1, 0, 0, -1, BLOCK_ID_DIRT);
    di_ticks(&rig, 40u);
    collision_box_t box;
    item_drop_box(drop->position, &box);
    CHECK(physics_box_is_clear(rig.world.physics, rig.world.map, &box));
    CHECK(drop->position[2] >= 0.125f64 - 1.0e-6f64);
    CHECK_EQ_U64(store->count, 1u);

    /* Buried with nothing open but the top of the world, it goes up through all of it. */
    const item_drop_t *const buried = di_put(store, di_item_of(BLOCK_ID_DIRT), 1u, 6.5f64, 6.5f64, 3.5f64);
    test_world_fill(t, &rig.world, 5, 5, 0, 7, 7, 8, BLOCK_ID_DIRT);
    di_ticks(&rig, 200u);
    CHECK(buried->position[2] >= 9.0f64);
    collision_box_t free_box;
    item_drop_box(buried->position, &free_box);
    CHECK(physics_box_is_clear(rig.world.physics, rig.world.map, &free_box));
    di_end(&rig);
}

TEST(in_water_it_comes_up_and_stays_near_the_top) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, 8, 8, -1, 14, 14, -1, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 8, 8, 0, 14, 14, 9, BLOCK_ID_WATER);
    item_drop_store_t *const store = di_store(&rig);
    const item_drop_t *const drop = di_put(store, di_item_of(BLOCK_ID_DIRT), 1u, 11.5f64, 11.5f64, 2.5f64);
    const float64_t start = drop->position[2];
    di_ticks(&rig, 30u);
    CHECK(drop->position[2] > start); /* with no gravity at all down there */
    di_ticks(&rig, 1000u);
    for (uint32_t i = 0u; i < 500u; i++) {
        di_ticks(&rig, 1u);
        CHECK(drop->position[2] > 9.0f64 && drop->position[2] < 10.2f64);
        CHECK_EQ_U64(drop->on_ground, 0u);
    }
    di_end(&rig);
}

TEST(lava_and_a_cactus_end_it_and_the_books_say_so) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    test_world_fill(t, &rig.world, 3, 3, 0, 3, 3, 0, BLOCK_ID_LAVA);
    test_world_fill(t, &rig.world, -3, -3, 0, -3, -3, 0, BLOCK_ID_CACTUS);
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    (void)di_put(store, item, 5u, 3.5f64, 3.5f64, 2.5f64);
    item_drop_t *const by_cactus = di_put(store, item, 3u, -1.7f64, -2.5f64, 0.125f64);
    by_cactus->velocity[0] = -0.2f64; /* to the east of it, going west */
    (void)di_put(store, item, 2u, 0.5f64, 6.5f64, 0.125f64);
    di_ticks(&rig, 60u);
    CHECK_EQ_U64(store->destroyed_count, 8u);
    CHECK_EQ_U64(store->count, 1u);
    CHECK_EQ_U64(item_drop_on_ground(store), 2u);
    di_books_balance(t, store);
    di_end(&rig);
}

TEST(where_no_column_is_loaded_it_waits_and_does_not_age_and_the_edge_is_a_wall) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    /* The fixture's chunk 4, 0 is not loaded. Solid under the edge, so that the second stays up. */
    test_world_fill(t, &rig.world, 48, 0, -1, 63, 15, -1, BLOCK_ID_STONE);
    const item_drop_t *const out = di_put(store, item, 1u, 70.5f64, 0.5f64, 5.5f64);
    /* Its box is over the line into what is not loaded. */
    const item_drop_t *const over = di_put(store, item, 1u, 63.95f64, 8.5f64, 3.5f64);
    item_drop_t *const going = di_put(store, item, 1u, 62.5f64, 4.5f64, 0.125f64);
    going->velocity[0] = 0.6f64;
    going->on_ground = 1u;
    const item_drop_t before = *out;
    di_ticks(&rig, 100u);
    CHECK_MEM_EQ(out, &before, sizeof(before));
    CHECK_NEAR(over->position[2], 3.5f64, 1.0e-12f64);
    CHECK_EQ_U64(over->age, 0u);
    /* It stops at the edge, as at any wall. */
    CHECK_NEAR(going->position[0], 64.0f64 - ITEM_DROP_SIZE / 2.0f64, 1.0e-6f64);
    CHECK_EQ_U64(going->age, 100u);
    di_end(&rig);
}

TEST(after_five_minutes_it_is_gone) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    item_drop_store_t *const store = di_store(&rig);
    (void)di_put(store, di_item_of(BLOCK_ID_DIRT), 7u, 0.5f64, 0.5f64, 0.125f64);
    di_ticks(&rig, ITEM_DROP_DESPAWN_TICKS - 1u);
    CHECK_EQ_U64(store->count, 1u);
    CHECK_EQ_U64(store->despawned_count, 0u);
    di_ticks(&rig, 1u);
    CHECK_EQ_U64(store->count, 0u);
    CHECK_EQ_U64(store->despawned_count, 7u);
    di_books_balance(t, store);
    di_end(&rig);
}

/* ---- Merging ------------------------------------------------------------------------------------------------------------ */

/* Two stacks in the air at these middles' distance, falling together, and whether a pass of merging joined them. */
static bool di_two_merge(test_context_t *t, float64_t dx, float64_t dy, float64_t dz) {
    di_rig_t rig;
    if (!di_begin(t, &rig)) {
        return false;
    }
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    /* Over a cell's corner, so that the two are in different cells and on both sides of zero. */
    (void)di_put(store, item, 2u, -0.05f64, -0.05f64, 30.05f64);
    (void)di_put(store, item, 3u, -0.05f64 + dx, -0.05f64 + dy, 30.05f64 + dz);
    di_ticks(&rig, ITEM_DROP_MERGE_EVERY_TICKS);
    const bool merged = store->count == 1u;
    if (merged) {
        CHECK_EQ_U64(store->borrowed_items[0].count, 5u);
        CHECK_EQ_U64(store->merged_count, 1u);
    }
    di_books_balance(t, store);
    di_end(&rig);
    return merged;
}

TEST(two_of_a_kind_merge_at_the_reach_and_not_past_it) {
    CHECK(di_two_merge(t, 0.0f64, 0.0f64, 0.0f64));
    CHECK(di_two_merge(t, 0.74f64, 0.0f64, 0.0f64));
    CHECK(!di_two_merge(t, 0.76f64, 0.0f64, 0.0f64));
    CHECK(di_two_merge(t, 0.0f64, 0.74f64, 0.0f64));
    CHECK(!di_two_merge(t, 0.0f64, 0.76f64, 0.0f64));
    CHECK(di_two_merge(t, -0.74f64, -0.74f64, 0.0f64));
    /* Up and down there is no reach beyond the boxes. */
    CHECK(di_two_merge(t, 0.0f64, 0.0f64, 0.24f64));
    CHECK(!di_two_merge(t, 0.0f64, 0.0f64, 0.26f64));
    CHECK(di_two_merge(t, 0.0f64, 0.0f64, -0.24f64));
    CHECK(!di_two_merge(t, 0.0f64, 0.0f64, -0.26f64));
}

TEST(different_items_do_not_merge) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    (void)di_put(store, di_item_of(BLOCK_ID_DIRT), 2u, 0.5f64, 0.5f64, 30.5f64);
    (void)di_put(store, di_item_of(BLOCK_ID_COAL_ORE), 2u, 0.5f64, 0.5f64, 30.5f64);
    di_ticks(&rig, 10u);
    CHECK_EQ_U64(store->count, 2u);
    CHECK_EQ_U64(store->merged_count, 0u);
    di_end(&rig);
}

TEST(a_stack_stops_at_64_and_the_rest_stays_beside_it) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    (void)di_put(store, item, 60u, 0.5f64, 0.5f64, 30.5f64);
    (void)di_put(store, item, 10u, 0.5f64, 0.5f64, 30.5f64);
    di_ticks(&rig, ITEM_DROP_MERGE_EVERY_TICKS);
    REQUIRE(store->count == 2u);
    const uint32_t first = store->borrowed_items[0].count, second = store->borrowed_items[1].count;
    CHECK_EQ_U64(first + second, 70u);
    CHECK_EQ_U64(first > second ? first : second, 64u);
    CHECK_EQ_U64(store->merged_count, 0u);
    /* A full stack takes no part, whatever comes near it. */
    di_ticks(&rig, 10u);
    CHECK_EQ_U64(store->count, 2u);
    di_books_balance(t, store);
    di_end(&rig);
}

TEST(a_merge_keeps_the_younger_age_and_the_longer_wait) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    item_drop_t *const old = di_put(store, item, 5u, 0.5f64, 0.5f64, 30.5f64);
    old->age = 5000u;
    item_drop_t *const young = di_put(store, item, 1u, 0.5f64, 0.5f64, 30.5f64);
    young->age = 100u;
    young->pickup_delay = 9u;
    di_ticks(&rig, ITEM_DROP_MERGE_EVERY_TICKS);
    REQUIRE(store->count == 1u);
    CHECK_EQ_U64(store->borrowed_items[0].count, 6u);
    /* Each aged and waited through the two ticks. */
    CHECK_EQ_U64(store->borrowed_items[0].age, 102u);
    CHECK_EQ_U64(store->borrowed_items[0].pickup_delay, 7u);
    di_end(&rig);
}

TEST(a_pile_comes_down_to_as_few_stacks_as_it_can) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    item_drop_store_t *const store = di_store(&rig);
    const float64_t middle[COLLISION_AXES] = {0.5f64, 0.5f64, 0.5f64};
    for (uint32_t i = 0u; i < 1000u; i++) {
        item_drop_spawn(store, di_item_of(BLOCK_ID_DIRT), 1u, middle);
    }
    CHECK_EQ_U64(store->count, 1000u);
    di_ticks(&rig, 40u);
    /* 1000 / 64 is 15.6, and what they scatter by may leave a few short ones out of each other's reach. */
    CHECK(store->count >= 16u && store->count <= 20u);
    CHECK_EQ_U64(item_drop_on_ground(store), 1000u);
    CHECK_EQ_U64(store->merged_count, 1000u - store->count);
    di_books_balance(t, store);
    di_end(&rig);
}

/* What the grid finds, against every pair: after a pass, no two that could merge are left in reach, and no item has come or gone. */
TEST(the_grid_misses_nothing_against_every_pair) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    terrain_rng_t random = terrain_rng_start(4711u);
    const uint16_t items[DI_RANDOM_ITEMS] = {di_item_of(BLOCK_ID_DIRT), di_item_of(BLOCK_ID_COAL_ORE),
                                             di_item_of(BLOCK_ID_STONE)};
    uint64_t totals[DI_RANDOM_ITEMS] = {0u, 0u, 0u};
    /* Falling together through nothing, so that only merging changes who is near whom. Around zero, where negative cells begin. */
    for (uint32_t i = 0u; i < DI_RANDOM_STACKS; i++) {
        const uint32_t kind = terrain_rng_next_below(&random, DI_RANDOM_ITEMS);
        const uint32_t count = 1u + terrain_rng_next_below(&random, 40u);
        (void)di_put(store, items[kind], count, terrain_rng_next_f64(&random) * 24.0f64 - 12.0f64,
                     terrain_rng_next_f64(&random) * 24.0f64 - 12.0f64,
                     terrain_rng_next_f64(&random) * 8.0f64 + 40.0f64);
        totals[kind] += count;
    }
    di_ticks(&rig, ITEM_DROP_MERGE_EVERY_TICKS);
    CHECK(store->merged_count > 50u);
    uint64_t left[DI_RANDOM_ITEMS] = {0u, 0u, 0u};
    for (uint32_t i = 0u; i < store->count; i++) {
        const item_drop_t *const a = &store->borrowed_items[i];
        for (uint32_t kind = 0u; kind < DI_RANDOM_ITEMS; kind++) {
            left[kind] += a->item == items[kind] ? a->count : 0u;
        }
        CHECK(a->count >= 1u && a->count <= ITEM_DROP_STACK_MAX);
        for (uint32_t j = i + 1u; j < store->count; j++) {
            const item_drop_t *const b = &store->borrowed_items[j];
            const bool mergeable =
                a->item == b->item && a->count < ITEM_DROP_STACK_MAX && b->count < ITEM_DROP_STACK_MAX;
            const bool near = fabs(a->position[0] - b->position[0]) < 0.75f64 &&
                              fabs(a->position[1] - b->position[1]) < 0.75f64 &&
                              fabs(a->position[2] - b->position[2]) < 0.25f64;
            CHECK(!(mergeable && near));
        }
    }
    CHECK_MEM_EQ(left, totals, sizeof(left));
    di_end(&rig);
}

/* ---- Pickup ---------------------------------------------------------------------------------------------------------- */

typedef struct di_taker_t {
    uint32_t wants;  /* 0: how many of each offer it takes */
    uint32_t offers; /* 4 */
} di_taker_t;
static_assert(sizeof(di_taker_t) == 8);

static uint32_t di_take(void *borrowed_taker, uint16_t item, uint16_t wear, uint32_t count) {
    (void)item;
    (void)wear;
    di_taker_t *const taker = borrowed_taker;
    taker->offers++;
    return count < taker->wants ? count : taker->wants;
}

static const collision_box_t di_reach = {.min = {0.0f64, 0.0f64, 0.0f64}, .max = {1.0f64, 1.0f64, 2.0f64}};

static uint32_t di_take_wear(void *borrowed_wear, uint16_t item, uint16_t wear, uint32_t count) {
    (void)item;
    *(uint16_t *)borrowed_wear = wear;
    return count;
}

TEST(wear_lies_on_the_ground_with_a_stack_and_only_the_same_wear_merges) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    const float64_t middle[COLLISION_AXES] = {0.5f64, 0.5f64, 1.0f64};
    item_drop_spawn_worn(store, item, 7u, 1u, middle);
    CHECK_EQ_U64(store->borrowed_items[0].wear, 7u);
    store->borrowed_items[0].pickup_delay = 0u;
    uint16_t seen = 0u;
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_take_wear, &seen), 1u);
    CHECK_EQ_U64(seen, 7u);
    CHECK_EQ_U64(store->count, 0u);

    /* Side by side: new, worn, as worn again and more worn. Only the two as worn as each other become one. */
    const uint16_t wears[] = {0u, 7u, 7u, 9u};
    for (uint32_t i = 0u; i < 4u; i++) {
        item_drop_t *const drop = di_put(store, item, 1u, 0.5f64, 0.5f64, 0.5f64);
        drop->wear = wears[i];
    }
    di_ticks(&rig, ITEM_DROP_MERGE_EVERY_TICKS);
    CHECK_EQ_U64(store->count, 3u);
    CHECK_EQ_U64(store->merged_count, 1u);
    for (uint32_t i = 0u; i < store->count; i++) {
        CHECK_EQ_U64(store->borrowed_items[i].count, store->borrowed_items[i].wear == 7u ? 2u : 1u);
    }
    di_end(&rig);
}

TEST(it_cannot_be_picked_up_until_the_tenth_tick) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    item_drop_store_t *const store = di_store(&rig);
    const float64_t middle[COLLISION_AXES] = {0.5f64, 0.5f64, 0.5f64};
    item_drop_spawn(store, di_item_of(BLOCK_ID_DIRT), 1u, middle);
    di_taker_t taker = {.wants = 64u, .offers = 0u};
    /* In every one of those ticks it is where it could be reached. */
    const collision_box_t wide = {.min = {-4.0f64, -4.0f64, -4.0f64}, .max = {4.0f64, 4.0f64, 8.0f64}};
    for (uint32_t tick = 1u; tick < ITEM_DROP_PICKUP_DELAY_TICKS; tick++) {
        di_ticks(&rig, 1u);
        CHECK_EQ_U64(item_drop_pickup(store, &wide, di_take, &taker), 0u);
    }
    CHECK_EQ_U64(taker.offers, 0u);
    di_ticks(&rig, 1u);
    CHECK_EQ_U64(item_drop_pickup(store, &wide, di_take, &taker), 1u);
    CHECK_EQ_U64(taker.offers, 1u);
    CHECK_EQ_U64(store->count, 0u);
    CHECK_EQ_U64(store->picked_up_count, 1u);
    di_books_balance(t, store);
    di_end(&rig);
}

TEST(it_takes_an_overlap_with_the_reach_and_a_touch_is_not_one) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    di_taker_t taker = {.wants = 64u, .offers = 0u};
    /* Its box's west face is on the reach's east face. */
    (void)di_put(store, item, 1u, 1.125f64, 0.5f64, 1.0f64);
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_take, &taker), 0u);
    /* A little further in, it is. */
    store->borrowed_items[0].position[0] = 1.12f64;
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_take, &taker), 1u);
    /* And out on the other axes, none. */
    (void)di_put(store, item, 1u, 0.5f64, -0.2f64, 1.0f64);
    (void)di_put(store, item, 1u, 0.5f64, 0.5f64, 2.2f64);
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_take, &taker), 0u);
    CHECK_EQ_U64(store->count, 2u);
    di_books_balance(t, store);
    di_end(&rig);
}

TEST(what_is_not_taken_stays_on_the_ground) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    (void)di_put(store, di_item_of(BLOCK_ID_DIRT), 10u, 0.5f64, 0.5f64, 1.0f64);
    di_taker_t none = {.wants = 0u, .offers = 0u};
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_take, &none), 0u);
    CHECK_EQ_U64(none.offers, 1u);
    CHECK_EQ_U64(store->borrowed_items[0].count, 10u);
    di_taker_t some = {.wants = 3u, .offers = 0u};
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_take, &some), 3u);
    CHECK_EQ_U64(store->borrowed_items[0].count, 7u);
    CHECK_EQ_U64(store->picked_up_count, 3u);
    di_books_balance(t, store);
    di_end(&rig);
}

static uint32_t di_hand_take(void *borrowed_hands, uint16_t item, uint16_t wear, uint32_t count) {
    (void)wear;
    return player_interaction_hand_take(borrowed_hands, item, count);
}

TEST(a_real_hand_takes_blocks_keeps_what_it_has_and_leaves_coal_lying) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    player_interaction_t *const hands = test_alloc(t, sizeof(player_interaction_t));
    player_interaction_start(hands, 1u);
    (void)di_put(store, di_item_of(BLOCK_ID_COAL_ORE), 2u, 0.5f64, 0.5f64, 1.0f64);
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_hand_take, hands), 0u);
    CHECK_EQ_U64(store->count, 1u);
    CHECK_EQ_U64(hands->hand_count, 0u);

    /* Grass gives dirt, which the hand takes. */
    (void)di_put(store, di_item_of(BLOCK_ID_GRASS_BLOCK), 3u, 0.5f64, 0.5f64, 1.0f64);
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_hand_take, hands), 3u);
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(hands->hand_count, 3u);

    /* What it does not hold stays there, and the dirt is not lost. */
    (void)di_put(store, di_item_of(BLOCK_ID_STONE), 1u, 0.5f64, 0.5f64, 1.0f64);
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_hand_take, hands), 0u);
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(hands->hand_count, 3u);

    /* Empty again, it takes that. */
    hands->hand_count = 0u;
    CHECK_EQ_U64(item_drop_pickup(store, &di_reach, di_hand_take, hands), 1u);
    CHECK_EQ_U64(hands->hand_block, block_drop_items[di_item_of(BLOCK_ID_STONE)].block);
    CHECK_EQ_U64(store->count, 1u); /* the coal */
    di_books_balance(t, store);
    di_end(&rig);
}

TEST(every_item_in_the_tables_can_lie_on_the_ground_and_only_blocks_go_in_a_hand) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    player_interaction_t *const hands = test_alloc(t, sizeof(player_interaction_t));
    for (uint16_t item = 0u; item < BLOCK_DROP_ITEM_COUNT; item++) {
        player_interaction_start(hands, 1u);
        (void)di_put(store, item, 2u, 0.5f64, 0.5f64, 1.0f64);
        const uint32_t taken = item_drop_pickup(store, &di_reach, di_hand_take, hands);
        if (block_drop_items[item].block == BLOCK_NO_ID) {
            CHECK_EQ_U64(taken, 0u);
            CHECK_EQ_U64(hands->hand_count, 0u);
            store->count = 0u; /* so that the next is alone */
            store->dropped_count -= 2u;
        } else {
            CHECK_EQ_U64(taken, 2u);
            CHECK_EQ_U64(hands->hand_block, block_drop_items[item].block);
        }
    }
    di_books_balance(t, store);
    di_end(&rig);
}

/* ---- The store and the Owner ------------------------------------------------------------------------------------------ */

TEST(a_full_store_gives_up_its_oldest_and_counts_them) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    item_drop_store_t *const store = di_store(&rig);
    const uint16_t item = di_item_of(BLOCK_ID_DIRT);
    const float64_t middle[COLLISION_AXES] = {0.5f64, 0.5f64, 0.5f64};
    for (uint32_t i = 0u; i < ITEM_DROP_CAPACITY; i++) {
        item_drop_spawn(store, item, 1u, middle);
    }
    CHECK_EQ_U64(store->count, ITEM_DROP_CAPACITY);
    store->borrowed_items[1234].age = 100u;
    store->borrowed_items[1234].count = 9u;
    store->dropped_count += 8u;
    item_drop_spawn(store, item, 2u, middle);
    CHECK_EQ_U64(store->count, ITEM_DROP_CAPACITY);
    CHECK_EQ_U64(store->lost_count, 9u);
    di_books_balance(t, store);
    di_end(&rig);
}

/* A busy little scene, the same whichever pass it is. */
static void di_scene(di_rig_t *rig, item_drop_store_t *store) {
    for (uint32_t i = 0u; i < 300u; i++) {
        if (i % 7u == 0u) {
            const float64_t at[COLLISION_AXES] = {(float64_t)(i % 5u) + 0.5f64, (float64_t)(i % 3u) + 0.5f64, 2.5f64};
            item_drop_spawn(store, di_item_of(i % 2u == 0u ? BLOCK_ID_DIRT : BLOCK_ID_STONE), 1u + i % 3u, at);
        }
        di_ticks(rig, 1u);
    }
}

TEST(the_same_seed_gives_the_same_bytes_and_a_tick_takes_nothing_from_the_heap) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -8, -8, -1, 8, 8, -1, BLOCK_ID_STONE);
    entity_t *const entity = rig.entity;
    item_drop_t *const copy = test_alloc(t, (uint64_t)ITEM_DROP_CAPACITY * sizeof(item_drop_t));
    item_drop_store_t first;
    heap_counters_t before, after;
    heap_counters_read(&before);
    di_scene(&rig, &entity->item_drops);
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    first = entity->item_drops;
    memcpy(copy, first.borrowed_items, (uint64_t)first.count * sizeof(item_drop_t));
    CHECK(first.dropped_count > 0u);
    CHECK(first.merged_count > 0u);

    entity_open(entity, 77u);
    CHECK_EQ_U64(entity->item_drops.count, 0u);
    di_scene(&rig, &entity->item_drops);
    CHECK_EQ_U64(entity->item_drops.count, first.count);
    CHECK_EQ_U64(entity->item_drops.dropped_count, first.dropped_count);
    CHECK_EQ_U64(entity->item_drops.merged_count, first.merged_count);
    CHECK_MEM_EQ(entity->item_drops.borrowed_items, copy, (uint64_t)first.count * sizeof(item_drop_t));

    entity_open(entity, 78u);
    di_scene(&rig, &entity->item_drops);
    CHECK(entity->item_drops.count != first.count ||
          memcmp(entity->item_drops.borrowed_items, copy, (uint64_t)first.count * sizeof(item_drop_t)) != 0);
    di_end(&rig);
}

TEST(the_owner_gives_back_all_it_took_and_opens_again_and_again) {
    physics_t *const physics = test_world_aligned(t, sizeof(physics_t));
    entity_t *const entity = test_world_aligned(t, sizeof(entity_t));
    /* Zeroed, as under app_t before anything ran. */
    entity_deinit(entity);
    REQUIRE(physics_init(physics));
    heap_counters_t before, after;
    heap_counters_read(&before);
    REQUIRE(entity_init(entity, physics));
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions + 1u);
    for (uint32_t i = 0u; i < 3u; i++) {
        entity_open(entity, i);
        const float64_t middle[COLLISION_AXES] = {0.5f64, 0.5f64, 0.5f64};
        item_drop_spawn(&entity->item_drops, di_item_of(BLOCK_ID_DIRT), 1u, middle);
        CHECK_EQ_U64(entity->item_drops.count, 1u);
        CHECK(entity->arena_world.used <= (uint64_t)ITEM_DROP_STORE_BYTES + (uint64_t)ENTITY_STORE_BYTES +
                                              (uint64_t)ENTITY_GRID_BYTES + (uint64_t)PROJECTILE_STORE_BYTES +
                                              (uint64_t)EXPLOSION_BYTES);
        entity_close(entity);
        CHECK_EQ_U64(entity->arena_world.used, 0u);
        CHECK_EQ_U64(entity->item_drops.count, 0u);
    }
    entity_deinit(entity);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    physics_deinit(physics);
}

/* What the worst case costs: every stack there can be, all moving, in the debug build this runs in. A tick has 50 ms for everything. */
TEST(a_store_full_of_moving_stacks_fits_in_a_tick) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -32, -32, -1, 31, 31, -1, BLOCK_ID_STONE);
    item_drop_store_t *const store = di_store(&rig);
    for (uint32_t i = 0u; i < ITEM_DROP_CAPACITY; i++) {
        item_drop_t *const drop = di_put(store, di_item_of(BLOCK_ID_DIRT), 1u, (float64_t)(i % 64u) - 31.5f64,
                                         (float64_t)(i / 64u) - 31.5f64, 0.125f64);
        drop->velocity[0] = 0.01f64;
        drop->on_ground = 1u;
    }
    const int64_t start_ns = time_now_ns();
    di_ticks(&rig, 2u);
    const int64_t took_ns = (time_now_ns() - start_ns) / 2;
    CHECK_EQ_U64(store->count, ITEM_DROP_CAPACITY);
    printf("a tick of %" PRIu32 " moving stacks took %.2f ms\n", store->count,
           (libc_double_t)((float64_t)took_ns / 1.0e6f64));
    CHECK(took_ns < 50000000);
    di_end(&rig);
}

/* ---- What went by itself ----------------------------------------------------------------------------------------------- */

static void di_tick_steps(di_rig_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        world_tick_step(&rig->world.world->tick);
    }
}

TEST(the_tick_says_what_water_washed_away_and_what_sand_buried_and_lava_gives_nothing) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -16, -16, -1, 32, 32, -1, BLOCK_ID_STONE);
    world_tick_t *const tick = &rig.world.world->tick;
    world_edit_t *const edit = &rig.world.world->edit;
    world_tick_broken_t broken;
    CHECK(!world_tick_broken_next(tick, &broken));

    /* Water beside a flower. Air has no drops, so the rest of its spread says nothing. */
    REQUIRE(world_edit_block_set(edit, 1, 0, 0, BLOCK_ID_DANDELION) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, 0, 0, 0, BLOCK_ID_WATER) == WORLD_EDIT_STATUS_CHANGED);
    di_tick_steps(&rig, 12u);
    REQUIRE(world_tick_broken_next(tick, &broken));
    CHECK_EQ_I64(broken.x, 1);
    CHECK_EQ_I64(broken.y, 0);
    CHECK_EQ_I64(broken.z, 0);
    CHECK_EQ_U64(broken.block, BLOCK_ID_DANDELION);
    CHECK(!world_tick_broken_next(tick, &broken));

    /* Sand onto a flower far from the water, and lava beside another. */
    REQUIRE(world_edit_block_set(edit, -10, -10, 0, BLOCK_ID_DANDELION) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, -10, -10, 1, BLOCK_ID_SAND) == WORLD_EDIT_STATUS_CHANGED);
    di_tick_steps(&rig, 6u);
    REQUIRE(world_tick_broken_next(tick, &broken));
    CHECK_EQ_I64(broken.x, -10);
    CHECK_EQ_I64(broken.z, 0);
    CHECK_EQ_U64(broken.block, BLOCK_ID_DANDELION);
    REQUIRE(world_edit_block_set(edit, 12, 12, 0, BLOCK_ID_DANDELION) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, 11, 12, 0, BLOCK_ID_LAVA) == WORLD_EDIT_STATUS_CHANGED);
    di_tick_steps(&rig, 40u);
    uint16_t burnt = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(rig.world.map, 12, 12, 0, &burnt) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(burnt, BLOCK_ID_LAVA);
    CHECK(!world_tick_broken_next(tick, &broken));
    CHECK_EQ_U64(tick->broken_lost_count, 0u);
    di_end(&rig);
}

TEST(a_full_ring_of_what_went_by_itself_loses_the_oldest_and_counts_them) {
    di_rig_t rig;
    REQUIRE(di_begin(t, &rig));
    test_world_fill(t, &rig.world, -16, -16, -1, 32, 32, -1, BLOCK_ID_STONE);
    world_tick_t *const tick = &rig.world.world->tick;
    world_edit_t *const edit = &rig.world.world->edit;
    /* A row of sand over a row of flowers, more than the ring holds, all falling in one tick. */
    const uint32_t extra = 3u;
    uint32_t placed = 0u;
    for (int32_t y = -16; y < 16 && placed < WORLD_TICK_BROKEN + extra; y++) {
        for (int32_t x = -16; x < 16 && placed < WORLD_TICK_BROKEN + extra; x++) {
            REQUIRE(world_edit_block_set(edit, x, y, 0, BLOCK_ID_DANDELION) == WORLD_EDIT_STATUS_CHANGED);
            REQUIRE(world_edit_block_set(edit, x, y, 1, BLOCK_ID_SAND) == WORLD_EDIT_STATUS_CHANGED);
            placed++;
        }
    }
    di_tick_steps(&rig, 6u);
    CHECK_EQ_U64(tick->broken_lost_count, extra);
    uint32_t left = 0u;
    world_tick_broken_t broken;
    while (world_tick_broken_next(tick, &broken)) {
        CHECK_EQ_U64(broken.block, BLOCK_ID_DANDELION);
        left++;
    }
    CHECK_EQ_U64(left, WORLD_TICK_BROKEN);
    di_end(&rig);
}

/* ---- Through a real body, in the order game_sim_tick has it ----------------------------------------------------------- */

typedef struct di_body_t {
    di_rig_t rig;     /* 0 */
    player_t *player; /* 32 */
    int64_t now_ns;   /* 40 */
} di_body_t;
static_assert(sizeof(di_body_t) == 48);

static void di_body_tick(di_body_t *body, int8_t forward, float32_t pitch, bool attack, bool place) {
    player_t *const player = body->player;
    const player_intent_t intent = {
        .yaw = 0.0f32, .forward = forward, .right = 0, .jump = 0u, .sneak = 0u, .sprint = 0u, ._pad0 = {0}};
    body->now_ns += PLAYER_TICK_NS;
    player_send(player, &intent, body->now_ns);
    player_look_send(player, 0.0f32, pitch);
    player_hands_send(player, attack, place);
    player_interact_tick(player, &body->rig.world.world->edit, body->now_ns);
    item_drop_store_t *const store = di_store(&body->rig);
    player_drop_t drop;
    while (player_interaction_drop_next(&player->interaction, &drop)) {
        item_drop_spawn(store, drop.item, drop.count, drop.position);
    }
    entity_tick(body->rig.entity, body->rig.world.map);
    collision_box_t reach;
    player_pickup_reach(player, &reach);
    (void)item_drop_pickup(store, &reach, di_hand_take, &player->interaction);
    player_tick(player, body->rig.world.map, body->now_ns);
}

TEST(a_body_mines_dirt_which_drops_it_walks_over_and_picks_it_up_and_places_it_back) {
    di_body_t body;
    REQUIRE(di_begin(t, &body.rig));
    test_world_fill(t, &body.rig.world, -16, -16, -1, 32, 32, -1, BLOCK_ID_STONE);
    test_world_fill(t, &body.rig.world, 4, 0, 0, 4, 0, 0, BLOCK_ID_DIRT);
    body.player = test_world_aligned(t, sizeof(player_t));
    player_init(body.player, body.rig.world.physics);
    player_spawn(body.player, 0.5f64, 0.5f64, 0.0f64);
    body.now_ns = 1000000000;
    player_t *const player = body.player;
    const player_interaction_t *const hands = &player->interaction;
    item_drop_store_t *const store = di_store(&body.rig);
    const float32_t pitch = -0.35f32;

    /* It settles on its feet, aims at the dirt, and has not yet broken it. */
    for (uint32_t i = 0u; i < 3u; i++) {
        di_body_tick(&body, 0, pitch, false, false);
    }
    REQUIRE(player->target.hit.block == BLOCK_ID_DIRT);
    CHECK_EQ_U64(store->count, 0u);

    /* 15 ticks by hand. On the last the dirt is gone, and an item is there and not yet in any hand. */
    for (uint32_t i = 0u; i < 14u; i++) {
        di_body_tick(&body, 0, pitch, true, false);
        CHECK_EQ_U64(store->count, 0u);
    }
    di_body_tick(&body, 0, pitch, true, false);
    uint16_t block = UINT16_MAX;
    REQUIRE(world_map_block_get(body.rig.world.map, 2, 0, 0, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK_EQ_U64(hands->mined_count, 1u);
    REQUIRE(store->count == 1u);
    CHECK_EQ_U64(store->borrowed_items[0].item, di_item_of(BLOCK_ID_DIRT));
    CHECK_EQ_U64(store->borrowed_items[0].count, 1u);
    CHECK_EQ_U64(hands->hand_count, 0u);

    /* Too far off to be picked up: its box's near face is past the body's by more than the reach. So it lies there. */
    for (uint32_t i = 0u; i < 40u; i++) {
        di_body_tick(&body, 0, pitch, false, false);
    }
    CHECK_EQ_U64(store->count, 1u);
    CHECK_EQ_U64(store->borrowed_items[0].on_ground, 1u);
    CHECK_NEAR(store->borrowed_items[0].position[2], 0.125f64, 1.0e-6f64);
    CHECK_EQ_U64(hands->hand_count, 0u);

    /* It walks east until it has it. */
    uint32_t walked = 0u;
    while (hands->hand_count == 0u && walked < 40u) {
        di_body_tick(&body, 1, pitch, false, false);
        walked++;
    }
    CHECK(walked < 40u);
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(hands->hand_count, 1u);
    CHECK_EQ_U64(store->count, 0u);
    CHECK_EQ_U64(store->picked_up_count, 1u);
    CHECK_EQ_U64(hands->picked_up_count, 1u);

    /* And it stops, and looks down a little more, and the block goes down on the stone it looks at. */
    const float32_t pitch_nearer = -0.6f32;
    for (uint32_t i = 0u; i < 20u; i++) {
        di_body_tick(&body, 0, pitch_nearer, false, false);
    }
    REQUIRE(player->target.hit.result == RAYCAST_RESULT_HIT);
    int32_t cell[COLLISION_AXES];
    raycast_hit_in_front(&player->target.hit, cell);
    di_body_tick(&body, 0, pitch_nearer, false, true);
    REQUIRE(world_map_block_get(body.rig.world.map, cell[0], cell[1], cell[2], &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(hands->placed_count, 1u);
    CHECK_EQ_U64(hands->hand_count, 0u);
    di_books_balance(t, store);
    player_deinit(player);
    di_end(&body.rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------ */

typedef struct di_temp_dir_t {
    char path[64]; /* 0 */
} di_temp_dir_t;
static_assert(sizeof(di_temp_dir_t) == 64);

static bool di_temp_dir_make(di_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_entity_drop_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void di_temp_dir_remove(const di_temp_dir_t *dir) {
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

/* Stands where the default seed starts it, looks straight down and mines the grass under its feet, which takes 18 ticks by hand and
 * drops dirt. It falls into the hole with the item, which it can pick up 10 ticks after it came out. Then it jumps, and with its feet
 * clear of the hole it places the dirt back under them, and lands where it started. */
typedef struct di_script_line_t {
    const char *borrowed_text; /* 0 */
    uint32_t tick;             /* 8 */
    uint8_t _pad0[4];          /* 12 */
} di_script_line_t;
static_assert(sizeof(di_script_line_t) == 16);

static const di_script_line_t di_script[] = {
    {.tick = 1u, .borrowed_text = "move 0 500"},           {.tick = 5u, .borrowed_text = "screenshot before.png"},
    {.tick = 6u, .borrowed_text = "button left down"},     {.tick = 26u, .borrowed_text = "button left up"},
    {.tick = 27u, .borrowed_text = "screenshot hole.png"}, {.tick = 45u, .borrowed_text = "key space down"},
    {.tick = 45u, .borrowed_text = "button right down"},   {.tick = 65u, .borrowed_text = "key space up"},
    {.tick = 65u, .borrowed_text = "button right up"},     {.tick = 90u, .borrowed_text = "screenshot back.png"},
};

/* A script that holds the lines before this tick, and ends there. A tick count on the command line could not say so: it may not be
 * shorter than the script. */
static void di_script_write(test_context_t *t, const char *path, uint32_t end_tick) {
    char text[1024];
    uint64_t used = 0u;
    for (uint32_t i = 0u; i < sizeof(di_script) / sizeof(di_script[0]); i++) {
        if (di_script[i].tick < end_tick) {
            used += (uint64_t)snprintf(text + used, sizeof(text) - used, "%" PRIu32 " %s\n", di_script[i].tick,
                                       di_script[i].borrowed_text);
        }
    }
    used += (uint64_t)snprintf(text + used, sizeof(text) - used, "%" PRIu32 " end\n", end_tick);
    CHECK(used < sizeof(text));
    CHECK_EQ_U64(file_write_atomic(path, text, used), FILE_OK);
}

static void di_program_run(test_context_t *t, const di_temp_dir_t *dir, const char *script, uint32_t end_tick,
                           test_process_t *out) {
    di_script_write(t, script, end_tick);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"3",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)dir->path,
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          nullptr};
    test_process_run(t, argv, DI_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *di_png_read(test_context_t *t, const di_temp_dir_t *dir, const char *name, uint64_t *out_length) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = 16ull << 20;
    uint8_t *const bytes = test_alloc(t, capacity);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, out_length), FILE_OK)) {
        *out_length = 0u;
    }
    return bytes;
}

/* The same script, stopped at each point of the story, and what the program says of itself there. */
TEST(the_program_drops_dirt_picks_it_up_and_places_it_back_the_same_each_time) {
    di_temp_dir_t dir, again;
    REQUIRE(di_temp_dir_make(&dir));
    REQUIRE(di_temp_dir_make(&again));
    char script[128];
    snprintf(script, sizeof(script), "%s/drop.script", dir.path);
    test_process_t run;

    /* Still mining: nothing has come out. */
    di_program_run(t, &dir, script, 20u, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output,
                   "the player mined 0 blocks and placed 0; the last mined was air, and the hand holds 0 of nothing");
    CHECK_CONTAINS(run.output, "0 items dropped, 0 picked up, 0 on the ground in 0 stacks");

    /* The grass has gone: dirt lies there, and the hand is still empty. */
    di_program_run(t, &dir, script, 27u, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(
        run.output,
        "the player mined 1 blocks and placed 0; the last mined was grass_block, and the hand holds 0 of nothing");
    CHECK_CONTAINS(run.output, "1 items dropped, 0 picked up, 1 on the ground in 1 stacks");

    /* Its wait is over: it is in the hand, and no longer on the ground. */
    di_program_run(t, &dir, script, 44u, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(
        run.output,
        "the player mined 1 blocks and placed 0; the last mined was grass_block, and the hand holds 1 of dirt");
    CHECK_CONTAINS(run.output, "1 items dropped, 1 picked up, 0 on the ground in 0 stacks");
    CHECK_CONTAINS(run.output, "the player is at -7.500, -7.500, 84.000"); /* down in the hole */

    /* And it is back in the world: the hand is empty, and the body stands where it started. */
    di_program_run(t, &dir, script, 95u, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(
        run.output,
        "the player mined 1 blocks and placed 1; the last mined was grass_block, and the hand holds 0 of nothing");
    CHECK_CONTAINS(run.output, "1 items dropped, 1 picked up, 0 on the ground in 0 stacks");
    CHECK_CONTAINS(run.output, "the player starts at -7.5, -7.5, 85.0");
    CHECK_CONTAINS(run.output, "the player is at -7.500, -7.500, 85.000");
    CHECK_CONTAINS(run.output, "0 bytes live");

    di_program_run(t, &again, script, 95u, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    static const char *const names[] = {"before.png", "hole.png", "back.png"};
    uint64_t lengths[3];
    const uint8_t *pictures[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        uint64_t again_length = 0u;
        pictures[i] = di_png_read(t, &dir, names[i], &lengths[i]);
        const uint8_t *const again_bytes = di_png_read(t, &again, names[i], &again_length);
        REQUIRE(lengths[i] != 0u && lengths[i] == again_length);
        CHECK_MEM_EQ(pictures[i], again_bytes, lengths[i]);
    }
    /* The hole shows, and then does not. */
    CHECK(lengths[0] != lengths[1] || memcmp(pictures[0], pictures[1], lengths[0]) != 0);
    CHECK(lengths[1] != lengths[2] || memcmp(pictures[1], pictures[2], lengths[1]) != 0);
    di_temp_dir_remove(&dir);
    di_temp_dir_remove(&again);
}

TEST_MAIN()
