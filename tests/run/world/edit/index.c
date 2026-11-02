/* ctest run_world_edit. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "tables/blocks/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    EDIT_TEST_ARENA_BYTES = 64ull << 20,
    EDIT_TEST_POOL_BYTES = 192ull << 20,
};
enum : uint32_t {
    /* z from -250 to 500, both included, in a 16x16 column. */
    EDIT_TEST_HEIGHT = COLUMN_Z_GAMEPLAY_MAX - COLUMN_Z_GAMEPLAY_MIN + 1,
    EDIT_TEST_COLUMN_VOXELS = COLUMN_SIZE * COLUMN_SIZE * EDIT_TEST_HEIGHT,
    EDIT_TEST_STONE = 1u,
};

/* A map, its edits and its snapshots, over memory of their own, so that a test can ask for any size. */
typedef struct edit_test_t {
    arena_t arena;
    arena_t arena_pool;
    section_pool_t pool;
    world_map_t map;
    world_edit_t edit;
    world_edit_snapshot_t snapshot;
} edit_test_t;
static_assert(sizeof(edit_test_t) == 1024);

/* What a reader builds from nothing but views. */
typedef struct edit_mirror_t {
    uint16_t *borrowed_blocks;      /* slot, then section, then voxel */
    uint8_t *borrowed_light;        /* the same */
    uint32_t *borrowed_generations; /* slot */
    uint64_t slot_count;
    uint64_t last_sequence;
    uint64_t error_count;
    uint64_t view_count;
    uint64_t section_count;
} edit_mirror_t;
static_assert(sizeof(edit_mirror_t) == 64);

typedef struct edit_reader_t {
    edit_test_t *borrowed_test;
    edit_mirror_t *borrowed_mirror;
    _Atomic uint32_t stop;
    uint8_t _pad0[4];
} edit_reader_t;
static_assert(sizeof(edit_reader_t) == 24);

static uint64_t edit_test_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

/* Every column in the view is live and empty when this returns. */
static edit_test_t *edit_test_begin(test_context_t *t, uint32_t table_radius, uint64_t log_capacity,
                                    uint32_t section_capacity) {
    uint8_t *const raw = test_alloc(t, sizeof(edit_test_t) + 64u);
    edit_test_t *const test = (edit_test_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(&test->arena, "edit_test", test_alloc(t, EDIT_TEST_ARENA_BYTES), EDIT_TEST_ARENA_BYTES);
    arena_init(&test->arena_pool, "edit_test_pool", test_alloc(t, EDIT_TEST_POOL_BYTES), EDIT_TEST_POOL_BYTES);
    section_pool_init(&test->pool, &test->arena_pool);
    world_map_init(&test->map, &test->arena, &test->pool, table_radius, 0, 0);
    world_edit_init(&test->edit, &test->arena, &test->map, log_capacity);
    world_edit_snapshot_init(&test->snapshot, &test->arena, &test->edit, section_capacity);
    world_map_request_t request;
    while (world_map_stream_take(&test->map, &request, 1u) == 1u) {
        CHECK(world_map_stream_commit(&test->map, &request) != nullptr);
    }
    return test;
}

static void edit_test_end(edit_test_t *test) {
    world_edit_snapshot_deinit(&test->snapshot);
    world_edit_deinit(&test->edit);
    world_map_deinit(&test->map);
    section_pool_deinit(&test->pool);
    arena_deinit(&test->arena_pool);
    arena_deinit(&test->arena);
}

static uint32_t edit_test_slot(const edit_test_t *test, int32_t chunk_x, int32_t chunk_y) {
    uint32_t slot = UINT32_MAX;
    const bool found = world_map_slot_of(&test->map, chunk_x, chunk_y, &slot);
    return found ? slot : UINT32_MAX;
}

static uint64_t edit_test_section_bit(int32_t z) {
    return 1ull << column_section_of(z);
}

/* ---- One edit ------------------------------------------------------------------------------------------------------ */

TEST(a_write_is_logged_and_flagged_for_every_consumer_with_before_and_after) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    const uint32_t slot = edit_test_slot(test, -1, 0);
    CHECK_EQ_U64(world_edit_block_set(edit, -1, 15, 40, 9u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, -1, 15, 40, 11u), WORLD_EDIT_STATUS_CHANGED);
    uint16_t block = 0;
    CHECK_EQ_U64(world_map_block_get(&test->map, -1, 15, 40, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, 11u);
    uint64_t cursor = 0;
    uint64_t lost = 99u;
    world_edit_record_t records[4];
    CHECK_EQ_U64(world_edit_log_read(edit, &cursor, records, 4u, &lost), 2u);
    CHECK_EQ_U64(lost, 0u);
    CHECK_EQ_I64(records[0].x, -1);
    CHECK_EQ_I64(records[0].y, 15);
    CHECK_EQ_I64(records[0].z, 40);
    CHECK_EQ_U64(records[0].before, BLOCK_ID_AIR);
    CHECK_EQ_U64(records[0].after, 9u);
    CHECK_EQ_U64(records[1].before, 9u);
    CHECK_EQ_U64(records[1].after, 11u);
    for (uint32_t i = 0; i < WORLD_EDIT_CONSUMER_COUNT; i++) {
        CHECK_EQ_U64(world_edit_dirty_peek(edit, slot, (world_edit_consumer_t)i), edit_test_section_bit(40));
    }
    CHECK_EQ_U64(world_edit_column_revision(edit, slot), edit->revision);
    edit_test_end(test);
}

TEST(a_light_write_is_for_the_renderer_alone_and_not_logged) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    const uint32_t slot = edit_test_slot(test, 0, 0);
    CHECK_EQ_U64(world_edit_light_set(edit, 3, 4, -5, 0x3Au), WORLD_EDIT_STATUS_CHANGED);
    uint8_t light = 0;
    CHECK_EQ_U64(world_map_light_get(&test->map, 3, 4, -5, &light), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(light, 0x3Au);
    CHECK_EQ_U64(world_edit_log_position(edit), 0u);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, slot, WORLD_EDIT_CONSUMER_RENDER), edit_test_section_bit(-5));
    CHECK_EQ_U64(world_edit_dirty_peek(edit, slot, WORLD_EDIT_CONSUMER_SAVE), 0u);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, slot, WORLD_EDIT_CONSUMER_LIGHT), 0u);
    CHECK_EQ_U64(world_edit_column_revision(edit, slot), edit->revision);
    edit_test_end(test);
}

/* Nothing that was refused, or changed nothing, may be seen by anyone. */
TEST(what_changes_nothing_leaves_no_trace) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    const uint64_t revision = edit->revision;
    /* Past the limits, with a column there. */
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, -251, 1u), WORLD_EDIT_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 501, 1u), WORLD_EDIT_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_edit_light_set(edit, 0, 0, -251, 1u), WORLD_EDIT_STATUS_Z_OUT_OF_RANGE);
    CHECK_EQ_U64(world_edit_light_set(edit, 0, 0, 501, 1u), WORLD_EDIT_STATUS_Z_OUT_OF_RANGE);
    /* And with none. */
    CHECK_EQ_U64(world_edit_block_set(edit, 4096, 0, 501, 1u), WORLD_EDIT_STATUS_Z_OUT_OF_RANGE);
    /* No column: outside the window, and in a corner of it that was never asked for. */
    CHECK_EQ_U64(world_edit_block_set(edit, 4096, 0, 0, 1u), WORLD_EDIT_STATUS_NOT_LOADED);
    CHECK_EQ_U64(world_edit_block_set(edit, -1, -1, 0, 1u), WORLD_EDIT_STATUS_NOT_LOADED);
    CHECK_EQ_U64(world_edit_light_set(edit, -1, -1, 0, 1u), WORLD_EDIT_STATUS_NOT_LOADED);
    /* What is already there. */
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, BLOCK_ID_AIR), WORLD_EDIT_STATUS_UNCHANGED);
    CHECK_EQ_U64(world_edit_light_set(edit, 0, 0, 0, SECTION_LIGHT_OPEN_SKY), WORLD_EDIT_STATUS_UNCHANGED);
    CHECK_EQ_U64(edit->revision, revision);
    CHECK_EQ_U64(world_edit_log_position(edit), 0u);
    world_edit_dirty_t dirty;
    for (uint32_t i = 0; i < WORLD_EDIT_CONSUMER_COUNT; i++) {
        CHECK_EQ_U64(edit->queue_count[i], 0u);
        CHECK(!world_edit_dirty_next(edit, (world_edit_consumer_t)i, &dirty));
    }
    /* Both ends of the range do work. */
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, -250, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 500, 1u), WORLD_EDIT_STATUS_CHANGED);
    edit_test_end(test);
}

