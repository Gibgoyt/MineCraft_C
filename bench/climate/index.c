/* bench_climate: what a climate tile costs, and the gate's pictures. See README.md. */
#include "climate/index.h"
#include "climate/layer/index.h"
#include "display/headless/png/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    BENCH_PLACES = 9u,
    BENCH_ROUNDS = 3u,
    BENCH_WORKERS = 16u,
    BENCH_RENDER_DISTANCE = 32u, /* the plan's spawn precompute: render distance 32 plus 2 km */
    BENCH_PICTURE_SIDE = 1024u,  /* pixels, at 16 blocks a pixel: 16 km a side */
    BENCH_PICTURE_STEP = 16u,
    BENCH_DAYS = 4u,           /* phase 04's gate days: 15, 105, 196, 288 */
    BENCH_SYNTHETIC_STEP = 8u, /* --synthetic: 8 blocks a pixel, 8 km a side round each continent */
};
static_assert(MC_THREADS_WORKERS >= BENCH_WORKERS);
static const float64_t BENCH_TARGET_MS = 100.0f64; /* one cold tile on one worker: the phase 03 file */
static const uint64_t BENCH_SEED = 0x1234567890ABCDEFull;

/* Tiles in different places: on the coast, inland, at sea, far out, either side of 0. */
static const int32_t BENCH_TILES[BENCH_PLACES][2] = {
    {0, 0}, {5, -3}, {-12, 7}, {30, 30}, {-40, 2}, {100, -100}, {3, 60}, {-77, -15}, {11, 11},
};

/* The terrain's height estimate, timed: what share of a tile is the ground. */
typedef struct bench_ground_t {
    const terrain_t *borrowed_terrain; /* 0 */
    _Atomic uint64_t ns;               /* 8 */
    _Atomic uint64_t columns;          /* 16 */
} bench_ground_t;
static_assert(sizeof(bench_ground_t) == 24);

static void bench_ground_fn(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                            uint32_t count_x, uint32_t count_y, float32_t *heights) {
    bench_ground_t *const ground = (bench_ground_t *)(uintptr_t)borrowed_context;
    const int64_t started = time_now_ns();
    terrain_height_estimate_area(ground->borrowed_terrain, origin_x, origin_y, step, count_x, count_y, heights);
    atomic_fetch_add_explicit(&ground->ns, (uint64_t)(time_now_ns() - started), memory_order_relaxed);
    atomic_fetch_add_explicit(&ground->columns, (uint64_t)count_x * count_y, memory_order_relaxed);
}

static void bench_ground_reset(bench_ground_t *ground) {
    atomic_store_explicit(&ground->ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&ground->columns, 0u, memory_order_relaxed);
}

/* One tile on whatever system is given, in a fresh climate (both tables empty), or warm: after its west neighbour. */
static float64_t bench_tile_ms(climate_t *climate, job_system_t *system, bench_ground_t *ground, int32_t tile_x,
                               int32_t tile_y, bool warm, float64_t *ground_share, uint64_t *columns) {
    climate_close(climate, system);
    if (!climate_open_ground(climate, BENCH_SEED, bench_ground_fn, ground)) {
        return -1.0f64;
    }
    if (warm) {
        climate_prepare_tile(climate, system, tile_x - 1, tile_y);
    }
    bench_ground_reset(ground);
    const int64_t started = time_now_ns();
    climate_prepare_tile(climate, system, tile_x, tile_y);
    const int64_t elapsed = time_now_ns() - started;
    *ground_share = (float64_t)atomic_load(&ground->ns) / (float64_t)elapsed;
    *columns = atomic_load(&ground->columns);
    return time_ns_to_ms(elapsed);
}

