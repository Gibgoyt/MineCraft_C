/* bench_mobs: one sim tick of every mob, its real mind and all, in the game's order, against the plan's target. See README.md. */
#include "entity/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "mob/index.h"
#include "mob/spawn/index.h"
#include "player/movement/index.h"
#include "player/survival/index.h"
#include "tables/blocks/index.h"
#include "tables/mobs/index.h"
#include "terrain/rng/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The plan's number: a sim tick at 100 mobs, of the 50 ms there are. */
#define BENCH_TARGET_MS 5.0f64
/* A player's size, as run/src/game/ tells the minds. */
#define BENCH_PLAYER_EYE 1.62f64
#define BENCH_PLAYER_HEIGHT 1.8f64
#define BENCH_PLAYER_HALF_WIDTH 0.3f64
/* It walks round and round, a step a tick being what a sprint comes to, so that no mind's answer stays good for long. */
#define BENCH_PLAYER_RADIANS_PER_TICK 0.02f64
/* Whatever has fallen through a hole a blast left is no more use here, and one in its place keeps the count. */
#define BENCH_FALLEN_Z (-8.0f64)

enum : uint32_t {
    BENCH_WARM_TICKS = 200u,
    BENCH_TICKS = 2000u,
    BENCH_TARGET_MOBS = 100u,
    BENCH_RADIUS = 3u, /* chunks: a square from -48 up to 64 */
    BENCH_FEATURES = 1500u,
    BENCH_ROWS = 5u,
    BENCH_ARENA_BYTES = (uint32_t)MOB_SPAWN_BYTES,
};
enum : int32_t {
    BENCH_GROUND_MIN = -44,
    BENCH_GROUND_SIDE = 104,
    BENCH_AIR_TOP = 8,
};

typedef enum bench_place_t : uint8_t {
    BENCH_PLACE_ALL_OVER = 0, /* around the whole floor, and the player far from most */
    BENCH_PLACE_PIT = 1,      /* all in a walled pit, with the player */
    BENCH_PLACE_AROUND = 2,   /* all within sight and reach of the player, in the open */
} bench_place_t;

typedef struct bench_row_t {
    const char *name;    /* 0 */
    uint32_t mobs;       /* 8 */
    bench_place_t place; /* 12 */
    uint8_t gated;       /* 13: its number has to meet the target */
    uint8_t _pad0[2];    /* 14 */
} bench_row_t;
static_assert(sizeof(bench_row_t) == 16);

static const bench_row_t BENCH_ROW_LIST[BENCH_ROWS] = {
    {"scattered, the target", BENCH_TARGET_MOBS, BENCH_PLACE_ALL_OVER, 1u, {0u, 0u}},
    {"packed in a pit", BENCH_TARGET_MOBS, BENCH_PLACE_PIT, 1u, {0u, 0u}},
    {"all around the player", BENCH_TARGET_MOBS, BENCH_PLACE_AROUND, 1u, {0u, 0u}},
    {"scattered", 400u, BENCH_PLACE_ALL_OVER, 0u, {0u, 0u}},
    {"scattered, a full store", ENTITY_STORE_CAPACITY, BENCH_PLACE_ALL_OVER, 0u, {0u, 0u}},
};

/* What came of a row, to say that the minds really did what is being timed. */
typedef struct bench_deeds_t {
    uint64_t searches; /* 0 */
    uint64_t blows;    /* 8 */
    uint64_t shots;    /* 16 */
    uint64_t bursts;   /* 24 */
    uint64_t hits;     /* 32 */
    uint64_t chasing;  /* 40: zombies chasing, summed over the timed ticks */
} bench_deeds_t;
static_assert(sizeof(bench_deeds_t) == 48);

