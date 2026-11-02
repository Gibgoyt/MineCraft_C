/* ctest run_world_column. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "tables/blocks/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    COLUMN_TEST_POOL_BYTES = 64ull << 20,
    COLUMN_TEST_COUNT = 16ull,
};
enum : uint32_t {
    /* z from -250 to 500, both included, in a 16x16 column. */
    COLUMN_TEST_HEIGHT = COLUMN_Z_GAMEPLAY_MAX - COLUMN_Z_GAMEPLAY_MIN + 1,
    COLUMN_TEST_VOXELS = COLUMN_SIZE * COLUMN_SIZE * COLUMN_TEST_HEIGHT,
};

/* A real world over real regions, so that what these tests run on is what the game runs on. */
typedef struct column_test_t {
    world_t world;
    column_t column;
    uint8_t _pad0[56];
} column_test_t;
static_assert(sizeof(column_test_t) == 2304);

static column_test_t *column_test_begin(test_context_t *t) {
    column_test_t *const raw = test_alloc(t, sizeof(column_test_t) + 64u);
    column_test_t *const test = (column_test_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    if (!CHECK(world_init(&test->world, (world_limits_t){COLUMN_TEST_POOL_BYTES, COLUMN_TEST_COUNT}))) {
        return nullptr;
    }
    column_init(&test->column, 3, -4);
    return test;
}

static void column_test_end(column_test_t *test) {
    column_deinit(&test->column, &test->world.section_pool);
    world_deinit(&test->world);
}

static uint64_t column_test_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

static uint32_t column_test_flat_index(uint32_t x, uint32_t y, int32_t z) {
    return (uint32_t)(z - COLUMN_Z_GAMEPLAY_MIN) * COLUMN_SIZE * COLUMN_SIZE + y * COLUMN_SIZE + x;
}

/* ---- The numbers ------------------------------------------------------------------------------------------------- */

TEST(the_limits_are_the_ones_the_plan_and_the_yaml_give) {
    CHECK_EQ_I64(COLUMN_Z_STORAGE_MIN, -256);
    CHECK_EQ_I64(COLUMN_Z_STORAGE_END, 512);
    CHECK_EQ_I64(COLUMN_Z_GAMEPLAY_MIN, -250);
    CHECK_EQ_I64(COLUMN_Z_GAMEPLAY_MAX, 500);
    CHECK_EQ_I64(COLUMN_Z_GAMEPLAY_MIN, MC_WORLD_Z_MIN);
    CHECK_EQ_I64(COLUMN_Z_GAMEPLAY_MAX, MC_WORLD_Z_MAX);
    CHECK_EQ_U64(COLUMN_SECTIONS, 48u);
    CHECK_EQ_U64(COLUMN_SIZE, 16u);
    CHECK_EQ_U64(sizeof(((column_t *)nullptr)->sections) / sizeof(section_t), 48u);
}

/* Each against floor division done by hand, not by the shift it is testing. */
TEST(a_coordinate_maps_to_its_chunk_and_place_with_floors_for_negatives) {
    CHECK_EQ_I64(column_chunk_of(0), 0);
    CHECK_EQ_I64(column_chunk_of(15), 0);
    CHECK_EQ_I64(column_chunk_of(16), 1);
    CHECK_EQ_I64(column_chunk_of(-1), -1);
    CHECK_EQ_I64(column_chunk_of(-16), -1);
    CHECK_EQ_I64(column_chunk_of(-17), -2);
    CHECK_EQ_I64(column_chunk_of(INT32_MAX), 134217727);
    CHECK_EQ_I64(column_chunk_of(INT32_MIN), -134217728);
    CHECK_EQ_U64(column_local_of(0), 0u);
    CHECK_EQ_U64(column_local_of(17), 1u);
    CHECK_EQ_U64(column_local_of(-1), 15u);
    CHECK_EQ_U64(column_local_of(-16), 0u);
    CHECK_EQ_U64(column_local_of(-17), 15u);
    CHECK_EQ_U64(column_local_of(INT32_MAX), 15u);
    CHECK_EQ_U64(column_local_of(INT32_MIN), 0u);
    for (int32_t c = -5000; c < 5000; c++) {
        const int64_t chunk = column_chunk_of(c);
        const uint32_t local = column_local_of(c);
        if (!CHECK(chunk * 16 + local == c && local < 16u)) {
            break;
        }
    }
}

TEST(every_z_in_storage_has_a_section_and_a_place_and_no_two_share_them) {
    CHECK_EQ_U64(column_section_of(-256), 0u);
    CHECK_EQ_U64(column_section_local_of(-256), 0u);
    CHECK_EQ_U64(column_section_of(-250), 0u);
    CHECK_EQ_U64(column_section_local_of(-250), 6u);
    CHECK_EQ_U64(column_section_of(-1), 15u);
    CHECK_EQ_U64(column_section_local_of(-1), 15u);
    CHECK_EQ_U64(column_section_of(0), 16u);
    CHECK_EQ_U64(column_section_local_of(0), 0u);
    CHECK_EQ_U64(column_section_of(500), 47u);
    CHECK_EQ_U64(column_section_local_of(500), 4u);
    CHECK_EQ_U64(column_section_of(511), 47u);
    CHECK_EQ_U64(column_section_local_of(511), 15u);
    uint32_t next = 0;
    for (int32_t z = COLUMN_Z_STORAGE_MIN; z < COLUMN_Z_STORAGE_END; z++) {
        CHECK(column_z_in_storage(z));
        CHECK_EQ_U64(column_section_of(z) * SECTION_SIZE + column_section_local_of(z), next);
        next++;
    }
    CHECK_EQ_U64(next, COLUMN_SECTIONS * SECTION_SIZE);
    CHECK(!column_z_in_storage(-257));
    CHECK(!column_z_in_storage(512));
}

TEST(gameplay_stops_at_minus_250_and_at_500) {
    CHECK(column_z_in_gameplay(-250));
    CHECK(column_z_in_gameplay(0));
    CHECK(column_z_in_gameplay(500));
    CHECK(!column_z_in_gameplay(-251));
    CHECK(!column_z_in_gameplay(501));
    CHECK(!column_z_in_gameplay(INT32_MIN));
    CHECK(!column_z_in_gameplay(INT32_MAX));
    CHECK_EQ_I64(column_z_clamp(-251), -250);
    CHECK_EQ_I64(column_z_clamp(-250), -250);
    CHECK_EQ_I64(column_z_clamp(0), 0);
    CHECK_EQ_I64(column_z_clamp(500), 500);
    CHECK_EQ_I64(column_z_clamp(501), 500);
    CHECK_EQ_I64(column_z_clamp(INT32_MIN), -250);
    CHECK_EQ_I64(column_z_clamp(INT32_MAX), 500);
}

/* ---- The limits, on a real column --------------------------------------------------------------------------------- */

TEST(a_write_at_minus_251_or_501_is_rejected_and_touches_nothing) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    section_pool_t *const pool = &test->world.section_pool;
    static const int32_t bad[] = {-251, 501, -256, 511, -257, 512, INT32_MIN, INT32_MAX};
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ_U64(column_block_set(&test->column, pool, 0u, 0u, bad[i], 1u), COLUMN_STATUS_Z_OUT_OF_RANGE);
        CHECK_EQ_U64(column_light_set(&test->column, pool, 0u, 0u, bad[i], 1u), COLUMN_STATUS_Z_OUT_OF_RANGE);
        uint16_t block = 0xABCDu;
        uint8_t light = 0xEFu;
        CHECK(!column_block_get(&test->column, 0u, 0u, bad[i], &block));
        CHECK(!column_light_get(&test->column, 0u, 0u, bad[i], &light));
        CHECK_EQ_U64(block, 0xABCDu);
        CHECK_EQ_U64(light, 0xEFu);
    }
    CHECK_EQ_U64(pool->live_bytes, 0u);
    CHECK_EQ_U64(pool->carved_bytes, 0u);
    CHECK_EQ_U64(column_memory_bytes(&test->column), sizeof(column_t));
    column_test_end(test);
}