TEST(running_out_of_room_changes_nothing_and_is_not_logged) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    (void)arena_alloc(&test->arena_pool, test->arena_pool.capacity, 1u);
    const uint64_t revision = edit->revision;
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, 1u), WORLD_EDIT_STATUS_NO_MEMORY);
    CHECK_EQ_U64(world_edit_light_set(edit, 0, 0, 0, 1u), WORLD_EDIT_STATUS_NO_MEMORY);
    CHECK_EQ_U64(edit->revision, revision);
    CHECK_EQ_U64(world_edit_log_position(edit), 0u);
    CHECK_EQ_U64(edit->queue_count[WORLD_EDIT_CONSUMER_RENDER], 0u);
    uint16_t block = 99u;
    CHECK_EQ_U64(world_map_block_get(&test->map, 0, 0, 0, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    edit_test_end(test);
}

/* ---- The log ------------------------------------------------------------------------------------------------------- */

TEST(the_log_wraps_and_says_how_much_a_slow_reader_lost) {
    edit_test_t *const test = edit_test_begin(t, 0u, 8u, 4u);
    world_edit_t *const edit = &test->edit;
    uint64_t behind = 0;
    uint64_t caught_up = 0;
    world_edit_record_t records[16];
    uint64_t lost = 0;
    CHECK_EQ_U64(world_edit_log_read(edit, &caught_up, records, 16u, &lost), 0u);
    for (uint32_t i = 0; i < 20u; i++) {
        CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, (int32_t)i, (uint16_t)(i + 1u)), WORLD_EDIT_STATUS_CHANGED);
        if (i < 5u) {
            CHECK_EQ_U64(world_edit_log_read(edit, &caught_up, records, 16u, &lost), 1u);
            CHECK_EQ_U64(lost, 0u);
        }
    }
    CHECK_EQ_U64(world_edit_log_position(edit), 20u);
    /* A cursor at 5, and the oldest left is 12. A small read first. */
    CHECK_EQ_U64(world_edit_log_read(edit, &caught_up, records, 3u, &lost), 3u);
    CHECK_EQ_U64(lost, 7u);
    CHECK_EQ_U64(caught_up, 15u);
    CHECK_EQ_I64(records[0].z, 12);
    CHECK_EQ_I64(records[2].z, 14);
    /* What it has not lost, it has only once more to read. */
    CHECK_EQ_U64(world_edit_log_read(edit, &caught_up, records, 16u, &lost), 5u);
    CHECK_EQ_U64(lost, 0u);
    CHECK_EQ_I64(records[4].z, 19);
    CHECK_EQ_U64(caught_up, 20u);
    /* Another reader, with its own cursor, is not moved by any of that. */
    CHECK_EQ_U64(world_edit_log_read(edit, &behind, records, 16u, &lost), 8u);
    CHECK_EQ_U64(lost, 12u);
    CHECK_EQ_I64(records[0].z, 12);
    CHECK_EQ_I64(records[7].z, 19);
    edit_test_end(test);
}

/* ---- Flags --------------------------------------------------------------------------------------------------------- */

TEST(each_consumer_has_its_own_flags_oldest_column_first) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    const uint32_t east = edit_test_slot(test, 1, 0);
    const uint32_t west = edit_test_slot(test, -1, 0);
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, -1, 0, 100, 1u), WORLD_EDIT_STATUS_CHANGED);
    /* A second write to the first column must not put it behind: it has waited longest. */
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 200, 1u), WORLD_EDIT_STATUS_CHANGED);
    const uint64_t east_mask = edit_test_section_bit(0) | edit_test_section_bit(200);
    CHECK_EQ_U64(edit->queue_count[WORLD_EDIT_CONSUMER_RENDER], 2u);
    world_edit_dirty_t dirty;
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_RENDER, &dirty));
    CHECK_EQ_U64(dirty.slot, east);
    CHECK_EQ_U64(dirty.section_mask, east_mask);
    CHECK_EQ_I64(dirty.chunk_x, 1);
    CHECK_EQ_I64(dirty.chunk_y, 0);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, east, WORLD_EDIT_CONSUMER_RENDER), 0u);
    /* The others' flags are still there. */
    CHECK_EQ_U64(world_edit_dirty_peek(edit, east, WORLD_EDIT_CONSUMER_SAVE), east_mask);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, east, WORLD_EDIT_CONSUMER_LIGHT), east_mask);
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_RENDER, &dirty));
    CHECK_EQ_U64(dirty.slot, west);
    CHECK_EQ_U64(dirty.section_mask, edit_test_section_bit(100));
    CHECK(!world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_RENDER, &dirty));
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &dirty));
    CHECK_EQ_U64(dirty.slot, east);
    edit_test_end(test);
}

TEST(flags_given_back_wait_their_turn_and_flags_cleared_are_never_handed_over) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    const uint32_t east = edit_test_slot(test, 1, 0);
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, -1, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
    world_edit_dirty_t first;
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &first));
    CHECK_EQ_U64(first.slot, east);
    const uint64_t revision = edit->revision;
    world_edit_dirty_restore(edit, &first, WORLD_EDIT_CONSUMER_SAVE);
    CHECK_EQ_U64(edit->revision, revision); /* nothing in the world changed */
    CHECK_EQ_U64(world_edit_dirty_peek(edit, east, WORLD_EDIT_CONSUMER_SAVE), first.section_mask);
    world_edit_dirty_t second;
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &second));
    CHECK(second.slot != east);
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &second));
    CHECK_EQ_U64(second.slot, east);
    CHECK(!world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &second));

    /* Clearing one flag leaves another. Clearing all leaves a queue entry that yields nothing. */
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 100, 2u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 200, 2u), WORLD_EDIT_STATUS_CHANGED);
    world_edit_dirty_clear(edit, east, WORLD_EDIT_CONSUMER_SAVE, edit_test_section_bit(100));
    CHECK_EQ_U64(world_edit_dirty_peek(edit, east, WORLD_EDIT_CONSUMER_SAVE), edit_test_section_bit(200));
    world_edit_dirty_clear(edit, east, WORLD_EDIT_CONSUMER_SAVE, WORLD_EDIT_SECTIONS_ALL);
    CHECK(!world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &second));
    CHECK_EQ_U64(edit->queue_count[WORLD_EDIT_CONSUMER_SAVE], 0u);
    /* And the column can queue again afterwards. */
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 300, 2u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &second));
    CHECK_EQ_U64(second.section_mask, edit_test_section_bit(300));
    edit_test_end(test);
}

/* Flags belong to one load of a column. */
TEST(a_column_that_goes_takes_its_flags_with_it) {
    edit_test_t *const test = edit_test_begin(t, 0u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    world_map_t *const map = &test->map;
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
    world_edit_dirty_t held;
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_RENDER, &held));
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 100, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_SAVE) != 0u);
    world_map_unload(map, 0u);
    /* Nothing is left of it, even with no new column there. */
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_SAVE), 0u);
    world_edit_dirty_t dirty;
    CHECK(!world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &dirty));
    /* A flag given back for a column gone does nothing, once more with a new one there. */
    world_edit_dirty_restore(edit, &held, WORLD_EDIT_CONSUMER_RENDER);
    CHECK_EQ_U64(edit->queue_count[WORLD_EDIT_CONSUMER_SAVE], 0u);
    world_edit_dirty_restore(edit, &held, WORLD_EDIT_CONSUMER_SAVE);
    CHECK_EQ_U64(edit->queue_count[WORLD_EDIT_CONSUMER_SAVE], 0u);
    world_map_request_t request;
    REQUIRE(world_map_stream_take(map, &request, 1u) == 1u);
    REQUIRE(world_map_stream_commit(map, &request) != nullptr);
    world_edit_dirty_restore(edit, &held, WORLD_EDIT_CONSUMER_RENDER);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_RENDER), 0u);
    CHECK(!world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_RENDER, &dirty));
    /* It starts clean, with a top that knows nothing of the old column. */
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), WORLD_EDIT_TOP_Z_NONE);
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 1, 1u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_RENDER, &dirty));
    CHECK_EQ_U64(dirty.section_mask, edit_test_section_bit(1));
    CHECK(dirty.generation != held.generation);
    edit_test_end(test);
}

