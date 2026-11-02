/* ctest run_render_upload. See README.md. */
#include "harness/index.h"

#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "render/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    UPLOAD_TEST_DEVICE_BYTES = 96ull << 20,
    UPLOAD_TEST_POOL_BYTES = 32ull << 20,
    UPLOAD_TEST_PUMP_NS = 60ull * 1000000000ull,
};
enum : uint32_t {
    UPLOAD_TEST_PROBE_COUNT = 1u << 16,
    UPLOAD_TEST_SIM_EDITS = 6000u,
};
enum : int32_t {
    /* The range the test world's blocks live in, with a section of nothing at each end. */
    UPLOAD_TEST_Z_LOW = -32,
    UPLOAD_TEST_Z_HIGH = 64,
};

static_assert(UPLOAD_TEST_PROBE_COUNT * WORLD_BUFFERS_PROBE_BYTES <= (16ull << 20));

/* The arena gives no alignment above what its callers ask for. */
static void *upload_test_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static uint64_t upload_test_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

/* Everything for a real run: the card, the workers, a world, the renderer. All of it on the test arena. */
typedef struct upload_fixture_t {
    gpu_bridge_t bridge;        /* 0 */
    job_system_t job;           /* 384 */
    world_t world;              /* 640 */
    render_t render;            /* 1984 */
    device_block_t scratch;     /* 3776 */
    thread_topology_t topology; /* 3800 */
    world_buffers_probe_query_t *queries;
    world_buffers_probe_result_t *results;
    uint8_t _pad0[16];
} upload_fixture_t;
static_assert(sizeof(upload_fixture_t) == 4352);

static const render_upload_budget_t upload_test_budget_large = {.bytes = 64ull << 20, .sections = 65536u};

static bool upload_fixture_begin(test_context_t *t, upload_fixture_t *f, uint32_t radius, uint64_t pool_bytes,
                                 render_upload_budget_t budget) {
    const uint32_t side = 2u * radius + 1u;
    gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    limits.device.pool_bytes[DEVICE_POOL_VOXELS] = UPLOAD_TEST_DEVICE_BYTES;
    limits.device.pool_bytes[DEVICE_POOL_FRAMES] = 32ull << 20;
    limits.device.pool_bytes[DEVICE_POOL_TEXTURES] = 1ull << 20;
    if (!CHECK_EQ_U64(gpu_bridge_init(&f->bridge, limits), GPU_STATUS_OK)) {
        return false;
    }
    if (!CHECK(thread_topology_read(&f->topology)) || !CHECK(job_init(&f->job, &f->topology))) {
        return false;
    }
    if (!CHECK(world_init(&f->world, (world_limits_t){64ull << 20, (uint64_t)side * side})) ||
        !CHECK(world_open(&f->world, radius, 0, 0))) {
        return false;
    }
    const render_limits_t render_limits = {
        .pool_bytes = UPLOAD_TEST_POOL_BYTES,
        .column_count = (uint64_t)side * side,
        .section_count = WORLD_EDIT_SNAPSHOT_SECTIONS_DEFAULT,
    };
    if (!CHECK(render_init(&f->render, &f->bridge.device_memory, &f->job, render_limits))) {
        return false;
    }
    if (!CHECK_EQ_U64(render_open(&f->render, &f->world.snapshot, pool_bytes, budget), GPU_STATUS_OK)) {
        return false;
    }
    if (!CHECK_EQ_U64(device_memory_alloc(&f->bridge.device_memory, DEVICE_POOL_FRAMES,
                                          UPLOAD_TEST_PROBE_COUNT * WORLD_BUFFERS_PROBE_BYTES, "probe", &f->scratch),
                      GPU_STATUS_OK)) {
        return false;
    }
    f->queries = test_alloc(t, UPLOAD_TEST_PROBE_COUNT * sizeof(f->queries[0]));
    f->results = test_alloc(t, UPLOAD_TEST_PROBE_COUNT * sizeof(f->results[0]));
    return true;
}

static void upload_fixture_end(test_context_t *t, upload_fixture_t *f) {
    CHECK_EQ_U64(device_memory_free(&f->bridge.device_memory, &f->scratch), GPU_STATUS_OK);
    render_close(&f->render);
    CHECK(gpu_bridge_exit_check(&f->bridge));
    render_deinit(&f->render);
    world_close(&f->world);
    world_deinit(&f->world);
    job_deinit(&f->job);
    gpu_bridge_deinit(&f->bridge);
}

/* ---- The test world: what a place holds is a function of where it is ---------------------------------------------- */

static uint32_t upload_world_hash(int32_t x, int32_t y, int32_t z) {
    uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ (uint32_t)z * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return h ^ (h >> 13);
}

static uint16_t upload_world_block(int32_t x, int32_t y, int32_t z) {
    const int32_t top = (int32_t)(upload_world_hash(x >> 2, y >> 2, 0) % 40u) - 8;
    if (z > top || z < -24) {
        return 0u;
    }
    const uint32_t h = upload_world_hash(x, y, z);
    return (h & 15u) == 0u ? 0u : (uint16_t)(1u + (h >> 8) % 400u);
}

/* Fills a column straight into its sections, as terrain will. */
static void upload_world_fill(test_context_t *t, upload_fixture_t *f, const world_map_request_t *request) {
    column_t *const column = world_map_stream_commit(&f->world.map, request);
    REQUIRE(column != nullptr);
    for (int32_t z = -24; z < 32; z++) {
        for (uint32_t y = 0; y < COLUMN_SIZE; y++) {
            for (uint32_t x = 0; x < COLUMN_SIZE; x++) {
                const int32_t wx = request->chunk_x * (int32_t)COLUMN_SIZE + (int32_t)x;
                const int32_t wy = request->chunk_y * (int32_t)COLUMN_SIZE + (int32_t)y;
                const uint16_t state = upload_world_block(wx, wy, z);
                if (state != 0u) {
                    CHECK_EQ_U64(column_block_set(column, &f->world.section_pool, x, y, z, state), COLUMN_STATUS_OK);
                    const uint8_t light = (uint8_t)(upload_world_hash(wx, wy, z) >> 24);
                    CHECK_EQ_U64(column_light_set(column, &f->world.section_pool, x, y, z, light), COLUMN_STATUS_OK);
                }
            }
        }
    }
    world_edit_column_filled(&f->world.edit, request->slot);
}

/* Everything streaming asks for now: leaves what has left, and fills what is wanted. Runs on the thread holding the map. */
static uint32_t upload_world_stream(test_context_t *t, upload_fixture_t *f) {
    world_map_leaving_t leaving;
    while (world_map_stream_leaving(&f->world.map, &leaving)) {
        world_map_unload(&f->world.map, leaving.slot);
    }
    uint32_t count = 0u;
    world_map_request_t requests[16];
    for (;;) {
        const uint32_t taken = world_map_stream_take(&f->world.map, requests, 16u);
        for (uint32_t i = 0; i < taken; i++) {
            upload_world_fill(t, f, &requests[i]);
        }
        count += taken;
        if (taken == 0u) {
            return count;
        }
    }
}

/* Frames and publishes until both sides have nothing left, as a game's loop would with nothing else happening. */
static bool upload_pump(test_context_t *t, upload_fixture_t *f) {
    const int64_t deadline = time_now_ns() + (int64_t)UPLOAD_TEST_PUMP_NS;
    render_upload_t *const upload = &f->render.upload;
    for (;;) {
        render_upload_resend_drain(upload, &f->world.edit);
        const bool published = world_edit_snapshot_publish(&f->world.snapshot);
        if (!CHECK_EQ_U64(render_upload_frame(upload), GPU_STATUS_OK)) {
            gpu_error_log(&f->render.buffers.last_error);
            return false;
        }
        /* Every view published has been read or replaced, so none is waiting. */
        const world_edit_snapshot_t *const snapshot = &f->world.snapshot;
        const bool none_waiting = snapshot->published_count == snapshot->acquired_count + snapshot->overwritten_count;
        if (!published && none_waiting && render_upload_idle(upload) && ring_count(&upload->resend) == 0u) {
            return true;
        }
        if (!CHECK(time_now_ns() < deadline)) {
            return false; /* it is stuck, and must not hang the whole suite */
        }
        sched_yield();
    }
}

