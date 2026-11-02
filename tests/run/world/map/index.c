/* ctest run_world_map. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "tables/blocks/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    MAP_TEST_ARENA_BYTES = 32ull << 20,
    MAP_TEST_POOL_BYTES = 16ull << 20,
};
enum : uint32_t {
    MAP_TEST_REQUESTS = 512u,
};

/* A map over an arena and a pool of its own, so that a test can ask for any radius. */
typedef struct map_test_t {
    arena_t arena;
    arena_t arena_pool;
    section_pool_t pool;
    world_map_t map;
    uint8_t _pad0[32];
} map_test_t;
static_assert(sizeof(map_test_t) == 384);

/* The table as brute force sees it, indexed by a slot worked out with plain division. */
typedef struct map_model_slot_t {
    int32_t chunk_x;
    int32_t chunk_y;
    uint32_t state;
    uint32_t stamp;
} map_model_slot_t;
static_assert(sizeof(map_model_slot_t) == 16);

static map_test_t *map_test_begin(test_context_t *t, uint32_t radius, int32_t center_x, int32_t center_y) {
    uint8_t *const raw = test_alloc(t, sizeof(map_test_t) + 64u);
    map_test_t *const test = (map_test_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(&test->arena, "map_test", test_alloc(t, MAP_TEST_ARENA_BYTES), MAP_TEST_ARENA_BYTES);
    arena_init(&test->arena_pool, "map_test_pool", test_alloc(t, MAP_TEST_POOL_BYTES), MAP_TEST_POOL_BYTES);
    section_pool_init(&test->pool, &test->arena_pool);
    world_map_init(&test->map, &test->arena, &test->pool, radius, center_x, center_y);
    return test;
}

static void map_test_end(map_test_t *test) {
    world_map_deinit(&test->map);
    section_pool_deinit(&test->pool);
    arena_deinit(&test->arena_pool);
    arena_deinit(&test->arena);
}

static uint64_t map_test_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

/* Not by the shortcut the map takes. */
static uint32_t map_test_slot(const world_map_t *map, int32_t chunk_x, int32_t chunk_y) {
    const int64_t side = map->side;
    const int64_t x = (((int64_t)chunk_x % side) + side) % side;
    const int64_t y = (((int64_t)chunk_y % side) + side) % side;
    return (uint32_t)(y * side + x);
}

static int64_t map_test_distance2(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    const int64_t dx = (int64_t)ax - bx;
    const int64_t dy = (int64_t)ay - by;
    return dx * dx + dy * dy;
}

static bool map_test_wanted(const world_map_t *map, int32_t chunk_x, int32_t chunk_y) {
    return map_test_distance2(chunk_x, chunk_y, map->center_x, map->center_y) <=
           (int64_t)map->view_radius * map->view_radius;
}

/* Takes and fills everything on offer. Returns how many. */
static uint32_t map_test_fill_all(test_context_t *t, world_map_t *map) {
    world_map_request_t requests[MAP_TEST_REQUESTS];
    uint32_t total = 0;
    for (;;) {
        const uint32_t taken = world_map_stream_take(map, requests, MAP_TEST_REQUESTS);
        if (taken == 0u) {
            return total;
        }
        for (uint32_t i = 0; i < taken; i++) {
            CHECK(world_map_stream_commit(map, &requests[i]) != nullptr);
        }
        total += taken;
    }
}

/* ---- The table ---------------------------------------------------------------------------------------------------- */

TEST(the_radius_for_a_count_is_the_largest_that_fits) {
    CHECK_EQ_U64(world_map_radius_for(1u), 0u);
    CHECK_EQ_U64(world_map_radius_for(8u), 0u);
    CHECK_EQ_U64(world_map_radius_for(9u), 1u);
    CHECK_EQ_U64(world_map_radius_for(24u), 1u);
    CHECK_EQ_U64(world_map_radius_for(25u), 2u);
    CHECK_EQ_U64(world_map_radius_for(1025ull * 1025ull - 1ull), 511u);
    CHECK_EQ_U64(world_map_radius_for(1025ull * 1025ull), 512u);
    CHECK_EQ_U64(world_map_radius_for(UINT64_MAX), WORLD_MAP_RADIUS_MAX);
    CHECK_EQ_U64(world_map_radius_for(WORLD_COLUMN_COUNT_DEFAULT), MC_GAME_RENDER_DISTANCE_MAX);
}

/* Wherever the window is, including across zero and at the ends of what a coordinate can reach. */
TEST(each_chunk_in_the_window_has_its_own_slot_and_none_outside_has_one) {
    const int32_t centers[][2] = {
        {0, 0},
        {-1, -1},
        {3, -5},
        {100, 7},
        {-1000, 1000},
        {column_chunk_of(INT32_MAX), column_chunk_of(INT32_MIN)},
        {column_chunk_of(INT32_MIN), column_chunk_of(INT32_MAX)},
    };
    const uint32_t radius = 3u;
    map_test_t *const test = map_test_begin(t, radius, 0, 0);
    world_map_t *const map = &test->map;
    uint8_t *const seen = test_alloc(t, map->slot_count);
    for (uint32_t c = 0; c < sizeof(centers) / sizeof(centers[0]); c++) {
        world_map_set_center(map, centers[c][0], centers[c][1]);
        memset(seen, 0, map->slot_count);
        for (int32_t dy = -6; dy <= 6; dy++) {
            for (int32_t dx = -6; dx <= 6; dx++) {
                const int32_t x = centers[c][0] + dx;
                const int32_t y = centers[c][1] + dy;
                uint32_t slot = UINT32_MAX;
                const bool inside = dx >= -3 && dx <= 3 && dy >= -3 && dy <= 3;
                CHECK_EQ_U64(world_map_slot_of(map, x, y, &slot) ? 1u : 0u, inside ? 1u : 0u);
                if (inside) {
                    CHECK_EQ_U64(slot, map_test_slot(map, x, y));
                    REQUIRE(slot < map->slot_count);
                    CHECK_EQ_U64(seen[slot], 0u);
                    seen[slot] = 1u;
                } else {
                    CHECK_EQ_U64(slot, UINT32_MAX);
                }
            }
        }
    }
    map_test_end(test);
}

/* What makes it toroidal: a column stays where it is, and what was written there with it. */
TEST(moving_the_center_moves_no_column) {
    map_test_t *const test = map_test_begin(t, 4u, 0, 0);
    world_map_t *const map = &test->map;
    map_test_fill_all(t, map);
    column_t *const column = world_map_column_at(map, 2, -1);
    REQUIRE(column != nullptr);
    CHECK_EQ_U64(column_block_set(column, &test->pool, 3u, 4u, 5, 7u), COLUMN_STATUS_OK);
    const uint64_t change_before = map->change_count;
    for (int32_t step = 1; step <= 3; step++) {
        world_map_set_center(map, step, -step);
        CHECK(world_map_column_at(map, 2, -1) == column);
        uint16_t block = 0;
        /* 2 * 16 + 3, -1 * 16 + 4 */
        CHECK_EQ_U64(world_map_block_get(map, 35, -12, 5, &block), WORLD_MAP_STATUS_OK);
        CHECK_EQ_U64(block, 7u);
    }
    CHECK_EQ_U64(map->change_count, change_before + 3u);
    /* Going nowhere changes nothing a snapshot could see. */
    world_map_set_center(map, 3, -3);
    CHECK_EQ_U64(map->change_count, change_before + 3u);
    map_test_end(test);
}

/* ---- Streaming ---------------------------------------------------------------------------------------------------- */

TEST(requests_come_nearest_first_and_cover_the_view_exactly) {
    const int32_t centers[][2] = {{0, 0}, {-17, 40}};
    for (uint32_t c = 0; c < 2u; c++) {
        const int32_t cx = centers[c][0];
        const int32_t cy = centers[c][1];
        map_test_t *const test = map_test_begin(t, 7u, cx, cy);
        world_map_t *const map = &test->map;
        world_map_set_view_radius(map, 5u);
        uint8_t *const offered = test_alloc(t, map->slot_count);
        int64_t last_distance = 0;
        uint32_t offered_count = 0;
        world_map_request_t request;
        while (world_map_stream_take(map, &request, 1u) == 1u) {
            const int64_t distance = map_test_distance2(request.chunk_x, request.chunk_y, cx, cy);
            CHECK(distance >= last_distance);
            CHECK(distance <= 25);
            last_distance = distance;
            CHECK_EQ_U64(request.slot, map_test_slot(map, request.chunk_x, request.chunk_y));
            CHECK_EQ_U64(offered[request.slot], 0u);
            offered[request.slot] = 1u;
            offered_count++;
            CHECK_EQ_U64(map->borrowed_slots[request.slot].state, WORLD_MAP_STATE_PENDING);
        }
        uint32_t expected = 0;
        for (int32_t dy = -7; dy <= 7; dy++) {
            for (int32_t dx = -7; dx <= 7; dx++) {
                if (dx * dx + dy * dy <= 25) {
                    expected++;
                    CHECK_EQ_U64(offered[map_test_slot(map, cx + dx, cy + dy)], 1u);
                }
            }
        }
        CHECK_EQ_U64(offered_count, expected);
        CHECK_EQ_U64(expected, 81u); /* the count of integer points within a circle of radius 5 */
        CHECK_EQ_U64(map->pending_count, expected);
        CHECK_EQ_U64(map->ready_count, 0u);
        CHECK_EQ_U64(world_map_stream_take(map, &request, 1u), 0u);
        map_test_end(test);
    }
}

TEST(the_first_request_is_the_center_and_a_batch_keeps_to_its_max) {
    map_test_t *const test = map_test_begin(t, 3u, 10, 20);
    world_map_t *const map = &test->map;
    world_map_request_t requests[8];
    CHECK_EQ_U64(world_map_stream_take(map, requests, 0u), 0u);
    memset(requests, 0xAB, sizeof(requests));
    CHECK_EQ_U64(world_map_stream_take(map, requests, 5u), 5u);
    CHECK_EQ_I64(requests[0].chunk_x, 10);
    CHECK_EQ_I64(requests[0].chunk_y, 20);
    /* Past the max, nothing written. */
    CHECK_EQ_U64(requests[5].slot, 0xABABABABu);
    for (uint32_t i = 1; i < 5u; i++) {
        CHECK_EQ_I64(map_test_distance2(requests[i].chunk_x, requests[i].chunk_y, 10, 20), 1);
    }
    map_test_end(test);
}

TEST(a_request_that_comes_back_becomes_an_empty_column) {
    map_test_t *const test = map_test_begin(t, 2u, -3, 4);
    world_map_t *const map = &test->map;
    world_map_request_t request;
    REQUIRE(world_map_stream_take(map, &request, 1u) == 1u);
    CHECK(world_map_column_at(map, -3, 4) == nullptr);
    uint16_t block = 99u;
    CHECK_EQ_U64(world_map_block_get(map, -48, 64, 0, &block), WORLD_MAP_STATUS_NOT_LOADED);
    CHECK_EQ_U64(block, 99u);
    const uint64_t change_before = map->change_count;
    column_t *const column = world_map_stream_commit(map, &request);
    REQUIRE(column != nullptr);
    CHECK_EQ_U64(map->change_count, change_before + 1u);
    CHECK(world_map_column_at(map, -3, 4) == column);
    CHECK_EQ_I64(column->chunk_x, -3);
    CHECK_EQ_I64(column->chunk_y, 4);
    CHECK_EQ_U64(map->ready_count, 1u);
    CHECK_EQ_U64(map->pending_count, 0u);
    CHECK_EQ_U64(world_map_block_get(map, -48, 64, 0, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    uint8_t light = 0;
    CHECK_EQ_U64(world_map_light_get(map, -48, 64, 0, &light), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(light, SECTION_LIGHT_OPEN_SKY);
    /* Only once. */
    CHECK(world_map_stream_commit(map, &request) == nullptr);
    CHECK(!world_map_stream_cancel(map, &request));
    CHECK_EQ_U64(map->ready_count, 1u);
    map_test_end(test);
}

TEST(a_cancelled_request_is_offered_again_and_the_old_one_is_out_of_date) {
    map_test_t *const test = map_test_begin(t, 2u, 0, 0);
    world_map_t *const map = &test->map;
    world_map_request_t first;
    REQUIRE(world_map_stream_take(map, &first, 1u) == 1u);
    CHECK(world_map_stream_cancel(map, &first));
    CHECK_EQ_U64(map->pending_count, 0u);
    CHECK(!world_map_stream_cancel(map, &first));
    world_map_request_t second;
    REQUIRE(world_map_stream_take(map, &second, 1u) == 1u);
    CHECK_EQ_I64(second.chunk_x, first.chunk_x);
    CHECK_EQ_I64(second.chunk_y, first.chunk_y);
    CHECK_EQ_U64(second.slot, first.slot);
    CHECK(second.generation != first.generation);
    /* A slow job for the old request cannot fill the new one's place. */
    CHECK(world_map_stream_commit(map, &first) == nullptr);
    CHECK_EQ_U64(map->pending_count, 1u);
    CHECK(world_map_stream_commit(map, &second) != nullptr);
    map_test_end(test);
}

TEST(a_request_the_center_has_left_behind_is_dropped) {
    map_test_t *const test = map_test_begin(t, 5u, 0, 0);
    world_map_t *const map = &test->map;
    world_map_set_view_radius(map, 1u);
    world_map_request_t requests[8];
    REQUIRE(world_map_stream_take(map, requests, 8u) == 5u);
    world_map_set_center(map, 100, 100);
    CHECK_EQ_U64(map->pending_count, 0u);
    for (uint32_t i = 0; i < 5u; i++) {
        CHECK(world_map_stream_commit(map, &requests[i]) == nullptr);
        CHECK(!world_map_stream_cancel(map, &requests[i]));
    }
    CHECK_EQ_U64(map->active_count, 0u);
    map_test_end(test);
}

/* A pending request still in range survives a move. */
TEST(a_request_still_in_range_after_a_move_stands) {
    map_test_t *const test = map_test_begin(t, 5u, 0, 0);
    world_map_t *const map = &test->map;
    world_map_set_view_radius(map, 3u);
    world_map_request_t request;
    REQUIRE(world_map_stream_take(map, &request, 1u) == 1u);
    world_map_set_center(map, 1, 0);
    CHECK_EQ_U64(map->pending_count, 1u);
    CHECK(world_map_stream_commit(map, &request) != nullptr);
    /* And is not offered a second time. */
    world_map_request_t others[64];
    const uint32_t taken = world_map_stream_take(map, others, 64u);
    for (uint32_t i = 0; i < taken; i++) {
        CHECK(!(others[i].chunk_x == 0 && others[i].chunk_y == 0));
    }
    map_test_end(test);
}

TEST(a_column_stays_for_the_margin_then_leaves_until_unloaded) {
    map_test_t *const test = map_test_begin(t, 10u, 0, 0);
    world_map_t *const map = &test->map;
    world_map_set_view_radius(map, 1u);
    CHECK_EQ_U64(map_test_fill_all(t, map), 5u);
    world_map_leaving_t leaving;
    CHECK(!world_map_stream_leaving(map, &leaving));
    /* The view is 1 and the margin 2, so a column may be 3 away: -1,0 is exactly that from here, and stays. */
    world_map_set_center(map, 2, 0);
    CHECK_EQ_U64(map->leaving_count, 0u);
    /* From here -1,0 is 4 away and 0,1 and 0,-1 are the root of 10, while 0,0 at 3 and 1,0 at 2 stay. */
    world_map_set_center(map, 3, 0);
    CHECK_EQ_U64(map->leaving_count, 3u);
    /* Each is still there, and goes on being offered, round and round. */
    uint32_t offers[3] = {0u, 0u, 0u};
    for (uint32_t i = 0; i < 9u; i++) {
        REQUIRE(world_map_stream_leaving(map, &leaving));
        CHECK(world_map_column_at(map, leaving.chunk_x, leaving.chunk_y) != nullptr);
        CHECK_EQ_U64(leaving.slot, map_test_slot(map, leaving.chunk_x, leaving.chunk_y));
        if (leaving.chunk_x == -1 && leaving.chunk_y == 0) {
            offers[0]++;
        } else if (leaving.chunk_x == 0 && leaving.chunk_y == 1) {
            offers[1]++;
        } else if (leaving.chunk_x == 0 && leaving.chunk_y == -1) {
            offers[2]++;
        } else {
            CHECK(false);
        }
    }
    CHECK_EQ_U64(offers[0], 3u);
    CHECK_EQ_U64(offers[1], 3u);
    CHECK_EQ_U64(offers[2], 3u);
    /* The center comes back: no longer leaving, and nothing has been lost. */
    world_map_set_center(map, 0, 0);
    CHECK_EQ_U64(map->leaving_count, 0u);
    CHECK(!world_map_stream_leaving(map, &leaving));
    CHECK(world_map_column_at(map, -1, 0) != nullptr);
    /* Away again, and this time they go. */
    world_map_set_center(map, 3, 0);
    uint32_t unloaded = 0;
    while (world_map_stream_leaving(map, &leaving)) {
        world_map_unload(map, leaving.slot);
        unloaded++;
    }
    CHECK_EQ_U64(unloaded, 3u);
    CHECK_EQ_U64(map->leaving_count, 0u);
    CHECK_EQ_U64(map->ready_count, 2u);
    CHECK(world_map_column_at(map, -1, 0) == nullptr);
    CHECK(world_map_column_at(map, 0, 0) != nullptr);
    /* Back, and they are wanted once more. */
    world_map_set_center(map, 0, 0);
    world_map_request_t request;
    uint32_t found = 0;
    uint32_t total = 0;
    while (world_map_stream_take(map, &request, 1u) == 1u) {
        found += request.chunk_x == -1 && request.chunk_y == 0 ? 1u : 0u;
        total++;
    }
    CHECK_EQ_U64(found, 1u);
    CHECK_EQ_U64(total, 3u);
    map_test_end(test);
}

/* The one case where a slot has an old occupant: a center that has moved by a whole side. */
TEST(a_place_held_by_a_column_leaving_is_offered_once_it_has_gone) {
    map_test_t *const test = map_test_begin(t, 1u, 0, 0);
    world_map_t *const map = &test->map;
    CHECK_EQ_U64(map_test_fill_all(t, map), 5u); /* a radius of 1 is a cross */
    /* 3 is the side: every place of the new window is in a slot the old one used. */
    world_map_set_center(map, 3, 0);
    CHECK_EQ_U64(map->leaving_count, 5u);
    /* The old center and its new twin share a slot. Everything in the new cross is blocked by one in the old. */
    world_map_request_t request;
    CHECK_EQ_U64(world_map_stream_take(map, &request, 1u), 0u);
    /* The old column does not answer for the new coordinates, though the slot is the same. */
    CHECK(world_map_column_at(map, 3, 0) == nullptr);
    CHECK(world_map_column_at(map, 0, 0) == nullptr); /* out of the window, while still in memory */
    world_map_leaving_t leaving;
    uint32_t unloaded = 0;
    while (world_map_stream_leaving(map, &leaving)) {
        world_map_unload(map, leaving.slot);
        unloaded++;
    }
    CHECK_EQ_U64(unloaded, 5u);
    CHECK_EQ_U64(map_test_fill_all(t, map), 5u);
    CHECK(world_map_column_at(map, 3, 0) != nullptr);
    CHECK_EQ_I64(world_map_column_at(map, 3, 0)->chunk_x, 3);
    map_test_end(test);
}

TEST(the_view_radius_narrows_and_widens) {
    map_test_t *const test = map_test_begin(t, 6u, 0, 0);
    world_map_t *const map = &test->map;
    CHECK_EQ_U64(map->view_radius, 6u);
    world_map_set_view_radius(map, 5u);
    CHECK_EQ_U64(map_test_fill_all(t, map), 81u);
    /* Requests beyond the new view are dropped, columns within the margin stay, and others leave. */
    world_map_set_view_radius(map, 1u);
    world_map_leaving_t leaving;
    uint32_t left = 0;
    while (world_map_stream_leaving(map, &leaving)) {
        CHECK(map_test_distance2(leaving.chunk_x, leaving.chunk_y, 0, 0) > 9);
        world_map_unload(map, leaving.slot);
        left++;
    }
    /* 29 points are within a radius of 3. */
    CHECK_EQ_U64(left, 81u - 29u);
    CHECK_EQ_U64(map->ready_count, 29u);
    CHECK_EQ_U64(map_test_fill_all(t, map), 0u);
    world_map_set_view_radius(map, 3u);
    CHECK_EQ_U64(map_test_fill_all(t, map), 0u);
    world_map_set_view_radius(map, 6u);
    CHECK_EQ_U64(map_test_fill_all(t, map), 113u - 29u); /* 113 points within a radius of 6 */
    map_test_end(test);
}

TEST(a_table_of_radius_zero_still_works) {
    map_test_t *const test = map_test_begin(t, 0u, 5, 5);
    world_map_t *const map = &test->map;
    CHECK_EQ_U64(map->slot_count, 1u);
    CHECK_EQ_U64(map_test_fill_all(t, map), 1u);
    CHECK(world_map_column_at(map, 5, 5) != nullptr);
    world_map_set_center(map, 6, 5);
    CHECK(world_map_column_at(map, 5, 5) == nullptr);
    world_map_request_t request;
    CHECK_EQ_U64(world_map_stream_take(map, &request, 1u), 0u);
    world_map_leaving_t leaving;
    REQUIRE(world_map_stream_leaving(map, &leaving));
    world_map_unload(map, leaving.slot);
    CHECK_EQ_U64(map_test_fill_all(t, map), 1u);
    map_test_end(test);
}

/* ---- Voxels ------------------------------------------------------------------------------------------------------- */

TEST(z_is_refused_past_gameplay_whether_or_not_a_column_is_there) {
    map_test_t *const test = map_test_begin(t, 1u, 0, 0);
    world_map_t *const map = &test->map;
    uint16_t block = 77u;
    uint8_t light = 77u;
    /* Nothing loaded. */
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, -251, &block), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, 501, &block), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, -250, &block), WORLD_MAP_STATUS_NOT_LOADED);
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, 500, &block), WORLD_MAP_STATUS_NOT_LOADED);
    CHECK_EQ_U64(world_map_light_get(map, 0, 0, -251, &light), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_map_light_get(map, 0, 0, 501, &light), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_map_light_get(map, 0, 0, 500, &light), WORLD_MAP_STATUS_NOT_LOADED);
    map_test_fill_all(t, map);
    /* Loaded. */
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, -251, &block), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, 501, &block), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_map_light_get(map, 0, 0, -251, &light), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_map_light_get(map, 0, 0, 501, &light), WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(block, 77u);
    CHECK_EQ_U64(light, 77u);
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, -250, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    block = 77u;
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, 500, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK_EQ_U64(world_map_light_get(map, 0, 0, 500, &light), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(light, SECTION_LIGHT_OPEN_SKY);
    map_test_end(test);
}

