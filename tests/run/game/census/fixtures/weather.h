/* A census rig with no game and no card, for plan 004 phase 07's tests here and in tests/run/save/climate: a climate on a
 * ground of the test's, a job system of some workers, the census of run/src/game/census driven step by step, and a sloped
 * coast for ground, so water runs to the sea. */
#ifndef MC_TESTS_RUN_GAME_CENSUS_FIXTURES_WEATHER_H
#define MC_TESTS_RUN_GAME_CENSUS_FIXTURES_WEATHER_H

#include "harness/index.h"

#include "climate/index.h"
#include "climate/systems/index.h"
#include "climate/weather/index.h"
#include "game/census/index.h"
#include "job/index.h"
#include "save/climate/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    GW_PATH_BYTES = 512u,
};
static const uint64_t GW_SEED = 0x5EED0007ull;
/* 15 degrees north: the trades, hurricane latitudes, and thunderstorms in summer. */
static const int32_t GW_Y = 6383;

/* Land west of x = 0 rising 3 blocks in 100 toward the west from 20, the sea east of it: rivers run east to the coast. */
[[maybe_unused]] static void gw_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                       uint32_t count_x, uint32_t count_y, float32_t *heights, uint8_t *dry) {
    (void)dry;
    (void)borrowed_context;
    (void)origin_y;
    for (uint32_t j = 0u; j < count_y; j++) {
        for (uint32_t i = 0u; i < count_x; i++) {
            const int64_t x = (int64_t)origin_x + (int64_t)step * (int64_t)i;
            heights[(uint64_t)j * count_x + i] = x <= 0 ? 20.0f32 + 0.03f32 * (float32_t)(-x) : -60.0f32;
        }
    }
}

typedef struct gw_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
    arena_t *arena;              /* 24: the census's World arena */
    game_census_t *census;       /* 32 */
    char *directory;             /* 40: a world directory of its own under /tmp, or nullptr */
} gw_rig_t;
static_assert(sizeof(gw_rig_t) == 48);

[[maybe_unused]] static void *gw_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

[[maybe_unused]] static bool gw_start(test_context_t *t, gw_rig_t *rig, uint32_t workers) {
    memset(rig, 0, sizeof(*rig));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = gw_aligned(t, sizeof(job_system_t));
    rig->climate = gw_aligned(t, sizeof(climate_t));
    rig->census = gw_aligned(t, sizeof(game_census_t));
    rig->arena = gw_aligned(t, sizeof(arena_t));
    arena_init(rig->arena, "test_census", gw_aligned(t, GAME_CENSUS_ARENA_BYTES), GAME_CENSUS_ARENA_BYTES);
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, workers) ||
        !climate_init(rig->climate)) {
        return false;
    }
    return climate_open_ground(rig->climate, GW_SEED, gw_ground, nullptr);
}

/* A directory of the test's own under /tmp, made once. */
[[maybe_unused]] static char *gw_directory(test_context_t *t) {
    char *const directory = test_alloc(t, GW_PATH_BYTES);
    snprintf(directory, GW_PATH_BYTES, "/tmp/mc_census_weather_XXXXXX");
    return mkdtemp(directory) != nullptr ? directory : nullptr;
}

[[maybe_unused]] static void gw_remove(test_context_t *t, const char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_census_weather_", 23u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
}

/* The census over the climate, from the file in directory (nullptr for none) at or before last_week. */
[[maybe_unused]] static void gw_open(gw_rig_t *rig, const char *directory, uint64_t last_week) {
    arena_reset(rig->arena);
    rig->directory = (char *)directory;
    game_census_open(rig->census, rig->arena, rig->climate, rig->job, directory, GW_SEED, last_week);
}

[[maybe_unused]] static void gw_close(gw_rig_t *rig) {
    game_census_close(rig->census);
}

[[maybe_unused]] static void gw_stop(gw_rig_t *rig) {
    game_census_close(rig->census);
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

/* Generated land: every cell of the tiles from (tile_x0, tile_y0) to (tile_x1, tile_y1), both ends included. */
[[maybe_unused]] static void gw_mark_tiles(gw_rig_t *rig, int32_t tile_x0, int32_t tile_y0, int32_t tile_x1,
                                           int32_t tile_y1) {
    climate_week_store_t *const store = climate_week_store(rig->climate);
    for (int32_t y = tile_y0 * CLIMATE_TILE_BLOCKS; y < (tile_y1 + 1) * CLIMATE_TILE_BLOCKS; y += CLIMATE_CELL_BLOCKS) {
        for (int32_t x = tile_x0 * CLIMATE_TILE_BLOCKS; x < (tile_x1 + 1) * CLIMATE_TILE_BLOCKS;
             x += CLIMATE_CELL_BLOCKS) {
            climate_week_store_mark(store, x, y);
        }
    }
}

/* Steps the census until nothing is owed, sleeping a little between steps, as the sim polls. */
[[maybe_unused]] static void gw_settle(gw_rig_t *rig, float64_t x, float64_t y) {
    while (!game_census_idle(rig->census)) {
        game_census_step(rig->census, x, y, 2048u);
        time_sleep_ns(200000);
    }
}

/* Weeks first to last owed at once, the oldest up to fast_until fast, then worked out. */
[[maybe_unused]] static void gw_weeks(gw_rig_t *rig, uint64_t first, uint64_t last, uint64_t fast_until) {
    for (uint64_t w = first; w <= last; w++) {
        game_census_week(rig->census, w, w <= fast_until);
    }
    gw_settle(rig, -1500.0f64, (float64_t)GW_Y);
}

/* The FNV-1a digest of every kept tile in (tile_y, tile_x) order: the bytes the file holds after its header. */
[[maybe_unused]] static uint64_t gw_digest(test_context_t *t, const climate_t *climate) {
    const climate_week_store_t *const store = climate_week_store(climate);
    uint32_t *const sorted = test_alloc(t, (store->tile_count + 1u) * sizeof(uint32_t));
    uint32_t *const scratch = test_alloc(t, (store->tile_count + 1u) * sizeof(uint32_t));
    climate_week_store_sorted(store, sorted, scratch);
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t i = 0u; i < store->tile_count; i++) {
        hash = save_climate_hash(hash, &store->borrowed_tiles[sorted[i]], sizeof(climate_week_tile_t));
    }
    return hash;
}

/* A whole file, into the test arena. */
[[maybe_unused]] static uint8_t *gw_file(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint64_t bytes = 0u;
    if (file_path_size(path, &bytes) != FILE_OK) {
        *out_bytes = 0u;
        return nullptr;
    }
    uint8_t *const data = test_alloc(t, bytes + 1u);
    if (file_read_whole(path, data, bytes + 1u, out_bytes) != FILE_OK) {
        *out_bytes = 0u;
        return nullptr;
    }
    return data;
}

#endif
