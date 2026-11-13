/* bench_climate: what a climate tile costs, and the gate's pictures. See README.md. */
#include "climate/air/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/layer/index.h"
#include "climate/rain/index.h"
#include "climate/season/index.h"
#include "climate/systems/field/index.h"
#include "climate/systems/index.h"
#include "climate/systems/name/index.h"
#include "display/headless/png/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"

#include <errno.h>
#include <math.h>
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
    BENCH_DAYS_MAX = 32u,      /* --days: at most this many */
    BENCH_MONTHS = 12u,        /* phase 05's default days: the middle of each month */
};
static const uint32_t BENCH_MONTH_DAYS[BENCH_MONTHS] = {15u,  46u,  76u,  107u, 137u, 168u,
                                                        198u, 229u, 259u, 290u, 320u, 351u};
/* The direct build's arena: the window, the cell build's scratch and the air's. */
static const uint64_t BENCH_DIRECT_ARENA_BYTES = 8ull << 20;
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
                            uint32_t count_x, uint32_t count_y, float32_t *heights, uint8_t *dry) {
    bench_ground_t *const ground = (bench_ground_t *)(uintptr_t)borrowed_context;
    const int64_t started = time_now_ns();
    terrain_height_estimate_area(ground->borrowed_terrain, origin_x, origin_y, step, count_x, count_y, heights, dry);
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

static void bench_terrain_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                 uint32_t count_x, uint32_t count_y, float32_t *heights, uint8_t *dry) {
    terrain_height_estimate_area(borrowed_context, origin_x, origin_y, step, count_x, count_y, heights, dry);
}

/* ---- Phase 05: what each part of a tile costs ------------------------------------------------------------------------- */

typedef struct bench_parts_t {
    float64_t ns[7]; /* 0: window, cells, air, heat, rain, season, the snow balance alone */
} bench_parts_t;
static_assert(sizeof(bench_parts_t) == 56);

/* One tile built straight from its window on this thread, each part timed: what the tile job does, without the cache. */
static void bench_parts_tile(const climate_normals_t *normals, arena_t *arena, const terrain_t *terrain, int32_t tile_x,
                             int32_t tile_y, float32_t *heights, climate_cell_t *cells, bench_parts_t *out) {
    arena_reset(arena);
    const int32_t half = CLIMATE_WINDOW_TILES / 2;
    int64_t at = time_now_ns();
    terrain_height_estimate_area(terrain, (tile_x - half) * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2,
                                 (tile_y - half) * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2, CLIMATE_CELL_BLOCKS,
                                 (uint32_t)CLIMATE_WINDOW_CELLS, (uint32_t)CLIMATE_WINDOW_CELLS, heights, nullptr);
    int64_t next = time_now_ns();
    out->ns[0] = (float64_t)(next - at);
    at = next;
    climate_window_t window;
    climate_cell_build_tile(arena, bench_terrain_ground, terrain, tile_x, tile_y, heights, cells, &window);
    next = time_now_ns();
    out->ns[1] = (float64_t)(next - at);
    at = next;
    climate_air_build_tile(normals, arena, &window, cells, nullptr);
    next = time_now_ns();
    out->ns[2] = (float64_t)(next - at);
    at = next;
    climate_heat_build_tile(normals, cells);
    next = time_now_ns();
    out->ns[3] = (float64_t)(next - at);
    at = next;
    climate_rain_build_tile(normals, nullptr, &window, cells);
    next = time_now_ns();
    out->ns[4] = (float64_t)(next - at);
    at = next;
    climate_season_build_tile(normals, nullptr, &window, cells);
    next = time_now_ns();
    out->ns[5] = (float64_t)(next - at);
    at = next;
    float32_t sink = 0.0f32;
    for (uint32_t i = 0u; i < (uint32_t)CLIMATE_TILE_CELL_COUNT; i++) {
        if ((cells[i].flags & CLIMATE_CELL_SEA) == 0u) {
            sink += climate_season_snow_balance(&cells[i], (cells[i].flags & CLIMATE_CELL_LAKE) == 0u);
        }
    }
    out->ns[6] = (float64_t)(time_now_ns() - at);
    if (sink != sink) {
        printf("(a NaN snow balance)\n"); /* keeps the loop from being thrown away */
    }
}

