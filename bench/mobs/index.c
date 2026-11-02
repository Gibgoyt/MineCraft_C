/* bench_mobs: one sim tick of everything an entity is, against the plan's target. See README.md. */
#include "entity/index.h"
#include "heap/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* The plan's number: a sim tick at 100 mobs, of the 50 ms there are. */
#define BENCH_TARGET_MS 5.0f64
#define BENCH_TURN 6.28318530717959f64

enum : uint32_t {
    BENCH_WARM_TICKS = 200u,
    BENCH_TICKS = 2000u,
    BENCH_TARGET_MOBS = 100u,
    BENCH_RADIUS = 3u, /* chunks: a square from -48 up to 64 */
    BENCH_FEATURES = 1500u,
    BENCH_NEIGHBOURS_MAX = 64u,
    BENCH_TURN_EVERY_TICKS = 40u,
    BENCH_ROWS = 4u,
};
enum : int32_t {
    BENCH_GROUND_MIN = -44,
    BENCH_GROUND_SIDE = 104,
};
/* What a mob does in a tick besides move, until Stage 15.2 and on bring its real mind: asks who is around. */
#define BENCH_LOOK_BLOCKS 8.0f64

typedef struct bench_row_t {
    const char *name; /* 0 */
    uint32_t mobs;    /* 8 */
    uint8_t packed;   /* 12: all in one pit rather than scattered */
    uint8_t gated;    /* 13: its number has to meet the target */
    uint8_t _pad0[2]; /* 14 */
} bench_row_t;
static_assert(sizeof(bench_row_t) == 16);

static const bench_row_t BENCH_ROW_LIST[BENCH_ROWS] = {
    {"scattered, the target", BENCH_TARGET_MOBS, 0u, 1u, {0u, 0u}},
    {"packed in a pit", BENCH_TARGET_MOBS, 1u, 1u, {0u, 0u}},
    {"scattered", 400u, 0u, 0u, {0u, 0u}},
    {"scattered, a full store", ENTITY_STORE_CAPACITY, 0u, 0u, {0u, 0u}},
};

typedef struct bench_t {
    heap_region_t owned_memory;  /* 0 */
    world_t *borrowed_world;     /* 16 */
    physics_t *borrowed_physics; /* 24 */
    entity_t *borrowed_entity;   /* 32 */
    int64_t *borrowed_samples;   /* 40: BENCH_TICKS */
} bench_t;
static_assert(sizeof(bench_t) == 48);