/* A queue as long as the table holds, however columns come and go with a stale entry behind them. */
TEST(columns_coming_and_going_cannot_overflow_a_queue) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    world_map_t *const map = &test->map;
    for (uint32_t round = 0; round < 200u; round++) {
        const int32_t x = (int32_t)(round % 2u) * 512;
        world_map_set_center(map, x / 16, 0);
        world_map_leaving_t leaving;
        while (world_map_stream_leaving(map, &leaving)) {
            world_map_unload(map, leaving.slot);
        }
        world_map_request_t request;
        while (world_map_stream_take(map, &request, 1u) == 1u) {
            REQUIRE(world_map_stream_commit(map, &request) != nullptr);
            const int32_t world_x = request.chunk_x * 16;
            const int32_t world_y = request.chunk_y * 16;
            CHECK_EQ_U64(world_edit_block_set(edit, world_x, world_y, 0, (uint16_t)(round + 1u)),
                         WORLD_EDIT_STATUS_CHANGED);
        }
        for (uint32_t i = 0; i < WORLD_EDIT_CONSUMER_COUNT; i++) {
            CHECK(edit->queue_count[i] <= map->slot_count);
        }
    }
    /* Each live column gives what it was flagged with last, and nothing else does. */
    world_edit_dirty_t dirty;
    uint32_t handed = 0;
    while (world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_SAVE, &dirty)) {
        CHECK_EQ_U64(dirty.section_mask, edit_test_section_bit(0));
        CHECK(world_map_column_at(map, dirty.chunk_x, dirty.chunk_y) != nullptr);
        handed++;
    }
    CHECK_EQ_U64(handed, map->ready_count);
    edit_test_end(test);
}

TEST(a_column_just_filled_is_for_render_and_light_and_is_not_logged_or_saved) {
    edit_test_t *const test = edit_test_begin(t, 0u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    column_t *const column = world_map_column_at(&test->map, 0, 0);
    REQUIRE(column != nullptr);
    /* Nothing yet: all air under open sky is what a renderer starts with. */
    world_edit_column_filled(edit, 0u);
    CHECK_EQ_U64(edit->queue_count[WORLD_EDIT_CONSUMER_RENDER], 0u);
    CHECK_EQ_U64(edit->queue_count[WORLD_EDIT_CONSUMER_LIGHT], 0u);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), WORLD_EDIT_TOP_Z_NONE);
    /* A uniform section of stone, a section of air with other light, and all the rest as they were. */
    section_block_fill(&column->sections[column_section_of(-16)], &test->pool, EDIT_TEST_STONE);
    section_light_fill(&column->sections[column_section_of(100)], &test->pool, 0x70u);
    const uint64_t mask = edit_test_section_bit(-16) | edit_test_section_bit(100);
    world_edit_column_filled(edit, 0u);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_RENDER), mask);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_LIGHT), mask);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_SAVE), 0u);
    CHECK_EQ_U64(world_edit_log_position(edit), 0u);
    /* Light is not a block. The section of -16 is from -16 to -1. */
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), -1);
    edit_test_end(test);
}

TEST(sections_marked_are_flagged_for_those_asked_and_the_top_is_looked_for) {
    edit_test_t *const test = edit_test_begin(t, 0u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    column_t *const column = world_map_column_at(&test->map, 0, 0);
    REQUIRE(column != nullptr);
    section_block_fill(&column->sections[column_section_of(32)], &test->pool, EDIT_TEST_STONE);
    const uint64_t revision = edit->revision;
    world_edit_sections_mark(edit, 0u, edit_test_section_bit(32), WORLD_EDIT_FOR_RENDER | WORLD_EDIT_FOR_SAVE);
    CHECK_EQ_U64(edit->revision, revision + 1u);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_RENDER), edit_test_section_bit(32));
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_SAVE), edit_test_section_bit(32));
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_LIGHT), 0u);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), 47);
    /* A mask of nothing is nothing. */
    world_edit_sections_mark(edit, 0u, 0u, WORLD_EDIT_FOR_ALL);
    CHECK_EQ_U64(edit->revision, revision + 1u);
    /* Emptied by hand: the top goes when told. */
    section_block_fill(&column->sections[column_section_of(32)], &test->pool, BLOCK_ID_AIR);
    world_edit_sections_mark(edit, 0u, edit_test_section_bit(32), WORLD_EDIT_FOR_RENDER);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), WORLD_EDIT_TOP_Z_NONE);
    edit_test_end(test);
}

TEST(the_top_follows_blocks_up_and_down) {
    edit_test_t *const test = edit_test_begin(t, 0u, 64u, 4u);
    world_edit_t *const edit = &test->edit;
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), WORLD_EDIT_TOP_Z_NONE);
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), 15);
    CHECK_EQ_U64(world_edit_block_set(edit, 1, 0, 100, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), 111);
    /* Lower writes do not bring it down. */
    CHECK_EQ_U64(world_edit_block_set(edit, 2, 0, -200, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), 111);
    /* The very top of the world, and the very bottom. */
    CHECK_EQ_U64(world_edit_block_set(edit, 3, 0, 500, 1u), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), 511);
    CHECK_EQ_U64(world_edit_block_set(edit, 3, 0, 500, BLOCK_ID_AIR), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), 111);
    /* Emptying a section under the top leaves it. */
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, BLOCK_ID_AIR), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), 111);
    /* Emptying the top looks further down, past the empty sections. */
    CHECK_EQ_U64(world_edit_block_set(edit, 1, 0, 100, BLOCK_ID_AIR), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), -193);
    CHECK_EQ_U64(world_edit_block_set(edit, 2, 0, -200, BLOCK_ID_AIR), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_I64(world_edit_column_top_z(edit, 0u), WORLD_EDIT_TOP_Z_NONE);
    edit_test_end(test);
}

/* ---- Everything at once, against brute force ------------------------------------------------------------------------ */

static uint32_t edit_flat_index(uint32_t x, uint32_t y, int32_t z) {
    return (uint32_t)(z - COLUMN_Z_GAMEPLAY_MIN) * COLUMN_SIZE * COLUMN_SIZE + y * COLUMN_SIZE + x;
}

/* Every column in a table of radius 1: a cross of five. x and y are from 0 to 47, and the corners are not loaded. */
static int32_t edit_test_world_coordinate(uint32_t place) {
    return (int32_t)place - 24;
}

