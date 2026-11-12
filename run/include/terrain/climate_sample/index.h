/* See run/src/terrain/climate_sample/README.md. */
#ifndef MC_RUN_TERRAIN_CLIMATE_SAMPLE_INDEX_H
#define MC_RUN_TERRAIN_CLIMATE_SAMPLE_INDEX_H

#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 006 master 2.1: the climate a column job's terrain may read, as plain numbers per 64-block cell. A seam, not a
 * dependency: terrain (row 9) may not include climate (row 8), so game, or the climate itself, copies the climate's cells in,
 * and where there is no climate terrain_climate_fallback works them out from latitude alone. Integers only, so a block never
 * flips on a last-bit difference. Each number here that mirrors one of the climate's is held to it by run_terrain_climate_sample. */
enum : int32_t {
    TERRAIN_CLIMATE_SIDE = 6,            /* cells on the sample's side: 6 by 6, 384 blocks */
    TERRAIN_CLIMATE_CELL_BLOCKS = 64,    /* CLIMATE_CELL_BLOCKS */
    TERRAIN_CLIMATE_CELL_SHIFT = 6,      /* CLIMATE_CELL_SHIFT */
    TERRAIN_CLIMATE_HALF_CELL = 32,      /* a cell's centre from its corner */
    TERRAIN_CLIMATE_WARP_BLOCKS = 32,    /* CLIMATE_WARP_BLOCKS: the warp moves a read at most this far each way */
    TERRAIN_CLIMATE_WEST_BLOCKS = 112,   /* the sample starts this far west and south of the column: 16 + 32 + 64 */
    TERRAIN_CLIMATE_NO_SNOW_LINE = 9999, /* CLIMATE_SNOW_BALANCE_NONE: snow never outlasts the summer here */
    /* Plan 004 6.10's thresholds, in centi-degrees and mm, as climate_season_classify has them. */
    TERRAIN_CLIMATE_TUNDRA_MAX_CC = 1000,
    TERRAIN_CLIMATE_TROPICAL_MIN_CC = 1800,
    TERRAIN_CLIMATE_TEMPERATE_MIN_CC = -300,
    TERRAIN_CLIMATE_CONTINENTAL_MAX_CC = 2200,
    TERRAIN_CLIMATE_WET_MONTH_MM = 60,
    TERRAIN_CLIMATE_DRY_PER_C = 20, /* P_th = 20 T + 140, mm */
    TERRAIN_CLIMATE_DRY_BASE_MM = 140,
    /* Plan 004 6.6: the lapse, 12 centi-degrees each 10 blocks (0.012 degrees a block), and the basin heat below sea level on
     * land, 25 each 10 blocks (0.025). */
    TERRAIN_CLIMATE_LAPSE_CC_PER_10 = 12,
    TERRAIN_CLIMATE_BASIN_CC_PER_10 = 25,
    /* CLIMATE_MONTH_WAVE1 = 0.98862, sin(pi/12) / (pi/12): a month's mean of the yearly wave, so t_warm and t_cold are the
     * warmest and coldest months as the climate's class sees them. A ratio of integers. */
    TERRAIN_CLIMATE_MONTH_WAVE_NUM = 98862,
    TERRAIN_CLIMATE_MONTH_WAVE_DEN = 100000,
};
static_assert(TERRAIN_CLIMATE_CELL_BLOCKS == 1 << TERRAIN_CLIMATE_CELL_SHIFT);
static_assert(TERRAIN_CLIMATE_SIDE * TERRAIN_CLIMATE_CELL_BLOCKS >= 2 * TERRAIN_CLIMATE_WEST_BLOCKS + 16 + 64);

