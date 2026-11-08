/* See run/src/climate/README.md. */
#ifndef MC_RUN_CLIMATE_INDEX_H
#define MC_RUN_CLIMATE_INDEX_H

#include "arena/index.h"
#include "climate/air/index.h"
#include "climate/cache/index.h"
#include "climate/cell/index.h"
#include "climate/heat/index.h"
#include "climate/mask/index.h"
#include "climate/normals/index.h"
#include "climate/rain/index.h"
#include "climate/sea/index.h"
#include "climate/season/index.h"
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
#define CLIMATE_TWO_PI (2.0f64 * CLIMATE_PI)
/* Latitude of a pole, degrees: 5.1. */
#define CLIMATE_POLE_DEG 90.0f64
/* The polar circles, degrees: 90 - 23.5 = 66.5, past which there is polar night and midnight sun. 4.1, 5.2 */
#define CLIMATE_POLAR_CIRCLE_DEG (CLIMATE_POLE_DEG - CLIMATE_TILT_DEG)
/* A hash to a uniform in [0, 1): its top 53 bits, the shift that keeps them, and 2^-53. Phases 06 and 07. */
enum : uint32_t { CLIMATE_UNIFORM_SHIFT = 11u };
#define CLIMATE_UNIFORM_SCALE 0x1p-53f64

/* phi(y) of master plan 5.1, in degrees: positive north, from -90 to 90, a triangle wave in y. Any finite y. Pure. */
float64_t climate_latitude_deg(float64_t y);

/* ---- The climate cells: plan 004 phase 03 --------------------------------------------------------------------------- */

enum : uint32_t {
    CLIMATE_JOBS_IN_FLIGHT_MAX = 4u, /* tile jobs the sim keeps queued, leaving 12 workers for columns */
    CLIMATE_WARP_FIELDS = 2u,
};

/* Where everything sits in the one region climate_init reserves. Only pages touched cost memory. */
enum : uint64_t {
    CLIMATE_STATE_BYTES = 8192u, /* climate_state_t at the start, then the warp bank's fields, then the normals */
    CLIMATE_TABLES_OFFSET = 512u,
    CLIMATE_NORMALS_OFFSET = 4096u, /* phase 04: the normals arena, to CLIMATE_STATE_BYTES */
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
static_assert(CLIMATE_TABLES_OFFSET + CLIMATE_WARP_FIELDS * sizeof(terrain_noise_field_t) + 64u <=
              CLIMATE_NORMALS_OFFSET);
static_assert(CLIMATE_NORMALS_OFFSET + sizeof(climate_normals_t) + 64u <= CLIMATE_STATE_BYTES);

/* Where climate_prepare_begin's rounds stand: main's, while a world opens, so the loading screen can be drawn between them. */
typedef enum climate_prepare_phase_t : uint8_t {
    CLIMATE_PREPARE_NONE = 0,  /* nothing begun, or all built */
    CLIMATE_PREPARE_PAGES = 1, /* the mask pages' jobs are in */
    CLIMATE_PREPARE_TILES = 2, /* the tiles' jobs are in */
} climate_prepare_phase_t;

typedef struct climate_prepare_t {
    int32_t tile_x_min;   /* 0: the tiles to build, both ends included */
    int32_t tile_y_min;   /* 4 */
    int32_t tile_x_max;   /* 8 */
    int32_t tile_y_max;   /* 12 */
    int32_t keep_x;       /* 16: the keep square's middle, the rectangle's */
    int32_t keep_y;       /* 20 */
    uint32_t start;       /* 24: jobs_done when this round's jobs went in */
    uint32_t submitted;   /* 28: this round's jobs */
    uint32_t done_before; /* 32: what earlier rounds did */
    uint32_t total;       /* 36: every page and tile not built when it began */
    uint8_t phase;        /* 40: climate_prepare_phase_t */
    uint8_t _pad0[23];    /* 41 */
} climate_prepare_t;
static_assert(sizeof(climate_prepare_t) == 64);

/* Plan 004 phase 06, for tests only: each field at 0 means off. In climate_state_t; climate_open puts it back to all off. */
typedef struct climate_systems_hooks_t {
    float32_t hurricane_s;         /* 0: if above 0, s_hur is this between hurricane_first and hurricane_last, else 0 */
    float32_t hurricane_current_c; /* 4: added to the track-time sea temperature */
    uint16_t hurricane_first;      /* 8: days of the year */
    uint16_t hurricane_last;       /* 10 */
    uint8_t big_off;               /* 12: bit per kind: no spawns of that kind */
    uint8_t _pad0[3];              /* 13 */
} climate_systems_hooks_t;
static_assert(sizeof(climate_systems_hooks_t) == 16);

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
    uint8_t _pad0[6];                             /* 226 */
    const climate_normals_t *borrowed_normals;    /* 232: phase 04's curves, in arena_normals; from climate_init */
    climate_test_hooks_t hooks;                   /* 240: phase 05, a test's: all zeros in every real run */
    /* A line of its own: the sim writes the keep square, workers count down. */
    _Atomic int32_t keep_tile_x;                 /* 256 */
    _Atomic int32_t keep_tile_y;                 /* 260 */
    _Atomic uint32_t jobs_in_flight;             /* 264: tile jobs queued and not finished */
    _Atomic uint32_t jobs_done;                  /* 268: every job the climate has finished, for progress */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 16]; /* 272 */
    climate_prepare_t prepare;                   /* 320: main's, between climate_prepare_begin and its last step */
    arena_t arena_normals;                       /* 384: the normals' curves; Process lifetime, built in climate_init */
    uint64_t systems_seed[4];                    /* 448: phase 06: each weather system kind's dice, from climate_open */
    climate_systems_hooks_t systems_hooks;       /* 480: phase 06, a test's: all zeros in every real run */
    uint8_t _pad2[16];                           /* 496 */
} climate_state_t;
static_assert(sizeof(climate_state_t) == 512);
static_assert(offsetof(climate_state_t, systems_seed) == 448);
static_assert(offsetof(climate_state_t, systems_hooks) == 480);
static_assert(offsetof(climate_state_t, borrowed_normals) == 232);
static_assert(offsetof(climate_state_t, hooks) == 240);
static_assert(offsetof(climate_state_t, arena_normals) == 384);
static_assert(offsetof(climate_state_t, prepare) == 320);
static_assert(alignof(climate_state_t) == 64);
static_assert(offsetof(climate_state_t, group) == 64);
static_assert(offsetof(climate_state_t, keep_tile_x) == 256);
static_assert(sizeof(climate_state_t) <= CLIMATE_TABLES_OFFSET);