static int32_t bench_timings(climate_t *climate, const thread_topology_t *topology, bench_ground_t *ground) {
    job_system_t system;
    if (!job_init_count(&system, topology, 1u)) {
        return 2;
    }
    int32_t status = 0;
    printf(
        "cold tile, 1 worker: a fresh climate each time, so every one of the window's 289 pages is built; best of %u\n",
        BENCH_ROUNDS);
    printf("%-12s %10s %10s %14s %12s\n", "tile", "ms", "estimates", "estimates/s", "ground share");
    float64_t worst = 0.0f64;
    for (uint32_t p = 0u; p < BENCH_PLACES; p++) {
        float64_t best = 1.0e30f64, share = 0.0f64;
        uint64_t columns = 0u;
        for (uint32_t r = 0u; r < BENCH_ROUNDS; r++) {
            float64_t round_share = 0.0f64;
            uint64_t round_columns = 0u;
            const float64_t ms = bench_tile_ms(climate, &system, ground, BENCH_TILES[p][0], BENCH_TILES[p][1], false,
                                               &round_share, &round_columns);
            if (ms < 0.0f64) {
                job_deinit(&system);
                return 2;
            }
            if (ms < best) {
                best = ms;
                share = round_share;
                columns = round_columns;
            }
        }
        worst = best > worst ? best : worst;
        printf("(%4d, %4d) %10.2f %10" PRIu64 " %14.0f %11.1f%%%s\n", BENCH_TILES[p][0], BENCH_TILES[p][1],
               (libc_double_t)best, columns, (libc_double_t)((float64_t)columns / (best / 1000.0f64)),
               (libc_double_t)(share * 100.0f64), best > BENCH_TARGET_MS ? "  OVER" : "");
        status = best > BENCH_TARGET_MS ? 1 : status;
    }
    printf("slowest cold tile %.2f ms, target %.0f ms: %s\n", (libc_double_t)worst, (libc_double_t)BENCH_TARGET_MS,
           status == 0 ? "met" : "MISSED");

    printf("warm tile, 1 worker: the tile east of one just built, so 17 of its 289 pages are new, as when walking\n");
    float64_t warm_worst = 0.0f64;
    for (uint32_t p = 0u; p < BENCH_PLACES; p++) {
        float64_t share = 0.0f64;
        uint64_t columns = 0u;
        float64_t best = 1.0e30f64;
        for (uint32_t r = 0u; r < BENCH_ROUNDS; r++) {
            const float64_t ms =
                bench_tile_ms(climate, &system, ground, BENCH_TILES[p][0], BENCH_TILES[p][1], true, &share, &columns);
            best = ms < best ? ms : best;
        }
        warm_worst = best > warm_worst ? best : warm_worst;
    }
    printf("slowest warm tile %.2f ms\n", (libc_double_t)warm_worst);
    climate_close(climate, &system);
    job_deinit(&system);

    if (!job_init_count(&system, topology, BENCH_WORKERS)) {
        return 2;
    }
    if (!climate_open_ground(climate, BENCH_SEED, bench_ground_fn, ground)) {
        job_deinit(&system);
        return 2;
    }
    const int32_t reach = (int32_t)(BENCH_RENDER_DISTANCE * 16u + 2048u);
    bench_ground_reset(ground);
    const int64_t started = time_now_ns();
    climate_prepare_area(climate, &system, -reach, -reach, reach, reach, nullptr, nullptr);
    const int64_t elapsed = time_now_ns() - started;
    const int32_t tiles_side = ((reach + CLIMATE_READ_REACH_BLOCKS) >> CLIMATE_TILE_SHIFT) -
                               ((-reach - CLIMATE_READ_REACH_BLOCKS) >> CLIMATE_TILE_SHIFT) + 1;
    printf("spawn precompute, %u workers, render distance %u plus 2 km: %d tiles and %d mask pages in %.1f ms "
           "(%" PRIu64 " estimates)\n",
           BENCH_WORKERS, BENCH_RENDER_DISTANCE, tiles_side * tiles_side,
           (tiles_side + CLIMATE_WINDOW_TILES - 1) * (tiles_side + CLIMATE_WINDOW_TILES - 1),
           (libc_double_t)time_ns_to_ms(elapsed), atomic_load(&ground->columns));
    climate_close(climate, &system);
    job_deinit(&system);
    return status;
}