TEST(a_read_finds_each_voxel_in_its_own_column_either_side_of_zero) {
    map_test_t *const test = map_test_begin(t, 1u, 0, 0);
    world_map_t *const map = &test->map;
    map_test_fill_all(t, map);
    column_t *const here = world_map_column_at(map, 0, 0);
    column_t *const west = world_map_column_at(map, -1, 0);
    column_t *const south = world_map_column_at(map, 0, -1);
    REQUIRE(here != nullptr && west != nullptr && south != nullptr);
    CHECK_EQ_U64(column_block_set(here, &test->pool, 0u, 0u, 7, 1u), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_block_set(west, &test->pool, 15u, 0u, 7, 2u), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_block_set(south, &test->pool, 0u, 15u, 7, 3u), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_light_set(west, &test->pool, 15u, 0u, 7, 0x12u), COLUMN_STATUS_OK);
    uint16_t block = 0;
    uint8_t light = 0;
    CHECK_EQ_U64(world_map_block_get(map, 0, 0, 7, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 1u);
    CHECK_EQ_U64(world_map_block_get(map, -1, 0, 7, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 2u);
    CHECK_EQ_U64(world_map_block_get(map, 0, -1, 7, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 3u);
    CHECK_EQ_U64(world_map_light_get(map, -1, 0, 7, &light), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(light, 0x12u);
    /* The corner chunk at -1,-1 is outside a radius of 1 from the center, so was never asked for. */
    CHECK_EQ_U64(world_map_block_get(map, -1, -1, 7, &block), WORLD_MAP_STATUS_NOT_LOADED);
    /* Outside the window altogether. */
    CHECK_EQ_U64(world_map_block_get(map, 32, 0, 7, &block), WORLD_MAP_STATUS_NOT_LOADED);
    map_test_end(test);
}

/* ---- Everything at once, against brute force ------------------------------------------------------------------------ */

static uint16_t map_test_stamp_block(uint32_t stamp) {
    return (uint16_t)(stamp % 4000u + 1u);
}

/* The map's whole answer, against the model's, by looking at every slot and more of the plane than the window. */
static void map_test_compare(test_context_t *t, map_test_t *test, const map_model_slot_t *model) {
    world_map_t *const map = &test->map;
    uint32_t ready = 0;
    uint32_t pending = 0;
    uint32_t leaving = 0;
    const int64_t keep = (int64_t)map->view_radius + WORLD_MAP_KEEP_EXTRA;
    const int64_t table_radius = map->table_radius;
    for (uint32_t slot = 0; slot < map->slot_count; slot++) {
        const map_model_slot_t *const expected = &model[slot];
        const world_map_slot_t *const actual = &map->borrowed_slots[slot];
        CHECK_EQ_U64(actual->state, expected->state);
        if (expected->state == WORLD_MAP_STATE_EMPTY) {
            continue;
        }
        CHECK_EQ_I64(actual->chunk_x, expected->chunk_x);
        CHECK_EQ_I64(actual->chunk_y, expected->chunk_y);
        /* A place is where brute force says it is, wherever the center has gone since. */
        CHECK_EQ_U64(map_test_slot(map, expected->chunk_x, expected->chunk_y), slot);
        if (expected->state == WORLD_MAP_STATE_PENDING) {
            pending++;
            continue;
        }
        ready++;
        const int64_t dx = (int64_t)expected->chunk_x - map->center_x;
        const int64_t dy = (int64_t)expected->chunk_y - map->center_y;
        const bool in_window = dx >= -table_radius && dx <= table_radius && dy >= -table_radius && dy <= table_radius;
        const bool stays = in_window && dx * dx + dy * dy <= keep * keep;
        leaving += stays ? 0u : 1u;
        CHECK_EQ_U64(actual->leaving, stays ? 0u : 1u);
        column_t *const column = world_map_column_at(map, expected->chunk_x, expected->chunk_y);
        if (in_window) {
            CHECK(column == &map->borrowed_columns[slot]);
        } else {
            CHECK(column == nullptr);
        }
        /* What was written when it streamed in is what is there now. */
        uint16_t block = 0;
        const world_map_status_t status =
            world_map_block_get(map, expected->chunk_x * 16 + 5, expected->chunk_y * 16 + 9, 11, &block);
        if (in_window) {
            CHECK_EQ_U64(status, WORLD_MAP_STATUS_OK);
            CHECK_EQ_U64(block, map_test_stamp_block(expected->stamp));
        } else {
            CHECK_EQ_U64(status, WORLD_MAP_STATUS_NOT_LOADED);
        }
    }
    CHECK_EQ_U64(map->ready_count, ready);
    CHECK_EQ_U64(map->pending_count, pending);
    CHECK_EQ_U64(map->leaving_count, leaving);
    CHECK_EQ_U64(map->active_count, ready + pending);
}

/* Wandering, teleporting, changing the render distance and with columns arriving late or never. */
TEST(a_random_walk_agrees_with_brute_force_at_every_step) {
    const uint32_t radius = 6u;
    map_test_t *const test = map_test_begin(t, radius, 3, -2);
    world_map_t *const map = &test->map;
    map_model_slot_t *const model = test_alloc(t, map->slot_count * sizeof(map_model_slot_t));
    world_map_request_t *const out = test_alloc(t, MAP_TEST_REQUESTS * sizeof(world_map_request_t));
    world_map_request_t *const late = test_alloc(t, MAP_TEST_REQUESTS * sizeof(world_map_request_t) * 8u);
    uint32_t late_count = 0;
    uint64_t rng = 0x1234ABCD9876ull;
    uint32_t stamp = 0;
    uint64_t commits = 0;
    uint64_t stale = 0;
    uint64_t unloads = 0;
    for (uint32_t step = 0; step < 6000u; step++) {
        const uint32_t pick = (uint32_t)(map_test_random(&rng) % 100u);
        if (pick < 6u) {
            const int32_t dx = (int32_t)(map_test_random(&rng) % 5u) - 2;
            const int32_t dy = (int32_t)(map_test_random(&rng) % 5u) - 2;
            world_map_set_center(map, map->center_x + dx, map->center_y + dy);
        } else if (pick < 7u) {
            /* A teleport, sometimes by a whole number of sides so that every slot is claimed by a column far away. */
            const int32_t side = (int32_t)map->side;
            const int32_t by = (int32_t)(map_test_random(&rng) % 3u) + 1;
            world_map_set_center(map, map->center_x + by * side, map->center_y - by * side);
        } else if (pick < 8u) {
            world_map_set_view_radius(map, (uint32_t)(map_test_random(&rng) % (radius + 1u)));
        }
        /* The model's side of whatever just moved: a request now out of range is dropped. */
        for (uint32_t slot = 0; slot < map->slot_count; slot++) {
            map_model_slot_t *const entry = &model[slot];
            if (entry->state == WORLD_MAP_STATE_PENDING) {
                const int64_t dx = (int64_t)entry->chunk_x - map->center_x;
                const int64_t dy = (int64_t)entry->chunk_y - map->center_y;
                const int64_t keep = (int64_t)map->view_radius + WORLD_MAP_KEEP_EXTRA;
                const int64_t r = radius;
                if (dx < -r || dx > r || dy < -r || dy > r || dx * dx + dy * dy > keep * keep) {
                    entry->state = WORLD_MAP_STATE_EMPTY;
                }
            }
        }
        if (pick >= 8u && pick < 45u) {
            /* Requests left behind pile up here until they come back, so stop asking when there is no room. */
            const uint32_t room = MAP_TEST_REQUESTS * 8u - late_count;
            const uint32_t wish = (uint32_t)(map_test_random(&rng) % 5u);
            const uint32_t max = wish < room ? wish : room;
            const uint32_t taken = world_map_stream_take(map, out, max);
            CHECK(taken <= max);
            for (uint32_t i = 0; i < taken; i++) {
                CHECK(map_test_wanted(map, out[i].chunk_x, out[i].chunk_y));
                const uint32_t slot = map_test_slot(map, out[i].chunk_x, out[i].chunk_y);
                CHECK_EQ_U64(out[i].slot, slot);
                /* Never a place already taken. */
                CHECK_EQ_U64(model[slot].state, WORLD_MAP_STATE_EMPTY);
                model[slot] = (map_model_slot_t){out[i].chunk_x, out[i].chunk_y, WORLD_MAP_STATE_PENDING, 0u};
                late[late_count] = out[i];
                late_count++;
            }
        } else if (pick >= 45u && pick < 85u && late_count != 0u) {
            /* One comes back, in no order. Most become columns, and some never will. */
            /* Mostly a recent one, since a heap of old ones would otherwise crowd out any that can still succeed. */
            const uint32_t recent = late_count < 16u ? late_count : 16u;
            const uint32_t which = map_test_random(&rng) % 8u != 0u
                                       ? late_count - 1u - (uint32_t)(map_test_random(&rng) % recent)
                                       : (uint32_t)(map_test_random(&rng) % late_count);
            const world_map_request_t request = late[which];
            late[which] = late[late_count - 1u];
            late_count--;
            /* Only the model knows whether this request is still the one in its slot. */
            const map_model_slot_t *const entry = &model[request.slot];
            const bool current = entry->state == WORLD_MAP_STATE_PENDING && entry->chunk_x == request.chunk_x &&
                                 entry->chunk_y == request.chunk_y &&
                                 map->borrowed_slots[request.slot].generation == request.generation;
            if (map_test_random(&rng) % 8u == 0u) {
                CHECK_EQ_U64(world_map_stream_cancel(map, &request) ? 1u : 0u, current ? 1u : 0u);
                if (current) {
                    model[request.slot].state = WORLD_MAP_STATE_EMPTY;
                }
            } else {
                column_t *const column = world_map_stream_commit(map, &request);
                CHECK_EQ_U64(column != nullptr ? 1u : 0u, current ? 1u : 0u);
                if (column != nullptr) {
                    stamp++;
                    CHECK_EQ_U64(column_block_set(column, &test->pool, 5u, 9u, 11, map_test_stamp_block(stamp)),
                                 COLUMN_STATUS_OK);
                    model[request.slot].state = WORLD_MAP_STATE_READY;
                    model[request.slot].stamp = stamp;
                    commits++;
                } else {
                    stale++;
                }
            }
        } else if (pick >= 85u) {
            world_map_leaving_t leaving;
            if (world_map_stream_leaving(map, &leaving)) {
                CHECK_EQ_U64(model[leaving.slot].state, WORLD_MAP_STATE_READY);
                CHECK_EQ_I64(leaving.chunk_x, model[leaving.slot].chunk_x);
                CHECK_EQ_I64(leaving.chunk_y, model[leaving.slot].chunk_y);
                world_map_unload(map, leaving.slot);
                model[leaving.slot].state = WORLD_MAP_STATE_EMPTY;
                unloads++;
            }
        }
        map_test_compare(t, test, model);
        if (t->failure_count != 0u) {
            break;
        }
    }
    /* Everything this walk is meant to reach, it reached. */
    printf("# a walk of 6000 steps: %" PRIu64 " commits, %" PRIu64 " out of date, %" PRIu64 " unloads\n", commits,
           stale, unloads);
    CHECK(commits > 500u);
    CHECK(stale > 20u);
    CHECK(unloads > 200u);

    /* Settle: nothing late any more, nothing left to leave, everything wanted fetched. Then the set is exact. */
    for (uint32_t i = 0; i < late_count; i++) {
        (void)world_map_stream_cancel(map, &late[i]);
    }
    world_map_leaving_t leaving;
    while (world_map_stream_leaving(map, &leaving)) {
        world_map_unload(map, leaving.slot);
    }
    map_test_fill_all(t, map);
    CHECK_EQ_U64(map->pending_count, 0u);
    for (int32_t dy = -(int32_t)radius; dy <= (int32_t)radius; dy++) {
        for (int32_t dx = -(int32_t)radius; dx <= (int32_t)radius; dx++) {
            const int64_t distance = (int64_t)dx * dx + (int64_t)dy * dy;
            column_t *const column = world_map_column_at(map, map->center_x + dx, map->center_y + dy);
            if (distance <= (int64_t)map->view_radius * map->view_radius) {
                CHECK(column != nullptr);
            }
            const int64_t keep = (int64_t)map->view_radius + WORLD_MAP_KEEP_EXTRA;
            if (distance > keep * keep) {
                CHECK(column == nullptr);
            }
        }
    }
    world_map_deinit(map);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    map_test_end(test);
}

/* ---- Memory ------------------------------------------------------------------------------------------------------- */

/* The heap's counters are the standard's test for a hot path, and carved_bytes for what the pool takes. */
TEST(walking_the_world_takes_no_new_memory_after_the_first_round) {
    map_test_t *const test = map_test_begin(t, 6u, 0, 0);
    world_map_t *const map = &test->map;
    world_map_set_view_radius(map, 4u);
    world_map_request_t requests[MAP_TEST_REQUESTS];
    uint64_t carved_after_first = 0;
    heap_counters_t before;
    heap_counters_t after;
    heap_counters_read(&before);
    const uint64_t arena_used = test->arena.used;
    for (uint32_t round = 0; round < 40u; round++) {
        /* Far enough that every column changes. */
        world_map_set_center(map, (int32_t)(round + 1u) * 40, 0);
        world_map_leaving_t leaving;
        while (world_map_stream_leaving(map, &leaving)) {
            world_map_unload(map, leaving.slot);
        }
        const uint32_t taken = world_map_stream_take(map, requests, MAP_TEST_REQUESTS);
        CHECK(taken != 0u);
        for (uint32_t i = 0; i < taken; i++) {
            column_t *const column = world_map_stream_commit(map, &requests[i]);
            REQUIRE(column != nullptr);
            CHECK_EQ_U64(column_block_set(column, &test->pool, 1u, 2u, (int32_t)i, (uint16_t)(i + 1u)),
                         COLUMN_STATUS_OK);
        }
        if (round == 0u) {
            carved_after_first = test->pool.carved_bytes;
        }
    }
    CHECK(carved_after_first != 0u);
    CHECK_EQ_U64(test->pool.carved_bytes, carved_after_first);
    CHECK_EQ_U64(test->arena.used, arena_used);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    world_map_deinit(map);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    map_test_end(test);
}

TEST(a_map_takes_from_its_arena_no_more_than_it_says) {
    const uint32_t radii[] = {0u, 1u, 2u, 3u, 16u, 32u, 64u};
    for (uint32_t r = 0; r < sizeof(radii) / sizeof(radii[0]); r++) {
        map_test_t *const test = map_test_begin(t, radii[r], 0, 0);
        const uint64_t slots = test->map.slot_count;
        CHECK(test->arena.used <= slots * (sizeof(column_t) + WORLD_MAP_BYTES_PER_SLOT) + WORLD_MAP_BYTES_FIXED);
        CHECK(test->arena.used >= slots * sizeof(column_t));
        map_test_end(test);
    }
}

TEST(deinit_is_safe_on_zero_bytes) {
    uint8_t *const raw = test_alloc(t, sizeof(world_map_t));
    world_map_t *const map = (world_map_t *)(void *)raw;
    world_map_deinit(map);
    CHECK_EQ_U64(map->slot_count, 0u);
}

/* ---- world_open: the Owner --------------------------------------------------------------------------------------- */

static world_t *map_test_world(test_context_t *t) {
    uint8_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
    return (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
}

/* The refused radius is well past the limit, since the heap rounds a region up, and that much room is real. */
TEST(a_world_opens_at_the_table_its_limits_allow_and_refuses_one_past_them) {
    world_t *const world = map_test_world(t);
    const uint64_t columns = 49u; /* a radius of 3 */
    REQUIRE(world_init(world, (world_limits_t){32ull << 20, columns}));
    CHECK(!world_open(world, 200u, 0, 0));
    CHECK(world->map.borrowed_slots == nullptr);
    CHECK_EQ_U64(world->arena_columns.used, 0u);
    CHECK(!world_open(world, WORLD_MAP_RADIUS_MAX + 1u, 0, 0));
    CHECK_EQ_U64(world->arena_columns.used, 0u);
    const uint32_t radius = world_map_radius_for(columns);
    CHECK_EQ_U64(radius, 3u);
    REQUIRE(world_open(world, radius, 7, 8));
    CHECK_EQ_I64(world->map.center_x, 7);
    CHECK_EQ_I64(world->map.center_y, 8);
    CHECK(world->arena_columns.used <= world_open_bytes(radius));
    CHECK_EQ_U64(world->edit.log_capacity, WORLD_EDIT_LOG_CAPACITY_DEFAULT);
    CHECK_EQ_U64(world->snapshot.section_capacity, WORLD_EDIT_SNAPSHOT_SECTIONS_DEFAULT);
    world_deinit(world); /* still open: this ends it too */
    CHECK_EQ_U64(world->owned_columns.bytes, 0u);
}

TEST(a_closed_world_gives_back_everything_and_opens_again) {
    world_t *const world = map_test_world(t);
    heap_counters_t before;
    heap_counters_read(&before);
    REQUIRE(world_init(world, (world_limits_t){32ull << 20, 81u}));
    uint64_t arena_used = 0;
    for (uint32_t round = 0; round < 3u; round++) {
        REQUIRE(world_open(world, 4u, (int32_t)round, 0));
        if (round == 0u) {
            arena_used = world->arena_columns.used;
        }
        CHECK_EQ_U64(world->arena_columns.used, arena_used);
        map_test_fill_all(t, &world->map);
        CHECK(world->map.ready_count != 0u);
        CHECK_EQ_U64(world_edit_block_set(&world->edit, 0, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
        CHECK(world->section_pool.live_bytes != 0u);
        world_close(world);
        CHECK(world->map.borrowed_slots == nullptr);
        CHECK_EQ_U64(world->arena_columns.used, 0u);
        CHECK_EQ_U64(world->arena_sections.used, 0u);
        CHECK_EQ_U64(world->section_pool.live_bytes, 0u);
        world_close(world); /* does nothing */
    }
    world_deinit(world);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
}

/* The real default limits, at the largest render distance there is. This does touch its tables: 25 MiB of slots, 3 of
 * offsets and 42 of edit records. */
TEST(the_default_limits_hold_a_table_of_the_largest_render_distance) {
    world_t *const world = map_test_world(t);
    const world_limits_t limits = world_limits_default();
    REQUIRE(world_init(world, limits));
    const uint32_t radius = world_map_radius_for(limits.column_count);
    CHECK_EQ_U64(radius, MC_GAME_RENDER_DISTANCE_MAX);
    REQUIRE(world_open(world, radius, 0, 0));
    CHECK(world->arena_columns.used <= world_open_bytes(radius));
    CHECK(world->arena_columns.used <= world->arena_columns.capacity);
    /* A little of it, all the way out at the corner of the view. */
    world_map_set_view_radius(&world->map, 2u);
    CHECK_EQ_U64(map_test_fill_all(t, &world->map), 13u);
    world_map_set_center(&world->map, 500, -500);
    CHECK_EQ_U64(map_test_fill_all(t, &world->map), 13u);
    CHECK(world_map_column_at(&world->map, 500, -500) != nullptr);
    world_close(world);
    world_deinit(world);
}

TEST_MAIN()