TEST(a_write_at_minus_250_or_500_is_accepted_and_lands_in_the_end_sections) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    section_pool_t *const pool = &test->world.section_pool;
    CHECK_EQ_U64(column_block_set(&test->column, pool, 1u, 2u, -250, 7u), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_block_set(&test->column, pool, 3u, 4u, 500, 8u), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_light_set(&test->column, pool, 1u, 2u, -250, 0x12u), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_light_set(&test->column, pool, 3u, 4u, 500, 0x34u), COLUMN_STATUS_OK);
    uint16_t block = 0;
    uint8_t light = 0;
    CHECK(column_block_get(&test->column, 1u, 2u, -250, &block));
    CHECK_EQ_U64(block, 7u);
    CHECK(column_block_get(&test->column, 3u, 4u, 500, &block));
    CHECK_EQ_U64(block, 8u);
    CHECK(column_light_get(&test->column, 1u, 2u, -250, &light));
    CHECK_EQ_U64(light, 0x12u);
    CHECK(column_light_get(&test->column, 3u, 4u, 500, &light));
    CHECK_EQ_U64(light, 0x34u);
    CHECK_EQ_U64(section_block_get(&test->column.sections[0], section_voxel_index(1u, 2u, 6u)), 7u);
    CHECK_EQ_U64(section_block_get(&test->column.sections[47], section_voxel_index(3u, 4u, 4u)), 8u);
    column_test_end(test);
}