/* ---- Asking the card ------------------------------------------------------------------------------------------- */

static void upload_expect_mismatch(test_context_t *t, const world_buffers_probe_query_t *query,
                                   const world_buffers_probe_result_t *result, uint32_t state, uint16_t material,
                                   uint8_t light) {
    fprintf(stderr,
            "voxel (%" PRId32 ", %" PRId32 ", %" PRId32 "): the card says state %" PRIu32 ", block %" PRIu32
            ", light %" PRIu32 "; the world says state %" PRIu32 ", block %" PRIu32 ", light %" PRIu32 "\n",
            query->x, query->y, query->z, (uint32_t)result->state, (uint32_t)result->material, (uint32_t)result->light,
            state, (uint32_t)material, (uint32_t)light);
    CHECK(false);
}

/* Up to UPLOAD_TEST_PROBE_COUNT questions at once; each answer against the world as it now is, read from its sections. */
static uint64_t upload_probe_flush(test_context_t *t, upload_fixture_t *f, uint32_t count) {
    world_buffers_t *const buffers = &f->render.buffers;
    if (!CHECK_EQ_U64(world_buffers_probe(buffers, &f->scratch, f->queries, f->results, count), GPU_STATUS_OK)) {
        gpu_error_log(&buffers->last_error);
        return 1u;
    }
    uint64_t wrong = 0u;
    for (uint32_t i = 0; i < count; i++) {
        const world_buffers_probe_query_t *const query = &f->queries[i];
        const world_buffers_probe_result_t *const result = &f->results[i];
        const column_t *const column =
            world_map_column_at(&f->world.map, column_chunk_of(query->x), column_chunk_of(query->y));
        uint32_t state = WORLD_BUFFERS_VOXEL_NOT_LOADED;
        uint16_t material = 0u;
        uint8_t light = 0u;
        if (column != nullptr) {
            state = WORLD_BUFFERS_VOXEL_EMPTY;
            if (column_z_in_storage(query->z)) {
                const section_t *const section = &column->sections[column_section_of(query->z)];
                const uint32_t index = section_voxel_index(column_local_of(query->x), column_local_of(query->y),
                                                           column_section_local_of(query->z));
                material = section_block_get(section, index);
                if (material != 0u) {
                    state = WORLD_BUFFERS_VOXEL_OCCUPIED;
                    light = section_light_get(section, index);
                }
            }
        }
        if (result->state != state || result->material != material || result->light != light) {
            if (wrong < 5u) {
                upload_expect_mismatch(t, query, result, state, material, light);
            }
            wrong++;
        }
    }
    return wrong;
}

/* Every voxel of every chunk in the window and two past it, through a real kernel. Returns how many disagree. */
static uint64_t upload_compare_world(test_context_t *t, upload_fixture_t *f) {
    const world_map_t *const map = &f->world.map;
    const int32_t size = (int32_t)COLUMN_SIZE;
    uint64_t wrong = 0u;
    uint32_t count = 0u;
    static const int32_t edges[] = {-1000, -257, -256, -251, 400, 500, 511, 512, 1000};
    enum : uint32_t { EDGE_COUNT = sizeof(edges) / sizeof(edges[0]) };
    const int32_t z_count = UPLOAD_TEST_Z_HIGH - UPLOAD_TEST_Z_LOW;
    for (int32_t chunk_y = map->min_y - 2; chunk_y < map->min_y + (int32_t)map->side + 2; chunk_y++) {
        for (int32_t chunk_x = map->min_x - 2; chunk_x < map->min_x + (int32_t)map->side + 2; chunk_x++) {
            for (int32_t y = chunk_y * size; y < (chunk_y + 1) * size; y++) {
                for (int32_t x = chunk_x * size; x < (chunk_x + 1) * size; x++) {
                    for (int32_t k = 0; k < z_count + (int32_t)EDGE_COUNT; k++) {
                        const int32_t z = k < z_count ? UPLOAD_TEST_Z_LOW + k : edges[k - z_count];
                        f->queries[count] = (world_buffers_probe_query_t){x, y, z};
                        count++;
                        if (count == UPLOAD_TEST_PROBE_COUNT) {
                            wrong += upload_probe_flush(t, f, count);
                            count = 0u;
                        }
                    }
                }
            }
        }
    }
    wrong += upload_probe_flush(t, f, count);
    return wrong;
}

/* The table, read back from the card, against the map and the edit log's own records: the section places and the height. */
static void upload_compare_table(test_context_t *t, upload_fixture_t *f) {
    const world_map_t *const map = &f->world.map;
    world_buffers_t *const buffers = &f->render.buffers;
    const uint64_t table_bytes = world_buffers_table_bytes(map->slot_count);
    world_buffers_column_t *const card = test_alloc(t, table_bytes);
    REQUIRE(device_memory_read(&f->bridge.device_memory, &f->render.owned_table, 0u, card, table_bytes) ==
            GPU_STATUS_OK);
    CHECK_MEM_EQ(card, buffers->borrowed_shadow, table_bytes);
    uint32_t sections = 0u;
    for (uint32_t slot = 0; slot < map->slot_count; slot++) {
        const world_map_slot_t *const entry = &map->borrowed_slots[slot];
        const world_buffers_column_t *const column = &card[slot];
        if (entry->state != WORLD_MAP_STATE_READY) {
            CHECK_EQ_U64(column->ready, 0u);
            continue;
        }
        CHECK_EQ_U64(column->ready, 1u);
        CHECK_EQ_I64(column->chunk_x, entry->chunk_x);
        CHECK_EQ_I64(column->chunk_y, entry->chunk_y);
        CHECK_EQ_U64(column->generation, entry->generation);
        CHECK_EQ_I64(column->top_z, world_edit_column_top_z(&f->world.edit, slot));
        for (uint32_t i = 0; i < COLUMN_SECTIONS; i++) {
            const bool has = column->sections[i] != 0u;
            CHECK_EQ_U64((column->section_mask >> i) & 1u, has ? 1u : 0u);
            sections += has ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(sections, buffers->section_count);
}

/* ---- The owner ---------------------------------------------------------------------------------------------------- */

TEST(render_init_takes_nothing_on_the_card_and_deinit_leaves_nothing) {
    heap_counters_t before;
    heap_counters_read(&before);
    render_t *const render = upload_test_aligned(t, sizeof(*render));
    /* No card has been started in this test, and no job system: init must not need them. */
    device_memory_t *const never_opened = test_alloc(t, sizeof(*never_opened));
    REQUIRE(render_init(render, never_opened, nullptr, render_limits_default()));
    CHECK_EQ_U64(never_opened->alloc_calls, 0u);
    render_deinit(render);
    render_deinit(render); /* a zeroed one, as on a partial start */
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls + 1u);
}

TEST(the_default_limits_fit_the_voxel_budget_on_whole_pages) {
    const render_limits_t limits = render_limits_default();
    CHECK_EQ_U64(limits.pool_bytes % WORLD_BUFFERS_PAGE_BYTES, 0u);
    CHECK(limits.pool_bytes + world_buffers_table_bytes((uint32_t)limits.column_count) <=
          (uint64_t)MC_GPU_BUDGET_VOXELS_MIB << 20);
    CHECK_EQ_U64(limits.section_count, WORLD_EDIT_SNAPSHOT_SECTIONS_DEFAULT);
}

TEST(render_init_refuses_limits_it_cannot_use) {
    render_t *const render = upload_test_aligned(t, sizeof(*render));
    device_memory_t *const memory = test_alloc(t, sizeof(*memory));
    render_limits_t limits = render_limits_default();
    limits.pool_bytes = WORLD_BUFFERS_PAGE_BYTES + 1u;
    CHECK(!render_init(render, memory, nullptr, limits));
    limits = render_limits_default();
    limits.column_count = 0u;
    CHECK(!render_init(render, memory, nullptr, limits));
    limits = render_limits_default();
    limits.section_count = 0u;
    CHECK(!render_init(render, memory, nullptr, limits));
}

TEST(a_pool_size_follows_the_table_within_the_limits) {
    render_t *const render = upload_test_aligned(t, sizeof(*render));
    device_memory_t *const memory = test_alloc(t, sizeof(*memory));
    REQUIRE(render_init(render, memory, nullptr, render_limits_default()));
    CHECK_EQ_U64(render_pool_bytes_for(render, 1u), RENDER_POOL_BYTES_MIN);
    const uint64_t r32 = render_pool_bytes_for(render, 65u * 65u);
    CHECK_EQ_U64(r32 % WORLD_BUFFERS_PAGE_BYTES, 0u);
    CHECK(r32 >= 65ull * 65ull * RENDER_POOL_BYTES_PER_COLUMN - WORLD_BUFFERS_PAGE_BYTES);
    CHECK_EQ_U64(render_pool_bytes_for(render, UINT32_MAX), RENDER_POOL_BYTES_DEFAULT);
    render_deinit(render);
}

TEST(render_open_past_the_limits_or_the_budget_takes_nothing) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, 2ull << 20, upload_test_budget_large));
    device_memory_t *const memory = &f->bridge.device_memory;
    render_close(&f->render);
    const uint64_t live = memory->live_blocks;
    CHECK_EQ_U64(render_open(&f->render, &f->world.snapshot, UPLOAD_TEST_POOL_BYTES + WORLD_BUFFERS_PAGE_BYTES,
                             upload_test_budget_large),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(memory->live_blocks, live);
    /* Within the limits, and not a whole number of pages: the blocks it took for that come back. */
    CHECK_EQ_U64(render_open(&f->render, &f->world.snapshot, WORLD_BUFFERS_PAGE_BYTES + 128u, upload_test_budget_large),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(memory->live_blocks, live);
    CHECK_EQ_U64(f->render.open, 0u);
    /* And after all that it opens. */
    CHECK_EQ_U64(render_open(&f->render, &f->world.snapshot, 2ull << 20, upload_test_budget_large), GPU_STATUS_OK);
    CHECK_EQ_U64(memory->live_blocks, live + 2u);
    upload_fixture_end(t, f);
}

TEST(deinit_closes_a_world_still_open) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, 2ull << 20, upload_test_budget_large));
    device_memory_t *const memory = &f->bridge.device_memory;
    CHECK_EQ_U64(memory->live_blocks, 3u); /* the pool, the table, and the probe's */
    render_deinit(&f->render);
    CHECK_EQ_U64(memory->live_blocks, 1u);
    upload_fixture_end(t, f);
}