typedef struct bench_t {
    arena_t arena_row;                    /* 0: what mob_spawn_t takes */
    mob_spawn_t spawn;                    /* 64 */
    heap_region_t owned_memory;           /* 104 */
    world_t *borrowed_world;              /* 120 */
    physics_t *borrowed_physics;          /* 128 */
    entity_t *borrowed_entity;            /* 136 */
    mob_t *borrowed_mob;                  /* 144 */
    job_system_t *borrowed_job;           /* 152: real workers, as in the game */
    player_movement_t *borrowed_movement; /* 160 */
    player_survival_t *borrowed_survival; /* 168 */
    int64_t *borrowed_samples;            /* 176: BENCH_TICKS */
    uint8_t _pad0[8];                     /* 184 */
} bench_t;
static_assert(sizeof(bench_t) == 192);

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

/* All the columns come in, as nothing else here would ask for them. */
static bool bench_load_world(bench_t *bench) {
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
    return true;
}

/* A broken ground: stone with walls, steps, slabs and pools to walk into, and a pit of its own at the middle. The same every time, as
 * whatever the last row's blasts took is put back. */
static bool bench_shape_world(bench_t *bench) {
    const int32_t a = BENCH_GROUND_MIN, b = BENCH_GROUND_MIN + BENCH_GROUND_SIDE - 1;
    if (!bench_fill(bench, a, a, -2, b, b, -1, BLOCK_ID_STONE) ||
        !bench_fill(bench, a, a, 0, b, b, BENCH_AIR_TOP, BLOCK_ID_AIR)) {
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

/* One more of the three kinds in turn, somewhere its row puts them, to fall to whatever is under it. */
static void bench_spawn_one(bench_t *bench, const bench_row_t *row, terrain_rng_t *random, uint32_t turn) {
    entity_store_t *const store = &bench->borrowed_entity->store;
    float64_t from_x = (float64_t)BENCH_GROUND_MIN + 2.0f64, from_y = from_x,
              spread = (float64_t)BENCH_GROUND_SIDE - 4.0f64;
    if (row->place == BENCH_PLACE_PIT) {
        from_x = -2.0f64;
        from_y = -2.0f64;
        spread = 6.0f64;
    } else if (row->place == BENCH_PLACE_AROUND) {
        from_x = bench->borrowed_movement->position[0] - 12.0f64;
        from_y = bench->borrowed_movement->position[1] - 12.0f64;
        spread = 24.0f64;
    }
    const float64_t position[COLLISION_AXES] = {from_x + terrain_rng_next_f64(random) * spread,
                                                from_y + terrain_rng_next_f64(random) * spread, 6.0f64};
    const mob_id_t kinds[3] = {MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER};
    (void)mob_spawn_body(&bench->spawn, store, kinds[turn % 3u], position, 0.0f32);
}

/* Where the player is this tick, alive and whole. Outside what is timed: a real body has its own tick, which is not a mob's. */
static void bench_player_set(bench_t *bench, const bench_row_t *row, uint32_t tick) {
    const float64_t angle = (float64_t)tick * BENCH_PLAYER_RADIANS_PER_TICK;
    const bool in_pit = row->place == BENCH_PLACE_PIT;
    const float64_t middle_x = in_pit ? 1.5f64 : 20.5f64, middle_y = in_pit ? 1.5f64 : 20.5f64;
    const float64_t radius = in_pit ? 2.0f64 : 10.0f64;
    const float64_t x = middle_x + radius * cos(angle), y = middle_y + radius * sin(angle);
    player_movement_start(bench->borrowed_movement, x, y, 0.0f64);
    bench->borrowed_movement->eye_height = BENCH_PLAYER_EYE;
    player_survival_start(bench->borrowed_survival, x, y, 0.0f64);
}

/* What has fallen out of the world goes, and the count stands at the row's: bursts and despawns are in what is timed, and their refill
 * is not. */
static void bench_keep_count(bench_t *bench, const bench_row_t *row, terrain_rng_t *random, uint32_t *turn) {
    entity_store_t *const store = &bench->borrowed_entity->store;
    for (uint32_t i = store->count; i-- > 0u;) {
        if (store->borrowed_position[2][i] < BENCH_FALLEN_Z) {
            (void)entity_store_remove(store, store->borrowed_handle[i]);
        }
    }
    while (store->count < row->mobs) {
        const uint32_t before = store->count;
        bench_spawn_one(bench, row, random, *turn);
        (*turn)++;
        if (store->count == before) {
            return;
        }
    }
}

static void bench_arrow_hurt(void *borrowed_bench, float32_t amount, const float64_t direction[COLLISION_AXES]) {
    (void)direction;
    bench_t *const bench = borrowed_bench;
    (void)player_survival_hurt(bench->borrowed_survival, amount, PLAYER_SURVIVAL_CAUSE_ATTACK);
}

/* One tick of everything a mob is, in the order game_sim_tick and game_items_tick run it. */
static void bench_tick(bench_t *bench) {
    entity_t *const entity = bench->borrowed_entity;
    mob_t *const mob = bench->borrowed_mob;
    world_t *const world = bench->borrowed_world;
    player_movement_t *const movement = bench->borrowed_movement;
    const float64_t *const at = movement->position;
    mob_tick(mob);
    const mob_zombie_view_t zombie_view = {
        .position = {at[0], at[1], at[2]},
        .eye = BENCH_PLAYER_EYE,
        .half_width = BENCH_PLAYER_HALF_WIDTH,
        .height = BENCH_PLAYER_HEIGHT,
        .alive = 1u,
        ._pad0 = {0},
    };
    const float32_t blow = mob_minds_tick(mob, entity, &zombie_view);
    if (blow > 0.0f32) {
        (void)player_survival_hurt(bench->borrowed_survival, blow, PLAYER_SURVIVAL_CAUSE_ATTACK);
    }
    const mob_spawn_view_t spawn_view = {.position = {at[0], at[1], at[2]}, .sun_height = -1.0f64};
    mob_spawn_tick(&bench->spawn, &entity->store, entity->borrowed_physics, &world->map, &spawn_view);
    const mob_skeleton_view_t skeleton_view = {
        .position = {at[0], at[1], at[2]},
        .eye_height = BENCH_PLAYER_EYE,
        .height = BENCH_PLAYER_HEIGHT,
        .alive = 1u,
        ._pad0 = {0},
    };
    mob_skeleton_tick(&mob->skeleton, &mob->path, entity, &world->map, &skeleton_view);
    const mob_creeper_scene_t scene = {
        .borrowed_entity = entity,
        .borrowed_path = &mob->path,
        .borrowed_edit = &world->edit,
        .borrowed_movement = movement,
        .borrowed_survival = bench->borrowed_survival,
    };
    mob_creeper_tick(&mob->creeper, &scene);
    entity_tick(entity, &world->map);
    mob_combat_tick(&mob->combat, &entity->store);
    collision_box_t box;
    player_movement_box(at, BENCH_PLAYER_HEIGHT, &box);
    projectile_tick(&entity->projectiles, &entity->store, &entity->grid, &world->map, &box, bench_arrow_hurt, bench);
}

static uint64_t bench_chasing(const bench_t *bench) {
    const entity_store_t *const store = &bench->borrowed_entity->store;
    const mob_zombie_t *const zombie = &bench->borrowed_mob->zombie;
    uint64_t chasing = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        if (store->borrowed_kind[i] == (uint16_t)MOB_ID_ZOMBIE) {
            const mob_zombie_body_t *const body =
                &zombie->borrowed_bodies[entity_handle_slot(store->borrowed_handle[i])];
            chasing += body->owner == store->borrowed_handle[i] && body->chasing != 0u ? 1u : 0u;
        }
    }
    return chasing;
}

static int32_t bench_compare(const void *left, const void *right) {
    const int64_t a = *(const int64_t *)left, b = *(const int64_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* Returns whether the row met its target. */
static bool bench_row(bench_t *bench, const bench_row_t *row) {
    entity_t *const entity = bench->borrowed_entity;
    mob_t *const mob = bench->borrowed_mob;
    if (!bench_shape_world(bench)) {
        return false;
    }
    entity_open(entity, 1u);
    mob_open(mob, bench->borrowed_world, 1u);
    arena_reset(&bench->arena_row);
    mob_spawn_start(&bench->spawn, &bench->arena_row, 2u);
    terrain_rng_t random = terrain_rng_start(0x424D4F42u);
    uint32_t turn = 0u;
    bench_player_set(bench, row, 0u);
    bench_keep_count(bench, row, &random, &turn);
    for (uint32_t tick = 0u; tick < BENCH_WARM_TICKS; tick++) {
        bench_player_set(bench, row, tick);
        bench_tick(bench);
        bench_keep_count(bench, row, &random, &turn);
    }
    bench_deeds_t deeds = {0};
    heap_counters_t before, after;
    heap_counters_read(&before);
    int64_t total = 0;
    uint32_t fewest = entity->store.count;
    for (uint32_t tick = 0u; tick < BENCH_TICKS; tick++) {
        bench_player_set(bench, row, BENCH_WARM_TICKS + tick);
        fewest = entity->store.count < fewest ? entity->store.count : fewest;
        const int64_t start = time_now_ns();
        bench_tick(bench);
        const int64_t spent = time_now_ns() - start;
        bench->borrowed_samples[tick] = spent;
        total += spent;
        deeds.chasing += bench_chasing(bench);
        bench_keep_count(bench, row, &random, &turn);
    }
    heap_counters_read(&after);
    mob_path_stats_t paths;
    mob_path_stats_read(&mob->path, &paths);
    deeds.searches = paths.searches;
    deeds.blows = mob->zombie.struck_count;
    deeds.shots = mob->skeleton.shot_count;
    deeds.bursts = mob->creeper.burst_count;
    deeds.hits = entity->projectiles.target_hits + entity->projectiles.body_hits;
    const uint64_t pushes = entity->grid.pushes;
    mob_close(mob);
    entity_close(entity);

    qsort(bench->borrowed_samples, BENCH_TICKS, sizeof(int64_t), bench_compare);
    const float64_t mean = time_ns_to_ms(total) / (float64_t)BENCH_TICKS;
    const float64_t median = time_ns_to_ms(bench->borrowed_samples[BENCH_TICKS / 2u]);
    const float64_t p99 = time_ns_to_ms(bench->borrowed_samples[BENCH_TICKS - BENCH_TICKS / 100u]);
    const float64_t worst = time_ns_to_ms(bench->borrowed_samples[BENCH_TICKS - 1u]);
    const bool quiet = after.reserve_calls == before.reserve_calls && after.release_calls == before.release_calls;
    /* A row with the minds idle would be a fast number that says nothing, so each gated row has to have seen them work. */
    const bool worked = deeds.searches != 0u && deeds.shots != 0u && deeds.chasing != 0u;
    /* The gate is on the 99th percentile: the worst single tick on a shared machine says more about the machine. */
    const bool met = row->gated == 0u || (p99 <= BENCH_TARGET_MS && quiet && worked && fewest == row->mobs);
    printf("%-26s %5" PRIu32 " mobs  mean %7.3f ms  median %7.3f ms  p99 %7.3f ms  worst %7.3f ms  %s\n", row->name,
           fewest, (libc_double_t)mean, (libc_double_t)median, (libc_double_t)p99, (libc_double_t)worst,
           row->gated == 0u ? "(no target)" : (met ? "met" : "MISSED"));
    printf("%-26s %" PRIu64 " searches, %.1f zombies chasing, %" PRIu64 " blows, %" PRIu64 " arrows of which %" PRIu64
           " hit, %" PRIu64 " bursts, %" PRIu64 " pairs pushed, heap calls %" PRIu64 "\n",
           "", deeds.searches, (libc_double_t)deeds.chasing / (libc_double_t)BENCH_TICKS, deeds.blows, deeds.shots,
           deeds.hits, deeds.bursts, pushes,
           (after.reserve_calls - before.reserve_calls) + (after.release_calls - before.release_calls));
    return met;
}

static uint64_t bench_page(uint64_t bytes) {
    return (bytes + 4095ull) & ~4095ull;
}

int32_t main(int32_t argc, char **argv) {
    (void)argc;
    (void)argv;
    bench_t bench;
    memset(&bench, 0, sizeof(bench));
    const uint64_t sizes[] = {
        bench_page(sizeof(world_t)),
        bench_page(sizeof(physics_t)),
        bench_page(sizeof(entity_t)),
        bench_page(sizeof(mob_t)),
        bench_page(sizeof(job_system_t)),
        bench_page(sizeof(player_movement_t)),
        bench_page(sizeof(player_survival_t)),
        bench_page(sizeof(thread_topology_t)),
        bench_page((uint64_t)BENCH_TICKS * sizeof(int64_t)),
        bench_page(BENCH_ARENA_BYTES),
    };
    uint64_t total = 0u;
    for (uint32_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        total += sizes[i];
    }
    if (!heap_reserve(total, &bench.owned_memory)) {
        return 2;
    }
    uint8_t *at = bench.owned_memory.owned_base;
    bench.borrowed_world = (world_t *)at;
    at += sizes[0];
    bench.borrowed_physics = (physics_t *)at;
    at += sizes[1];
    bench.borrowed_entity = (entity_t *)at;
    at += sizes[2];
    bench.borrowed_mob = (mob_t *)at;
    at += sizes[3];
    bench.borrowed_job = (job_system_t *)at;
    at += sizes[4];
    bench.borrowed_movement = (player_movement_t *)at;
    at += sizes[5];
    bench.borrowed_survival = (player_survival_t *)at;
    at += sizes[6];
    thread_topology_t *const topology = (thread_topology_t *)at;
    at += sizes[7];
    bench.borrowed_samples = (int64_t *)at;
    at += sizes[8];
    arena_init(&bench.arena_row, "bench_mobs", at, sizes[9]);

    int32_t status = 2;
    if (!world_init(bench.borrowed_world, (world_limits_t){256ull << 20, 64u})) {
        arena_deinit(&bench.arena_row);
        heap_release(&bench.owned_memory);
        return status;
    }
    if (world_open(bench.borrowed_world, BENCH_RADIUS, 0, 0) && physics_init(bench.borrowed_physics)) {
        if (entity_init(bench.borrowed_entity, bench.borrowed_physics)) {
            if (thread_topology_read(topology) && job_init(bench.borrowed_job, topology)) {
                if (mob_init(bench.borrowed_mob, bench.borrowed_job)) {
                    if (bench_load_world(&bench)) {
                        status = 0;
                        for (uint32_t r = 0u; r < BENCH_ROWS; r++) {
                            status = bench_row(&bench, &BENCH_ROW_LIST[r]) ? status : 1;
                        }
                        printf("target: a tick at %" PRIu32 " mobs at most %.1f ms, on the 99th percentile: %s\n",
                               (uint32_t)BENCH_TARGET_MOBS, (libc_double_t)BENCH_TARGET_MS,
                               status == 0 ? "met" : "MISSED");
                    }
                    mob_deinit(bench.borrowed_mob);
                }
                job_deinit(bench.borrowed_job);
            }
            entity_deinit(bench.borrowed_entity);
        }
        physics_deinit(bench.borrowed_physics);
    }
    world_close(bench.borrowed_world);
    world_deinit(bench.borrowed_world);
    arena_deinit(&bench.arena_row);
    heap_release(&bench.owned_memory);
    return status;
}