/* ---- A column as a whole ------------------------------------------------------------------------------------------ */

TEST(a_new_column_is_air_under_open_sky_and_costs_only_itself) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    CHECK_EQ_I64(test->column.chunk_x, 3);
    CHECK_EQ_I64(test->column.chunk_y, -4);
    CHECK_EQ_U64(column_memory_bytes(&test->column), sizeof(column_t));
    for (int32_t z = COLUMN_Z_GAMEPLAY_MIN; z <= COLUMN_Z_GAMEPLAY_MAX; z += 13) {
        uint16_t block = 1u;
        uint8_t light = 0u;
        CHECK(column_block_get(&test->column, 15u, 15u, z, &block));
        CHECK(column_light_get(&test->column, 15u, 15u, z, &light));
        CHECK_EQ_U64(block, BLOCK_ID_AIR);
        CHECK_EQ_U64(section_light_sky(light), 15u);
        CHECK_EQ_U64(section_light_block(light), 0u);
    }
    column_test_end(test);
}

TEST(one_block_makes_one_array_and_the_sections_either_side_stay_empty) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    section_pool_t *const pool = &test->world.section_pool;
    CHECK_EQ_U64(column_block_set(&test->column, pool, 0u, 0u, -1, 5u), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_memory_bytes(&test->column), sizeof(column_t) + 576u);
    for (uint32_t s = 0; s < COLUMN_SECTIONS; s++) {
        CHECK_EQ_U64(test->column.sections[s].borrowed_storage != nullptr, s == 15u);
    }
    uint16_t block = 1u;
    CHECK(column_block_get(&test->column, 0u, 0u, 0, &block));
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK(column_block_get(&test->column, 0u, 0u, -2, &block));
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    CHECK_EQ_U64(column_block_set(&test->column, pool, 0u, 0u, -1, BLOCK_ID_AIR), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_memory_bytes(&test->column), sizeof(column_t));
    CHECK_EQ_U64(pool->live_bytes, 0u);
    column_test_end(test);
}

