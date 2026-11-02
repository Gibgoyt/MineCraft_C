/* A small real world with nothing in it, for tests of anything that moves: a real world_t, with every column in view streamed
 * in as air. Columns further than radius from the center are not loaded, which is what a test of that wants. */
#ifndef MC_TESTS_RUN_PHYSICS_FIXTURES_TEST_WORLD_H
#define MC_TESTS_RUN_PHYSICS_FIXTURES_TEST_WORLD_H

#include "harness/index.h"

#include "physics/index.h"
#include "world/index.h"

#include <stdint.h>

/* Reaches chunks at distance 3: a chunk's square from -48 up to 64 along an axis, and not (3, 3), or (4, 0). */
enum : uint32_t { TEST_WORLD_RADIUS = 3u };

typedef struct test_world_t {
    world_t *world;     /* 0 */
    physics_t *physics; /* 8 */
    world_map_t *map;   /* 16 */
} test_world_t;
static_assert(sizeof(test_world_t) == 24);

[[maybe_unused]] static void *test_world_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* Lets go of what has left the view, and gives every column on offer an empty column. */
[[maybe_unused]] static void test_world_stream_all(test_context_t *t, test_world_t *w) {
    world_map_leaving_t leaving;
    while (world_map_stream_leaving(w->map, &leaving)) {
        world_map_unload(w->map, leaving.slot);
    }
    world_map_request_t request;
    while (world_map_stream_take(w->map, &request, 1u) == 1u) {
        column_t *const column = world_map_stream_commit(w->map, &request);
        CHECK(column != nullptr);
    }
}

[[maybe_unused]] static bool test_world_begin(test_context_t *t, test_world_t *w) {
    w->world = test_world_aligned(t, sizeof(world_t));
    w->physics = test_world_aligned(t, sizeof(physics_t));
    if (!CHECK(world_init(w->world, (world_limits_t){256ull << 20, 64u})) ||
        !CHECK(world_open(w->world, TEST_WORLD_RADIUS, 0, 0)) || !CHECK(physics_init(w->physics))) {
        return false;
    }
    w->map = &w->world->map;
    test_world_stream_all(t, w);
    return true;
}

[[maybe_unused]] static void test_world_end(test_world_t *w) {
    physics_deinit(w->physics);
    world_close(w->world);
    world_deinit(w->world);
}

/* Every cell from one corner to the other, both included. */
[[maybe_unused]] static void test_world_fill(test_context_t *t, test_world_t *w, int32_t x0, int32_t y0, int32_t z0,
                                             int32_t x1, int32_t y1, int32_t z1, uint16_t block) {
    for (int32_t x = x0; x <= x1; x++) {
        for (int32_t y = y0; y <= y1; y++) {
            column_t *const column = world_map_column_at(w->map, column_chunk_of(x), column_chunk_of(y));
            if (!CHECK(column != nullptr)) {
                return;
            }
            for (int32_t z = z0; z <= z1; z++) {
                const column_status_t status =
                    column_block_set(column, &w->world->section_pool, column_local_of(x), column_local_of(y), z, block);
                CHECK_EQ_U64(status, COLUMN_STATUS_OK);
            }
        }
    }
}

#endif