TEST(random_edits_agree_with_a_brute_force_array_and_brute_force_flags) {
    /* Room for a whole section wiped between two reads of the log. */
    edit_test_t *const test = edit_test_begin(t, 1u, 1u << 14, 4u);
    world_edit_t *const edit = &test->edit;
    const world_map_t *const map = &test->map;
    /* Per slot of the map. Everything starts as air under open sky. */
    uint16_t *const blocks = test_alloc(t, (uint64_t)map->slot_count * EDIT_TEST_COLUMN_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, (uint64_t)map->slot_count * EDIT_TEST_COLUMN_VOXELS);
    uint32_t *const solid = test_alloc(t, (uint64_t)map->slot_count * COLUMN_SECTIONS * sizeof(uint32_t));
    uint64_t *const dirty = test_alloc(t, (uint64_t)map->slot_count * WORLD_EDIT_CONSUMER_COUNT * sizeof(uint64_t));
    world_edit_record_t *const records = test_alloc(t, sizeof(world_edit_record_t) * (1u << 12));
    memset(light, SECTION_LIGHT_OPEN_SKY, (uint64_t)map->slot_count * EDIT_TEST_COLUMN_VOXELS);
    world_edit_record_t *const expected_log = test_alloc(t, sizeof(world_edit_record_t) * (1u << 20));
    uint64_t expected_log_count = 0;
    uint64_t cursor = 0;
    uint64_t rng = 0xDEADBEEF1234ull;
    uint64_t changed = 0;
    uint64_t unchanged = 0;
    uint64_t refused_z = 0;
    uint64_t not_loaded = 0;
    uint32_t forms_seen = 0;
    uint32_t top_fell = 0;

    for (uint32_t step = 0; step < 300000u; step++) {
        /* Most in a few sections so that forms change; some wide; a section at a time wiped. */
        const uint64_t roll = edit_test_random(&rng);
        const int32_t x = edit_test_world_coordinate((uint32_t)(edit_test_random(&rng) % 48u));
        const int32_t y = edit_test_world_coordinate((uint32_t)(edit_test_random(&rng) % 48u));
        const uint32_t where = (uint32_t)(roll % 1024u);
        int32_t z;
        if (where < 700u) {
            z = (int32_t)(edit_test_random(&rng) % 48u);
        } else if (where < 1016u) {
            z = COLUMN_Z_GAMEPLAY_MIN + (int32_t)(edit_test_random(&rng) % EDIT_TEST_HEIGHT);
        } else if (where < 1020u) {
            z = COLUMN_Z_GAMEPLAY_MIN - 1 - (int32_t)(edit_test_random(&rng) % 6u);
        } else {
            z = COLUMN_Z_GAMEPLAY_MAX + 1 + (int32_t)(edit_test_random(&rng) % 11u);
        }
        const int32_t chunk_x = column_chunk_of(x);
        const int32_t chunk_y = column_chunk_of(y);
        const bool loaded = world_map_column_at(map, chunk_x, chunk_y) != nullptr;
        const bool cross = chunk_x * chunk_x + chunk_y * chunk_y <= 1;
        CHECK_EQ_U64(loaded ? 1u : 0u, cross ? 1u : 0u);
        const bool in_range = z >= -250 && z <= 500;
        const uint32_t slot = loaded ? edit_test_slot(test, chunk_x, chunk_y) : 0u;
        const uint64_t base = (uint64_t)slot * EDIT_TEST_COLUMN_VOXELS;
        const uint32_t flat = in_range ? edit_flat_index(column_local_of(x), column_local_of(y), z) : 0u;
        const uint32_t kind = (uint32_t)((roll >> 16) % 8u);
        if (kind == 0u) {
            /* Light. */
            const uint8_t packed = (uint8_t)((edit_test_random(&rng) % 4u) * 0x11u);
            const world_edit_status_t status = world_edit_light_set(edit, x, y, z, packed);
            if (!in_range) {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_Z_OUT_OF_RANGE);
                refused_z++;
            } else if (!loaded) {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_NOT_LOADED);
                not_loaded++;
            } else if (light[base + flat] == packed) {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_UNCHANGED);
            } else {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_CHANGED);
                light[base + flat] = packed;
                dirty[slot * WORLD_EDIT_CONSUMER_COUNT + WORLD_EDIT_CONSUMER_RENDER] |= edit_test_section_bit(z);
            }
        } else {
            /* Blocks. A wide run of states for a while, so that sections go all the way to direct form. */
            const bool wide = ((step >> 12) & 3u) == 3u;
            const uint16_t state = kind == 1u ? (uint16_t)BLOCK_ID_AIR
                                   : wide     ? (uint16_t)(edit_test_random(&rng) % 4000u + 1u)
                                              : (uint16_t)(edit_test_random(&rng) % 6u);
            const world_edit_status_t status = world_edit_block_set(edit, x, y, z, state);
            if (!in_range) {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_Z_OUT_OF_RANGE);
                refused_z++;
            } else if (!loaded) {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_NOT_LOADED);
                not_loaded++;
            } else if (blocks[base + flat] == state) {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_UNCHANGED);
                unchanged++;
            } else {
                CHECK_EQ_U64(status, WORLD_EDIT_STATUS_CHANGED);
                changed++;
                const uint32_t section = column_section_of(z);
                uint32_t *const count = &solid[(uint64_t)slot * COLUMN_SECTIONS + section];
                *count -= blocks[base + flat] != BLOCK_ID_AIR ? 1u : 0u;
                *count += state != BLOCK_ID_AIR ? 1u : 0u;
                expected_log[expected_log_count] = (world_edit_record_t){x, y, z, blocks[base + flat], state};
                expected_log_count++;
                blocks[base + flat] = state;
                for (uint32_t i = 0; i < WORLD_EDIT_CONSUMER_COUNT; i++) {
                    dirty[slot * WORLD_EDIT_CONSUMER_COUNT + i] |= edit_test_section_bit(z);
                }
            }
        }

        /* Now and then, one whole section emptied by writes, which makes a top fall. */
        if (step % 5000u == 4999u && loaded) {
            const int32_t top_before = world_edit_column_top_z(edit, slot);
            const int32_t section_z = top_before == WORLD_EDIT_TOP_Z_NONE ? 0 : top_before - 15;
            for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
                const uint32_t lx = i & 15u;
                const uint32_t ly = (i >> 4) & 15u;
                const int32_t vz = section_z + (int32_t)(i >> 8);
                if (vz < -250 || vz > 500) {
                    continue;
                }
                const int32_t vx = chunk_x * 16 + (int32_t)lx;
                const int32_t vy = chunk_y * 16 + (int32_t)ly;
                const uint32_t vflat = edit_flat_index(lx, ly, vz);
                const uint16_t old = blocks[base + vflat];
                const world_edit_status_t status = world_edit_block_set(edit, vx, vy, vz, BLOCK_ID_AIR);
                CHECK_EQ_U64(status, old != BLOCK_ID_AIR ? WORLD_EDIT_STATUS_CHANGED : WORLD_EDIT_STATUS_UNCHANGED);
                if (old != BLOCK_ID_AIR) {
                    expected_log[expected_log_count] = (world_edit_record_t){vx, vy, vz, old, BLOCK_ID_AIR};
                    expected_log_count++;
                    blocks[base + vflat] = BLOCK_ID_AIR;
                    solid[(uint64_t)slot * COLUMN_SECTIONS + column_section_of(vz)]--;
                    for (uint32_t c = 0; c < WORLD_EDIT_CONSUMER_COUNT; c++) {
                        dirty[slot * WORLD_EDIT_CONSUMER_COUNT + c] |= edit_test_section_bit(vz);
                    }
                }
            }
            top_fell += world_edit_column_top_z(edit, slot) < top_before ? 1u : 0u;
        }

        /* The log, every so often, well inside the ring. */
        if (step % 512u == 0u) {
            uint64_t lost = 99u;
            for (;;) {
                const uint64_t read_from = cursor;
                const uint64_t got = world_edit_log_read(edit, &cursor, records, 1u << 12, &lost);
                CHECK_EQ_U64(lost, 0u);
                if (got == 0u) {
                    break;
                }
                CHECK_MEM_EQ(records, &expected_log[read_from], got * sizeof(world_edit_record_t));
            }
            CHECK_EQ_U64(cursor, expected_log_count);
        }

        /* A consumer takes some flags, whose masks must be just what brute force has. */
        if (step % 97u == 0u) {
            const world_edit_consumer_t consumer = (world_edit_consumer_t)(edit_test_random(&rng) % 3u);
            world_edit_dirty_t handed;
            uint32_t want = (uint32_t)(edit_test_random(&rng) % 4u);
            while (want > 0u && world_edit_dirty_next(edit, consumer, &handed)) {
                uint64_t *const expected = &dirty[handed.slot * WORLD_EDIT_CONSUMER_COUNT + (uint32_t)consumer];
                CHECK_EQ_U64(handed.section_mask, *expected);
                *expected = 0u;
                want--;
            }
        }

        /* The top of what was written to: never below the truth, and over it only for an unnoticed direct array. */
        if (step % 16u == 0u && loaded) {
            const int32_t top = world_edit_column_top_z(edit, slot);
            int32_t truth = WORLD_EDIT_TOP_Z_NONE;
            for (uint32_t i = COLUMN_SECTIONS; i > 0u; i--) {
                if (solid[(uint64_t)slot * COLUMN_SECTIONS + i - 1u] != 0u) {
                    truth = COLUMN_Z_STORAGE_MIN + (int32_t)(i * SECTION_SIZE) - 1;
                    break;
                }
            }
            CHECK(top >= truth);
            if (top != truth) {
                const uint32_t section = column_section_of(top);
                CHECK_EQ_U64(map->borrowed_columns[slot].sections[section].bits, SECTION_BITS_DIRECT);
            }
        }

        /* Which forms the sections reached, so that this test cannot quietly check less than it says. */
        if (loaded && in_range) {
            const uint32_t bits = map->borrowed_columns[slot].sections[column_section_of(z)].bits;
            forms_seen |= 1u << bits;
        }
        if (t->failure_count != 0u) {
            break;
        }
    }

    /* Only 5 of the 16 chunks these coordinates reach are loaded. */
    CHECK(changed > 50000u);
    CHECK(unchanged > 100u);
    CHECK(refused_z > 1000u);
    CHECK(not_loaded > 10000u);
    CHECK(top_fell > 3u);
    CHECK((forms_seen & (1u << SECTION_BITS_UNIFORM)) != 0u);
    CHECK((forms_seen & (1u << 4u)) != 0u);
    CHECK((forms_seen & (1u << SECTION_BITS_DIRECT)) != 0u);

    /* Every voxel of every live column. */
    for (uint32_t slot = 0; slot < map->slot_count; slot++) {
        const world_map_slot_t *const entry = &map->borrowed_slots[slot];
        if (entry->state != WORLD_MAP_STATE_READY) {
            continue;
        }
        const uint64_t base = (uint64_t)slot * EDIT_TEST_COLUMN_VOXELS;
        uint64_t mismatches = 0;
        for (int32_t z = COLUMN_Z_GAMEPLAY_MIN; z <= COLUMN_Z_GAMEPLAY_MAX; z++) {
            for (uint32_t y = 0; y < COLUMN_SIZE; y++) {
                for (uint32_t x = 0; x < COLUMN_SIZE; x++) {
                    uint16_t block = 0;
                    uint8_t packed = 0;
                    const int32_t wx = entry->chunk_x * 16 + (int32_t)x;
                    const int32_t wy = entry->chunk_y * 16 + (int32_t)y;
                    const uint32_t flat = edit_flat_index(x, y, z);
                    if (world_map_block_get(map, wx, wy, z, &block) != WORLD_MAP_STATUS_OK ||
                        world_map_light_get(map, wx, wy, z, &packed) != WORLD_MAP_STATUS_OK ||
                        block != blocks[base + flat] || packed != light[base + flat]) {
                        mismatches++;
                    }
                }
            }
        }
        CHECK_EQ_U64(mismatches, 0u);
    }
    /* What is still flagged is what brute force still holds. */
    for (uint32_t slot = 0; slot < map->slot_count; slot++) {
        for (uint32_t c = 0; c < WORLD_EDIT_CONSUMER_COUNT; c++) {
            CHECK_EQ_U64(world_edit_dirty_peek(edit, slot, (world_edit_consumer_t)c),
                         dirty[slot * WORLD_EDIT_CONSUMER_COUNT + c]);
        }
    }
    edit_test_end(test);
}

