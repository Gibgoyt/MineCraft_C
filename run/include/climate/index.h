/* See run/src/climate/README.md. */
#ifndef MC_RUN_CLIMATE_INDEX_H
#define MC_RUN_CLIMATE_INDEX_H

#include "arena/index.h"
#include "climate/cache/index.h"
#include "climate/cell/index.h"
#include "climate/mask/index.h"
#include "climate/sea/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "terrain/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Ahmed, 2026-11-04: the tropic, at 23.5 degrees, is 10 km from the equator, and latitude rises evenly with y. */
#define CLIMATE_TILT_DEG 23.5f64
#define CLIMATE_TROPIC_Y 10000.0f64
/* Y in master plan 5.1: blocks from the equator to a pole, 38297.87. */
#define CLIMATE_POLE_Y (CLIMATE_TROPIC_Y * 90.0f64 / CLIMATE_TILT_DEG)
/* Equator, north pole, equator, south pole, equator: 153191.49 blocks. */
#define CLIMATE_LATITUDE_PERIOD (4.0f64 * CLIMATE_POLE_Y)
#define CLIMATE_PI 3.14159265358979323846f64

/* phi(y) of master plan 5.1, in degrees: positive north, from -90 to 90, a triangle wave in y. Any finite y. Pure. */
float64_t climate_latitude_deg(float64_t y);

/* ---- The climate cells: plan 004 phase 03 --------------------------------------------------------------------------- */

enum : uint32_t {
    CLIMATE_JOBS_IN_FLIGHT_MAX = 4u, /* tile jobs the sim keeps queued, leaving 12 workers for columns */
    CLIMATE_WARP_FIELDS = 2u,
};

/* Where everything sits in the one region climate_init reserves. Only pages touched cost memory. */
enum : uint64_t {
    CLIMATE_STATE_BYTES = 4096u, /* climate_state_t at the start, then the warp bank's fields */
    CLIMATE_TABLES_OFFSET = 512u,
    CLIMATE_TILE_SLOTS_OFFSET = CLIMATE_STATE_BYTES,
    CLIMATE_MASK_SLOTS_OFFSET = CLIMATE_TILE_SLOTS_OFFSET + (uint64_t)CLIMATE_TILE_SLOTS * sizeof(climate_slot_t),
    CLIMATE_SLOTS_END = CLIMATE_MASK_SLOTS_OFFSET + (uint64_t)CLIMATE_MASK_SLOTS * sizeof(climate_slot_t),
    CLIMATE_CELLS_OFFSET =
        (CLIMATE_SLOTS_END + HEAP_REGION_ALIGNMENT - 1u) / HEAP_REGION_ALIGNMENT * HEAP_REGION_ALIGNMENT,
    CLIMATE_CELLS_BYTES = (uint64_t)CLIMATE_TILE_SLOTS * CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t), /* 128 MiB */
    CLIMATE_MASK_OFFSET = CLIMATE_CELLS_OFFSET + CLIMATE_CELLS_BYTES,
    CLIMATE_MASK_BYTES = (uint64_t)CLIMATE_MASK_SLOTS * CLIMATE_TILE_CELL_COUNT * sizeof(float32_t), /* 16 MiB */
    CLIMATE_REGION_BYTES = CLIMATE_MASK_OFFSET + CLIMATE_MASK_BYTES,                                 /* 146 MiB */
};
static_assert(CLIMATE_REGION_BYTES % HEAP_REGION_ALIGNMENT == 0);
static_assert(CLIMATE_TABLES_OFFSET + CLIMATE_WARP_FIELDS * sizeof(terrain_noise_field_t) + 64u <= CLIMATE_STATE_BYTES);

/* One world's climate cells, at the start of the climate's region. Written by the sim (scheduling), workers (tiles, pages)
 * and readers (the reader counts), so the atomics have a line of their own. */