/* ---- The slab pool ------------------------------------------------------------------------------------------------ */

TEST(slabs_come_in_the_stated_sizes) {
    CHECK_EQ_U64(world_buffers_slab_bytes(0u), 0u);
    CHECK_EQ_U64(world_buffers_slab_bytes(1u), 128u);
    CHECK_EQ_U64(world_buffers_slab_bytes(128u), 128u);
    CHECK_EQ_U64(world_buffers_slab_bytes(129u), 256u);
    CHECK_EQ_U64(world_buffers_slab_bytes(513u), 768u);
    CHECK_EQ_U64(world_buffers_slab_bytes(8192u), 8192u);
    CHECK_EQ_U64(world_buffers_slab_bytes(8193u), 13056u);
    CHECK_EQ_U64(world_buffers_slab_bytes(WORLD_BUFFERS_SECTION_BYTES_MAX), 13056u);
    CHECK_EQ_U64(world_buffers_slab_bytes(WORLD_BUFFERS_SECTION_BYTES_MAX + 1u), 0u);
}

/* One world_buffers_t on blocks of its own: no world, no workers. */
typedef struct upload_buffers_t {
    gpu_bridge_t bridge;
    device_block_t pool;
    device_block_t table;
    world_buffers_t buffers;
    uint8_t *host;
    uint8_t _pad0[32];
} upload_buffers_t;
static_assert(sizeof(upload_buffers_t) == 704);

static world_buffers_window_t upload_window(uint32_t side) {
    return (world_buffers_window_t){0, 0, 0u, 0u, side, side * side};
}

static bool upload_buffers_begin(test_context_t *t, upload_buffers_t *b, uint32_t side, uint64_t pool_bytes) {
    gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    limits.device.pool_bytes[DEVICE_POOL_VOXELS] = UPLOAD_TEST_DEVICE_BYTES;
    if (!CHECK_EQ_U64(gpu_bridge_init(&b->bridge, limits), GPU_STATUS_OK)) {
        return false;
    }
    device_memory_t *const memory = &b->bridge.device_memory;
    const world_buffers_window_t window = upload_window(side);
    if (!CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_VOXELS, pool_bytes, "test pool", &b->pool),
                      GPU_STATUS_OK) ||
        !CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_VOXELS, world_buffers_table_bytes(window.slot_count),
                                          "test table", &b->table),
                      GPU_STATUS_OK)) {
        return false;
    }
    const uint64_t host_bytes = world_buffers_host_bytes(window.slot_count, pool_bytes);
    b->host = upload_test_aligned(t, host_bytes);
    return CHECK_EQ_U64(world_buffers_init(&b->buffers, memory, &b->pool, &b->table, window, b->host, host_bytes),
                        GPU_STATUS_OK);
}

static void upload_buffers_end(test_context_t *t, upload_buffers_t *b) {
    world_buffers_deinit(&b->buffers);
    device_memory_t *const memory = &b->bridge.device_memory;
    CHECK_EQ_U64(device_memory_free(memory, &b->pool), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_free(memory, &b->table), GPU_STATUS_OK);
    gpu_bridge_deinit(&b->bridge);
}

/* What the model says should be in one section's place. */
typedef struct upload_model_t {
    uint32_t bytes; /* 0 for none */
    uint32_t seed;
} upload_model_t;
static_assert(sizeof(upload_model_t) == 8);

static void upload_pattern(uint8_t *out, uint32_t bytes, uint32_t seed) {
    uint32_t h = seed;
    for (uint32_t i = 0; i < bytes; i++) {
        h = h * 0x01000193u + 0x9Eu;
        out[i] = (uint8_t)(h >> 24);
    }
}

/* Every live section as the card holds it, its slab inside the pool, and no two slabs sharing a granule. */
static void upload_model_compare(test_context_t *t, upload_buffers_t *b, const upload_model_t *model,
                                 uint32_t slot_count) {
    device_memory_t *const memory = &b->bridge.device_memory;
    const world_buffers_t *const buffers = &b->buffers;
    uint8_t *const used = test_alloc(t, b->pool.bytes / WORLD_BUFFERS_GRANULE_BYTES);
    uint8_t *const card = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    uint8_t *const expected = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    uint64_t slab_bytes = 0u;
    uint32_t sections = 0u;
    world_buffers_column_t *const table = test_alloc(t, world_buffers_table_bytes(slot_count));
    REQUIRE(device_memory_read(memory, &b->table, 0u, table, world_buffers_table_bytes(slot_count)) == GPU_STATUS_OK);
    CHECK_MEM_EQ(table, buffers->borrowed_shadow, world_buffers_table_bytes(slot_count));
    for (uint32_t slot = 0; slot < slot_count; slot++) {
        const world_buffers_column_t *const column = &table[slot];
        for (uint32_t i = 0; i < WORLD_BUFFERS_COLUMN_SECTIONS; i++) {
            const upload_model_t *const entry = &model[slot * WORLD_BUFFERS_COLUMN_SECTIONS + i];
            CHECK_EQ_U64((column->section_mask >> i) & 1u, entry->bytes != 0u ? 1u : 0u);
            if (entry->bytes == 0u) {
                CHECK_EQ_U64(column->sections[i], 0u);
                continue;
            }
            REQUIRE(column->sections[i] != 0u);
            const uint64_t offset = (uint64_t)(column->sections[i] - 1u) * WORLD_BUFFERS_GRANULE_BYTES;
            const uint64_t slab = world_buffers_slab_bytes(entry->bytes);
            /* Within one page: the bump allocator never lets a slab cross. */
            CHECK_EQ_U64(offset / WORLD_BUFFERS_PAGE_BYTES, (offset + slab - 1u) / WORLD_BUFFERS_PAGE_BYTES);
            REQUIRE(offset + slab <= b->pool.bytes);
            for (uint64_t g = offset / WORLD_BUFFERS_GRANULE_BYTES; g < (offset + slab) / WORLD_BUFFERS_GRANULE_BYTES;
                 g++) {
                CHECK_EQ_U64(used[g], 0u);
                used[g] = 1u;
            }
            REQUIRE(device_memory_read(memory, &b->pool, offset, card, entry->bytes) == GPU_STATUS_OK);
            upload_pattern(expected, entry->bytes, entry->seed);
            CHECK_MEM_EQ(card, expected, entry->bytes);
            slab_bytes += slab;
            sections++;
        }
    }
    CHECK_EQ_U64(buffers->slab_bytes, slab_bytes);
    CHECK_EQ_U64(buffers->section_count, sections);
}