TEST(an_edit_takes_from_its_arena_no_more_than_it_says) {
    uint8_t *const raw = test_alloc(t, sizeof(edit_test_t) + 64u);
    edit_test_t *const test = (edit_test_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(&test->arena, "edit_test", test_alloc(t, EDIT_TEST_ARENA_BYTES), EDIT_TEST_ARENA_BYTES);
    arena_init(&test->arena_pool, "edit_test_pool", test_alloc(t, 1u << 20), 1u << 20);
    section_pool_init(&test->pool, &test->arena_pool);
    world_map_init(&test->map, &test->arena, &test->pool, 8u, 0, 0);
    const uint64_t slots = test->map.slot_count;
    uint64_t used = test->arena.used;
    world_edit_init(&test->edit, &test->arena, &test->map, 256u);
    CHECK(test->arena.used - used <=
          slots * WORLD_EDIT_BYTES_PER_SLOT + 256u * sizeof(world_edit_record_t) + WORLD_EDIT_BYTES_FIXED);
    used = test->arena.used;
    world_edit_snapshot_init(&test->snapshot, &test->arena, &test->edit, 7u);
    CHECK(test->arena.used - used <= slots * WORLD_EDIT_SNAPSHOT_BYTES_PER_SLOT +
                                         7u * WORLD_EDIT_SNAPSHOT_BYTES_PER_SECTION + WORLD_EDIT_SNAPSHOT_BYTES_FIXED);
    edit_test_end(test);
}

/* ---- Snapshots ----------------------------------------------------------------------------------------------------- */

TEST(the_first_publish_says_where_the_window_is_and_nothing_new_is_no_view) {
    edit_test_t *const test = edit_test_begin(t, 2u, 64u, 8u);
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    CHECK(world_edit_snapshot_acquire(snapshot) == nullptr);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    CHECK(!world_edit_snapshot_publish(snapshot));
    const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK(world_edit_snapshot_acquire(snapshot) == nullptr);
    CHECK_EQ_U64(view->sequence, 1u);
    CHECK_EQ_U64(view->slot_count, test->map.slot_count);
    CHECK_EQ_U64(view->side, test->map.side);
    CHECK_EQ_I64(view->center_x, 0);
    CHECK_EQ_I64(view->min_x, -2);
    CHECK_EQ_U64(view->ready_count, 13u);
    CHECK_EQ_U64(view->section_count, 0u);
    CHECK_EQ_U64(view->view_radius, 2u);
    CHECK_EQ_U64(snapshot->published_count, 1u);
    CHECK_EQ_U64(snapshot->acquired_count, 1u);
    CHECK_EQ_U64(snapshot->overwritten_count, 0u);
    edit_test_end(test);
}

TEST(a_view_table_says_what_the_map_says) {
    edit_test_t *const test = edit_test_begin(t, 3u, 64u, 8u);
    world_edit_t *const edit = &test->edit;
    world_map_t *const map = &test->map;
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 16, 100, 5u), WORLD_EDIT_STATUS_CHANGED);
    /* Something on its way counts as no column. */
    world_map_set_center(map, 1, 0);
    world_map_request_t request;
    REQUIRE(world_map_stream_take(map, &request, 1u) == 1u);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK_EQ_U64(view->revision, edit->revision);
    CHECK_EQ_I64(view->center_x, 1);
    CHECK_EQ_I64(view->min_x, 1 - 3);
    uint32_t ready = 0;
    for (int32_t y = -6; y <= 6; y++) {
        for (int32_t x = -6; x <= 8; x++) {
            const column_t *const live = world_map_column_at(map, x, y);
            const world_edit_snapshot_column_t *const record = world_edit_view_column_at(view, x, y);
            CHECK_EQ_U64(record != nullptr ? 1u : 0u, live != nullptr ? 1u : 0u);
            if (record != nullptr && live != nullptr) {
                ready++;
                uint32_t slot = 0;
                REQUIRE(world_map_slot_of(map, x, y, &slot));
                CHECK_EQ_I64(record->chunk_x, x);
                CHECK_EQ_I64(record->chunk_y, y);
                CHECK_EQ_U64(record->state, WORLD_MAP_STATE_READY);
                CHECK_EQ_U64(record->generation, map->borrowed_slots[slot].generation);
                CHECK_EQ_U64(record->revision, world_edit_column_revision(edit, slot));
                CHECK_EQ_I64(record->top_z, world_edit_column_top_z(edit, slot));
            }
        }
    }
    CHECK(ready > 20u);
    CHECK_EQ_I64(world_edit_view_column_at(view, 1, 1)->top_z, 111);
    CHECK(world_edit_view_column_at(view, request.chunk_x, request.chunk_y) == nullptr);
    edit_test_end(test);
}