/* Plan 004 phase 07's daily weather and weekly state: run/include/climate/weather/index.h, at the week region's start. */
typedef struct climate_weather_t climate_weather_t;

/* What app_t holds: the region and the state at its start. Small on purpose, as main's frame holds app_t: one line, so it
 * sits between app_t's lined-up members with no padding of the compiler's. */
typedef struct climate_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) heap_region_t owned_region; /* 0: everything, CLIMATE_REGION_BYTES */
    climate_state_t *borrowed_state;             /* 16: at owned_region's start; nullptr before climate_init */
    heap_region_t owned_week_region;             /* 24: phase 07: the weather's, CLIMATE_WEEK_REGION_BYTES */
    climate_weather_t *borrowed_weather;         /* 40: at owned_week_region's start; nullptr before climate_init */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 48]; /* 48 */
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

/* The same, a round at a time, for a thread that must keep drawing meanwhile: begin queues the mask pages' jobs and
 * returns. Then climate_prepare_step, from the same thread, now and then. A begin while one is going finishes that one
 * first, waiting. */
void climate_prepare_begin(climate_t *climate, job_system_t *system, int32_t x_min, int32_t y_min, int32_t x_max,
                           int32_t y_max);
/* Never waits for a job still running: writes how many of the begun jobs are done and of how many, queues the tiles'
 * jobs once the pages' are done, and is true once every tile is built and the group waited on, so every job's writes are
 * this thread's to see. True at once, with 0 of 0, if nothing was begun or the climate is closed. */
bool climate_prepare_step(climate_t *climate, job_system_t *system, uint32_t *out_done, uint32_t *out_total);

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

/* ---- Currents, pressure and winds: plan 004 phase 04 ---------------------------------------------------------------- */

/* Air at a block and a day (0 to below 365, fractions allowed): the stored waves and C x strength / 255 blended between the
 * same four cells climate_sample_at blends, at the same warped point; the belt pressure at this exact y. False, writing
 * nothing, if a tile it needs is not built. Any thread. */
bool climate_air_at(const climate_t *climate, float64_t x, float64_t y, float32_t day, climate_air_t *out);

/* Master plan 6.3: T_sea = T_lat(phi) + C x strength / 255 + A_sea(phi) s_h cos(2 pi (d - 215) / 365), deg C, at the cell's
 * latitude. For a sea cell. Any thread, once climate_init has run. */
float32_t climate_sea_temperature(const climate_t *climate, const climate_cell_t *cell, float32_t day);

/* A current's flow direction for the Atlas, a unit vector along the coast: cold toward the equator, warm toward the pole.
 * (0, 0) for NONE. A sea cell's coast is its land_direction; a land cell's is the way back from its sea, -sea_direction
 * (Issues found 6 in the phase 04 file). y_centre is the cell's centre y. Pure. */
void climate_current_flow(const climate_cell_t *cell, float32_t y_centre, float32_t out[2]);

/* The curves climate_init built, for the layers and tests. */
const climate_normals_t *climate_normals(const climate_t *climate);

/* ---- Temperature, rain and storm seasons: plan 004 phase 05 ---------------------------------------------------------- */

/* A place's normals on a day: what it is usually like there. The phase 05 file's climate_normals_t, named apart from phase
 * 04's knot curves, which hold that name (Issues found in the phase 05 file). */