TEST(random_writes_match_a_brute_force_model_on_the_card) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 4u, 32ull << 20)); /* room for each of the sizes to hold a few pages */
    world_buffers_t *const buffers = &b->buffers;
    const uint32_t slots = 16u;
    upload_model_t *const model = test_alloc(t, slots * WORLD_BUFFERS_COLUMN_SECTIONS * sizeof(upload_model_t));
    uint8_t *const pattern = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    for (uint32_t slot = 0; slot < slots; slot++) {
        world_buffers_column_set(buffers, slot, (int32_t)(slot & 3u), (int32_t)(slot >> 2), 1u, 0);
    }
    uint64_t rng = 0x7C15F0A3D29E4B68ull;
    for (uint32_t op = 0; op < 20000u; op++) {
        const uint64_t r = upload_test_random(&rng);
        const uint32_t slot = (uint32_t)(r % slots);
        const uint32_t section = (uint32_t)((r >> 8) % WORLD_BUFFERS_COLUMN_SECTIONS);
        upload_model_t *const entry = &model[slot * WORLD_BUFFERS_COLUMN_SECTIONS + section];
        /* Mostly small, so that every size gets exercised at once, as in a real world. */
        uint32_t bytes = (r >> 16) % 8u == 0u ? 0u : 1u + (uint32_t)((r >> 20) % WORLD_BUFFERS_SECTION_BYTES_MAX);
        bytes = (r >> 56) % 2u == 0u ? bytes / 16u : bytes;
        const uint32_t seed = (uint32_t)(r >> 32);
        upload_pattern(pattern, bytes, seed);
        const gpu_status_t status = world_buffers_section_write(buffers, slot, section, pattern, bytes);
        REQUIRE(status == GPU_STATUS_OK);
        *entry = (upload_model_t){bytes, seed};
        if (op % 2500u == 0u) {
            REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
            upload_model_compare(t, b, model, slots);
        }
    }
    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    upload_model_compare(t, b, model, slots);
    /* All of it back, and every page is free for any size. */
    for (uint32_t slot = 0; slot < slots; slot++) {
        world_buffers_column_clear(buffers, slot);
    }
    CHECK_EQ_U64(buffers->slab_bytes, 0u);
    CHECK_EQ_U64(buffers->section_count, 0u);
    CHECK_EQ_U64(buffers->pages_live, 0u);
    upload_buffers_end(t, b);
}

TEST(a_full_pool_refuses_keeps_what_was_there_and_recovers) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 4u, WORLD_BUFFERS_PAGE_BYTES));
    world_buffers_t *const buffers = &b->buffers;
    device_memory_t *const memory = &b->bridge.device_memory;
    uint8_t *const pattern = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    uint8_t *const card = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    for (uint32_t slot = 0; slot < 16u; slot++) {
        world_buffers_column_set(buffers, slot, (int32_t)(slot & 3u), (int32_t)(slot >> 2), 1u, 0);
    }
    /* 1 MiB holds 80 slabs of 13056 bytes. */
    for (uint32_t i = 0; i < 80u; i++) {
        upload_pattern(pattern, WORLD_BUFFERS_SECTION_BYTES_MAX, i);
        REQUIRE(world_buffers_section_write(buffers, i / WORLD_BUFFERS_COLUMN_SECTIONS,
                                            i % WORLD_BUFFERS_COLUMN_SECTIONS, pattern,
                                            WORLD_BUFFERS_SECTION_BYTES_MAX) == GPU_STATUS_OK);
    }
    CHECK_EQ_U64(buffers->pool_full_count, 0u);
    CHECK_EQ_U64(buffers->pages_live, 1u);

    /* A new section has nowhere to go, in this size or in any other. */
    CHECK_EQ_U64(world_buffers_section_write(buffers, 2u, 0u, pattern, 100u), GPU_STATUS_OVER_BUDGET);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 2u, 0u, pattern, WORLD_BUFFERS_SECTION_BYTES_MAX),
                 GPU_STATUS_OVER_BUDGET);
    CHECK_EQ_U64(buffers->pool_full_count, 2u);
    CHECK_EQ_U64(buffers->borrowed_shadow[2].section_mask, 0u);

    /* A section that would move to a smaller size stays whole where it is, with no slab of that size. */
    upload_pattern(pattern, 100u, 999u);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 0u, 5u, pattern, 100u), GPU_STATUS_OVER_BUDGET);
    upload_pattern(pattern, WORLD_BUFFERS_SECTION_BYTES_MAX, 5u);
    const uint64_t offset = (uint64_t)(buffers->borrowed_shadow[0].sections[5] - 1u) * WORLD_BUFFERS_GRANULE_BYTES;
    REQUIRE(device_memory_read(memory, &b->pool, offset, card, WORLD_BUFFERS_SECTION_BYTES_MAX) == GPU_STATUS_OK);
    CHECK_MEM_EQ(card, pattern, WORLD_BUFFERS_SECTION_BYTES_MAX);

    /* But the same size replaces in place, full as the pool is. */
    upload_pattern(pattern, 12000u, 1000u);
    const uint32_t entry_before = buffers->borrowed_shadow[0].sections[5];
    CHECK_EQ_U64(world_buffers_section_write(buffers, 0u, 5u, pattern, 12000u), GPU_STATUS_OK);
    CHECK_EQ_U64(buffers->borrowed_shadow[0].sections[5], entry_before);

    /* One given back, and one can come. */
    CHECK_EQ_U64(world_buffers_section_write(buffers, 0u, 6u, nullptr, 0u), GPU_STATUS_OK);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 2u, 0u, pattern, WORLD_BUFFERS_SECTION_BYTES_MAX), GPU_STATUS_OK);

    /* All given back, and the page serves a different size: one size cannot starve another for good. */
    for (uint32_t slot = 0; slot < 16u; slot++) {
        world_buffers_column_clear(buffers, slot);
    }
    CHECK_EQ_U64(buffers->pages_live, 0u);
    world_buffers_column_set(buffers, 0u, 0, 0, 2u, 0);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 0u, 0u, pattern, 100u), GPU_STATUS_OK);
    CHECK_EQ_U64(buffers->pages_live, 1u);
    CHECK_EQ_U64(buffers->pages_used, 1u);
    upload_buffers_end(t, b);
}