/* climate_class: the CLIMATE_CLASS_* numbers, copied as numbers. */
enum : uint8_t {
    TERRAIN_CLIMATE_CLASS_NONE = 0, /* sea */
    TERRAIN_CLIMATE_CLASS_ICE = 1,
    TERRAIN_CLIMATE_CLASS_TUNDRA = 2,
    TERRAIN_CLIMATE_CLASS_DESERT = 3,
    TERRAIN_CLIMATE_CLASS_STEPPE = 4,
    TERRAIN_CLIMATE_CLASS_RAINFOREST = 5,
    TERRAIN_CLIMATE_CLASS_MONSOON = 6,
    TERRAIN_CLIMATE_CLASS_SAVANNA = 7,
    TERRAIN_CLIMATE_CLASS_MEDITERRANEAN = 8,
    TERRAIN_CLIMATE_CLASS_TEMPERATE = 9,
    TERRAIN_CLIMATE_CLASS_CONTINENTAL = 10,
    TERRAIN_CLIMATE_CLASS_BOREAL = 11,
    TERRAIN_CLIMATE_CLASS_COUNT = 12,
};
/* flags. */
enum : uint8_t {
    TERRAIN_CLIMATE_SEA = 1u << 0,
    TERRAIN_CLIMATE_LAKE = 1u << 1, /* a large lake: not land for the basin heat, as the climate has it */
    TERRAIN_CLIMATE_MONSOON = 1u << 2,
    TERRAIN_CLIMATE_WARM_COAST = 1u << 3, /* a warm current of the nearest sea */
    TERRAIN_CLIMATE_COLD_COAST = 1u << 4, /* a cold one */
};

typedef struct terrain_climate_cell_t {
    int32_t t_mean_cc;     /* 0: yearly mean at sea level, centi-degrees C, from det_* maths */
    int32_t t_swing_cc;    /* 4: half the yearly swing, centi-degrees, signed by hemisphere */
    int32_t rain_mm_year;  /* 8: yearly rain and snow as water, mm */
    int32_t snow_line_z;   /* 12: snow_balance_z, blocks; TERRAIN_CLIMATE_NO_SNOW_LINE when never */
    int32_t dry_month_mm;  /* 16: the driest month's rain, mm, for rainforest against savanna */
    uint8_t climate_class; /* 20: TERRAIN_CLIMATE_CLASS_* */
    uint8_t flags;         /* 21: TERRAIN_CLIMATE_SEA and the rest */
    uint8_t _pad0[2];      /* 22 */
} terrain_climate_cell_t;
static_assert(sizeof(terrain_climate_cell_t) == 24);
static_assert(alignof(terrain_climate_cell_t) == 4);
static_assert(offsetof(terrain_climate_cell_t, t_mean_cc) == 0);
static_assert(offsetof(terrain_climate_cell_t, t_swing_cc) == 4);
static_assert(offsetof(terrain_climate_cell_t, rain_mm_year) == 8);
static_assert(offsetof(terrain_climate_cell_t, snow_line_z) == 12);
static_assert(offsetof(terrain_climate_cell_t, dry_month_mm) == 16);
static_assert(offsetof(terrain_climate_cell_t, climate_class) == 20);
static_assert(offsetof(terrain_climate_cell_t, flags) == 21);
static_assert(offsetof(terrain_climate_cell_t, _pad0) == 22);

/* 6 by 6 cells round a column, row 0 at the south, x fastest: cells[j][i] has its south-west corner at
 * (origin_x + 64 i, origin_y + 64 j). Filled once per column job; read only after. */
typedef struct terrain_climate_sample_t {
    int64_t origin_x;                                                         /* 0: blocks, a multiple of 64 */
    int64_t origin_y;                                                         /* 8 */
    terrain_climate_cell_t cells[TERRAIN_CLIMATE_SIDE][TERRAIN_CLIMATE_SIDE]; /* 16: 864 bytes */
    uint8_t from_climate; /* 880: 1 when every cell came from the climate's tiles, 0 when any is the fallback's */
    uint8_t warp_off;     /* 881: 1 for tests: terrain_climate_at reads at the point itself */
    uint8_t _pad0[2];     /* 882 */
    uint32_t warp_seed;   /* 884: terrain_climate_warp_seed of the world's seed */
} terrain_climate_sample_t;
static_assert(sizeof(terrain_climate_sample_t) == 888);
static_assert(alignof(terrain_climate_sample_t) == 8);
static_assert(offsetof(terrain_climate_sample_t, origin_x) == 0);
static_assert(offsetof(terrain_climate_sample_t, origin_y) == 8);
static_assert(offsetof(terrain_climate_sample_t, cells) == 16);
static_assert(offsetof(terrain_climate_sample_t, from_climate) == 880);
static_assert(offsetof(terrain_climate_sample_t, warp_off) == 881);
static_assert(offsetof(terrain_climate_sample_t, _pad0) == 882);
static_assert(offsetof(terrain_climate_sample_t, warp_seed) == 884);