static int32_t bench_parts(const climate_t *climate, const terrain_t *terrain) {
    heap_region_t memory = {0};
    const uint64_t heights_bytes = (uint64_t)CLIMATE_WINDOW_CELL_COUNT * sizeof(float32_t);
    const uint64_t cells_bytes = (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t);
    if (!heap_reserve(BENCH_DIRECT_ARENA_BYTES + heights_bytes + cells_bytes, &memory)) {
        return 2;
    }
    arena_t arena;
    arena_init(&arena, "bench_parts", memory.owned_base, BENCH_DIRECT_ARENA_BYTES);
    float32_t *const heights = (float32_t *)(void *)(memory.owned_base + BENCH_DIRECT_ARENA_BYTES);
    climate_cell_t *const cells =
        (climate_cell_t *)(void *)(memory.owned_base + BENCH_DIRECT_ARENA_BYTES + heights_bytes);
    static const char *const NAMES[7] = {"window (phase 03's pages)",
                                         "cells (phase 03)",
                                         "air (phase 04)",
                                         "heat (phase 05)",
                                         "rain (phase 05)",
                                         "season (phase 05)",
                                         "  of which the snow balance"};
    bench_parts_t sum = {0};
    for (uint32_t p = 0u; p < BENCH_PLACES; p++) {
        bench_parts_t best = {0};
        for (uint32_t r = 0u; r < BENCH_ROUNDS; r++) {
            bench_parts_t round = {0};
            bench_parts_tile(climate_normals(climate), &arena, terrain, BENCH_TILES[p][0], BENCH_TILES[p][1], heights,
                             cells, &round);
            for (uint32_t k = 0u; k < 7u; k++) {
                best.ns[k] = r == 0u || round.ns[k] < best.ns[k] ? round.ns[k] : best.ns[k];
            }
        }
        for (uint32_t k = 0u; k < 7u; k++) {
            sum.ns[k] += best.ns[k];
        }
    }
    const float64_t total = sum.ns[0] + sum.ns[1] + sum.ns[2] + sum.ns[3] + sum.ns[4] + sum.ns[5];
    printf("a tile's parts, built straight from its window on one thread, mean over %u places, best of %u each "
           "(the rain walk reads no mask page past the window: every step is inside it)\n",
           BENCH_PLACES, BENCH_ROUNDS);
    for (uint32_t k = 0u; k < 7u; k++) {
        printf("  %-28s %8.3f ms %6.1f%%\n", NAMES[k], (libc_double_t)(sum.ns[k] / (float64_t)BENCH_PLACES / 1.0e6f64),
               (libc_double_t)(100.0f64 * sum.ns[k] / total));
    }
    printf("  %-28s %8.3f ms\n", "all", (libc_double_t)(total / (float64_t)BENCH_PLACES / 1.0e6f64));
    arena_deinit(&arena);
    heap_release(&memory);
    return 0;
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

/* The days phase 05's day layers are drawn for. */
typedef struct bench_days_t {
    uint32_t days[BENCH_DAYS_MAX]; /* 0 */
    uint32_t count;                /* 128 */
} bench_days_t;
static_assert(sizeof(bench_days_t) == 132);

/* Phase 05's layers: the year's once, the day ones for each listed day, named <layer>_dDDD.png. */
static bool bench_normals_pictures(const climate_t *climate, const char *directory, int32_t west_x, int32_t north_y,
                                   int32_t step, const char *prefix, const bench_days_t *days, uint8_t *pixels,
                                   uint8_t *stream, uint64_t png_bytes) {
    static const climate_layer_t YEAR[6] = {CLIMATE_LAYER_TEMPERATURE_MEAN,    CLIMATE_LAYER_TEMPERATURE_WARMEST,
                                            CLIMATE_LAYER_TEMPERATURE_COLDEST, CLIMATE_LAYER_RAIN_YEAR,
                                            CLIMATE_LAYER_CLIMATE_CLASS,       CLIMATE_LAYER_SNOW_BALANCE};
    static const char *const YEAR_NAMES[6] = {"temperature_mean", "temperature_warmest", "temperature_coldest",
                                              "rain_year",        "climate_class",       "snow_balance"};
    static const climate_layer_t BY_DAY[4] = {CLIMATE_LAYER_TEMPERATURE_DAY, CLIMATE_LAYER_RAIN_DAY,
                                              CLIMATE_LAYER_HURRICANE_SEASON, CLIMATE_LAYER_MONSOON};
    static const char *const DAY_NAMES[4] = {"temperature_day", "rain_day", "hurricane_season", "monsoon"};
    char name[128];
    for (uint32_t l = 0u; l < 6u; l++) {
        climate_layer_draw(climate, YEAR[l], west_x, north_y, step, BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE, 0u, pixels,
                           BENCH_PICTURE_SIDE * 4u);
        (void)snprintf(name, sizeof(name), "%s%s.png", prefix, YEAR_NAMES[l]);
        if (!bench_png_write(directory, name, pixels, stream, png_bytes)) {
            return false;
        }
    }
    /* The monsoon flag on the first day listed: the gate's monsoon.png. */
    climate_layer_draw(climate, CLIMATE_LAYER_MONSOON, west_x, north_y, step, BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE,
                       days->days[0], pixels, BENCH_PICTURE_SIDE * 4u);
    (void)snprintf(name, sizeof(name), "%smonsoon.png", prefix);
    if (!bench_png_write(directory, name, pixels, stream, png_bytes)) {
        return false;
    }
    for (uint32_t d = 0u; d < days->count; d++) {
        for (uint32_t l = 0u; l < 4u; l++) {
            climate_layer_draw(climate, BY_DAY[l], west_x, north_y, step, BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE,
                               days->days[d], pixels, BENCH_PICTURE_SIDE * 4u);
            (void)snprintf(name, sizeof(name), "%s%s_d%03u.png", prefix, DAY_NAMES[l], days->days[d]);
            if (!bench_png_write(directory, name, pixels, stream, png_bytes)) {
                return false;
            }
        }
    }
    return true;
}

static int32_t bench_pictures(climate_t *climate, const thread_topology_t *topology, const char *directory,
                              uint64_t seed, const bench_days_t *days) {
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
    if (ok) {
        ok = bench_normals_pictures(climate, directory, -half, half, (int32_t)BENCH_PICTURE_STEP, "", days, pixels,
                                    stream, png_bytes);
    }
    heap_release(&memory);
    climate_close(climate, &system);
    job_deinit(&system);
    terrain_deinit(&terrain);
    return ok ? 0 : 2;
}

/* ---- Plan 004 phase 09: every Atlas layer through the Atlas's own drawing --------------------------------------------- */

/* What --layer, --zoom and --flat ask for. */
typedef struct bench_atlas_t {
    const char *layer; /* 0: a registry key, or "all"; nullptr: not asked */
    int32_t zoom;      /* 8: blocks a pixel 2^(zoom / 2) */
    uint8_t flat;      /* 12: hill shading off and every pixel sampled */
    uint8_t _pad0[3];  /* 13 */
} bench_atlas_t;
static_assert(sizeof(bench_atlas_t) == 16);

/* Each layer asked for, 1024 by 1024 round (0, 0), through ui_atlas_draw_picture over the terrain's estimate (everything
 * generated), to atlas_<key>.png. With --flat at a whole number of blocks a pixel, each phase 03 to 05 layer is checked
 * against climate_layer_draw's bytes at the same place. */
static int32_t bench_atlas_pictures(climate_t *climate, const thread_topology_t *topology, const char *directory,
                                    uint64_t seed, const bench_atlas_t *atlas) {
    const uint32_t only = strcmp(atlas->layer, "all") == 0 ? UI_ATLAS_LAYER_COUNT : ui_atlas_layer_find(atlas->layer);
    if (strcmp(atlas->layer, "all") != 0 && only == UI_ATLAS_LAYER_COUNT) {
        fprintf(stderr, "bench_climate: no Atlas layer is called '%s'\n", atlas->layer);
        return 2;
    }
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_climate: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    const uint32_t side = BENCH_PICTURE_SIDE;
    const uint64_t pixels = (uint64_t)side * side;
    const uint64_t png_bytes = display_png_bytes_needed(side, side);
    const uint64_t band_bytes = ui_atlas_band_scratch_bytes(side, (side + UI_ATLAS_BANDS - 1u) / UI_ATLAS_BANDS);
    /* The pictures, their grey, field and overlay, a climate picture to check against, a PNG, a ui and its texture, the
     * terrain, the bands' scratch and the systems' arena, each where the next 64-byte line starts. */
    const uint64_t sizes[11] = {pixels * 4u,       pixels,
                                pixels * 4u,       UI_ATLAS_OVERLAY_SCRATCH_BYTES,
                                pixels * 4u,       png_bytes,
                                sizeof(ui_t),      sizeof(texture_t),
                                sizeof(terrain_t), band_bytes + (1ull << 20),
                                16ull << 20};
    uint64_t total = 0u;
    for (uint32_t k = 0u; k < 11u; k++) {
        total += (sizes[k] + 63u) & ~63ull;
    }
    heap_region_t memory = {0};
    if (!heap_reserve(total, &memory)) {
        return 2;
    }
    uint8_t *parts[11];
    uint8_t *at = memory.owned_base;
    for (uint32_t k = 0u; k < 11u; k++) {
        parts[k] = at;
        at += (sizes[k] + 63u) & ~63ull;
    }
    ui_t *const ui = (ui_t *)(void *)parts[6];
    texture_t *const texture = (texture_t *)(void *)parts[7];
    terrain_t *const terrain = (terrain_t *)(void *)parts[8];
    job_system_t system;
    int32_t status = 2;
    if (!job_init_count(&system, topology, BENCH_WORKERS)) {
        heap_release(&memory);
        return 2;
    }
    if (texture_init(texture) && ui_init(ui, texture) && terrain_init(terrain, seed) &&
        climate_open(climate, terrain)) {
        arena_t scratch, systems;
        arena_init(&scratch, "bench_atlas_bands", parts[9], sizes[9]);
        arena_init(&systems, "bench_atlas_systems", parts[10], sizes[10]);
        const ui_atlas_target_t target = {(uint32_t *)(void *)parts[0], parts[1], (float32_t *)(void *)parts[2],
                                          parts[3]};
        const float64_t step = ui_atlas_blocks_per_pixel(atlas->zoom);
        const int32_t half = (int32_t)ceil(step * (float64_t)side / 2.0f64);
        const int64_t started = time_now_ns();
        climate_prepare_area(climate, &system, -half - 64, -half - 64, half + 63, half + 63, nullptr, nullptr);
        printf("seed %" PRIu64
               ": %d by %d blocks round 0 worked out in %.1f ms; Atlas zoom %d, %.3f blocks a pixel%s\n",
               seed, 2 * half, 2 * half, (libc_double_t)time_ns_to_ms(time_now_ns() - started), atlas->zoom,
               (libc_double_t)step, atlas->flat != 0u ? ", flat" : "");
        ui_atlas_estimate_t estimate = {.borrowed_terrain = terrain, .generated_east_x = INT64_MAX};
        job_group_t group;
        job_group_init(&group);
        status = 0;
        uint32_t same = 0u, checked = 0u;
        for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT && status == 0; layer++) {
            if (only != UI_ATLAS_LAYER_COUNT && layer != only) {
                continue;
            }
            const ui_atlas_layer_t *const row = &ui_atlas_layers[layer];
            ui_atlas_draw_input_t input = {
                .view = {.center = {0.0f64, 0.0f64},
                         .zoom = atlas->zoom,
                         .width = side,
                         .height = side,
                         .day = row->time == UI_ATLAS_TIME_NORMALS ? 196u : 0u,
                         .layer = (uint8_t)layer},
                .ground = {.borrowed_context = &estimate, .read = ui_atlas_estimate_read},
                .borrowed_climate = climate,
                .borrowed_week = climate_week_store(climate),
                .borrowed_font = &ui->font,
                .t = 196.4f64,
                .census_week = 28u,
                .sample = atlas->flat != 0u ? 1u : UI_ATLAS_SAMPLE_PIXELS,
                .flat = atlas->flat,
            };
            arena_reset(&systems);
            climate_systems_day_t day;
            if (row->time == UI_ATLAS_TIME_NOW &&
                ui_atlas_systems_build(climate, &systems, &input.view, input.t, &day)) {
                input.borrowed_systems = &day;
            }
            const int64_t drawn_at = time_now_ns();
            ui_atlas_draw_picture(&input, &target, &system, &group, UI_ATLAS_BANDS, &scratch);
            const float64_t ms = time_ns_to_ms(time_now_ns() - drawn_at);
            char name[64];
            snprintf(name, sizeof(name), "atlas_%s.png", row->key);
            printf("%-20s %.1f ms\n", row->key, (libc_double_t)ms);
            if (!bench_png_write(directory, name, parts[0], parts[5], png_bytes)) {
                status = 2;
            }
            /* The same code: a phase 03 to 05 field, flat at a whole number of blocks a pixel, is climate_layer_draw's. */
            const ui_atlas_geometry_t g = ui_atlas_geometry(&input.view);
            const bool whole = step >= 1.0f64 && step == floor(step);
            if (atlas->flat != 0u && whole && row->climate_layer != UI_ATLAS_NO_CLIMATE_LAYER &&
                row->climate_layer != CLIMATE_LAYER_LATITUDE) {
                climate_layer_draw(climate, (climate_layer_t)row->climate_layer, (int32_t)((float64_t)g.column0 * step),
                                   (int32_t)(-(float64_t)g.row0 * step), (int32_t)step, side, side, input.view.day,
                                   parts[4], side * 4u);
                uint64_t differ = 0u;
                for (uint64_t k = 0u; k < pixels; k++) {
                    uint32_t c = 0u;
                    memcpy(&c, parts[4] + 4u * k, 4u);
                    differ += c == CLIMATE_LAYER_GREY || (target.borrowed_pixels[k] & 0xFFFFFFu) == c ? 0u : 1u;
                }
                printf("  %s: %s climate_layer_draw's bytes (%" PRIu64 " pixels differ)\n", row->key,
                       differ == 0u ? "the same as" : "NOT the same as", differ);
                same += differ == 0u ? 1u : 0u;
                checked++;
            }
        }
        if (checked > 0u) {
            printf("%u of %u climate fields the same bytes as climate_layer_draw\n", same, checked);
            status = same == checked ? status : 1;
        }
        job_group_deinit(&group);
        climate_close(climate, &system);
    }
    terrain_deinit(terrain);
    ui_deinit(ui);
    texture_deinit(texture);
    job_deinit(&system);
    heap_release(&memory);
    return status;
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
                                uint32_t count_x, uint32_t count_y, float32_t *heights, uint8_t *dry) {
    (void)dry;
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

/* The phase 05 file's South Africa fixture, by x alone (as tests/run/climate/fixtures/ground.h has it): the west sea to
 * x = 0, a coast rising to the 500 plateau by 1000, the plateau to 10,000, a crest of 750 at 10,400, down to 250 at
 * 10,912, the escarpment face to 1 at 11,742, the coastal strip to 11,968, then the east sea. */
static void bench_south_africa_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                      uint32_t count_x, uint32_t count_y, float32_t *heights, uint8_t *dry) {
    (void)dry;
    (void)borrowed_context;
    (void)origin_y;
    static const float32_t KNOT_X[7] = {0.0f32, 1000.0f32, 10000.0f32, 10400.0f32, 10912.0f32, 11742.0f32, 11968.0f32};
    static const float32_t KNOT_H[7] = {1.0f32, 500.0f32, 500.0f32, 750.0f32, 250.0f32, 1.0f32, 1.0f32};
    for (uint32_t i = 0u; i < count_x; i++) {
        const int64_t x = (int64_t)origin_x + (int64_t)step * i;
        float32_t h = -50.0f32;
        if (x >= 0 && x < 11968) {
            const float32_t at = (float32_t)x;
            for (uint32_t k = 1u; k < 7u; k++) {
                if (at < KNOT_X[k]) {
                    const float32_t t = (at - KNOT_X[k - 1u]) / (KNOT_X[k] - KNOT_X[k - 1u]);
                    h = KNOT_H[k - 1u] + t * (KNOT_H[k] - KNOT_H[k - 1u]);
                    break;
                }
            }
        }
        for (uint32_t j = 0u; j < count_y; j++) {
            heights[(uint64_t)j * count_x + i] = h;
        }
    }
}