typedef struct climate_normals_at_t {
    float32_t t_mean_c;          /* 0: yearly mean at z */
    float32_t t_day_c;           /* 4: the normal daily mean on day, at z */
    float32_t t_range_c;         /* 8: the normal daily range on day */
    float32_t t_high_c;          /* 12: t_day + range / 2 */
    float32_t t_low_c;           /* 16: t_day - range / 2 */
    float32_t t_max_month_c;     /* 20: warmest month mean at z */
    float32_t t_min_month_c;     /* 24: coldest month mean at z */
    float32_t rain_mm_day;       /* 28: P(day) / 7, clamped at 0 */
    float32_t rain_mm_year;      /* 32: P0 x 365 / 7 */
    float32_t rain_max_mm_day;   /* 36: the wettest day of this place's year, from 73 days 5 apart: phase 06 reads it */
    float32_t snow_share;        /* 40: f_s(t_day) */
    float32_t snow_balance_z;    /* 44: blocks, blended over the corners that are not sea */
    float32_t sea_temperature_c; /* 48: for the nearest cell if it is sea, else NaN */
    float32_t hurricane_s;       /* 52: s_hur today, for the nearest cell if sea; 0 otherwise */
    uint8_t climate_class;       /* 56: the nearest cell's */
    uint8_t flags;               /* 57: the nearest cell's CLIMATE_CELL_* */
    uint8_t hurricane_season;    /* 58: 1 if today is inside the nearest cell's stored season */
    uint8_t monsoon_today;       /* 59: 1 if climate_season_monsoon_today holds for the nearest cell */
    float32_t z;                 /* 60: the height all of it is at: z, or the blended ground when z was NaN */
} climate_normals_at_t;
static_assert(sizeof(climate_normals_at_t) == 64);

/* The normals at a block and height on a day: t_mean, t_swing, t_lag_days, kappa, rain and snow_balance_z blended between
 * the four cells climate_sample_at blends, at the same warped point, then evaluated; categories from the nearest cell to the
 * warped point. z is the player's height for the HUD; NaN means the blended ground, for maps. B applies below 0 when the
 * nearest cell is neither sea nor a large lake. False, writing nothing, if a tile it needs is not built. Any thread. day
 * from 0 to below 365. */
bool climate_normals_at(const climate_t *climate, float64_t x, float64_t y, float32_t z, float32_t day,
                        climate_normals_at_t *out);
/* Plan 004 phase 07: the same for count days at one place, into out[0 .. count - 1]: the warp, the four cells and what does
 * not change with the day (the wettest day, the months) once, so each day costs a fraction of a call. Each out[i] is the
 * bytes climate_normals_at gives for days[i]. False, writing nothing, as there. */
bool climate_normals_days_at(const climate_t *climate, float64_t x, float64_t y, float32_t z, const float32_t *days,
                             uint32_t count, climate_normals_at_t *out);

/* For tests only: the hooks every tile built from now on uses (climate_open puts them back to all off). */
void climate_test_hooks_set(climate_t *climate, const climate_test_hooks_t *hooks);

/* ---- Weather systems: plan 004 phase 06 ------------------------------------------------------------------------------ */

/* The climate's world seed, once open. */
uint64_t climate_seed(const climate_t *climate);
/* Weather system kind k's dice (0 to 3: hurricane, low, high, thunder): terrain_rng_derive(seed, terrain_rng_salt(name)),
 * worked out once in climate_open. */
uint64_t climate_systems_seed(const climate_t *climate, uint8_t kind);
/* For tests only: the weather systems' hooks from now on (climate_open puts them back to all off). */
void climate_systems_hooks_set(climate_t *climate, const climate_systems_hooks_t *hooks);
/* The hooks now: all zeros in every real run. */
climate_systems_hooks_t climate_systems_hooks(const climate_t *climate);

/* The coarse land mask's page for tile (tile_x, tile_y), into out (256 heights at the cell centres): from the table under
 * the read protocol, or claimed and built there inside the keep square, or built privately outside it or while someone
 * else holds its claim. The same heights whichever way. Never waits. Any thread. */
climate_mask_source_t climate_mask_page_read(const climate_t *climate, int32_t tile_x, int32_t tile_y, float32_t *out);
/* Whether a mask page may be kept in the table: inside the mask's keep square. Any thread. */
bool climate_mask_page_kept(const climate_t *climate, int32_t tile_x, int32_t tile_y);
/* The coarse land mask's height at the 64-block cell holding (x, y) from the ground alone, one column: the same bits as the
 * page's value there. Any thread. */
float32_t climate_mask_point(const climate_t *climate, float64_t x, float64_t y);
/* The coarse land mask's height at the 64-block cell holding (x, y): the one value from the cached page under the read
 * protocol, or by claiming and building the page inside the keep square (1 KiB of arena for the copy), or outside it from
 * the ground's one column there, which is the page's value. The same number whichever way. Any thread. In the Owner, not in
 * mask/, which cannot see climate_t. */
float32_t climate_mask_height_at(const climate_t *climate, arena_t *arena, float64_t x, float64_t y);

#endif
