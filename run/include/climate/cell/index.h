/* See run/src/climate/cell/README.md. */
#ifndef MC_RUN_CLIMATE_CELL_INDEX_H
#define MC_RUN_CLIMATE_CELL_INDEX_H

#include "arena/index.h"
#include "climate/harmonic/index.h"
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
    CLIMATE_HALF_CELL_BLOCKS = CLIMATE_CELL_BLOCKS / 2, /* a cell's centre from its corner: 32 */
};
/* Plan 004 phase 11: the shapes every climate file shares, one home each (vision rule 5). */
enum : uint32_t {
    CLIMATE_CORNERS = 4u, /* the four cells a bilinear read blends: south-west, south-east, north-west, north-east */
    CLIMATE_RAIN_COEFFICIENTS = CLIMATE_HARMONIC2_COEFFICIENTS,  /* rain_mm_week: P0, then two waves, 6.9 */
    CLIMATE_WAVE1_COEFFICIENTS = CLIMATE_HARMONIC1_COEFFICIENTS, /* pressure, and u or v alone: a mean and one wave */
    CLIMATE_WIND_COEFFICIENTS = 2u * CLIMATE_HARMONIC1_COEFFICIENTS, /* wind: u, then v, 6.9 */
    CLIMATE_ALIGN_BYTES = 64u,                                       /* bytes: arena blocks start on a cache line */
    CLIMATE_CURRENT_STRENGTH_MAX = 255u,                             /* current_strength at full strength: a byte */
    CLIMATE_KEY_HALF_BITS = 32u, /* a 64-bit key or id folds as its two 32-bit halves */
};
#define CLIMATE_CURRENT_STRENGTH_MAX_F32 ((float32_t)CLIMATE_CURRENT_STRENGTH_MAX)
/* A vector shorter than this, in its own units, has no direction to keep. */
static const float32_t CLIMATE_DIRECTION_EPSILON = 1.0e-6f32;
static_assert(CLIMATE_CELL_BLOCKS == 1 << CLIMATE_CELL_SHIFT);
static_assert(CLIMATE_TILE_BLOCKS == 1 << CLIMATE_TILE_SHIFT);
static_assert(CLIMATE_TILE_BLOCKS == CLIMATE_CELL_BLOCKS * CLIMATE_TILE_CELLS);
static_assert(CLIMATE_WINDOW_CELLS == CLIMATE_TILE_CELLS + 2 * CLIMATE_MARGIN_CELLS);
static_assert(CLIMATE_WINDOW_CELLS == CLIMATE_WINDOW_TILES * CLIMATE_TILE_CELLS);
static_assert(CLIMATE_GROUND_SAMPLES_SIDE * CLIMATE_GROUND_SAMPLE_STEP == CLIMATE_CELL_BLOCKS);
static_assert(CLIMATE_READ_REACH_BLOCKS == CLIMATE_CELL_BLOCKS + CLIMATE_WARP_BLOCKS);

/* Blocks: master plan 6.2. A macro since phase 11, so the layer scales can be built from it at compile time. */
#define CLIMATE_SEA_DISTANCE_MAX 8192.0f32
static const float32_t CLIMATE_KAPPA_SCALE = 1500.0f32; /* blocks: master plan 6.2 */
/* Phase 03, Issues found 17: water this deep somewhere makes its whole body sea. The terrain's sea bed falls below -30 from
 * continentalness about -0.57 (DENSITY_KNOTS_CONTINENTAL); inland only the deepest hill hollows reach it. */
static const float32_t CLIMATE_SEA_FLOOR_BLOCKS = -30.0f32;
static const float32_t CLIMATE_WARP_FREQUENCY = 1.0f32 / 256.0f32; /* phase 03: cycles per block, first octave */
/* Phase 03: a raw warp value this far from 0 is -1 or 1, the same spread as the density's detail field. */
static const float64_t CLIMATE_WARP_LIMIT = 0.35f64;
/* Phase 03: the smooth clamp u (1.5 - 0.5 u^2), which is 1 with zero slope at u = 1. */
static const float64_t CLIMATE_WARP_CUBIC = 1.5f64;

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

/* One tile's window: the tile plus CLIMATE_MARGIN_CELLS on every side, CLIMATE_WINDOW_CELLS a side, south row first, x
 * fastest. All in the building worker's Job arena, gone when the job ends. Built by climate_cell_build_tile, read by
 * climate_air_build_tile (plan 004 phase 04). Window cell (i, j) has its centre at x = (tile_x - 8) 1024 + 64 i + 32,
 * y = (tile_y - 8) 1024 + 64 j + 32; the tile's own cells are i, j = 128 to 143. */
typedef struct climate_window_t {
    const float32_t *heights;       /* 0: ground at each cell centre, blocks */
    const uint8_t *kind;            /* 8: CLIMATE_SEA_KIND_* */
    const uint32_t *sea_distance2;  /* 16: cells^2, to the nearest sea; CLIMATE_SEA_FAR with none */
    const uint32_t *sea_nearest;    /* 24: window index of that sea cell */
    const uint32_t *land_distance2; /* 32: cells^2, to the nearest land or small lake */
    const uint32_t *land_nearest;   /* 40 */
    int32_t tile_x;                 /* 48 */
    int32_t tile_y;                 /* 52 */
} climate_window_t;
static_assert(sizeof(climate_window_t) == 56);

/* Every cell of tile (tile_x, tile_y), into cells (256, south row first, x fastest), from its window: 272 by 272 cell-centre
 * heights, row 0 at the south, window cell (128, 128) the tile's south-west cell. ground gives the 64 by 64 samples for
 * z_ground. Pure: the same bytes whatever else exists. arena is the worker's Job arena; about 2.4 MiB is taken from it, and
 * out_window points into it, for the rest of the job. */
void climate_cell_build_tile(arena_t *arena, climate_ground_fn_t ground, const void *ground_context, int32_t tile_x,
                             int32_t tile_y, const float32_t *window_heights, climate_cell_t *cells,
                             climate_window_t *out_window);

/* A squared distance in cells to blocks: min(8192, max(0, 64 sqrt(D) - 32)), the coast half a cell in from the marked
 * centre; CLIMATE_SEA_DISTANCE_MAX for CLIMATE_SEA_FAR. What every cell's distances are. Pure. */
float32_t climate_cell_distance_blocks(uint32_t distance2);

/* Continentality from the sea distance in blocks: 1 - e^(-d / 1500), master plan 6.2. Pure. */
float32_t climate_cell_kappa(float32_t sea_distance);

/* The bilinear blend of four cells, south-west, south-east, north-west, north-east, at fractions t_x and t_y (0 to 1):
 * along x, then y. lat_deg is left at 0 for the caller, who knows the exact y. Pure. */
void climate_cell_blend(const climate_cell_t *const corners[4], float32_t t_x, float32_t t_y, climate_sample_t *out);

#endif