typedef struct climate_state_t {
    arena_t arena_tables;                         /* 0: the warp bank's fields; World lifetime */
    job_group_t group;                            /* 64: every job the climate queues */
    terrain_noise_bank_t warp_bank;               /* 128 */
    const void *borrowed_ground_context;          /* 152: the terrain_t, or a test's synthetic ground */
    climate_ground_fn_t ground;                   /* 160: nullptr while closed */
    const terrain_noise_field_t *borrowed_warp_x; /* 168 */
    const terrain_noise_field_t *borrowed_warp_y; /* 176 */
    climate_slot_t *borrowed_tile_slots;          /* 184: CLIMATE_TILE_SLOTS */
    climate_slot_t *borrowed_mask_slots;          /* 192: CLIMATE_MASK_SLOTS */
    climate_cell_t *borrowed_cells;               /* 200: CLIMATE_TILE_SLOTS * 256 */
    float32_t *borrowed_mask_heights;             /* 208: CLIMATE_MASK_SLOTS * 256 */
    uint64_t seed;                                /* 216 */
    uint8_t open;                                 /* 224 */
    uint8_t warp_off;                             /* 225: a test's: reads are not warped */
    uint8_t _pad0[30];                            /* 226 */
    /* A line of its own: the sim writes the keep square, workers count down. */
    _Atomic int32_t keep_tile_x;                 /* 256 */
    _Atomic int32_t keep_tile_y;                 /* 260 */
    _Atomic uint32_t jobs_in_flight;             /* 264: tile jobs queued and not finished */
    _Atomic uint32_t jobs_done;                  /* 268: every job the climate has finished, for progress */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 16]; /* 272 */
} climate_state_t;
static_assert(sizeof(climate_state_t) == 320);
static_assert(alignof(climate_state_t) == 64);
static_assert(offsetof(climate_state_t, group) == 64);
static_assert(offsetof(climate_state_t, keep_tile_x) == 256);
static_assert(sizeof(climate_state_t) <= CLIMATE_TABLES_OFFSET);

/* What app_t holds: the region and the state at its start. Small on purpose, as main's frame holds app_t: one line, so it
 * sits between app_t's lined-up members with no padding of the compiler's. */
typedef struct climate_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) heap_region_t owned_region; /* 0: everything, CLIMATE_REGION_BYTES */
    climate_state_t *borrowed_state;             /* 16: at owned_region's start; nullptr before climate_init */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 24]; /* 24 */
} climate_t;
static_assert(sizeof(climate_t) == 64);
static_assert(alignof(climate_t) == 64);

/* Process lifetime: reserves the region, all of it untouched. False, having logged why, if the heap says no. */
[[nodiscard]] bool climate_init(climate_t *climate);
/* Safe on a zeroed climate_t. The climate must be closed. */
void climate_deinit(climate_t *climate);

/* World lifetime: binds a terrain (the ground is terrain_height_estimate_area), seeds the warp bank, empties both tables.
 * No job of a previous world may be running: climate_close first. */
[[nodiscard]] bool climate_open(climate_t *climate, const terrain_t *terrain);
/* The same on any ground, for tests and the bench. */
[[nodiscard]] bool climate_open_ground(climate_t *climate, uint64_t seed, climate_ground_fn_t ground,
                                       const void *borrowed_context);
/* Waits for its jobs (not on a worker), then forgets the world. Safe on a closed or zeroed climate. */
void climate_close(climate_t *climate, const job_system_t *system);
/* Whether a world is bound. */
bool climate_is_open(const climate_t *climate);

/* The sim, once a step: moves the keep square to the player and queues up to CLIMATE_JOBS_IN_FLIGHT_MAX tiles not yet
 * built within radius_blocks (plus the read reach), nearest first. Never waits. */
void climate_schedule(climate_t *climate, job_system_t *system, int32_t player_x, int32_t player_y,
                      uint32_t radius_blocks);

/* Whether every tile a read anywhere in this block rectangle (both ends included) touches is built. Any thread. */
bool climate_area_ready(const climate_t *climate, int32_t x_min, int32_t y_min, int32_t x_max, int32_t y_max);

/* Any thread but a worker: builds every tile a read in the rectangle touches, on all workers, and waits. Mask pages first,
 * one job each, then tiles, so no two jobs build one page. progress (may be nullptr) is told done and total as jobs
 * finish, from this thread. Moves the keep square to the rectangle's middle, so the rectangle must fit in it. */
void climate_prepare_area(climate_t *climate, job_system_t *system, int32_t x_min, int32_t y_min, int32_t x_max,
                          int32_t y_max, void (*progress)(void *context, uint32_t done, uint32_t total),
                          void *progress_context);

/* Any thread but a worker: builds the one tile (tile_x, tile_y) in one job, its window's missing pages inside that job, and
 * waits. Moves the keep square to it. For tests and the bench: what a tile costs, cold or warm. */
void climate_prepare_tile(climate_t *climate, job_system_t *system, int32_t tile_x, int32_t tile_y);

/* The cell holding a block, copied out. False if its tile is not built: nothing is written. Any thread. */
bool climate_cell_get(const climate_t *climate, int32_t x, int32_t y, climate_cell_t *out);

/* Phase 03's fields at a block: warped, bilinear, latitude exact from y. False if a tile it needs is not built: nothing is
 * written. Any thread. */
bool climate_sample_at(const climate_t *climate, float64_t x, float64_t y, climate_sample_t *out);

/* Where a read at (x, y) is taken from: (x + 32 Wx, y + 32 Wy). With the warp off, (x, y). Any thread, once open. */
void climate_warp_at(const climate_t *climate, float64_t x, float64_t y, float64_t *out_x, float64_t *out_y);

/* A test's hook: reads unwarped from here on, or warped again. */
void climate_warp_set(climate_t *climate, bool on);

#endif