TEST(random_edits_of_blocks_and_light_match_a_plain_array_of_the_whole_range) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    section_pool_t *const pool = &test->world.section_pool;
    uint16_t *const blocks = test_alloc(t, COLUMN_TEST_VOXELS * sizeof(uint16_t));
    uint8_t *const lights = test_alloc(t, COLUMN_TEST_VOXELS);
    memset(lights, SECTION_LIGHT_OPEN_SKY, COLUMN_TEST_VOXELS);
    uint64_t rng = 0xC01DBEEFu;
    uint32_t rejected = 0;
    for (uint32_t edit = 0; edit < 600000u; edit++) {
        const uint64_t r = column_test_random(&rng);
        const uint32_t x = (uint32_t)r & 15u;
        const uint32_t y = (uint32_t)(r >> 4) & 15u;
        /* Reaching past both ends, and most edits in bands, as real worlds do: some sections fill, and most stay empty. */
        const int32_t z = (r >> 8) % 8u == 0u ? COLUMN_Z_STORAGE_MIN + (int32_t)((r >> 16) % 768u)
                                              : -40 + (int32_t)((r >> 16) % 120u);
        const uint16_t state = (uint16_t)((r >> 32) % ((r >> 60) < 12u ? 6u : 400u));
        const uint8_t light = (uint8_t)(r >> 48);
        if (!column_z_in_gameplay(z)) {
            CHECK_EQ_U64(column_block_set(&test->column, pool, x, y, z, state), COLUMN_STATUS_Z_OUT_OF_RANGE);
            rejected++;
            continue;
        }
        const uint32_t flat = column_test_flat_index(x, y, z);
        if ((r >> 56) % 4u == 0u) {
            CHECK_EQ_U64(column_light_set(&test->column, pool, x, y, z, light), COLUMN_STATUS_OK);
            lights[flat] = light;
        } else {
            CHECK_EQ_U64(column_block_set(&test->column, pool, x, y, z, state), COLUMN_STATUS_OK);
            blocks[flat] = state;
        }
        if (t->failure_count != 0u) {
            break;
        }
    }
    CHECK(rejected > 0u);
    /* Every voxel of the gameplay range. */
    bool same = true;
    for (int32_t z = COLUMN_Z_GAMEPLAY_MIN; z <= COLUMN_Z_GAMEPLAY_MAX && same; z++) {
        for (uint32_t y = 0; y < COLUMN_SIZE; y++) {
            for (uint32_t x = 0; x < COLUMN_SIZE; x++) {
                uint16_t block = 0;
                uint8_t light = 0;
                same = same && column_block_get(&test->column, x, y, z, &block) &&
                       column_light_get(&test->column, x, y, z, &light);
                same = same && block == blocks[column_test_flat_index(x, y, z)];
                same = same && light == lights[column_test_flat_index(x, y, z)];
            }
        }
    }
    CHECK(same);
    /* Storage outside the gameplay range was never written: those sections' first six and last eleven layers are untouched. */
    const section_t *const lowest = &test->column.sections[0];
    const section_t *const highest = &test->column.sections[47];
    for (uint32_t y = 0; y < SECTION_SIZE; y++) {
        for (uint32_t x = 0; x < SECTION_SIZE; x++) {
            for (uint32_t local_z = 0; local_z < 6u; local_z++) {
                CHECK_EQ_U64(section_block_get(lowest, section_voxel_index(x, y, local_z)), BLOCK_ID_AIR);
            }
            for (uint32_t local_z = 5; local_z < 16u; local_z++) {
                CHECK_EQ_U64(section_block_get(highest, section_voxel_index(x, y, local_z)), BLOCK_ID_AIR);
            }
        }
    }
    CHECK_EQ_U64(column_memory_bytes(&test->column) - sizeof(column_t), pool->live_bytes);
    column_test_end(test);
}

TEST(a_full_pool_says_so_as_its_own_status) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    /* A pool with nothing left: the arena has given its all to something else. */
    (void)arena_alloc(&test->world.arena_sections, test->world.arena_sections.capacity, 64u);
    section_pool_t *const pool = &test->world.section_pool;
    CHECK_EQ_U64(column_block_set(&test->column, pool, 0u, 0u, 0, 1u), COLUMN_STATUS_NO_MEMORY);
    CHECK_EQ_U64(column_light_set(&test->column, pool, 0u, 0u, 0, 1u), COLUMN_STATUS_NO_MEMORY);
    uint16_t block = 1u;
    CHECK(column_block_get(&test->column, 0u, 0u, 0, &block));
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    column_test_end(test);
}