TEST(a_page_taken_back_into_its_list_when_a_slab_returns_serves_again) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 4u, 2ull * WORLD_BUFFERS_PAGE_BYTES));
    world_buffers_t *const buffers = &b->buffers;
    uint8_t *const pattern = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    for (uint32_t slot = 0; slot < 16u; slot++) {
        world_buffers_column_set(buffers, slot, (int32_t)(slot & 3u), (int32_t)(slot >> 2), 1u, 0);
    }
    /* Two pages' worth of the largest, across four columns. */
    for (uint32_t i = 0; i < 160u; i++) {
        REQUIRE(world_buffers_section_write(buffers, i / WORLD_BUFFERS_COLUMN_SECTIONS,
                                            i % WORLD_BUFFERS_COLUMN_SECTIONS, pattern,
                                            WORLD_BUFFERS_SECTION_BYTES_MAX) == GPU_STATUS_OK);
    }
    CHECK_EQ_U64(buffers->pages_live, 2u);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 15u, 0u, pattern, 12900u), GPU_STATUS_OVER_BUDGET);
    /* Back and forth over the same place, many more times than there is room for if any were lost. */
    for (uint32_t i = 0; i < 1000u; i++) {
        REQUIRE(world_buffers_section_write(buffers, 0u, 0u, nullptr, 0u) == GPU_STATUS_OK);
        REQUIRE(world_buffers_section_write(buffers, 15u, 0u, pattern, WORLD_BUFFERS_SECTION_BYTES_MAX) ==
                GPU_STATUS_OK);
        REQUIRE(world_buffers_section_write(buffers, 15u, 0u, nullptr, 0u) == GPU_STATUS_OK);
        REQUIRE(world_buffers_section_write(buffers, 0u, 0u, pattern, WORLD_BUFFERS_SECTION_BYTES_MAX) ==
                GPU_STATUS_OK);
    }
    CHECK_EQ_U64(buffers->section_count, 160u);
    CHECK_EQ_U64(buffers->slab_bytes, 160ull * 13056ull);
    upload_buffers_end(t, b);
}

TEST(a_write_to_a_place_that_cannot_exist_is_refused) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 2u, WORLD_BUFFERS_PAGE_BYTES));
    world_buffers_t *const buffers = &b->buffers;
    uint8_t byte = 1u;
    CHECK_EQ_U64(world_buffers_section_write(buffers, 0u, 0u, &byte, 1u), GPU_STATUS_INVALID_ARGUMENT); /* no column */
    world_buffers_column_set(buffers, 0u, 0, 0, 1u, 0);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 4u, 0u, &byte, 1u), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 0u, 48u, &byte, 1u), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(world_buffers_section_write(buffers, 0u, 0u, &byte, WORLD_BUFFERS_SECTION_BYTES_MAX + 1ull),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(buffers->section_count, 0u);
    /* A scratch too small for the answers. */
    device_block_t *const scratch = test_alloc(t, sizeof(*scratch));
    device_memory_t *const memory = &b->bridge.device_memory;
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, WORLD_BUFFERS_PROBE_BYTES - 1u, "small", scratch) ==
            GPU_STATUS_OK);
    world_buffers_probe_query_t query = {0, 0, 0};
    world_buffers_probe_result_t result;
    CHECK_EQ_U64(world_buffers_probe(buffers, scratch, &query, &result, 1u), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(device_memory_free(memory, scratch), GPU_STATUS_OK);
    upload_buffers_end(t, b);
}

TEST(init_refuses_blocks_and_memory_of_the_wrong_size) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 2u, WORLD_BUFFERS_PAGE_BYTES));
    device_memory_t *const memory = &b->bridge.device_memory;
    world_buffers_t *const other = test_alloc(t, sizeof(*other));
    const uint64_t host_bytes = world_buffers_host_bytes(4u, WORLD_BUFFERS_PAGE_BYTES);
    CHECK_EQ_U64(world_buffers_init(other, memory, &b->pool, &b->table, upload_window(3u), b->host, host_bytes),
                 GPU_STATUS_INVALID_ARGUMENT); /* a table block for a different table */
    CHECK_EQ_U64(world_buffers_init(other, memory, &b->pool, &b->table, upload_window(2u), b->host, host_bytes - 1u),
                 GPU_STATUS_INVALID_ARGUMENT); /* too little host memory */
    CHECK_EQ_U64(world_buffers_init(other, memory, &b->pool, &b->table, upload_window(2u), b->host + 1, host_bytes),
                 GPU_STATUS_INVALID_ARGUMENT); /* misaligned */
    world_buffers_window_t not_square = upload_window(2u);
    not_square.slot_count = 5u;
    CHECK_EQ_U64(world_buffers_init(other, memory, &b->pool, &b->table, not_square, b->host, host_bytes),
                 GPU_STATUS_INVALID_ARGUMENT);
    upload_buffers_end(t, b);
}

/* ---- The table and the kernel ----------------------------------------------------------------------------------- */

TEST(the_card_sees_a_change_only_after_a_flush_and_only_what_changed_is_sent) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 16u, WORLD_BUFFERS_PAGE_BYTES));
    world_buffers_t *const buffers = &b->buffers;
    device_memory_t *const memory = &b->bridge.device_memory;
    device_block_t *const scratch = test_alloc(t, sizeof(*scratch));
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, 16u * WORLD_BUFFERS_PROBE_BYTES, "probe", scratch) ==
            GPU_STATUS_OK);
    world_buffers_probe_query_t query = {3, 4, 5};
    world_buffers_probe_result_t result = {};

    /* A fresh table says nothing is loaded, even over whatever the card's memory held. */
    REQUIRE(world_buffers_probe(buffers, scratch, &query, &result, 1u) == GPU_STATUS_OK);
    CHECK_EQ_U64(result.state, WORLD_BUFFERS_VOXEL_NOT_LOADED);

    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const encoded = upload_test_aligned(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    blocks[section_voxel_index(3u, 4u, 5u)] = 77u;
    const render_encode_source_t source = {.borrowed_blocks = blocks, .light_uniform = 0x42u};
    const uint32_t bytes = render_encode_section(&source, encoded);
    world_buffers_column_set(buffers, 0u, 0, 0, 1u, 5);
    REQUIRE(world_buffers_section_write(buffers, 0u, 16u, encoded, bytes) == GPU_STATUS_OK);
    REQUIRE(world_buffers_probe(buffers, scratch, &query, &result, 1u) == GPU_STATUS_OK);
    CHECK_EQ_U64(result.state, WORLD_BUFFERS_VOXEL_NOT_LOADED); /* the section is there, and nothing points at it yet */

    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    CHECK_EQ_U64(buffers->flush_bytes, sizeof(world_buffers_column_t)); /* one slot of 256, and not the table */
    CHECK_EQ_U64(buffers->flush_whole_count, 0u);
    REQUIRE(world_buffers_probe(buffers, scratch, &query, &result, 1u) == GPU_STATUS_OK);
    CHECK_EQ_U64(result.state, WORLD_BUFFERS_VOXEL_OCCUPIED);
    CHECK_EQ_U64(result.material, 77u);
    CHECK_EQ_U64(result.light, 0x42u);

    /* A flush with nothing to send sends nothing. */
    const uint32_t flushes = buffers->flush_count;
    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    CHECK_EQ_U64(buffers->flush_count, flushes);
    CHECK_EQ_U64(buffers->dirty_count, 0u);

    /* Many slots at once go as one copy, and arrive all the same. */
    for (uint32_t slot = 1u; slot < 40u; slot++) {
        world_buffers_column_set(buffers, slot, (int32_t)(slot % 16u), (int32_t)(slot / 16u), 1u, 1);
    }
    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    CHECK_EQ_U64(buffers->flush_whole_count, 1u);
    world_buffers_column_t *const card = test_alloc(t, world_buffers_table_bytes(256u));
    REQUIRE(device_memory_read(memory, &b->table, 0u, card, world_buffers_table_bytes(256u)) == GPU_STATUS_OK);
    CHECK_MEM_EQ(card, buffers->borrowed_shadow, world_buffers_table_bytes(256u));

    CHECK_EQ_U64(device_memory_free(memory, scratch), GPU_STATUS_OK);
    upload_buffers_end(t, b);
}

TEST(a_new_load_of_a_slot_starts_empty_and_its_old_slabs_come_back) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 2u, WORLD_BUFFERS_PAGE_BYTES));
    world_buffers_t *const buffers = &b->buffers;
    uint8_t *const pattern = test_alloc(t, 1000u);
    world_buffers_column_set(buffers, 1u, 7, 8, 1u, 9);
    REQUIRE(world_buffers_section_write(buffers, 1u, 3u, pattern, 1000u) == GPU_STATUS_OK);
    REQUIRE(world_buffers_section_write(buffers, 1u, 4u, pattern, 1000u) == GPU_STATUS_OK);
    CHECK_EQ_U64(buffers->section_count, 2u);
    /* The same load with a new height keeps its sections. */
    world_buffers_column_set(buffers, 1u, 7, 8, 1u, 30);
    CHECK_EQ_U64(buffers->section_count, 2u);
    CHECK_EQ_I64(buffers->borrowed_shadow[1].top_z, 30);
    /* A new load does not, even in the same chunk. */
    world_buffers_column_set(buffers, 1u, 7, 8, 2u, 9);
    CHECK_EQ_U64(buffers->section_count, 0u);
    CHECK_EQ_U64(buffers->borrowed_shadow[1].section_mask, 0u);
    CHECK_EQ_U64(buffers->slab_bytes, 0u);
    upload_buffers_end(t, b);
}