/* Every form a section can be in, voxel for voxel. */
TEST(a_view_carries_each_section_as_it_was) {
    edit_test_t *const test = edit_test_begin(t, 0u, 64u, 16u);
    world_edit_t *const edit = &test->edit;
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    column_t *const column = world_map_column_at(&test->map, 0, 0);
    REQUIRE(column != nullptr);
    /* Blocks: one voxel, a uniform section, and direct form. Light: one voxel. */
    CHECK_EQ_U64(world_edit_block_set(edit, 7, 7, 3, 42u), WORLD_EDIT_STATUS_CHANGED);
    section_block_fill(&column->sections[column_section_of(-32)], &test->pool, EDIT_TEST_STONE);
    world_edit_sections_mark(edit, 0u, edit_test_section_bit(-32), WORLD_EDIT_FOR_RENDER);
    for (uint32_t i = 0; i < 400u; i++) {
        CHECK_EQ_U64(world_edit_block_set(edit, (int32_t)(i & 15u), (int32_t)((i >> 4) & 15u), 64 + (int32_t)(i >> 8),
                                          (uint16_t)(i + 1u)),
                     WORLD_EDIT_STATUS_CHANGED);
    }
    CHECK_EQ_U64(column->sections[column_section_of(64)].bits, SECTION_BITS_DIRECT);
    CHECK_EQ_U64(world_edit_light_set(edit, 1, 2, 3, 0x45u), WORLD_EDIT_STATUS_CHANGED);

    REQUIRE(world_edit_snapshot_publish(snapshot));
    const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK_EQ_U64(view->section_count, 3u);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_RENDER), 0u);
    /* Lowest first, within a column. */
    CHECK_EQ_U64(view->borrowed_sections[0].section_index, column_section_of(-32));
    CHECK_EQ_U64(view->borrowed_sections[1].section_index, column_section_of(3));
    CHECK_EQ_U64(view->borrowed_sections[2].section_index, column_section_of(64));
    CHECK_EQ_U64(view->borrowed_sections[0].has_blocks, 0u);
    CHECK_EQ_U64(view->borrowed_sections[0].block_uniform, EDIT_TEST_STONE);
    CHECK_EQ_U64(view->borrowed_sections[0].has_light, 0u);
    CHECK_EQ_U64(view->borrowed_sections[0].light_uniform, SECTION_LIGHT_OPEN_SKY);
    CHECK_EQ_U64(view->borrowed_sections[1].has_blocks, 1u);
    CHECK_EQ_U64(view->borrowed_sections[1].has_light, 1u);
    CHECK_EQ_U64(view->borrowed_sections[2].has_blocks, 1u);
    for (uint32_t s = 0; s < view->section_count; s++) {
        const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[s];
        CHECK_EQ_I64(entry->chunk_x, 0);
        CHECK_EQ_U64(entry->slot, 0u);
        CHECK_EQ_U64(entry->generation, test->map.borrowed_slots[0].generation);
        const section_t *const live = &column->sections[entry->section_index];
        uint64_t mismatches = 0;
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            mismatches += world_edit_view_block(view, s, i) != section_block_get(live, i) ? 1u : 0u;
            mismatches += world_edit_view_light(view, s, i) != section_light_get(live, i) ? 1u : 0u;
        }
        CHECK_EQ_U64(mismatches, 0u);
    }
    /* The other consumers are not touched by a publish. */
    CHECK(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_SAVE) != 0u);
    edit_test_end(test);
}

/* What the triple buffer is for. */
TEST(a_view_held_by_the_reader_does_not_change_whatever_is_published_after_it) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 8u);
    world_edit_t *const edit = &test->edit;
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, 7u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    const world_edit_snapshot_view_t *const held = world_edit_snapshot_acquire(snapshot);
    REQUIRE(held != nullptr);
    const uint64_t table_bytes = (uint64_t)held->slot_count * sizeof(world_edit_snapshot_column_t);
    uint8_t *const table = test_alloc(t, table_bytes);
    uint16_t *const voxels = test_alloc(t, sizeof(uint16_t) * SECTION_VOXELS);
    memcpy(table, held->borrowed_columns, table_bytes);
    memcpy(voxels, held->borrowed_voxels[0].blocks, sizeof(uint16_t) * SECTION_VOXELS);
    const uint64_t sequence = held->sequence;
    const uint32_t count = held->section_count;
    for (uint32_t i = 0; i < 50u; i++) {
        CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, (uint16_t)(100u + i)), WORLD_EDIT_STATUS_CHANGED);
        REQUIRE(world_edit_snapshot_publish(snapshot));
        CHECK(snapshot->back != snapshot->front);
    }
    CHECK_EQ_U64(held->sequence, sequence);
    CHECK_EQ_U64(held->section_count, count);
    CHECK_MEM_EQ(held->borrowed_columns, table, table_bytes);
    CHECK_MEM_EQ(held->borrowed_voxels[0].blocks, voxels, sizeof(uint16_t) * SECTION_VOXELS);
    /* The next one is the newest, and the earlier view is no longer this reader's. */
    const world_edit_snapshot_view_t *const newest = world_edit_snapshot_acquire(snapshot);
    REQUIRE(newest != nullptr);
    CHECK(newest != held);
    CHECK_EQ_U64(newest->sequence, 51u);
    CHECK_EQ_U64(world_edit_view_block(newest, 0u, section_voxel_index(0u, 0u, 0u)), 149u);
    edit_test_end(test);
}

TEST(a_publish_keeps_to_its_budget_and_the_rest_wait_their_turn) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 3u);
    world_edit_t *const edit = &test->edit;
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    /* Five sections in the first column, and one in the second. */
    for (int32_t i = 0; i < 5; i++) {
        CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, i * 16, 2u), WORLD_EDIT_STATUS_CHANGED);
    }
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 0, 2u), WORLD_EDIT_STATUS_CHANGED);
    const uint32_t first = edit_test_slot(test, 0, 0);
    const uint32_t second = edit_test_slot(test, 1, 0);

    REQUIRE(world_edit_snapshot_publish(snapshot));
    const world_edit_snapshot_view_t *view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK_EQ_U64(view->section_count, 3u);
    for (uint32_t i = 0; i < 3u; i++) {
        CHECK_EQ_U64(view->borrowed_sections[i].slot, first);
        CHECK_EQ_U64(view->borrowed_sections[i].section_index, column_section_of((int32_t)i * 16));
    }
    /* Two left of the first, which has gone behind the second. */
    REQUIRE(world_edit_snapshot_publish(snapshot));
    view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK_EQ_U64(view->section_count, 3u);
    CHECK_EQ_U64(view->borrowed_sections[0].slot, second);
    CHECK_EQ_U64(view->borrowed_sections[1].slot, first);
    CHECK_EQ_U64(view->borrowed_sections[1].section_index, column_section_of(48));
    CHECK_EQ_U64(view->borrowed_sections[2].section_index, column_section_of(64));
    /* All sent: nothing more to say. */
    CHECK(!world_edit_snapshot_publish(snapshot));
    edit_test_end(test);
}

TEST(a_view_nobody_read_gives_its_sections_back) {
    edit_test_t *const test = edit_test_begin(t, 1u, 64u, 8u);
    world_edit_t *const edit = &test->edit;
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    CHECK_EQ_U64(world_edit_dirty_peek(edit, edit_test_slot(test, 0, 0), WORLD_EDIT_CONSUMER_RENDER), 0u);
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 16, 2u), WORLD_EDIT_STATUS_CHANGED);
    /* Replaces the first view, which nobody took. */
    REQUIRE(world_edit_snapshot_publish(snapshot));
    CHECK_EQ_U64(snapshot->overwritten_count, 1u);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, edit_test_slot(test, 0, 0), WORLD_EDIT_CONSUMER_RENDER),
                 edit_test_section_bit(0));
    /* Changed since: what goes out is the newer. */
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, 3u), WORLD_EDIT_STATUS_CHANGED);
    /* The second view is what the reader gets, with only what the second publish had. */
    const world_edit_snapshot_view_t *const middle = world_edit_snapshot_acquire(snapshot);
    REQUIRE(middle != nullptr);
    CHECK_EQ_U64(middle->section_count, 1u);
    CHECK_EQ_I64(middle->borrowed_sections[0].chunk_x, 1);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    const world_edit_snapshot_view_t *const last = world_edit_snapshot_acquire(snapshot);
    REQUIRE(last != nullptr);
    CHECK_EQ_U64(last->section_count, 1u);
    CHECK_EQ_I64(last->borrowed_sections[0].chunk_x, 0);
    CHECK_EQ_U64(world_edit_view_block(last, 0u, section_voxel_index(0u, 0u, 0u)), 3u);
    /* Now given back with no change in the world since: a publish must still go out. */
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 16, 9u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_snapshot_publish(snapshot)); /* sends z = 16, nobody takes it */
    CHECK_EQ_U64(world_edit_block_set(edit, 16, 0, 32, 9u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_snapshot_publish(snapshot)); /* replaces it, and z = 16 is dirty again */
    CHECK_EQ_U64(snapshot->overwritten_count, 2u);
    REQUIRE(world_edit_snapshot_publish(snapshot)); /* nothing changed, and yet this has something to carry */
    CHECK_EQ_U64(snapshot->overwritten_count, 3u);
    const world_edit_snapshot_view_t *const delivered = world_edit_snapshot_acquire(snapshot);
    REQUIRE(delivered != nullptr);
    CHECK_EQ_U64(delivered->section_count, 1u);
    CHECK_EQ_U64(delivered->borrowed_sections[0].section_index, column_section_of(16));
    edit_test_end(test);
}