static bool bench_fill(bench_t *bench, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                       uint16_t block) {
    world_t *const world = bench->borrowed_world;
    for (int32_t x = x0; x <= x1; x++) {
        for (int32_t y = y0; y <= y1; y++) {
            column_t *const column = world_map_column_at(&world->map, column_chunk_of(x), column_chunk_of(y));
            if (column == nullptr) {
                continue;
            }
            for (int32_t z = z0; z <= z1; z++) {
                if (column_block_set(column, &world->section_pool, column_local_of(x), column_local_of(y), z, block) !=
                    COLUMN_STATUS_OK) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* A broken ground: stone with walls, steps, slabs and pools to walk into, and a pit of its own at the middle. */
static bool bench_build_world(bench_t *bench) {
    world_t *const world = bench->borrowed_world;
    world_map_leaving_t leaving;
    while (world_map_stream_leaving(&world->map, &leaving)) {
        world_map_unload(&world->map, leaving.slot);
    }
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        if (world_map_stream_commit(&world->map, &request) == nullptr) {
            return false;
        }
    }
    const int32_t a = BENCH_GROUND_MIN, b = BENCH_GROUND_MIN + BENCH_GROUND_SIDE - 1;
    if (!bench_fill(bench, a, a, -2, b, b, -1, BLOCK_ID_STONE)) {
        return false;
    }
    terrain_rng_t random = terrain_rng_start(0x4D4F4253u);
    for (uint32_t i = 0u; i < BENCH_FEATURES; i++) {
        const int32_t x = a + (int32_t)terrain_rng_next_below(&random, (uint32_t)BENCH_GROUND_SIDE);
        const int32_t y = a + (int32_t)terrain_rng_next_below(&random, (uint32_t)BENCH_GROUND_SIDE);
        const uint32_t pick = terrain_rng_next_below(&random, 8u);
        const int32_t long_way = (int32_t)terrain_rng_next_below(&random, 6u);
        const uint16_t block = pick == 0u ? BLOCK_ID_STONE_SLAB : (pick == 1u ? BLOCK_ID_WATER : BLOCK_ID_STONE);
        const int32_t top = pick < 2u ? 0 : (int32_t)terrain_rng_next_below(&random, 3u);
        if (!bench_fill(bench, x, y, 0, x + (pick % 2u == 0u ? long_way : 0), y + (pick % 2u == 0u ? 0 : long_way), top,
                        block)) {
            return false;
        }
    }
    /* Walls around the middle, and nothing inside them. */
    return bench_fill(bench, -3, -3, 0, 5, 5, 3, BLOCK_ID_STONE) && bench_fill(bench, -2, -2, 0, 4, 4, 3, BLOCK_ID_AIR);
}

static void bench_spawn(bench_t *bench, const bench_row_t *row, terrain_rng_t *random) {
    entity_store_t *const store = &bench->borrowed_entity->store;
    for (uint32_t i = 0u; i < row->mobs; i++) {
        const float64_t spread = row->packed != 0u ? 6.0f64 : (float64_t)(BENCH_GROUND_SIDE - 4);
        const float64_t from = row->packed != 0u ? -2.0f64 : (float64_t)BENCH_GROUND_MIN + 2.0f64;
        const entity_store_spawn_t spawn = {
            .position = {from + terrain_rng_next_f64(random) * spread, from + terrain_rng_next_f64(random) * spread,
                         6.0f64},
            .half_width = 0.3f32,
            .height = 1.95f32,
            .yaw = 0.0f32,
            .health = 20.0f32,
            .kind = (uint16_t)(i % 3u),
        };
        (void)entity_store_spawn(store, &spawn);
    }
}

/* What stands for a mind: a new way to walk now and then, a jump at a wall, and a look round at who is near. */
static uint64_t bench_think(bench_t *bench, uint32_t tick, terrain_rng_t *random) {
    entity_store_t *const store = &bench->borrowed_entity->store;
    entity_grid_t *const grid = &bench->borrowed_entity->grid;
    entity_handle_t near[BENCH_NEIGHBOURS_MAX];
    uint64_t seen = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        if ((tick + i) % BENCH_TURN_EVERY_TICKS == 0u) {
            const float64_t angle = terrain_rng_next_f64(random) * BENCH_TURN;
            entity_store_wish_set(store, i, cos(angle) * 0.115f64, sin(angle) * 0.115f64);
            store->borrowed_yaw[i] = (float32_t)angle;
        }
        if ((store->borrowed_flags[i] & (ENTITY_STORE_FLAG_HIT_WALL | ENTITY_STORE_FLAG_IN_WATER)) != 0u) {
            entity_store_jump(store, i);
        }
        collision_box_t look;
        entity_store_box(store, i, &look);
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            look.min[a] -= BENCH_LOOK_BLOCKS;
            look.max[a] += BENCH_LOOK_BLOCKS;
        }
        seen += entity_grid_query(grid, store, &look, near, BENCH_NEIGHBOURS_MAX);
    }
    return seen;
}

static int32_t bench_compare(const void *left, const void *right) {
    const int64_t a = *(const int64_t *)left, b = *(const int64_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* Returns whether the row met its target. */
static bool bench_row(bench_t *bench, const bench_row_t *row) {
    entity_t *const entity = bench->borrowed_entity;
    world_map_t *const map = &bench->borrowed_world->map;
    entity_open(entity, 1u);
    terrain_rng_t random = terrain_rng_start(0x424D4F42u);
    bench_spawn(bench, row, &random);
    for (uint32_t tick = 0u; tick < BENCH_WARM_TICKS; tick++) {
        (void)bench_think(bench, tick, &random);
        entity_tick(entity, map);
    }
    uint64_t seen = 0u;
    heap_counters_t before, after;
    heap_counters_read(&before);
    int64_t total = 0;
    for (uint32_t tick = 0u; tick < BENCH_TICKS; tick++) {
        const int64_t start = time_now_ns();
        seen += bench_think(bench, tick, &random);
        entity_tick(entity, map);
        const int64_t spent = time_now_ns() - start;
        bench->borrowed_samples[tick] = spent;
        total += spent;
    }
    heap_counters_read(&after);
    const uint64_t pushes = entity->grid.pushes;
    const uint32_t alive = entity->store.count;
    entity_close(entity);

    qsort(bench->borrowed_samples, BENCH_TICKS, sizeof(int64_t), bench_compare);
    const float64_t mean = time_ns_to_ms(total) / (float64_t)BENCH_TICKS;
    const float64_t median = time_ns_to_ms(bench->borrowed_samples[BENCH_TICKS / 2u]);
    const float64_t p99 = time_ns_to_ms(bench->borrowed_samples[BENCH_TICKS - BENCH_TICKS / 100u]);
    const float64_t worst = time_ns_to_ms(bench->borrowed_samples[BENCH_TICKS - 1u]);
    const bool quiet = after.reserve_calls == before.reserve_calls && after.release_calls == before.release_calls;
    /* The gate is on the 99th percentile: the worst single tick on a shared machine says more about the machine. */
    const bool met = row->gated == 0u || (p99 <= BENCH_TARGET_MS && quiet && alive == row->mobs);
    printf("%-26s %5" PRIu32 " mobs  mean %7.3f ms  median %7.3f ms  p99 %7.3f ms  worst %7.3f ms  %s\n", row->name,
           alive, (libc_double_t)mean, (libc_double_t)median, (libc_double_t)p99, (libc_double_t)worst,
           row->gated == 0u ? "(no target)" : (met ? "met" : "MISSED"));
    printf("%-26s seen by each %.1f a tick, %" PRIu64 " pairs pushed, heap calls %" PRIu64 "\n", "",
           (libc_double_t)seen / (libc_double_t)BENCH_TICKS / (libc_double_t)(alive != 0u ? alive : 1u), pushes,
           (after.reserve_calls - before.reserve_calls) + (after.release_calls - before.release_calls));
    return met;
}

int32_t main(int32_t argc, char **argv) {
    (void)argc;
    (void)argv;
    bench_t bench = {0};
    const uint64_t world_bytes = (sizeof(world_t) + 4095ull) & ~4095ull;
    const uint64_t physics_bytes = (sizeof(physics_t) + 4095ull) & ~4095ull;
    const uint64_t entity_bytes = (sizeof(entity_t) + 4095ull) & ~4095ull;
    const uint64_t sample_bytes = (uint64_t)BENCH_TICKS * sizeof(int64_t);
    if (!heap_reserve(world_bytes + physics_bytes + entity_bytes + sample_bytes, &bench.owned_memory)) {
        return 2;
    }
    uint8_t *const memory = bench.owned_memory.owned_base;
    bench.borrowed_world = (world_t *)memory;
    bench.borrowed_physics = (physics_t *)(memory + world_bytes);
    bench.borrowed_entity = (entity_t *)(memory + world_bytes + physics_bytes);
    bench.borrowed_samples = (int64_t *)(memory + world_bytes + physics_bytes + entity_bytes);

    int32_t status = 2;
    if (!world_init(bench.borrowed_world, (world_limits_t){256ull << 20, 64u})) {
        heap_release(&bench.owned_memory);
        return status;
    }
    if (world_open(bench.borrowed_world, BENCH_RADIUS, 0, 0) && physics_init(bench.borrowed_physics)) {
        if (entity_init(bench.borrowed_entity, bench.borrowed_physics)) {
            if (bench_build_world(&bench)) {
                status = 0;
                for (uint32_t r = 0u; r < BENCH_ROWS; r++) {
                    status = bench_row(&bench, &BENCH_ROW_LIST[r]) ? status : 1;
                }
                printf("target: a tick at %" PRIu32 " mobs at most %.1f ms, on the 99th percentile: %s\n",
                       (uint32_t)BENCH_TARGET_MOBS, (libc_double_t)BENCH_TARGET_MS, status == 0 ? "met" : "MISSED");
            }
            entity_deinit(bench.borrowed_entity);
        }
        physics_deinit(bench.borrowed_physics);
    }
    world_close(bench.borrowed_world);
    world_deinit(bench.borrowed_world);
    heap_release(&bench.owned_memory);
    return status;
}