TEST(a_slot_still_holding_another_chunk_after_a_scroll_says_not_loaded) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 2u, WORLD_BUFFERS_PAGE_BYTES));
    world_buffers_t *const buffers = &b->buffers;
    device_memory_t *const memory = &b->bridge.device_memory;
    device_block_t *const scratch = test_alloc(t, sizeof(*scratch));
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, 4u * WORLD_BUFFERS_PROBE_BYTES, "probe", scratch) ==
            GPU_STATUS_OK);
    world_buffers_column_set(buffers, 0u, 0, 0, 1u, 0);
    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    /* Chunk (2, 0) now has slot 0, and the column there is still chunk (0, 0), which has left the window. */
    world_buffers_set_window(buffers, (world_buffers_window_t){1, 0, 1u, 0u, 2u, 4u});
    const world_buffers_probe_query_t queries[3] = {{2 * 16, 0, 0}, {0, 0, 0}, {1 * 16, 0, 0}};
    world_buffers_probe_result_t results[3];
    REQUIRE(world_buffers_probe(buffers, scratch, queries, results, 3u) == GPU_STATUS_OK);
    CHECK_EQ_U64(results[0].state, WORLD_BUFFERS_VOXEL_NOT_LOADED);
    CHECK_EQ_U64(results[1].state, WORLD_BUFFERS_VOXEL_NOT_LOADED); /* outside the window altogether */
    CHECK_EQ_U64(results[2].state, WORLD_BUFFERS_VOXEL_NOT_LOADED); /* in it, and slot 1 holds nothing */
    CHECK_EQ_U64(device_memory_free(memory, scratch), GPU_STATUS_OK);
    upload_buffers_end(t, b);
}

/* ---- The whole way, against a real world ------------------------------------------------------------------------- */

TEST(a_world_reaches_the_card_voxel_for_voxel) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 2u, UPLOAD_TEST_POOL_BYTES, upload_test_budget_large));
    CHECK_EQ_U64(upload_world_stream(t, f), 13u); /* radius 2 is a circle: x * x + y * y <= 4 */
    REQUIRE(upload_pump(t, f));
    CHECK_EQ_U64(upload_compare_world(t, f), 0u);
    upload_compare_table(t, f);
    const render_upload_stats_t *const stats = &f->render.upload.stats;
    CHECK(stats->sections_sent >= 25u);
    CHECK_EQ_U64(stats->sections_stale, 0u);
    CHECK_EQ_U64(stats->sections_resent, 0u);
    /* Everything sent lives on the card: each section took the slab its size says. */
    CHECK_EQ_U64(f->render.buffers.section_count, stats->sections_sent);
    CHECK(f->render.buffers.slab_bytes >= stats->bytes_sent);
    upload_fixture_end(t, f);
}

TEST(edits_and_light_reach_the_card_and_an_emptied_section_gives_its_slab_back) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, UPLOAD_TEST_POOL_BYTES, upload_test_budget_large));
    upload_world_stream(t, f);
    REQUIRE(upload_pump(t, f));
    world_edit_t *const edit = &f->world.edit;
    uint64_t rng = 0x4B7E0D12C9A3F685ull;
    for (uint32_t i = 0; i < 2000u; i++) {
        const uint64_t r = upload_test_random(&rng);
        const int32_t x = (int32_t)(r % 48u) - 16;
        const int32_t y = (int32_t)((r >> 8) % 48u) - 16;
        const int32_t z = (int32_t)((r >> 16) % 80u) - 30;
        if ((r >> 24) % 4u == 0u) {
            (void)world_edit_light_set(edit, x, y, z, (uint8_t)(r >> 32));
        } else {
            (void)world_edit_block_set(edit, x, y, z, (r >> 26) % 3u == 0u ? 0u : (uint16_t)(1u + (r >> 40) % 1000u));
        }
        if (i % 257u == 0u) {
            REQUIRE(upload_pump(t, f));
        }
    }
    REQUIRE(upload_pump(t, f));
    CHECK_EQ_U64(upload_compare_world(t, f), 0u);
    upload_compare_table(t, f);

    /* A place high up holds one block, and then none: the slab comes and goes. */
    const uint32_t sections = f->render.buffers.section_count;
    const uint64_t slab_bytes = f->render.buffers.slab_bytes;
    CHECK_EQ_U64(world_edit_block_set(edit, 3, 3, 400, 5u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(upload_pump(t, f));
    CHECK_EQ_U64(f->render.buffers.section_count, sections + 1u);
    CHECK_EQ_U64(f->render.buffers.slab_bytes, slab_bytes + WORLD_BUFFERS_GRANULE_BYTES);
    CHECK_EQ_U64(upload_compare_world(t, f), 0u);
    CHECK_EQ_U64(world_edit_block_set(edit, 3, 3, 400, 0u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(upload_pump(t, f));
    CHECK_EQ_U64(f->render.buffers.section_count, sections);
    CHECK_EQ_U64(f->render.buffers.slab_bytes, slab_bytes);
    CHECK(f->render.upload.stats.sections_emptied > 0u);
    upload_fixture_end(t, f);
}

TEST(walking_scrolls_the_table_and_what_is_left_behind_comes_back) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 2u, UPLOAD_TEST_POOL_BYTES, upload_test_budget_large));
    world_map_set_view_radius(&f->world.map, 1u);
    upload_world_stream(t, f);
    REQUIRE(upload_pump(t, f));
    const world_buffers_t *const buffers = &f->render.buffers;
    const uint32_t pages_at_start = buffers->pages_live;
    int32_t center_x = 0;
    int32_t center_y = 0;
    const int32_t steps[][2] = {{1, 0}, {1, 1}, {3, 0}, {0, -5}, {-7, 0}, {9, 9}, {-40, 25}, {40, -25}};
    for (uint32_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        center_x += steps[i][0];
        center_y += steps[i][1];
        world_map_set_center(&f->world.map, center_x, center_y);
        upload_world_stream(t, f);
        REQUIRE(upload_pump(t, f));
        CHECK_EQ_U64(upload_compare_world(t, f), 0u);
        upload_compare_table(t, f);
    }
    /* Walked a long way, and the pool has not filled up with what is behind: only about the same is live. */
    CHECK(buffers->pages_live <= pages_at_start + WORLD_BUFFERS_CLASS_COUNT);
    upload_fixture_end(t, f);
}

static void upload_budget_run(test_context_t *t, render_upload_budget_t budget) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, UPLOAD_TEST_POOL_BYTES, budget));
    upload_world_stream(t, f);
    const render_upload_t *const upload = &f->render.upload;
    const int64_t deadline = time_now_ns() + (int64_t)UPLOAD_TEST_PUMP_NS;
    world_edit_snapshot_publish(&f->world.snapshot);
    uint32_t frames = 0u;
    uint64_t most_bytes = 0u;
    uint32_t most_sections = 0u;
    do {
        REQUIRE(render_upload_frame(&f->render.upload) == GPU_STATUS_OK);
        most_bytes = upload->stats.frame_bytes > most_bytes ? upload->stats.frame_bytes : most_bytes;
        most_sections = upload->stats.frame_sections > most_sections ? upload->stats.frame_sections : most_sections;
        frames++;
        REQUIRE(time_now_ns() < deadline);
        sched_yield();
    } while (!render_upload_idle(upload));
    CHECK(most_bytes <= budget.bytes);
    CHECK(most_sections <= budget.sections);
    CHECK(most_sections > 0u);
    /* The limits did hold it back, and were not simply never reached. */
    CHECK(upload->stats.budget_stop_count > 0u);
    CHECK(frames >= upload->stats.sections_sent / budget.sections);
    REQUIRE(upload_pump(t, f));
    CHECK_EQ_U64(upload_compare_world(t, f), 0u);
    upload_fixture_end(t, f);
}