/* What terrain_climate_at gives for one column at one height. */
typedef struct terrain_climate_point_t {
    int32_t t_mean_cc;     /* 0: the yearly mean at z, centi-degrees: lapse, and basin heat below 0 on land */
    int32_t t_warm_cc;     /* 4: the warmest month's mean at z */
    int32_t t_cold_cc;     /* 8: the coldest month's mean at z */
    int32_t rain_mm_year;  /* 12 */
    int32_t dry_month_mm;  /* 16 */
    int32_t snow_line_z;   /* 20: blocks; TERRAIN_CLIMATE_NO_SNOW_LINE when never */
    uint8_t climate_class; /* 24: the nearest cell's */
    uint8_t flags;         /* 25: the nearest cell's */
    uint8_t from_climate;  /* 26: the sample's */
    uint8_t _pad0[1];      /* 27 */
} terrain_climate_point_t;
static_assert(sizeof(terrain_climate_point_t) == 28);

/* Where the sample for a column whose south-west block is (west_x, south_y) starts: 112 blocks west and south, down to a
 * cell's corner. A point's sample is that of (x - 8, y - 8). */
void terrain_climate_sample_origin(int64_t west_x, int64_t south_y, int64_t *out_x, int64_t *out_y);

/* The 32-bit warp seed of a world's seed. Pure. */
uint32_t terrain_climate_warp_seed(uint64_t seed);

/* v rounded to the nearest integer, halves to even, clamped to int32_t; NaN gives 0. Round to nearest must be the mode, as
 * math_init checks. */
int32_t terrain_climate_round(float64_t v);

/* One cell from the climate's numbers in its own units (deg C, mm, blocks; snow_balance_z 9999 for never): the one place the
 * climate's floats become the sample's integers. */
void terrain_climate_cell_make(float64_t t_mean_c, float64_t t_swing_c, float64_t rain_mm_year,
                               float64_t snow_balance_z, float64_t dry_month_mm, uint8_t climate_class, uint8_t flags,
                               terrain_climate_cell_t *out);

/* Plan 004 6.10's first match, in integers: what a place's numbers make it. Pure. */
uint8_t terrain_climate_classify(int32_t t_mean_cc, int32_t t_warm_cc, int32_t t_cold_cc, int32_t rain_mm_year,
                                 int32_t dry_month_mm, bool monsoon);

/* Where terrain_climate_at reads for (x, y): moved up to 32 blocks each way by two octaves of hashed value noise, 256 and 128
 * blocks, all in integers, spread as the climate spreads its warp. Pure. */
void terrain_climate_warp(uint32_t warp_seed, int64_t x, int64_t y, int64_t *out_x, int64_t *out_y);

/* The climate at column (x, y) and height z: the four cells round the warped point blended bilinearly between centres, in
 * 64ths, rounded half up; temperatures at z; the snow line blended over the corners that are not sea; class and flags of
 * the nearest cell. A read past the sample's cells takes its edge. Pure. */
terrain_climate_point_t terrain_climate_at(const terrain_climate_sample_t *sample, int64_t x, int64_t y, int32_t z);

/* Plan 006 master 2.2: one cell where there is no climate, from the latitude of its centre's y alone: plan 004 6.6's
 * temperature with kappa 0.5 and no current, 6.7's rain belt at the latitude with moisture 0.6 and orography 1 (no waves, so
 * the driest month is a twelfth), the snow line where the warmest month's mean reaches 0 with the lapse, and the class at sea
 * level. Land. Pure and defined for every y. */
void terrain_climate_fallback_cell(int64_t centre_y, terrain_climate_cell_t *out);

/* A whole sample from the fallback, for the column at (west_x, south_y). from_climate is 0. */
void terrain_climate_fallback(uint64_t seed, int64_t west_x, int64_t south_y, terrain_climate_sample_t *out);

#endif