/* Streaming: a map fills a column, drops it, and fills another. */
TEST(columns_streaming_in_and_out_use_the_same_memory_again) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    section_pool_t *const pool = &test->world.section_pool;
    column_t *const columns = column_array_alloc(&test->world.arena_columns, COLUMN_TEST_COUNT);
    CHECK_EQ_U64((uintptr_t)columns % alignof(column_t), 0u);
    uint64_t carved_after_first = 0;
    for (uint32_t round = 0; round < 20u; round++) {
        for (uint32_t c = 0; c < COLUMN_TEST_COUNT; c++) {
            column_init(&columns[c], (int32_t)(c + round), 0);
            for (uint32_t i = 0; i < 300u; i++) {
                CHECK_EQ_U64(column_block_set(&columns[c], pool, i & 15u, (i >> 4) & 15u, (int32_t)i - 150,
                                              (uint16_t)(i % 7u + 1u)),
                             COLUMN_STATUS_OK);
            }
            /* The first and last sections go back too. */
            CHECK_EQ_U64(column_block_set(&columns[c], pool, 0u, 0u, COLUMN_Z_GAMEPLAY_MIN, 1u), COLUMN_STATUS_OK);
            CHECK_EQ_U64(column_light_set(&columns[c], pool, 0u, 0u, COLUMN_Z_GAMEPLAY_MAX, 1u), COLUMN_STATUS_OK);
        }
        for (uint32_t c = 0; c < COLUMN_TEST_COUNT; c++) {
            uint16_t block = 0;
            CHECK(column_block_get(&columns[c], 3u, 0u, 3 - 150, &block));
            CHECK_EQ_U64(block, 3u % 7u + 1u);
            column_deinit(&columns[c], pool);
            CHECK_EQ_I64(columns[c].chunk_x, 0);
        }
        CHECK_EQ_U64(pool->live_bytes, 0u);
        if (round == 0u) {
            carved_after_first = pool->carved_bytes;
        }
    }
    CHECK(carved_after_first != 0u);
    CHECK_EQ_U64(pool->carved_bytes, carved_after_first);
    column_test_end(test);
}

/* Recorded in the stage file. Plain arrays in all 48 sections would be 48 * 12288 bytes. */
TEST(memory_of_a_column_with_a_stone_body_a_surface_and_open_sky) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    section_pool_t *const pool = &test->world.section_pool;
    /* A shape, not terrain: that is Phase 10. Stone with a few ores to z = -1, a varied surface of 4 states up to z = 16,
     * air above. Light is set only near the surface. */
    uint64_t rng = 0xABCDEFu;
    for (int32_t z = COLUMN_Z_GAMEPLAY_MIN; z <= 16; z++) {
        for (uint32_t y = 0; y < COLUMN_SIZE; y++) {
            for (uint32_t x = 0; x < COLUMN_SIZE; x++) {
                const uint64_t r = column_test_random(&rng);
                const uint16_t underground = r % 32u == 0u ? (uint16_t)(1u + (r >> 8) % 7u) : BLOCK_ID_STONE;
                const uint16_t surface = (uint16_t)(BLOCK_ID_STONE + (r >> 16) % 4u);
                CHECK_EQ_U64(column_block_set(&test->column, pool, x, y, z, z < 0 ? underground : surface),
                             COLUMN_STATUS_OK);
                if (z >= 0) {
                    CHECK_EQ_U64(
                        column_light_set(&test->column, pool, x, y, z, section_light_pack((uint32_t)z % 16u, 0u)),
                        COLUMN_STATUS_OK);
                }
            }
        }
    }
    const uint64_t bytes = column_memory_bytes(&test->column);
    const uint64_t flat = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS * 3u;
    printf("column with a body and a surface: %" PRIu64 " bytes; plain arrays %" PRIu64 " bytes; %" PRIu64 "%%\n",
           bytes, flat, bytes * 100u / flat);
    CHECK(bytes < flat / 2u);
    column_test_end(test);
}

/* ---- run/src/world/ itself ---------------------------------------------------------------------------------------- */