TEST(no_frame_goes_over_its_bytes_and_the_world_still_arrives_whole) {
    upload_budget_run(t, (render_upload_budget_t){.bytes = 2ull * WORLD_BUFFERS_SECTION_BYTES_MAX, .sections = 65536u});
}

TEST(no_frame_goes_over_its_copies_and_the_world_still_arrives_whole) {
    upload_budget_run(t, (render_upload_budget_t){.bytes = 64ull << 20, .sections = 2u});
}

TEST(a_full_pool_sends_sections_back_and_they_arrive_once_there_is_room) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    /* The 113 columns inside radius 6, of about 30 KiB, will not go in four pages. A column has at most four sections here, so that one column does
     * fit, whichever slab sizes it wants. */
    REQUIRE(upload_fixture_begin(t, f, 6u, 4ull * WORLD_BUFFERS_PAGE_BYTES, upload_test_budget_large));
    upload_world_stream(t, f);
    render_upload_t *const upload = &f->render.upload;
    /* Not to quiet: this cannot settle while the pool is full. A fair few rounds of asking. */
    const int64_t deadline = time_now_ns() + (int64_t)UPLOAD_TEST_PUMP_NS;
    while ((upload->stats.sections_resent < 50u || ring_count(&upload->resend) == 0u) && time_now_ns() < deadline) {
        render_upload_resend_drain(upload, &f->world.edit);
        world_edit_snapshot_publish(&f->world.snapshot);
        REQUIRE(render_upload_frame(upload) == GPU_STATUS_OK);
        sched_yield();
    }
    REQUIRE(upload->stats.sections_resent >= 50u && ring_count(&upload->resend) > 0u);
    CHECK(f->render.buffers.pool_full_count >= 50u);

    /* The section refused last, read from the ring without taking it. Nothing else will ever mark it dirty again, so if the
     * refusal is lost, this section never arrives. */
    const uint64_t last = atomic_load_explicit(&upload->resend.tail, memory_order_acquire) - 1u;
    world_edit_dirty_t refused;
    memcpy(&refused, upload->resend.borrowed_storage + (last & (RENDER_UPLOAD_RESEND_CAPACITY - 1u)) * sizeof(refused),
           sizeof(refused));
    const uint32_t refused_index = (uint32_t)__builtin_ctzll(refused.section_mask);
    CHECK_EQ_U64((f->render.buffers.borrowed_shadow[refused.slot].section_mask >> refused_index) & 1u, 0u);

    /* All but its column leaves, which makes room. */
    world_map_set_view_radius(&f->world.map, 0u);
    for (uint32_t slot = 0; slot < f->world.map.slot_count; slot++) {
        if (slot != refused.slot && f->world.map.borrowed_slots[slot].state == WORLD_MAP_STATE_READY) {
            world_map_unload(&f->world.map, slot);
        }
    }
    REQUIRE(upload_pump(t, f));
    CHECK_EQ_U64(f->world.map.ready_count, 1u);
    CHECK_EQ_U64((f->render.buffers.borrowed_shadow[refused.slot].section_mask >> refused_index) & 1u, 1u);
    CHECK_EQ_U64(upload_compare_world(t, f), 0u);
    upload_compare_table(t, f);
    upload_fixture_end(t, f);
}

/* ---- What a view's lifetime depends on ---------------------------------------------------------------------------- */

typedef struct upload_gate_t {
    alignas(64) _Atomic uint32_t open;
    uint8_t _pad0[60];
    alignas(64) _Atomic uint32_t entered;
    uint8_t _pad1[60];
} upload_gate_t;
static_assert(sizeof(upload_gate_t) == 128);

static void upload_gate_job(job_worker_t *worker, void *borrowed_gate) {
    (void)worker;
    upload_gate_t *const gate = borrowed_gate;
    atomic_fetch_add_explicit(&gate->entered, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&gate->open, memory_order_acquire) == 0u) {
        sched_yield();
    }
}

/* Every worker held at a gate, so that an encode job cannot run, and the next view is there to be taken. Taking it would let
 * the publisher write over what those jobs are about to read. */
TEST(a_view_is_not_let_go_while_a_job_still_has_to_read_it) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, UPLOAD_TEST_POOL_BYTES, upload_test_budget_large));
    upload_world_stream(t, f);
    upload_gate_t *const gate = upload_test_aligned(t, sizeof(*gate));
    job_group_t *const gates = upload_test_aligned(t, sizeof(*gates));
    job_group_init(gates);
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_submit(&f->job, gates, upload_gate_job, gate);
    }
    while (atomic_load_explicit(&gate->entered, memory_order_relaxed) != MC_THREADS_WORKERS) {
        sched_yield();
    }

    render_upload_t *const upload = &f->render.upload;
    REQUIRE(world_edit_snapshot_publish(&f->world.snapshot));
    REQUIRE(render_upload_frame(upload) == GPU_STATUS_OK);
    CHECK_EQ_U64(upload->stats.views, 1u);
    CHECK(upload->submitted > 0u);
    CHECK(!render_upload_idle(upload));
    CHECK_EQ_U64(upload->stats.sections_sent, 0u); /* and frames did not wait for the workers either */

    /* A newer view, and frame after frame: it must wait, however many frames that takes. */
    CHECK_EQ_U64(world_edit_block_set(&f->world.edit, 0, 0, 100, 3u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_snapshot_publish(&f->world.snapshot));
    for (uint32_t i = 0; i < 20u; i++) {
        REQUIRE(render_upload_frame(upload) == GPU_STATUS_OK);
    }
    CHECK_EQ_U64(upload->stats.views, 1u);
    CHECK_EQ_U64(f->world.snapshot.acquired_count, 1u);

    atomic_store_explicit(&gate->open, 1u, memory_order_release);
    job_group_wait(&f->job, gates);
    job_group_deinit(gates);
    REQUIRE(upload_pump(t, f));
    CHECK_EQ_U64(upload->stats.views, 2u);
    CHECK_EQ_U64(upload_compare_world(t, f), 0u);
    upload_fixture_end(t, f);
}

/* A view's sections always match its own table, so this cannot happen by itself: the test takes the column away behind the
 * queue's back. A write to a column that has gone would be refused as a bug, and stop the frame. */
TEST(a_section_whose_column_has_gone_is_dropped_and_no_failure) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, UPLOAD_TEST_POOL_BYTES, upload_test_budget_large));
    upload_world_stream(t, f);
    upload_gate_t *const gate = upload_test_aligned(t, sizeof(*gate));
    job_group_t *const gates = upload_test_aligned(t, sizeof(*gates));
    job_group_init(gates);
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_submit(&f->job, gates, upload_gate_job, gate);
    }
    render_upload_t *const upload = &f->render.upload;
    REQUIRE(world_edit_snapshot_publish(&f->world.snapshot));
    REQUIRE(render_upload_frame(upload) == GPU_STATUS_OK);
    REQUIRE(upload->borrowed_view != nullptr && upload->borrowed_view->section_count > 0u);
    world_buffers_column_clear(&f->render.buffers, upload->borrowed_view->borrowed_sections[0].slot);
    atomic_store_explicit(&gate->open, 1u, memory_order_release);
    job_group_wait(&f->job, gates);
    job_group_deinit(gates);
    REQUIRE(upload_pump(t, f));
    CHECK(upload->stats.sections_stale > 0u);
    CHECK(upload->stats.sections_sent > 0u);
    upload_fixture_end(t, f);
}

static void upload_gate_open_later(void *borrowed_gate) {
    upload_gate_t *const gate = borrowed_gate;
    const int64_t until = time_now_ns() + 50000000;
    while (time_now_ns() < until) {
        sched_yield();
    }
    atomic_store_explicit(&gate->open, 1u, memory_order_release);
}

