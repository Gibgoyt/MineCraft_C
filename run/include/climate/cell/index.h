/* See run/src/climate/cell/README.md. */
#ifndef MC_RUN_CLIMATE_CELL_INDEX_H
#define MC_RUN_CLIMATE_CELL_INDEX_H

#include "arena/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : int32_t {
    CLIMATE_CELL_BLOCKS = 64, /* blocks on a cell's side: master plan 1 */
    CLIMATE_CELL_SHIFT = 6,
    CLIMATE_TILE_CELLS = 16, /* cells on a tile's side: master plan 1 */
    CLIMATE_TILE_BLOCKS = 1024,
    CLIMATE_TILE_SHIFT = 10,
    CLIMATE_MARGIN_CELLS = 128,      /* the 8 km margin, 8192 / 64: master plan 6.2 */
    CLIMATE_WINDOW_CELLS = 272,      /* 16 + 2 * 128 on a side */
    CLIMATE_WINDOW_TILES = 17,       /* pages on a side of the window */
    CLIMATE_WARP_BLOCKS = 32,        /* the warp's reach: master plan 6.9 */
    CLIMATE_READ_REACH_BLOCKS = 96,  /* a read's reach past its point: one cell's blend plus the warp, 64 + 32 */
    CLIMATE_GROUND_SAMPLES_SIDE = 4, /* phase 03: z_ground is the mean of 4 by 4 samples, 16 blocks apart */
    CLIMATE_GROUND_SAMPLE_STEP = 16,
    CLIMATE_TILE_CELL_COUNT = CLIMATE_TILE_CELLS * CLIMATE_TILE_CELLS,            /* 256 */
    CLIMATE_WINDOW_CELL_COUNT = CLIMATE_WINDOW_CELLS * CLIMATE_WINDOW_CELLS,      /* 73,984 */
    CLIMATE_TILE_SAMPLES_SIDE = CLIMATE_TILE_CELLS * CLIMATE_GROUND_SAMPLES_SIDE, /* 64 */
};
static_assert(CLIMATE_CELL_BLOCKS == 1 << CLIMATE_CELL_SHIFT);
static_assert(CLIMATE_TILE_BLOCKS == 1 << CLIMATE_TILE_SHIFT);
static_assert(CLIMATE_TILE_BLOCKS == CLIMATE_CELL_BLOCKS * CLIMATE_TILE_CELLS);
static_assert(CLIMATE_WINDOW_CELLS == CLIMATE_TILE_CELLS + 2 * CLIMATE_MARGIN_CELLS);
static_assert(CLIMATE_WINDOW_CELLS == CLIMATE_WINDOW_TILES * CLIMATE_TILE_CELLS);
static_assert(CLIMATE_GROUND_SAMPLES_SIDE * CLIMATE_GROUND_SAMPLE_STEP == CLIMATE_CELL_BLOCKS);
static_assert(CLIMATE_READ_REACH_BLOCKS == CLIMATE_CELL_BLOCKS + CLIMATE_WARP_BLOCKS);

static const float32_t CLIMATE_SEA_DISTANCE_MAX = 8192.0f32; /* blocks: master plan 6.2 */
static const float32_t CLIMATE_KAPPA_SCALE = 1500.0f32;      /* blocks: master plan 6.2 */
/* Phase 03, Issues found 17: water this deep somewhere makes its whole body sea. The terrain's sea bed falls below -30 from
 * continentalness about -0.57 (DENSITY_KNOTS_CONTINENTAL); inland only the deepest hill hollows reach it. */
static const float32_t CLIMATE_SEA_FLOOR_BLOCKS = -30.0f32;
static const float32_t CLIMATE_WARP_FREQUENCY = 1.0f32 / 256.0f32; /* phase 03: cycles per block, first octave */

/* One 64 by 64 block cell's climate normals: worked out once from the seed, never saved, never changed by play. Master plan
 * 6.9's struct, with land_distance and land_direction in its spare bytes (phase 03 file). */