TEST(world_init_and_deinit_balance_the_heap) {
    heap_counters_t before;
    heap_counters_t during;
    heap_counters_t after;
    heap_counters_read(&before);
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    heap_counters_read(&during);
    CHECK_EQ_U64(during.live_regions, before.live_regions + 2u);
    CHECK(test->world.arena_sections.capacity >= COLUMN_TEST_POOL_BYTES);
    CHECK(test->world.arena_columns.capacity >= COLUMN_TEST_COUNT * sizeof(column_t));
    CHECK(test->world.section_pool.borrowed_arena == &test->world.arena_sections);
    column_test_end(test);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    /* And the handle is empty, so a second deinit does nothing. */
    CHECK(test->world.owned_sections.owned_base == nullptr);
    world_deinit(&test->world);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.release_calls, during.release_calls + 2u);
}

TEST(a_zeroed_world_is_safe_to_deinit) {
    world_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
    world_t *const world = (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    heap_counters_t before;
    heap_counters_t after;
    heap_counters_read(&before);
    world_deinit(world);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
}

TEST(a_world_that_cannot_start_leaves_nothing_behind) {
    world_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
    world_t *const world = (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    heap_counters_t before;
    heap_counters_t after;
    heap_counters_read(&before);
    /* Each of the two reservations failing in turn, and a count whose size would not fit in 64 bits. */
    const world_limits_t bad[] = {
        {0u, 1u},
        {HEAP_BUDGET_BYTES + 1u, 1u},
        {COLUMN_TEST_POOL_BYTES, HEAP_BUDGET_BYTES},
        {COLUMN_TEST_POOL_BYTES, 0u},
        {COLUMN_TEST_POOL_BYTES, UINT64_MAX},
        /* Its size wraps round to something small, which the heap would give. */
        {COLUMN_TEST_POOL_BYTES, UINT64_MAX / sizeof(column_t) + 2u},
    };
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!world_init(world, bad[i]));
        CHECK(world->owned_sections.owned_base == nullptr);
        CHECK(world->owned_columns.owned_base == nullptr);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    world_deinit(world);
}

TEST(the_default_limits_start_and_stay_inside_the_budget) {
    world_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
    world_t *const world = (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    const world_limits_t limits = world_limits_default();
    CHECK_EQ_U64(limits.column_count, 1025u * 1025u);
    CHECK_EQ_U64(limits.section_bytes, HEAP_BUDGET_BYTES / 8u * 3u);
    heap_counters_t before;
    heap_counters_read(&before);
    REQUIRE(world_init(world, limits));
    CHECK(world->owned_sections.bytes + world->owned_columns.bytes + before.live_bytes <= HEAP_BUDGET_BYTES);
    /* The far ends are usable, without touching what lies between. */
    column_t *const columns = column_array_alloc(&world->arena_columns, limits.column_count);
    column_init(&columns[limits.column_count - 1u], 1, 2);
    CHECK_EQ_I64(columns[limits.column_count - 1u].chunk_y, 2);
    world_deinit(world);
}

TEST(a_world_reset_takes_back_everything_and_runs_again) {
    column_test_t *const test = column_test_begin(t);
    REQUIRE(test != nullptr);
    section_pool_t *const pool = &test->world.section_pool;
    column_t *const columns = column_array_alloc(&test->world.arena_columns, 2u);
    column_init(&columns[0], 0, 0);
    CHECK_EQ_U64(column_block_set(&columns[0], pool, 0u, 0u, 0, 1u), COLUMN_STATUS_OK);
    CHECK(test->world.arena_columns.used != 0u);
    CHECK(test->world.arena_sections.used != 0u);
    /* The test's own column has nothing in it, or it would be dead too. */
    world_reset(&test->world);
    CHECK_EQ_U64(test->world.arena_columns.used, 0u);
    CHECK_EQ_U64(test->world.arena_sections.used, 0u);
    CHECK_EQ_U64(pool->live_bytes, 0u);
    CHECK_EQ_U64(pool->carved_bytes, 0u);
    column_t *const again = column_array_alloc(&test->world.arena_columns, 1u);
    column_init(again, 5, 5);
    CHECK_EQ_U64(column_block_set(again, pool, 0u, 0u, 0, 2u), COLUMN_STATUS_OK);
    uint16_t block = 0;
    CHECK(column_block_get(again, 0u, 0u, 0, &block));
    CHECK_EQ_U64(block, 2u);
    column_test_end(test);
}

TEST_MAIN()