/* The continents at 30 S and 50 N, which show the master plan's table with no real coast's noise; from phase 05 also the
 * South Africa sketch at 25 S, 16 km round its middle. */
static int32_t bench_synthetic(climate_t *climate, const thread_topology_t *topology, const char *directory,
                               const bench_days_t *days) {
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
    if (ok && climate_open_ground(climate, BENCH_SEED, bench_south_africa_ground, nullptr)) {
        const int32_t sa_half = (int32_t)(BENCH_PICTURE_SIDE * BENCH_PICTURE_STEP / 2u);
        const int32_t sa_x = 6000, sa_y = -10656;
        climate_prepare_area(climate, &system, sa_x - sa_half, sa_y - sa_half, sa_x + sa_half - 1, sa_y + sa_half - 1,
                             nullptr, nullptr);
        ok = bench_normals_pictures(climate, directory, sa_x - sa_half, sa_y + sa_half, (int32_t)BENCH_PICTURE_STEP,
                                    "south_africa_", days, memory.owned_base, memory.owned_base + pixel_bytes,
                                    png_bytes);
        climate_close(climate, &system);
    } else {
        ok = false;
    }
    heap_release(&memory);
    job_deinit(&system);
    return ok ? 0 : 2;
}

/* ---- Weather systems: plan 004 phase 06 ---------------------------------------------------------------------------- */