/* Nothing was edited and nothing is dirty, and the reader must still learn the column has gone. */
TEST(a_column_unloaded_with_nothing_else_changed_is_published) {
    edit_test_t *const test = edit_test_begin(t, 0u, 64u, 8u);
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    REQUIRE(world_edit_snapshot_publish(snapshot));
    const world_edit_snapshot_view_t *view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK(world_edit_view_column_at(view, 0, 0) != nullptr);
    CHECK(!world_edit_snapshot_publish(snapshot));
    world_map_unload(&test->map, 0u);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK(world_edit_view_column_at(view, 0, 0) == nullptr);
    CHECK_EQ_U64(view->ready_count, 0u);
    edit_test_end(test);
}

TEST(a_section_of_a_column_gone_does_not_come_back_with_a_view_nobody_read) {
    edit_test_t *const test = edit_test_begin(t, 0u, 64u, 8u);
    world_edit_t *const edit = &test->edit;
    world_map_t *const map = &test->map;
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    CHECK_EQ_U64(world_edit_block_set(edit, 0, 0, 0, 1u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    /* It goes and another takes its slot, before anybody read that view. */
    world_map_unload(map, 0u);
    world_map_request_t request;
    REQUIRE(world_map_stream_take(map, &request, 1u) == 1u);
    REQUIRE(world_map_stream_commit(map, &request) != nullptr);
    REQUIRE(world_edit_snapshot_publish(snapshot));
    CHECK_EQ_U64(snapshot->overwritten_count, 1u);
    CHECK_EQ_U64(world_edit_dirty_peek(edit, 0u, WORLD_EDIT_CONSUMER_RENDER), 0u);
    /* The reader's table has the new generation, and no section for it. */
    const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(snapshot);
    REQUIRE(view != nullptr);
    CHECK_EQ_U64(view->section_count, 0u);
    CHECK_EQ_U64(view->borrowed_columns[0].generation, request.generation);
    edit_test_end(test);
}

/* A mirror built from nothing but views. Reader's side: no test_check, since this may run on another thread. */
static void edit_mirror_slot_reset(edit_mirror_t *mirror, uint32_t slot) {
    const uint64_t voxels = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS;
    memset(&mirror->borrowed_blocks[slot * voxels], 0, voxels * sizeof(uint16_t));
    memset(&mirror->borrowed_light[slot * voxels], SECTION_LIGHT_OPEN_SKY, voxels);
}

static void edit_mirror_apply(edit_mirror_t *mirror, const world_edit_snapshot_view_t *view) {
    mirror->view_count++;
    if (view->sequence <= mirror->last_sequence || view->slot_count != mirror->slot_count) {
        mirror->error_count++;
    }
    mirror->last_sequence = view->sequence;
    const uint64_t voxels = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS;
    for (uint32_t slot = 0; slot < view->slot_count; slot++) {
        const world_edit_snapshot_column_t *const column = &view->borrowed_columns[slot];
        if (column->state == WORLD_MAP_STATE_EMPTY) {
            mirror->borrowed_generations[slot] = 0u;
            continue;
        }
        if (column->revision > view->revision) {
            mirror->error_count++;
        }
        if (column->generation != mirror->borrowed_generations[slot]) {
            edit_mirror_slot_reset(mirror, slot);
            mirror->borrowed_generations[slot] = column->generation;
        }
    }
    for (uint32_t s = 0; s < view->section_count; s++) {
        const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[s];
        if (entry->slot >= view->slot_count || entry->section_index >= COLUMN_SECTIONS ||
            view->borrowed_columns[entry->slot].generation != entry->generation) {
            mirror->error_count++;
            continue;
        }
        uint16_t *const blocks = &mirror->borrowed_blocks[entry->slot * voxels + entry->section_index * SECTION_VOXELS];
        uint8_t *const light = &mirror->borrowed_light[entry->slot * voxels + entry->section_index * SECTION_VOXELS];
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            blocks[i] = world_edit_view_block(view, s, i);
            light[i] = world_edit_view_light(view, s, i);
        }
        mirror->section_count++;
    }
}

static edit_mirror_t *edit_mirror_begin(test_context_t *t, const world_map_t *map) {
    edit_mirror_t *const mirror = test_alloc(t, sizeof(edit_mirror_t));
    const uint64_t voxels = (uint64_t)map->slot_count * COLUMN_SECTIONS * SECTION_VOXELS;
    mirror->borrowed_blocks = test_alloc(t, voxels * sizeof(uint16_t));
    mirror->borrowed_light = test_alloc(t, voxels);
    mirror->borrowed_generations = test_alloc(t, (uint64_t)map->slot_count * sizeof(uint32_t));
    mirror->slot_count = map->slot_count;
    return mirror;
}

/* One step of a world going on: mostly edits, and sometimes a move and what streams with it. */
static void edit_world_step(test_context_t *t, edit_test_t *test, uint64_t *rng) {
    world_edit_t *const edit = &test->edit;
    world_map_t *const map = &test->map;
    const uint32_t pick = (uint32_t)(edit_test_random(rng) % 1000u);
    if (pick < 970u) {
        const int64_t span = (int64_t)map->table_radius * 2 * 16 + 16;
        const int32_t x = (int32_t)((int64_t)(edit_test_random(rng) % (uint64_t)span) + (int64_t)map->min_x * 16);
        const int32_t y = (int32_t)((int64_t)(edit_test_random(rng) % (uint64_t)span) + (int64_t)map->min_y * 16);
        const int32_t z = (int32_t)(edit_test_random(rng) % 64u) - 16;
        if (pick < 800u) {
            (void)world_edit_block_set(edit, x, y, z, (uint16_t)(edit_test_random(rng) % 20u));
        } else {
            (void)world_edit_light_set(edit, x, y, z, (uint8_t)(edit_test_random(rng) % 256u));
        }
    } else if (pick < 985u) {
        const int32_t dx = (int32_t)(edit_test_random(rng) % 9u) - 4;
        const int32_t dy = (int32_t)(edit_test_random(rng) % 9u) - 4;
        world_map_set_center(map, map->center_x + dx, map->center_y + dy);
    } else {
        world_map_leaving_t leaving;
        while (world_map_stream_leaving(map, &leaving)) {
            world_map_unload(map, leaving.slot);
        }
        world_map_request_t request;
        while (world_map_stream_take(map, &request, 1u) == 1u) {
            column_t *const column = world_map_stream_commit(map, &request);
            REQUIRE(column != nullptr);
            /* Something generated in it: some sections with something in them, and most with nothing. */
            section_block_fill(&column->sections[column_section_of(-32)], &test->pool, EDIT_TEST_STONE);
            if (edit_test_random(rng) % 2u == 0u) {
                section_light_fill(&column->sections[column_section_of(-16)], &test->pool, 0x31u);
            }
            world_edit_column_filled(edit, request.slot);
        }
    }
}

/* The reader has seen everything once it has had every section of every live column, as they now are. */
static void edit_mirror_check(test_context_t *t, const edit_mirror_t *mirror, const world_map_t *map) {
    uint16_t *const blocks = test_alloc(t, sizeof(uint16_t) * SECTION_VOXELS);
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    const uint64_t voxels = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS;
    uint32_t columns = 0;
    for (uint32_t slot = 0; slot < map->slot_count; slot++) {
        const world_map_slot_t *const entry = &map->borrowed_slots[slot];
        if (entry->state != WORLD_MAP_STATE_READY) {
            CHECK_EQ_U64(mirror->borrowed_generations[slot], 0u);
            continue;
        }
        columns++;
        CHECK_EQ_U64(mirror->borrowed_generations[slot], entry->generation);
        for (uint32_t s = 0; s < COLUMN_SECTIONS; s++) {
            const section_t *const live = &map->borrowed_columns[slot].sections[s];
            section_blocks_export(live, blocks);
            section_lights_export(live, light);
            const uint64_t at = slot * voxels + s * SECTION_VOXELS;
            CHECK(memcmp(&mirror->borrowed_blocks[at], blocks, sizeof(uint16_t) * SECTION_VOXELS) == 0);
            CHECK(memcmp(&mirror->borrowed_light[at], light, SECTION_VOXELS) == 0);
        }
    }
    CHECK_EQ_U64(columns, map->ready_count);
    CHECK(columns != 0u);
    CHECK_EQ_U64(mirror->error_count, 0u);
}

/* Until there is nothing more: the flags given back by the last replaced view included. */
static void edit_mirror_drain(test_context_t *t, edit_test_t *test, edit_mirror_t *mirror) {
    uint32_t rounds = 0;
    const world_edit_snapshot_view_t *view = world_edit_snapshot_acquire(&test->snapshot);
    while (view != nullptr || world_edit_snapshot_publish(&test->snapshot)) {
        if (view != nullptr) {
            edit_mirror_apply(mirror, view);
        }
        view = world_edit_snapshot_acquire(&test->snapshot);
        rounds++;
        if (rounds > 100000u) {
            CHECK(false);
            return;
        }
    }
}

/* All on one thread, so that which views get skipped is chosen at random, and the same every run. */
TEST(a_reader_skipping_views_at_random_ends_with_an_exact_copy_of_the_world) {
    edit_test_t *const test = edit_test_begin(t, 2u, 64u, 6u);
    edit_mirror_t *const mirror = edit_mirror_begin(t, &test->map);
    uint64_t rng = 0xFACEFEED5555ull;
    for (uint32_t step = 0; step < 40000u; step++) {
        edit_world_step(t, test, &rng);
        if (step % 16u == 0u) {
            (void)world_edit_snapshot_publish(&test->snapshot);
        }
        if (edit_test_random(&rng) % 64u == 0u) {
            const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(&test->snapshot);
            if (view != nullptr) {
                edit_mirror_apply(mirror, view);
            }
        }
    }
    edit_mirror_drain(t, test, mirror);
    CHECK(test->snapshot.overwritten_count > 100u);
    CHECK(mirror->view_count > 100u);
    CHECK(mirror->section_count > 100u);
    edit_mirror_check(t, mirror, &test->map);
    edit_test_end(test);
}

static void edit_reader_run(void *borrowed_argument) {
    edit_reader_t *const reader = borrowed_argument;
    uint64_t seen = 0;
    while (atomic_load_explicit(&reader->stop, memory_order_acquire) == 0u) {
        const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(&reader->borrowed_test->snapshot);
        if (view == nullptr) {
            (void)sched_yield();
            continue;
        }
        edit_mirror_apply(reader->borrowed_mirror, view);
        seen++;
        /* Now and then a slow frame, so that the sim thread goes on without it. */
        if (seen % 32u == 0u) {
            time_sleep_ns(200000);
        }
    }
}

/* As it runs in the game: a real reader thread. What it cannot show is a race that happens not to bite; the sanitize preset
 * has no thread checker. It does show that the two sides agree in the end, and neither waits. */
TEST(a_reader_on_its_own_thread_ends_with_an_exact_copy_of_the_world) {
    edit_test_t *const test = edit_test_begin(t, 2u, 64u, 6u);
    edit_mirror_t *const mirror = edit_mirror_begin(t, &test->map);
    edit_reader_t *const reader = test_alloc(t, sizeof(edit_reader_t));
    reader->borrowed_test = test;
    reader->borrowed_mirror = mirror;
    atomic_init(&reader->stop, 0u);
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(thread_topology_read(topology));
    thread_t *const thread = test_alloc(t, sizeof(thread_t));
    REQUIRE(thread_start(thread, topology, THREAD_ROLE_GPU, 0u, edit_reader_run, reader));
    uint64_t rng = 0xABCDEF012345ull;
    for (uint32_t step = 0; step < 60000u; step++) {
        edit_world_step(t, test, &rng);
        if (step % 16u == 0u) {
            (void)world_edit_snapshot_publish(&test->snapshot);
        }
    }
    atomic_store_explicit(&reader->stop, 1u, memory_order_release);
    thread_join(thread);
    /* The reader has stopped, so this thread is the reader now. */
    edit_mirror_drain(t, test, mirror);
    printf("# a reader thread: %" PRIu64 " published, %" PRIu64 " read, %" PRIu64 " replaced unread, %" PRIu64
           " sections\n",
           test->snapshot.published_count, mirror->view_count, test->snapshot.overwritten_count, mirror->section_count);
    CHECK(mirror->view_count > 10u);
    edit_mirror_check(t, mirror, &test->map);
    edit_test_end(test);
}

TEST(publishing_takes_no_new_memory) {
    edit_test_t *const test = edit_test_begin(t, 2u, 64u, 6u);
    world_edit_snapshot_t *const snapshot = &test->snapshot;
    uint64_t rng = 0x777123ABCull;
    heap_counters_t before;
    heap_counters_t after;
    heap_counters_read(&before);
    const uint64_t used = test->arena.used;
    for (uint32_t i = 0; i < 2000u; i++) {
        const world_edit_status_t status =
            world_edit_block_set(&test->edit, (int32_t)(edit_test_random(&rng) % 16u), 0, 0, (uint16_t)(i % 8u + 1u));
        CHECK(status != WORLD_EDIT_STATUS_NO_MEMORY);
        (void)world_edit_snapshot_publish(snapshot);
        (void)world_edit_snapshot_acquire(snapshot);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(test->arena.used, used);
    edit_test_end(test);
}

TEST(world_open_gives_a_snapshot_that_works) {
    uint8_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
    world_t *const world = (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    REQUIRE(world_init(world, (world_limits_t){32ull << 20, 25u}));
    REQUIRE(world_open(world, 2u, -5, 9));
    world_map_request_t request;
    REQUIRE(world_map_stream_take(&world->map, &request, 1u) == 1u);
    REQUIRE(world_map_stream_commit(&world->map, &request) != nullptr);
    CHECK_EQ_U64(world_edit_block_set(&world->edit, -5 * 16, 9 * 16, 0, 3u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_snapshot_publish(&world->snapshot));
    const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(&world->snapshot);
    REQUIRE(view != nullptr);
    CHECK_EQ_I64(view->center_x, -5);
    CHECK_EQ_U64(view->section_count, 1u);
    CHECK_EQ_U64(world_edit_view_block(view, 0u, section_voxel_index(0u, 0u, 0u)), 3u);
    world_close(world);
    world_deinit(world);
}

/* Prints, for the stage file. A timing on a shared machine would make a flaky test, so nothing is checked. */
TEST(snapshot_cost_on_this_machine) {
    const uint32_t radii[] = {32u, 128u};
    for (uint32_t r = 0; r < 2u; r++) {
        uint8_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
        world_t *const world = (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
        const uint32_t radius = radii[r];
        const uint64_t side = 2ull * radius + 1ull;
        REQUIRE(world_init(world, (world_limits_t){512ull << 20, side * side}));
        const int64_t open_start = time_now_ns();
        REQUIRE(world_open(world, radius, 0, 0));
        const int64_t open_end = time_now_ns();
        world_map_request_t request;
        while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
            REQUIRE(world_map_stream_commit(&world->map, &request) != nullptr);
        }
        const uint32_t rounds = 50u;
        int64_t table_only_ns = 0;
        int64_t sections_ns = 0;
        uint64_t sections = 0;
        for (uint32_t i = 0; i < rounds; i++) {
            CHECK_EQ_U64(world_edit_block_set(&world->edit, 0, 0, 0, (uint16_t)(i + 1u)), WORLD_EDIT_STATUS_CHANGED);
            int64_t start = time_now_ns();
            CHECK(world_edit_snapshot_publish(&world->snapshot));
            table_only_ns += time_now_ns() - start;
            (void)world_edit_snapshot_acquire(&world->snapshot);
            for (uint32_t j = 0; j < 64u; j++) {
                const int32_t place = (int32_t)(j % 16u);
                const int32_t where = (int32_t)(j / 8u) - 2;
                CHECK_EQ_U64(
                    world_edit_block_set(&world->edit, where * 16 + place, 0, (int32_t)j * 2, (uint16_t)(i + j + 1u)),
                    WORLD_EDIT_STATUS_CHANGED);
                (void)world_edit_light_set(&world->edit, where * 16 + place, 1, (int32_t)j * 2, (uint8_t)(i + 1u));
            }
            start = time_now_ns();
            CHECK(world_edit_snapshot_publish(&world->snapshot));
            sections_ns += time_now_ns() - start;
            const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(&world->snapshot);
            REQUIRE(view != nullptr);
            sections += view->section_count;
        }
        printf("# radius %" PRIu32 ", %" PRIu32
               " columns: world_open %.2f ms, a publish of the table %.3f ms, with %.1f "
               "sections %.3f ms\n",
               radius, world->map.ready_count, (libc_double_t)time_ns_to_ms(open_end - open_start),
               (libc_double_t)time_ns_to_ms(table_only_ns / (int64_t)rounds), (libc_double_t)sections / rounds,
               (libc_double_t)time_ns_to_ms(sections_ns / (int64_t)rounds));
        world_close(world);
        world_deinit(world);
    }
}

TEST_MAIN()