/* ---- Pictures ------------------------------------------------------------------------------------------------------ */

static bool bench_png_write(const char *directory, const char *name, const uint8_t *pixels, uint8_t *stream,
                            uint64_t capacity) {
    char path[1024];
    const int32_t written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written <= 0 || (uint64_t)written >= sizeof(path)) {
        fprintf(stderr, "bench_climate: the path %s/%s is too long\n", directory, name);
        return false;
    }
    uint64_t bytes = 0u;
    if (!display_png_encode(pixels, BENCH_PICTURE_SIDE * 4u, BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE, stream, capacity,
                            &bytes)) {
        fprintf(stderr, "bench_climate: cannot encode %s\n", path);
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_climate: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    const bool wrote = fwrite(stream, 1u, bytes, file) == bytes;
    if (fclose(file) != 0 || !wrote) {
        fprintf(stderr, "bench_climate: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    printf("wrote %s\n", path);
    return true;
}

/* Phase 04's layers: currents once, pressure and wind on each of the gate's days, with the latitude lines over currents. */
static bool bench_air_pictures(const climate_t *climate, const char *directory, int32_t west_x, int32_t north_y,
                               int32_t step, const char *prefix, uint8_t *pixels, uint8_t *stream, uint64_t png_bytes) {
    static const uint32_t DAYS[BENCH_DAYS] = {15u, 105u, 196u, 288u};
    char name[128];
    climate_layer_draw(climate, CLIMATE_LAYER_CURRENTS, west_x, north_y, step, BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE,
                       0u, pixels, BENCH_PICTURE_SIDE * 4u);
    climate_layer_draw(climate, CLIMATE_LAYER_LATITUDE, west_x, north_y, step, BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE,
                       0u, pixels, BENCH_PICTURE_SIDE * 4u);
    (void)snprintf(name, sizeof(name), "%scurrents.png", prefix);
    if (!bench_png_write(directory, name, pixels, stream, png_bytes)) {
        return false;
    }
    for (uint32_t d = 0u; d < BENCH_DAYS; d++) {
        static const climate_layer_t LAYERS[2] = {CLIMATE_LAYER_PRESSURE, CLIMATE_LAYER_WIND};
        static const char *const NAMES[2] = {"pressure", "wind"};
        for (uint32_t l = 0u; l < 2u; l++) {
            climate_layer_draw(climate, LAYERS[l], west_x, north_y, step, BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE,
                               DAYS[d], pixels, BENCH_PICTURE_SIDE * 4u);
            (void)snprintf(name, sizeof(name), "%s%s_day%03u.png", prefix, NAMES[l], DAYS[d]);
            if (!bench_png_write(directory, name, pixels, stream, png_bytes)) {
                return false;
            }
        }
    }
    return true;
}

static int32_t bench_pictures(climate_t *climate, const thread_topology_t *topology, const char *directory,
                              uint64_t seed) {
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_climate: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    terrain_t terrain;
    if (!terrain_init(&terrain, seed)) {
        return 2;
    }
    job_system_t system;
    if (!job_init_count(&system, topology, BENCH_WORKERS)) {
        terrain_deinit(&terrain);
        return 2;
    }
    const uint64_t pixel_bytes = (uint64_t)BENCH_PICTURE_SIDE * BENCH_PICTURE_SIDE * 4u;
    const uint64_t png_bytes = display_png_bytes_needed(BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE);
    heap_region_t memory = {0};
    if (!climate_open(climate, &terrain) || !heap_reserve(pixel_bytes + png_bytes, &memory)) {
        climate_close(climate, &system);
        job_deinit(&system);
        terrain_deinit(&terrain);
        return 2;
    }
    uint8_t *const pixels = memory.owned_base;
    uint8_t *const stream = memory.owned_base + pixel_bytes;
    const int32_t half = (int32_t)(BENCH_PICTURE_SIDE * BENCH_PICTURE_STEP / 2u);
    const int64_t started = time_now_ns();
    climate_prepare_area(climate, &system, -half, -half, half - 1, half - 1, nullptr, nullptr);
    printf("seed %" PRIu64 ": %d by %d blocks round 0 worked out in %.1f ms\n", seed, 2 * half, 2 * half,
           (libc_double_t)time_ns_to_ms(time_now_ns() - started));
    /* What the cells say about this land, for the phase file. */
    uint64_t counts[4] = {0u, 0u, 0u, 0u};
    for (int32_t cy = -half / CLIMATE_CELL_BLOCKS; cy < half / CLIMATE_CELL_BLOCKS; cy++) {
        for (int32_t cx = -half / CLIMATE_CELL_BLOCKS; cx < half / CLIMATE_CELL_BLOCKS; cx++) {
            climate_cell_t cell;
            if (climate_cell_get(climate, cx * CLIMATE_CELL_BLOCKS, cy * CLIMATE_CELL_BLOCKS, &cell)) {
                const uint32_t which = (cell.flags & CLIMATE_CELL_SEA) != 0u          ? 1u
                                       : (cell.flags & CLIMATE_CELL_LAKE) != 0u       ? 2u
                                       : (cell.flags & CLIMATE_CELL_LAKE_SMALL) != 0u ? 3u
                                                                                      : 0u;
                counts[which]++;
            }
        }
    }
    const float64_t all = (float64_t)(counts[0] + counts[1] + counts[2] + counts[3]);
    printf(
        "cells: %.1f%% land, %.1f%% sea, %.1f%% large lake, %.1f%% small lake\n",
        (libc_double_t)(100.0f64 * (float64_t)counts[0] / all), (libc_double_t)(100.0f64 * (float64_t)counts[1] / all),
        (libc_double_t)(100.0f64 * (float64_t)counts[2] / all), (libc_double_t)(100.0f64 * (float64_t)counts[3] / all));
    static const char *const NAMES[3] = {"sea_distance.png", "continentality.png", "water.png"};
    bool ok = true;
    for (uint32_t layer = 0u; layer < 3u && ok; layer++) {
        climate_layer_draw(climate, (climate_layer_t)layer, -half, half, (int32_t)BENCH_PICTURE_STEP,
                           BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE, 0u, pixels, BENCH_PICTURE_SIDE * 4u);
        ok = bench_png_write(directory, NAMES[layer], pixels, stream, png_bytes);
    }
    if (ok) {
        /* Latitude over water: the pixels of the last picture are still there. */
        climate_layer_draw(climate, CLIMATE_LAYER_LATITUDE, -half, half, (int32_t)BENCH_PICTURE_STEP,
                           BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE, 0u, pixels, BENCH_PICTURE_SIDE * 4u);
        ok = bench_png_write(directory, "latitude.png", pixels, stream, png_bytes);
    }
    if (ok) {
        ok = bench_air_pictures(climate, directory, -half, half, (int32_t)BENCH_PICTURE_STEP, "", pixels, stream,
                                png_bytes);
    }
    heap_release(&memory);
    climate_close(climate, &system);
    job_deinit(&system);
    terrain_deinit(&terrain);
    return ok ? 0 : 2;
}

/* ---- Synthetic continents ------------------------------------------------------------------------------------------ */

/* The phase 04 file's synthetic continent: land (30) in a 4 km square, sea (-60) round it, both ends of each range given as
 * the first block in and the first block out. */
typedef struct bench_square_t {
    int32_t x_min; /* 0 */
    int32_t y_min; /* 4 */
    int32_t x_max; /* 8: the first block east of it */
    int32_t y_max; /* 12: the first block north of it */
} bench_square_t;
static_assert(sizeof(bench_square_t) == 16);

static void bench_square_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                uint32_t count_x, uint32_t count_y, float32_t *heights) {
    const bench_square_t *const square = borrowed_context;
    for (uint32_t j = 0u; j < count_y; j++) {
        const int64_t y = (int64_t)origin_y + (int64_t)step * j;
        for (uint32_t i = 0u; i < count_x; i++) {
            const int64_t x = (int64_t)origin_x + (int64_t)step * i;
            const bool land = x >= square->x_min && x < square->x_max && y >= square->y_min && y < square->y_max;
            heights[(uint64_t)j * count_x + i] = land ? 30.0f32 : -60.0f32;
        }
    }
}

/* The continents at 30 S and 50 N, which show the master plan's table with no real coast's noise. */
static int32_t bench_synthetic(climate_t *climate, const thread_topology_t *topology, const char *directory) {
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_climate: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    static const bench_square_t SQUARES[2] = {{-2048, -14766, 2048, -10766}, {-2048, 19277, 2048, 23277}};
    static const char *const PREFIXES[2] = {"south30_", "north50_"};
    job_system_t system;
    if (!job_init_count(&system, topology, BENCH_WORKERS)) {
        return 2;
    }
    const uint64_t pixel_bytes = (uint64_t)BENCH_PICTURE_SIDE * BENCH_PICTURE_SIDE * 4u;
    const uint64_t png_bytes = display_png_bytes_needed(BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE);
    heap_region_t memory = {0};
    if (!heap_reserve(pixel_bytes + png_bytes, &memory)) {
        job_deinit(&system);
        return 2;
    }
    bool ok = true;
    const int32_t half = (int32_t)(BENCH_PICTURE_SIDE * BENCH_SYNTHETIC_STEP / 2u);
    for (uint32_t s = 0u; s < 2u && ok; s++) {
        const bench_square_t *const square = &SQUARES[s];
        const int32_t centre_x = (square->x_min + square->x_max) / 2, centre_y = (square->y_min + square->y_max) / 2;
        if (!climate_open_ground(climate, BENCH_SEED, bench_square_ground, square)) {
            ok = false;
            break;
        }
        climate_prepare_area(climate, &system, centre_x - half, centre_y - half, centre_x + half - 1,
                             centre_y + half - 1, nullptr, nullptr);
        ok = bench_air_pictures(climate, directory, centre_x - half, centre_y + half, (int32_t)BENCH_SYNTHETIC_STEP,
                                PREFIXES[s], memory.owned_base, memory.owned_base + pixel_bytes, png_bytes);
        climate_close(climate, &system);
    }
    heap_release(&memory);
    job_deinit(&system);
    return ok ? 0 : 2;
}

int32_t main(int32_t argc, char **argv) {
    thread_topology_t topology;
    climate_t climate;
    if (!thread_topology_read(&topology) || !climate_init(&climate)) {
        return 2;
    }
    int32_t status = 0;
    if (argc == 4 && strcmp(argv[1], "--pictures") == 0) {
        char *end = nullptr;
        errno = 0;
        const uint64_t seed = strtoull(argv[3], &end, 10);
        if (errno != 0 || end == argv[3] || *end != '\0') {
            fprintf(stderr, "bench_climate: '%s' is not a seed\n", argv[3]);
            status = 2;
        } else {
            status = bench_pictures(&climate, &topology, argv[2], seed);
        }
    } else if (argc == 3 && strcmp(argv[1], "--synthetic") == 0) {
        status = bench_synthetic(&climate, &topology, argv[2]);
    } else if (argc == 1) {
        terrain_t terrain;
        if (!terrain_init(&terrain, BENCH_SEED)) {
            climate_deinit(&climate);
            return 2;
        }
        bench_ground_t ground = {.borrowed_terrain = &terrain};
        bench_ground_reset(&ground);
        status = bench_timings(&climate, &topology, &ground);
        terrain_deinit(&terrain);
    } else {
        fprintf(stderr, "usage: bench_climate [--pictures <directory> <seed> | --synthetic <directory>]\n");
        status = 2;
    }
    climate_deinit(&climate);
    return status;
}