enum : uint32_t {
    BENCH_SYSTEM_PLACES = 100u,
    BENCH_SYSTEM_COLD = 20u,      /* cold lists: a fresh climate for each */
    BENCH_SYSTEM_CELL_DAYS = 20u, /* lists with cells near the player */
    BENCH_SEASON_BLOCK = 128u,    /* a frame is drawn in jobs of 128 by 128 pixels */
    BENCH_SEASON_LISTS = 4u,      /* cells come from 4 by 4 lists over the view, each at most 8 km round */
    BENCH_SEASON_CELLS_MAX = 131072u,
    BENCH_SEASON_HURRICANES_MAX = 4096u,
};
static const float64_t BENCH_SYSTEMS_TARGET_MS = 2.0f64; /* master plan 7.3: the list near a point */
static const uint64_t BENCH_SYSTEMS_ARENA_BYTES = 16ull << 20;
static const int32_t BENCH_SEASON_STEP = 64;              /* blocks a pixel: 1024 pixels are 64 km */
static const float64_t BENCH_SEASON_SEARCH = 100000.0f64; /* the view's middle within 100 km of spawn */

static void bench_systems_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                 uint32_t count_x, uint32_t count_y, float32_t *heights, uint8_t *dry) {
    terrain_height_estimate_area(borrowed_context, origin_x, origin_y, step, count_x, count_y, heights, dry);
}