TEST(closing_waits_for_jobs_still_on_their_way) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, UPLOAD_TEST_POOL_BYTES, upload_test_budget_large));
    upload_world_stream(t, f);
    upload_gate_t *const gate = upload_test_aligned(t, sizeof(*gate));
    job_group_t *const gates = upload_test_aligned(t, sizeof(*gates));
    job_group_init(gates);
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_submit(&f->job, gates, upload_gate_job, gate);
    }
    REQUIRE(world_edit_snapshot_publish(&f->world.snapshot));
    REQUIRE(render_upload_frame(&f->render.upload) == GPU_STATUS_OK);
    CHECK(!render_upload_idle(&f->render.upload));
    thread_t *const opener = upload_test_aligned(t, sizeof(*opener));
    REQUIRE(thread_start(opener, &f->topology, THREAD_ROLE_SIM, 0u, upload_gate_open_later, gate));
    render_close(&f->render); /* job_group_deinit inside aborts if any is still out */
    CHECK_EQ_U64(f->render.open, 0u);
    thread_join(opener);
    job_group_wait(&f->job, gates);
    job_group_deinit(gates);
    upload_fixture_end(t, f);
}

/* ---- With everything running at once ----------------------------------------------------------------------------- */

TEST(frames_cost_no_heap_calls) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    REQUIRE(upload_fixture_begin(t, f, 1u, UPLOAD_TEST_POOL_BYTES, upload_test_budget_large));
    heap_counters_t before;
    heap_counters_read(&before);
    const uint64_t device_calls = f->bridge.device_memory.alloc_calls;
    upload_world_stream(t, f);
    REQUIRE(upload_pump(t, f));
    for (uint32_t i = 0; i < 200u; i++) {
        CHECK_EQ_U64(world_edit_block_set(&f->world.edit, (int32_t)(i % 16u), 0, 80, (uint16_t)(i + 1u)),
                     WORLD_EDIT_STATUS_CHANGED);
        REQUIRE(upload_pump(t, f));
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(f->bridge.device_memory.alloc_calls, device_calls);
    upload_fixture_end(t, f);
}

/* The sim thread's life: edits, walking, streaming, publishing, handing refused sections back. */
typedef struct upload_sim_t {
    alignas(64) _Atomic uint32_t done;
    uint8_t _pad0[60];
    upload_fixture_t *borrowed_fixture;
    test_context_t *borrowed_test;
    uint8_t _pad1[48];
} upload_sim_t;
static_assert(sizeof(upload_sim_t) == 128);

static void upload_sim_run(void *borrowed_sim) {
    upload_sim_t *const sim = borrowed_sim;
    upload_fixture_t *const f = sim->borrowed_fixture;
    test_context_t *const t = sim->borrowed_test;
    uint64_t rng = 0x93D2A17F6C0E5B48ull;
    int32_t center_x = 0;
    for (uint32_t i = 0; i < UPLOAD_TEST_SIM_EDITS; i++) {
        const uint64_t r = upload_test_random(&rng);
        const int32_t x = (center_x - 2) * 16 + (int32_t)(r % 80u);
        const int32_t y = (int32_t)((r >> 8) % 80u) - 32;
        const int32_t z = (int32_t)((r >> 16) % 96u) - 32;
        if ((r >> 24) % 5u == 0u) {
            (void)world_edit_light_set(&f->world.edit, x, y, z, (uint8_t)(r >> 32));
        } else {
            (void)world_edit_block_set(&f->world.edit, x, y, z,
                                       (r >> 28) % 3u == 0u ? 0u : (uint16_t)(1u + (r >> 40) % 900u));
        }
        if (i % 500u == 499u) {
            center_x += (r >> 50) % 2u == 0u ? 1 : -1;
            world_map_set_center(&f->world.map, center_x, 0);
            upload_world_stream(t, f);
        }
        if (i % 7u == 0u) {
            render_upload_resend_drain(&f->render.upload, &f->world.edit);
            (void)world_edit_snapshot_publish(&f->world.snapshot);
        }
    }
    atomic_store_explicit(&sim->done, 1u, memory_order_release);
}

TEST(a_real_sim_thread_edits_while_frames_run_and_the_card_ends_equal) {
    upload_fixture_t *const f = upload_test_aligned(t, sizeof(*f));
    const render_upload_budget_t budget = {.bytes = 1ull << 20, .sections = 64u};
    REQUIRE(upload_fixture_begin(t, f, 2u, UPLOAD_TEST_POOL_BYTES, budget));
    upload_world_stream(t, f);
    upload_sim_t *const sim = upload_test_aligned(t, sizeof(*sim));
    sim->borrowed_fixture = f;
    sim->borrowed_test = t;
    thread_t *const thread = upload_test_aligned(t, sizeof(*thread));
    REQUIRE(thread_start(thread, &f->topology, THREAD_ROLE_SIM, 0u, upload_sim_run, sim));
    /* No CHECK until the join: the sim thread has t until then. */
    uint64_t frames = 0u;
    gpu_status_t status = GPU_STATUS_OK;
    while (atomic_load_explicit(&sim->done, memory_order_acquire) == 0u) {
        const gpu_status_t frame_status = render_upload_frame(&f->render.upload);
        status = frame_status != GPU_STATUS_OK ? frame_status : status;
        frames++;
    }
    thread_join(thread);
    CHECK_EQ_U64(status, GPU_STATUS_OK);
    REQUIRE(upload_pump(t, f));
    CHECK(frames > 0u);
    CHECK(f->render.upload.stats.views > 1u);
    CHECK_EQ_U64(upload_compare_world(t, f), 0u);
    upload_compare_table(t, f);
    printf("with a sim thread: %" PRIu64 " frames, %" PRIu64 " views, %" PRIu64 " sections sent, %" PRIu64
           " stale, %" PRIu64 " published and overwritten\n",
           frames, f->render.upload.stats.views, f->render.upload.stats.sections_sent,
           f->render.upload.stats.sections_stale, f->world.snapshot.overwritten_count);
    upload_fixture_end(t, f);
}

/* Prints what the stage file records, and checks nothing about time, since a timing on a shared machine would be flaky. */
TEST(upload_cost_on_this_machine) {
    upload_buffers_t *const b = upload_test_aligned(t, sizeof(*b));
    REQUIRE(upload_buffers_begin(t, b, 69u, 16ull * WORLD_BUFFERS_PAGE_BYTES));
    world_buffers_t *const buffers = &b->buffers;
    uint8_t *const pattern = upload_test_aligned(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    for (uint32_t slot = 0; slot < 4761u; slot++) {
        world_buffers_column_set(buffers, slot, (int32_t)(slot % 69u), (int32_t)(slot / 69u), 1u, 0);
    }
    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    const uint32_t sizes[] = {640u, 3656u, WORLD_BUFFERS_SECTION_BYTES_MAX};
    for (uint32_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        const int64_t started = time_now_ns();
        for (uint32_t i = 0; i < 480u; i++) {
            REQUIRE(world_buffers_section_write(buffers, i / 48u, i % 48u, pattern, sizes[s]) == GPU_STATUS_OK);
        }
        printf("a section of %" PRIu32 " bytes: %" PRId64 " ns a copy\n", sizes[s], (time_now_ns() - started) / 480);
    }
    int64_t started = time_now_ns();
    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    printf("flush of 10 slots of 4761: %" PRId64 " ns\n", time_now_ns() - started);
    for (uint32_t slot = 0; slot < 75u; slot++) {
        world_buffers_column_set(buffers, slot, (int32_t)(slot % 69u), (int32_t)(slot / 69u), 1u, 1);
    }
    CHECK_EQ_U64(buffers->dirty_count, 75u);
    started = time_now_ns();
    REQUIRE(world_buffers_flush(buffers) == GPU_STATUS_OK);
    printf("flush of 75 slots of 4761, as one copy: %" PRId64 " ns\n", time_now_ns() - started);
    CHECK_EQ_U64(buffers->flush_whole_count, 2u); /* the first above, and this */
    upload_buffers_end(t, b);
}

TEST_MAIN()