typedef struct climate_cell_t {
    float32_t lat_deg;           /* 0: phi at the cell's centre */
    float32_t z_ground;          /* 4: mean of terrain_height_estimate over the cell */
    float32_t sea_distance;      /* 8: blocks to the nearest connected sea, at most 8192; 0 in the sea */
    float32_t kappa;             /* 12: continentality, 0 on the coast to 1 deep inland */
    float32_t sea_direction[2];  /* 16: unit vector toward that sea; (0, 0) in the sea or with none in reach */
    float32_t t_mean;            /* 24: deg C, the yearly mean (phase 05) */
    float32_t t_swing;           /* 28: deg C, A in 6.6, signed by hemisphere (phase 04) */
    float32_t t_lag_days;        /* 32: l in 6.6 (phase 04) */
    float32_t rain_mm_week[5];   /* 36: P0, P1c, P1s, P2c, P2s (phase 05) */
    float32_t wind[6];           /* 56: u0, u1c, u1s, v0, v1c, v1s, blocks a second (phase 04) */
    float32_t pressure[3];       /* 80: dp_heat as p0, p1c, p1s, hPa (phase 04) */
    float32_t snow_balance_z;    /* 92: height above which snow outlasts summer (phase 05) */
    uint8_t current;             /* 96: CLIMATE_CURRENT_*, of the nearest sea (phase 04) */
    uint8_t current_strength;    /* 97: 0 to 255 (phase 04) */
    uint8_t climate_class;       /* 98: CLIMATE_CLASS_* (phase 05) */
    uint8_t flags;               /* 99: CLIMATE_CELL_* */
    uint8_t hurricane_days[2];   /* 100: first and last day of hurricane season, 255 if none (phase 05) */
    uint8_t _pad0[2];            /* 102 */
    float32_t land_distance;     /* 104: phase 03: blocks to the nearest land, at most 8192; 0 on land */
    float32_t land_direction[2]; /* 108: phase 03: unit vector toward that land; (0, 0) on land or with none */
    uint8_t _pad1[12];           /* 116: still room to grow */
} climate_cell_t;
static_assert(sizeof(climate_cell_t) == 128);
static_assert(alignof(climate_cell_t) == 4);
static_assert(offsetof(climate_cell_t, lat_deg) == 0);
static_assert(offsetof(climate_cell_t, z_ground) == 4);
static_assert(offsetof(climate_cell_t, sea_distance) == 8);
static_assert(offsetof(climate_cell_t, kappa) == 12);
static_assert(offsetof(climate_cell_t, sea_direction) == 16);
static_assert(offsetof(climate_cell_t, t_mean) == 24);
static_assert(offsetof(climate_cell_t, t_swing) == 28);
static_assert(offsetof(climate_cell_t, t_lag_days) == 32);
static_assert(offsetof(climate_cell_t, rain_mm_week) == 36);
static_assert(offsetof(climate_cell_t, wind) == 56);
static_assert(offsetof(climate_cell_t, pressure) == 80);
static_assert(offsetof(climate_cell_t, snow_balance_z) == 92);
static_assert(offsetof(climate_cell_t, current) == 96);
static_assert(offsetof(climate_cell_t, current_strength) == 97);
static_assert(offsetof(climate_cell_t, climate_class) == 98);
static_assert(offsetof(climate_cell_t, flags) == 99);
static_assert(offsetof(climate_cell_t, hurricane_days) == 100);
static_assert(offsetof(climate_cell_t, _pad0) == 102);
static_assert(offsetof(climate_cell_t, land_distance) == 104);
static_assert(offsetof(climate_cell_t, land_direction) == 108);
static_assert(offsetof(climate_cell_t, _pad1) == 116);

enum : uint8_t {
    CLIMATE_CELL_SEA = 1u << 0,
    CLIMATE_CELL_LAKE = 1u << 1,            /* a lake of at least CLIMATE_LAKE_LARGE_CELLS */
    CLIMATE_CELL_MONSOON = 1u << 2,         /* phase 05 */
    CLIMATE_CELL_HURRICANE_COAST = 1u << 3, /* phase 05 */
    CLIMATE_CELL_LAKE_SMALL = 1u << 4,      /* phase 03: a smaller lake, land for every climate purpose; for the maps */
    CLIMATE_DAY_NONE = 255u,                /* hurricane_days with no season */
};

/* Heights in blocks at count_x by count_y columns, step apart, x fastest. Must be a pure function of its arguments, and
 * safe on any thread. The climate's one way of seeing the ground. */
typedef void (*climate_ground_fn_t)(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                    uint32_t count_x, uint32_t count_y, float32_t *heights);

/* What phase 03's fields blend to at a block: warped, bilinear, latitude exact. */
typedef struct climate_sample_t {
    float32_t lat_deg;          /* 0: exact, from y */
    float32_t z_ground;         /* 4 */
    float32_t sea_distance;     /* 8 */
    float32_t kappa;            /* 12 */
    float32_t sea_direction[2]; /* 16: blended, then made unit again; (0, 0) if it blends to nearly nothing */
    float32_t land_distance;    /* 24 */
    uint8_t flags;              /* 28: the nearest cell's, to the warped point */
    uint8_t _pad0[3];           /* 29 */
} climate_sample_t;
static_assert(sizeof(climate_sample_t) == 32);

/* Every cell of tile (tile_x, tile_y), into cells (256, south row first, x fastest), from its window: 272 by 272 cell-centre
 * heights, row 0 at the south, window cell (128, 128) the tile's south-west cell. ground gives the 64 by 64 samples for
 * z_ground. Pure: the same bytes whatever else exists. arena is the worker's Job arena; about 2.4 MiB is taken from it. */
void climate_cell_build_tile(arena_t *arena, climate_ground_fn_t ground, const void *ground_context, int32_t tile_x,
                             int32_t tile_y, const float32_t *window_heights, climate_cell_t *cells);

/* The bilinear blend of four cells, south-west, south-east, north-west, north-east, at fractions t_x and t_y (0 to 1):
 * along x, then y. lat_deg is left at 0 for the caller, who knows the exact y. Pure. */
void climate_cell_blend(const climate_cell_t *const corners[4], float32_t t_x, float32_t t_y, climate_sample_t *out);

#endif