static int32_t bench_compare_ms(const void *a, const void *b) {
    const float64_t x = *(const float64_t *)a, y = *(const float64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static float64_t bench_median(float64_t *values, uint32_t count) {
    qsort(values, count, sizeof(float64_t), bench_compare_ms);
    return values[count / 2u];
}

/* --systems' measurements, in an open climate: warm lists, cold lists (a fresh climate each), cells near the player. */
static int32_t bench_systems_measure(climate_t *climate, job_system_t *system, const terrain_t *terrain, arena_t *arena,
                                     float64_t *warm, float64_t *cold) {
    int32_t status = 0;
    climate_systems_stats_t total = {0};
    uint64_t systems_found = 0u;
    for (uint32_t p = 0u; p < BENCH_SYSTEM_PLACES; p++) {
        /* Places within 20 km of spawn, days across ten years. */
        const float64_t x = -20000.0f64 + 400.0f64 * (float64_t)p, y = 20000.0f64 - 397.0f64 * (float64_t)p;
        const int64_t d = (int64_t)(p * 37u % 3650u);
        climate_systems_day_t day;
        arena_reset(arena);
        if (!climate_systems_day_build(climate, arena, x, y, 0.0f32, 0.0f32, d, CLIMATE_SYSTEMS_BIG, &day)) {
            return 2;
        }
        float64_t best = 1.0e30f64;
        for (uint32_t r = 0u; r < BENCH_ROUNDS; r++) {
            climate_systems_stats_t stats = {0};
            arena_reset(arena);
            const int64_t started = time_now_ns();
            (void)climate_systems_day_build_stats(climate, arena, x, y, 0.0f32, 0.0f32, d, CLIMATE_SYSTEMS_BIG, &day,
                                                  &stats);
            const float64_t ms = time_ns_to_ms(time_now_ns() - started);
            best = ms < best ? ms : best;
            if (r == 0u) {
                total.hashed += stats.hashed;
                total.candidates += stats.candidates;
                total.kept += stats.kept;
                total.track_steps += stats.track_steps;
                total.mask_reads += stats.mask_reads;
                total.mask_pages += stats.mask_pages;
                systems_found += day.system_count;
            }
        }
        warm[p] = best;
    }
    const float64_t places = (float64_t)BENCH_SYSTEM_PLACES;
    printf(
        "per call: %.0f square-days hashed, %.1f candidates, %.1f kept, %.0f track steps, %.0f mask reads, %.1f mask "
        "pages fetched, %.2f systems listed\n",
        (libc_double_t)((float64_t)total.hashed / places), (libc_double_t)((float64_t)total.candidates / places),
        (libc_double_t)((float64_t)total.kept / places), (libc_double_t)((float64_t)total.track_steps / places),
        (libc_double_t)((float64_t)total.mask_reads / places), (libc_double_t)((float64_t)total.mask_pages / places),
        (libc_double_t)((float64_t)systems_found / places));
    const float64_t warm_median = bench_median(warm, BENCH_SYSTEM_PLACES);
    printf("big systems near a point, warm (mask pages cached), best of %u: median %.3f ms, slowest %.3f ms; target "
           "%.1f ms: %s\n",
           BENCH_ROUNDS, (libc_double_t)warm_median, (libc_double_t)warm[BENCH_SYSTEM_PLACES - 1u],
           (libc_double_t)BENCH_SYSTEMS_TARGET_MS, warm_median <= BENCH_SYSTEMS_TARGET_MS ? "met" : "MISSED");
    status = warm_median <= BENCH_SYSTEMS_TARGET_MS ? 0 : 1;
    for (uint32_t p = 0u; p < BENCH_SYSTEM_COLD; p++) {
        climate_close(climate, system);
        if (!climate_open_ground(climate, BENCH_SEED, bench_systems_ground, terrain)) {
            return 2;
        }
        const float64_t x = -20000.0f64 + 2000.0f64 * (float64_t)p, y = 20000.0f64 - 1985.0f64 * (float64_t)p;
        climate_systems_day_t day;
        arena_reset(arena);
        const int64_t started = time_now_ns();
        (void)climate_systems_day_build(climate, arena, x, y, 0.0f32, 0.0f32, (int64_t)(p * 183u), CLIMATE_SYSTEMS_BIG,
                                        &day);
        cold[p] = time_ns_to_ms(time_now_ns() - started);
    }
    printf("big systems near a point, cold (a fresh climate, every mask page built or read from the ground): median "
           "%.3f ms of %u\n",
           (libc_double_t)bench_median(cold, BENCH_SYSTEM_COLD), BENCH_SYSTEM_COLD);
    /* Cells near the player at 2 km, every tile they need built. */
    climate_prepare_area(climate, system, -8000, -8000, 8000, 8000, nullptr, nullptr);
    float64_t cells_ms[BENCH_SYSTEM_CELL_DAYS];
    uint64_t cells_found = 0u;
    for (uint32_t k = 0u; k < BENCH_SYSTEM_CELL_DAYS; k++) {
        climate_systems_day_t day;
        arena_reset(arena);
        const int64_t started = time_now_ns();
        (void)climate_systems_day_build(climate, arena, 0.0f64, 0.0f64, 0.0f32, 2048.0f32, (int64_t)(150u + 9u * k),
                                        CLIMATE_SYSTEMS_ALL, &day);
        cells_ms[k] = time_ns_to_ms(time_now_ns() - started);
        cells_found += day.thunder_count;
    }
    printf("everything near the player with cells at 2 km: median %.3f ms, %.1f cells a list\n",
           (libc_double_t)bench_median(cells_ms, BENCH_SYSTEM_CELL_DAYS),
           (libc_double_t)((float64_t)cells_found / (float64_t)BENCH_SYSTEM_CELL_DAYS));
    return status;
}

/* --systems: the big systems near a point, warm (mask pages cached) and cold (a fresh climate), and cells near the player.
 * Exit 1 if the warm median is over 2 ms. */
static int32_t bench_systems(climate_t *climate, const thread_topology_t *topology) {
    terrain_t terrain;
    if (!terrain_init(&terrain, BENCH_SEED)) {
        return 2;
    }
    job_system_t system;
    if (!job_init_count(&system, topology, BENCH_WORKERS)) {
        terrain_deinit(&terrain);
        return 2;
    }
    heap_region_t memory = {0};
    if (!heap_reserve(BENCH_SYSTEMS_ARENA_BYTES + 2u * BENCH_SYSTEM_PLACES * sizeof(float64_t), &memory)) {
        job_deinit(&system);
        terrain_deinit(&terrain);
        return 2;
    }
    arena_t arena;
    arena_init(&arena, "bench_systems", memory.owned_base, BENCH_SYSTEMS_ARENA_BYTES);
    float64_t *const warm = (float64_t *)(void *)(memory.owned_base + BENCH_SYSTEMS_ARENA_BYTES);
    int32_t status = 2;
    if (climate_open_ground(climate, BENCH_SEED, bench_systems_ground, &terrain)) {
        status = bench_systems_measure(climate, &system, &terrain, &arena, warm, warm + BENCH_SYSTEM_PLACES);
    }
    climate_close(climate, &system);
    arena_deinit(&arena);
    heap_release(&memory);
    job_deinit(&system);
    terrain_deinit(&terrain);
    return status;
}

/* One 128 by 128 pixel block of a frame, on a worker, with only the cells that can reach it. */
typedef struct bench_block_t {
    const climate_t *borrowed_climate;         /* 0 */
    const climate_systems_day_t *borrowed_day; /* 8: big systems and every cell of the view */
    uint8_t *borrowed_pixels;                  /* 16: the frame */
    float64_t t;                               /* 24 */
    int32_t west_x;                            /* 32: the frame's */
    int32_t north_y;                           /* 36 */
    uint32_t column;                           /* 40: the block's first pixel */
    uint32_t row;                              /* 44 */
    uint32_t layer;                            /* 48 */
    uint8_t _pad0[4];                          /* 52 */
} bench_block_t;
static_assert(sizeof(bench_block_t) == 56);

static void bench_block_job(job_worker_t *worker, void *borrowed_argument) {
    const bench_block_t *const block = borrowed_argument;
    const climate_systems_day_t *const all = block->borrowed_day;
    climate_systems_day_t view = *all;
    climate_thunder_t *const near =
        arena_alloc_array(&worker->arena_job, all->thunder_count > 0u ? all->thunder_count : 1u,
                          sizeof(climate_thunder_t), alignof(climate_thunder_t));
    const float64_t step = (float64_t)BENCH_SEASON_STEP, side = (float64_t)BENCH_SEASON_BLOCK * step;
    const float64_t west = (float64_t)block->west_x + (float64_t)block->column * step;
    const float64_t north = (float64_t)block->north_y - (float64_t)block->row * step;
    /* A cell reaches the block if its start is within its reach of it: 4 hours at 15 blocks a second, and its radius. */
    const float64_t margin = climate_systems_reach(CLIMATE_SYSTEM_THUNDER);
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < all->thunder_count; i++) {
        const climate_thunder_t *const cell = &all->borrowed_thunder[i];
        if (cell->start_x >= west - margin && cell->start_x <= west + side + margin &&
            cell->start_y <= north + margin && cell->start_y >= north - side - margin) {
            near[count++] = *cell;
        }
    }
    view.borrowed_thunder = near;
    view.thunder_count = count;
    climate_layer_draw_now(block->borrowed_climate, &view, (climate_layer_t)block->layer, block->t, (int32_t)west,
                           (int32_t)north, BENCH_SEASON_STEP, BENCH_SEASON_BLOCK, BENCH_SEASON_BLOCK,
                           block->borrowed_pixels + (uint64_t)block->row * BENCH_PICTURE_SIDE * 4u +
                               (uint64_t)block->column * 4u,
                           BENCH_PICTURE_SIDE * 4u);
}

/* What the season has seen of each hurricane, for systems.txt. */
typedef struct bench_hurricane_t {
    float64_t spawn_day; /* 0 */
    float64_t start_x;   /* 8 */
    float64_t start_y;   /* 16 */
    float64_t end_day;   /* 24: its track's last sample */
    float64_t left_day;  /* 32: the first sample outside the view after one inside it; -1 if never */
    uint64_t id;         /* 40 */
    float32_t peak_hpa;  /* 48 */
    float32_t deepest;   /* 52: the strongest it was, hPa */
    uint32_t name_seed;  /* 56 */
    uint8_t in_view;     /* 60: it started in the view */
    uint8_t _pad0[3];    /* 61 */
} bench_hurricane_t;
static_assert(sizeof(bench_hurricane_t) == 64);

/* The view with the most sea between 10 and 25 degrees north, its middle within 100 km of spawn: x every 8 km, y so the band
 * is in it. Counts the coarse mask every 1024 blocks. */
static void bench_season_view(const climate_t *climate, int32_t *out_x, int32_t *out_y) {
    const float64_t band_south = 10.0f64 * CLIMATE_POLE_Y / 90.0f64, band_north = 25.0f64 * CLIMATE_POLE_Y / 90.0f64;
    const float64_t half = 0.5f64 * (float64_t)BENCH_PICTURE_SIDE * (float64_t)BENCH_SEASON_STEP;
    const int32_t centre_y = (int32_t)(0.5f64 * (band_south + band_north));
    int32_t best_x = 0;
    int64_t best = -1;
    for (float64_t cx = -BENCH_SEASON_SEARCH + half; cx <= BENCH_SEASON_SEARCH - half; cx += 8192.0f64) {
        int64_t sea = 0;
        for (float64_t y = band_south; y <= band_north; y += 1024.0f64) {
            for (float64_t x = cx - half; x < cx + half; x += 1024.0f64) {
                sea += climate_mask_point(climate, x, y) <= 0.0f32 ? 1 : 0;
            }
        }
        if (sea > best || (sea == best && fabs(cx) < fabs((float64_t)best_x))) {
            best = sea;
            best_x = (int32_t)cx;
        }
    }
    const float64_t points = (band_north - band_south) / 1024.0f64 * 64.0f64;
    /* Open sea, where a hurricane may form: water there and CLIMATE_HURRICANE_OPEN_SEA_BLOCKS north, south, east and west (0
     * since the final check: the point alone). */
    int64_t open = 0;
    const float64_t d = (float64_t)CLIMATE_HURRICANE_OPEN_SEA_BLOCKS;
    for (float64_t y = band_south; y <= band_north; y += 1024.0f64) {
        for (float64_t x = (float64_t)best_x - half; x < (float64_t)best_x + half; x += 1024.0f64) {
            open += (climate_mask_point(climate, x, y) <= 0.0f32 && climate_mask_point(climate, x + d, y) <= 0.0f32 &&
                     climate_mask_point(climate, x - d, y) <= 0.0f32 &&
                     climate_mask_point(climate, x, y + d) <= 0.0f32 && climate_mask_point(climate, x, y - d) <= 0.0f32)
                        ? 1
                        : 0;
        }
    }
    printf("the view's band from 10 to 25 degrees north is %.0f%% sea on the coarse mask, %.0f%% open sea\n",
           (libc_double_t)(100.0f64 * (float64_t)best / points), (libc_double_t)(100.0f64 * (float64_t)open / points));
    *out_x = best_x;
    *out_y = centre_y;
}

/* The lists for day d: big systems over the whole view, and cells from 4 by 4 lists of 8 km each, merged by id. */
static bool bench_season_lists(const climate_t *climate, arena_t *arena, arena_t *parts, int32_t centre_x,
                               int32_t centre_y, int64_t d, climate_thunder_t *cells, climate_systems_day_t *out) {
    const float64_t half = 0.5f64 * (float64_t)BENCH_PICTURE_SIDE * (float64_t)BENCH_SEASON_STEP;
    if (!climate_systems_day_build(climate, arena, (float64_t)centre_x, (float64_t)centre_y,
                                   (float32_t)(half * 1.41421356237309505f64), 0.0f32, d, CLIMATE_SYSTEMS_BIG, out)) {
        return false;
    }
    uint32_t count = 0u;
    const float64_t spacing = 2.0f64 * half / (float64_t)BENCH_SEASON_LISTS;
    for (uint32_t j = 0u; j < BENCH_SEASON_LISTS; j++) {
        for (uint32_t i = 0u; i < BENCH_SEASON_LISTS; i++) {
            const float64_t x = (float64_t)centre_x - half + spacing * ((float64_t)i + 0.5f64);
            const float64_t y = (float64_t)centre_y - half + spacing * ((float64_t)j + 0.5f64);
            arena_reset(parts);
            climate_systems_day_t part;
            if (!climate_systems_day_build(climate, parts, x, y, 0.0f32, CLIMATE_THUNDER_QUERY_MAX, d,
                                           CLIMATE_SYSTEMS_ALL, &part)) {
                return false;
            }
            for (uint32_t k = 0u; k < part.thunder_count; k++) {
                bool seen = false;
                for (uint32_t m = 0u; m < count && !seen; m++) {
                    seen = cells[m].id == part.borrowed_thunder[k].id;
                }
                if (!seen && count < BENCH_SEASON_CELLS_MAX) {
                    cells[count++] = part.borrowed_thunder[k];
                }
            }
        }
    }
    out->borrowed_thunder = cells;
    out->thunder_count = count;
    out->thunder_capacity = BENCH_SEASON_CELLS_MAX;
    return true;
}

/* Notes each hurricane of the day's list: its first sighting, and the first sample outside the view after one inside. */
static uint32_t bench_season_note(const climate_systems_day_t *day, int32_t centre_x, int32_t centre_y,
                                  bench_hurricane_t *seen, uint32_t count) {
    const float64_t half = 0.5f64 * (float64_t)BENCH_PICTURE_SIDE * (float64_t)BENCH_SEASON_STEP;
    for (uint32_t i = 0u; i < day->system_count; i++) {
        const climate_system_t *const h = &day->borrowed_systems[i];
        if (h->kind != CLIMATE_SYSTEM_HURRICANE) {
            continue;
        }
        bool known = false;
        for (uint32_t k = 0u; k < count && !known; k++) {
            known = seen[k].id == h->id;
        }
        if (known || count == BENCH_SEASON_HURRICANES_MAX) {
            continue;
        }
        bench_hurricane_t *const note = &seen[count++];
        memset(note, 0, sizeof(*note));
        note->id = h->id;
        note->spawn_day = h->spawn_day;
        note->start_x = h->start_x;
        note->start_y = h->start_y;
        note->peak_hpa = h->peak_hpa;
        note->name_seed = h->name_seed;
        note->end_day = h->spawn_day + CLIMATE_TRACK_STEP_DAYS * (float64_t)(h->sample_count - 1u);
        note->left_day = -1.0f64;
        bool inside_once = false;
        for (uint32_t k = 0u; k < h->sample_count; k++) {
            const bool inside = fabs(h->samples[k].x - (float64_t)centre_x) <= half &&
                                fabs(h->samples[k].y - (float64_t)centre_y) <= half;
            if (inside && k == 0u) {
                note->in_view = 1u;
            }
            if (inside_once && !inside && note->left_day < 0.0f64) {
                note->left_day = h->spawn_day + CLIMATE_TRACK_STEP_DAYS * (float64_t)k;
            }
            inside_once = inside_once || inside;
            climate_system_state_t state;
            climate_system_state_at(h, h->spawn_day + CLIMATE_TRACK_STEP_DAYS * (float64_t)k, &state);
            note->deepest = state.pressure_hpa < note->deepest ? state.pressure_hpa : note->deepest;
        }
    }
    return count;
}

/* --season <dir> <seed> <first_day> <last_day> <step_days>: the gate's frames, every step from first to last day, of the
 * 64 by 64 km view with the most sea between 10 and 25 degrees north within 100 km of spawn, and systems.txt. */
static int32_t bench_season(climate_t *climate, const thread_topology_t *topology, const char *directory, uint64_t seed,
                            float64_t first, float64_t last, float64_t step) {
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_climate: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    terrain_t terrain;
    if (!terrain_init(&terrain, seed)) {
        return 2;
    }
    job_system_t system;
    const uint64_t pixel_bytes = (uint64_t)BENCH_PICTURE_SIDE * BENCH_PICTURE_SIDE * 4u;
    const uint64_t png_bytes = display_png_bytes_needed(BENCH_PICTURE_SIDE, BENCH_PICTURE_SIDE);
    const uint64_t cells_bytes = (uint64_t)BENCH_SEASON_CELLS_MAX * sizeof(climate_thunder_t);
    const uint64_t seen_bytes = (uint64_t)BENCH_SEASON_HURRICANES_MAX * sizeof(bench_hurricane_t);
    const uint64_t blocks = (BENCH_PICTURE_SIDE / BENCH_SEASON_BLOCK) * (BENCH_PICTURE_SIDE / BENCH_SEASON_BLOCK);
    const uint64_t block_bytes = blocks * sizeof(bench_block_t);
    heap_region_t memory = {0};
    if (!job_init_count(&system, topology, BENCH_WORKERS)) {
        terrain_deinit(&terrain);
        return 2;
    }
    if (!heap_reserve(pixel_bytes + png_bytes + cells_bytes + seen_bytes + block_bytes + 2u * BENCH_SYSTEMS_ARENA_BYTES,
                      &memory) ||
        !climate_open_ground(climate, seed, bench_systems_ground, &terrain)) {
        heap_release(&memory);
        job_deinit(&system);
        terrain_deinit(&terrain);
        return 2;
    }
    uint8_t *at = memory.owned_base;
    uint8_t *const pixels = at;
    at += pixel_bytes;
    uint8_t *const stream = at;
    at += png_bytes;
    climate_thunder_t *const cells = (climate_thunder_t *)(void *)at;
    at += cells_bytes;
    bench_hurricane_t *const seen = (bench_hurricane_t *)(void *)at;
    at += seen_bytes;
    bench_block_t *const jobs = (bench_block_t *)(void *)at;
    at += block_bytes;
    arena_t arena, parts;
    arena_init(&arena, "bench_season", at, BENCH_SYSTEMS_ARENA_BYTES);
    arena_init(&parts, "bench_season_parts", at + BENCH_SYSTEMS_ARENA_BYTES, BENCH_SYSTEMS_ARENA_BYTES);
    int32_t centre_x = 0, centre_y = 0;
    bench_season_view(climate, &centre_x, &centre_y);
    const int32_t half = (int32_t)(BENCH_PICTURE_SIDE * (uint32_t)BENCH_SEASON_STEP / 2u);
    const int32_t west = centre_x - half, north = centre_y + half;
    printf("view: %d by %d blocks round (%d, %d), %.1f to %.1f degrees north\n", 2 * half, 2 * half, centre_x, centre_y,
           (libc_double_t)climate_latitude_deg((float64_t)(centre_y - half)),
           (libc_double_t)climate_latitude_deg((float64_t)(centre_y + half)));
    /* The tiles: as much of the view as the keep square holds, 62 km of its 64. */
    const int64_t prepared = time_now_ns();
    const int32_t built = 31 * CLIMATE_TILE_BLOCKS - 2 * CLIMATE_READ_REACH_BLOCKS;
    climate_prepare_area(climate, &system, centre_x - built, centre_y - built, centre_x + built, centre_y + built,
                         nullptr, nullptr);
    printf("tiles of the middle %d km built in %.1f s\n", 2 * built / 1000,
           (libc_double_t)(time_ns_to_ms(time_now_ns() - prepared) / 1000.0f64));
    static const climate_layer_t LAYERS[4] = {CLIMATE_LAYER_SYSTEMS, CLIMATE_LAYER_PRESSURE_NOW,
                                              CLIMATE_LAYER_RAIN_RADAR, CLIMATE_LAYER_THUNDER};
    static const char *const NAMES[4] = {"systems", "pressure_now", "rain_radar", "thunder"};
    int64_t listed_day = INT64_MIN;
    climate_systems_day_t day;
    memset(&day, 0, sizeof(day));
    uint32_t seen_count = 0u;
    bool ok = true;
    const int64_t started = time_now_ns();
    uint32_t frames = 0u;
    for (float64_t t = first; t <= last + 1.0e-9f64 && ok; t += step) {
        const int64_t d = (int64_t)floor(t);
        if (d != listed_day) {
            arena_reset(&arena);
            arena_reset(&parts);
            ok = bench_season_lists(climate, &arena, &parts, centre_x, centre_y, d, cells, &day);
            listed_day = d;
            seen_count = bench_season_note(&day, centre_x, centre_y, seen, seen_count);
            uint32_t kinds[CLIMATE_SYSTEM_THUNDER] = {0u, 0u, 0u};
            for (uint32_t i = 0u; i < day.system_count; i++) {
                kinds[day.borrowed_systems[i].kind]++;
            }
            printf("day %" PRId64 ": %u hurricanes, %u lows, %u highs, %u cells near the view\n", d, kinds[0], kinds[1],
                   kinds[2], day.thunder_count);
        }
        for (uint32_t l = 0u; l < 4u && ok; l++) {
            job_group_t group;
            job_group_init(&group);
            uint32_t n = 0u;
            for (uint32_t row = 0u; row < BENCH_PICTURE_SIDE; row += BENCH_SEASON_BLOCK) {
                for (uint32_t column = 0u; column < BENCH_PICTURE_SIDE; column += BENCH_SEASON_BLOCK) {
                    jobs[n] = (bench_block_t){.borrowed_climate = climate,
                                              .borrowed_day = &day,
                                              .borrowed_pixels = pixels,
                                              .t = t,
                                              .west_x = west,
                                              .north_y = north,
                                              .column = column,
                                              .row = row,
                                              .layer = (uint32_t)LAYERS[l]};
                    job_submit(&system, &group, bench_block_job, &jobs[n]);
                    n++;
                }
            }
            job_group_wait(&system, &group);
            job_group_deinit(&group);
            char name[128];
            (void)snprintf(name, sizeof(name), "%s_d%.1f.png", NAMES[l], (libc_double_t)t);
            ok = bench_png_write(directory, name, pixels, stream, png_bytes);
        }
        frames++;
    }
    printf("%u frames in %.1f s\n", frames, (libc_double_t)(time_ns_to_ms(time_now_ns() - started) / 1000.0f64));
    /* systems.txt: every hurricane seen, in the order first seen. */
    char path[1024];
    const int32_t written = snprintf(path, sizeof(path), "%s/systems.txt", directory);
    FILE *const file = (ok && written > 0 && (uint64_t)written < sizeof(path)) ? fopen(path, "w") : nullptr;
    if (file == nullptr) {
        ok = false;
    } else {
        uint32_t formed = 0u;
        fprintf(file, "# seed %" PRIu64 ", days %.1f to %.1f every %.2f; view %d by %d blocks round (%d, %d)\n", seed,
                (libc_double_t)first, (libc_double_t)last, (libc_double_t)step, 2 * half, 2 * half, centre_x, centre_y);
        fprintf(file,
                "# name, formed in view, spawn day, start x y, latitude, peak hPa, deepest hPa, died day, left view "
                "day (-1 never)\n");
        for (uint32_t k = 0u; k < seen_count; k++) {
            char title[CLIMATE_SYSTEM_NAME_BYTES];
            climate_system_name(seen[k].name_seed, title);
            formed += seen[k].in_view;
            fprintf(file, "%-8s %u %8.2f %9.0f %9.0f %6.2f %7.1f %7.1f %8.2f %8.2f\n", title, seen[k].in_view,
                    (libc_double_t)seen[k].spawn_day, (libc_double_t)seen[k].start_x, (libc_double_t)seen[k].start_y,
                    (libc_double_t)climate_latitude_deg(seen[k].start_y), (libc_double_t)seen[k].peak_hpa,
                    (libc_double_t)seen[k].deepest, (libc_double_t)seen[k].end_day, (libc_double_t)seen[k].left_day);
        }
        fprintf(file, "# %u hurricanes seen, %u formed in the view\n", seen_count, formed);
        printf("%u hurricanes seen, %u formed in the view; wrote %s\n", seen_count, formed, path);
        ok = fclose(file) == 0 && ok;
    }
    arena_deinit(&parts);
    arena_deinit(&arena);
    heap_release(&memory);
    climate_close(climate, &system);
    job_deinit(&system);
    terrain_deinit(&terrain);
    return ok ? 0 : 2;
}

/* "15,46,76": up to BENCH_DAYS_MAX days from 0 to 364. */
static bool bench_days_parse(const char *text, bench_days_t *out) {
    bench_days_t days = {0};
    const char *at = text;
    for (;;) {
        char *end = nullptr;
        errno = 0;
        const uint64_t day = strtoull(at, &end, 10);
        if (errno != 0 || end == at || day > 364u || days.count == BENCH_DAYS_MAX) {
            return false;
        }
        days.days[days.count++] = (uint32_t)day;
        if (*end == '\0') {
            break;
        }
        if (*end != ',') {
            return false;
        }
        at = end + 1;
    }
    *out = days;
    return true;
}

int32_t main(int32_t argc, char **argv) {
    thread_topology_t topology;
    climate_t climate;
    if (!thread_topology_read(&topology) || !climate_init(&climate)) {
        return 2;
    }
    int32_t status = 0;
    bench_days_t days = {.count = BENCH_MONTHS};
    memcpy(days.days, BENCH_MONTH_DAYS, sizeof(BENCH_MONTH_DAYS));
    /* Plan 004 phase 09: --layer <key|all>, --zoom <n> and --flat, after --pictures's arguments. */
    bench_atlas_t atlas = {.layer = nullptr, .zoom = 8, .flat = 0u, ._pad0 = {0}};
    while (argc >= 2 && strcmp(argv[argc - 1], "--flat") == 0) {
        atlas.flat = 1u;
        argc--;
    }
    while (argc >= 3 && (strcmp(argv[argc - 2], "--layer") == 0 || strcmp(argv[argc - 2], "--zoom") == 0)) {
        if (strcmp(argv[argc - 2], "--layer") == 0) {
            atlas.layer = argv[argc - 1];
        } else {
            char *end = nullptr;
            const int64_t zoom = strtoll(argv[argc - 1], &end, 10);
            if (end == argv[argc - 1] || *end != '\0' || zoom < UI_ATLAS_ZOOM_MIN || zoom > UI_ATLAS_ZOOM_MAX) {
                fprintf(stderr, "bench_climate: --zoom takes %d to %d\n", UI_ATLAS_ZOOM_MIN, UI_ATLAS_ZOOM_MAX);
                climate_deinit(&climate);
                return 2;
            }
            atlas.zoom = (int32_t)zoom;
        }
        argc -= 2;
        while (argc >= 2 && strcmp(argv[argc - 1], "--flat") == 0) {
            atlas.flat = 1u;
            argc--;
        }
    }
    const bool with_days = argc >= 3 && strcmp(argv[argc - 2], "--days") == 0;
    if (with_days && !bench_days_parse(argv[argc - 1], &days)) {
        fprintf(stderr, "bench_climate: '%s' is not a list of days from 0 to 364\n", argv[argc - 1]);
        climate_deinit(&climate);
        return 2;
    }
    const int32_t plain = with_days ? argc - 2 : argc;
    if (plain == 4 && strcmp(argv[1], "--pictures") == 0) {
        char *end = nullptr;
        errno = 0;
        const uint64_t seed = strtoull(argv[3], &end, 10);
        if (errno != 0 || end == argv[3] || *end != '\0') {
            fprintf(stderr, "bench_climate: '%s' is not a seed\n", argv[3]);
            status = 2;
        } else {
            status = atlas.layer != nullptr ? bench_atlas_pictures(&climate, &topology, argv[2], seed, &atlas)
                                            : bench_pictures(&climate, &topology, argv[2], seed, &days);
        }
    } else if (plain == 3 && strcmp(argv[1], "--synthetic") == 0) {
        status = bench_synthetic(&climate, &topology, argv[2], &days);
    } else if (argc == 2 && strcmp(argv[1], "--systems") == 0) {
        status = bench_systems(&climate, &topology);
    } else if (argc == 7 && strcmp(argv[1], "--season") == 0) {
        char *end = nullptr;
        errno = 0;
        const uint64_t seed = strtoull(argv[3], &end, 10);
        const bool seed_ok = errno == 0 && end != argv[3] && *end == '\0';
        char *first_end = nullptr, *last_end = nullptr, *step_end = nullptr;
        const float64_t first = (float64_t)strtod(argv[4], &first_end);
        const float64_t last = (float64_t)strtod(argv[5], &last_end);
        const float64_t step = (float64_t)strtod(argv[6], &step_end);
        if (!seed_ok || *first_end != '\0' || *last_end != '\0' || *step_end != '\0' || !(step > 0.0f64) ||
            last < first) {
            fprintf(stderr, "bench_climate: --season <directory> <seed> <first_day> <last_day> <step_days>\n");
            status = 2;
        } else {
            status = bench_season(&climate, &topology, argv[2], seed, first, last, step);
        }
    } else if (argc == 1) {
        terrain_t terrain;
        if (!terrain_init(&terrain, BENCH_SEED)) {
            climate_deinit(&climate);
            return 2;
        }
        bench_ground_t ground = {.borrowed_terrain = &terrain};
        bench_ground_reset(&ground);
        status = bench_timings(&climate, &topology, &ground);
        const int32_t parts = bench_parts(&climate, &terrain);
        status = parts != 0 ? parts : status;
        terrain_deinit(&terrain);
    } else {
        fprintf(stderr,
                "usage: bench_climate [--pictures <directory> <seed> [--layer <key|all> [--zoom <n>] [--flat]] | "
                "--synthetic <directory>] [--days d,d,...] | --systems | --season <directory> <seed> <first_day> "
                "<last_day> <step_days>\n");
        status = 2;
    }
    climate_deinit(&climate);
    return status;
}
