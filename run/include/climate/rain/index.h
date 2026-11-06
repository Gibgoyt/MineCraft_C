/* See run/src/climate/rain/README.md. */
#ifndef MC_RUN_CLIMATE_RAIN_INDEX_H
#define MC_RUN_CLIMATE_RAIN_INDEX_H

#include "climate/cell/index.h"
#include "climate/normals/index.h"
#include "types/index.h"

#include <stdint.h>

/* Master plan 6.7: the upwind walk, moisture and the orographic factor. One home each. */
enum : uint32_t {
    CLIMATE_WALK_STEPS_WINDOW = 16u, /* 6.7: 16 steps of 128 reading the window's kinds */
    CLIMATE_WALK_STEPS_MAX = 64u,    /* 6.7: on over the mask to 8 km, 8192 / 128 */
};
static const float32_t CLIMATE_WALK_STEP_BLOCKS = 128.0f32;  /* 6.7 */
static const float32_t CLIMATE_WALK_NONE_BLOCKS = 8192.0f32; /* d_up with no water met by step 64 */
static const float32_t CLIMATE_WALK_CALM = 1.0e-3f32;        /* blocks a second: below it a wind has no direction */
static const float32_t CLIMATE_MOISTURE_SCALE_BLOCKS = 3000.0f32; /* 6.7: e^(-d_up / 3000) */
static const float32_t CLIMATE_MOISTURE_MIN = 0.15f32;            /* 6.7 */
static const float32_t CLIMATE_MOISTURE_WARM = 1.25f32;           /* 6.7 */
static const float32_t CLIMATE_MOISTURE_COLD = 0.4f32;            /* 6.7 */
static const float32_t CLIMATE_MOISTURE_COLD_BLOCKS = 800.0f32;   /* 6.7 */
static const float32_t CLIMATE_OROGRAPHIC_GAIN = 8.0f32;          /* 6.7: 1 + 8 s_w */
static const float32_t CLIMATE_OROGRAPHIC_RISE_BLOCKS = 256.0f32; /* 6.7: s_w over the last 256 */
static const float32_t CLIMATE_SHADOW_BLOCKS = 400.0f32;          /* 6.7: (h_up - z) / 400 */
static const float32_t CLIMATE_OROGRAPHIC_MIN = 0.15f32;          /* 6.7 */
static const float32_t CLIMATE_OROGRAPHIC_MAX = 2.5f32;           /* 6.7 */
static const float32_t CLIMATE_DAYS_A_WEEK = 7.0f32;
static const float32_t CLIMATE_DAYS_A_YEAR = 365.0f32;

/* For tests only: the assumptions the master plan's South Africa numbers make, stated by the test instead of hidden. Held
 * by the climate's state, set through climate_test_hooks_set, all zeros (off) in every real run and after every open. */
typedef struct climate_test_hooks_t {
    float32_t wind[2];      /* 0: if either is not 0, the rain walk and the monsoon test use this wind on every day */
    float32_t belt_mm_year; /* 8: if above 0, P_belt is this constant */
    uint8_t freeze_seasons; /* 12: delta taken as 0 in psi */
    uint8_t orographic_one; /* 13: O forced to 1 */
    uint8_t _pad0[2];       /* 14 */
} climate_test_hooks_t;
static_assert(sizeof(climate_test_hooks_t) == 16);

/* The walk's answer for one cell and one wind. */
typedef struct climate_rain_walk_t {
    float32_t d_up_blocks; /* 0: 128 (k_w - 1), 0 on water, 8192 if none met */
    float32_t h_up;        /* 4: highest ground met before water, blocks, sea as 0; z_ground at the least */
    float32_t s_w;         /* 8: rise into the wind over the last 256 blocks, 0 if falling */
    uint8_t water_current; /* 12: CLIMATE_CURRENT_* of the water met, NONE if over the mask or none met */
    uint8_t met_water;     /* 13: 1 if water was met by step 64 */
    uint8_t steps;         /* 14: steps taken, the one on water included */
    uint8_t _pad0[1];      /* 15 */
} climate_rain_walk_t;
static_assert(sizeof(climate_rain_walk_t) == 16);

/* Walks upwind from tile cell (i, j) of the window (each 128 to 143) against the unit wind (wx, wy), z_ground being that
 * cell's. Steps 1 to 16 read the window's kinds and its sea cells' currents; steps 17 to 64 read heights only, as the
 * coarse land mask has them, so water there (height <= 0) has no current and any size counts. A tile cell is 8192 blocks
 * and half a cell from the window's edge and step 64 is 8192 away, so every step is inside the window. Pure. */
void climate_rain_walk(const climate_window_t *window, uint32_t i, uint32_t j, float32_t z_ground, float32_t wx,
                       float32_t wy, climate_rain_walk_t *out);

/* 6.7's M from a walk and the cell: max(0.15, e^(-d_up / 3000)) times 0.4 within 800 blocks of a cold current, else 1.25
 * if the air left over a warm current, else 1: in that order. */
float32_t climate_rain_moisture(const climate_rain_walk_t *walk, const climate_cell_t *cell);

/* 6.7's O: clamp(1 + 8 s_w - max(0, h_up - z) / 400, 0.15, 2.5). */
float32_t climate_rain_orographic(const climate_rain_walk_t *walk, float32_t z_ground);

/* P_belt(|psi|) in mm a year, from the spline, or the hooks' constant when it is set (hooks may be nullptr). */
float32_t climate_rain_belt_mm_year(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                                    float32_t psi_deg);

/* The wind a cell has on a day for the walk and the monsoon test: the stored waves, or the hooks' fixed wind. */
void climate_rain_wind(const climate_cell_t *cell, const climate_test_hooks_t *hooks, float32_t day, float32_t out[2]);

/* psi on a day for the belts: phase 04's phi - 0.5 delta(d - 30), or phi with the seasons frozen. */
float32_t climate_rain_psi(const climate_test_hooks_t *hooks, float32_t lat_deg, float32_t day);

/* The walk tile cell (i, j) takes on one day with wind (wx, wy), the no-wind case included (6.7: the cell's own sea
 * distance, h_up = z_ground, s_w = 0, the cell's current), and the cell's day's rain from it in mm a week. Sea and large
 * lake cells walk nowhere: d_up = 0 with no current, and O = 1. Pure. */
float32_t climate_rain_day_mm_week(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                                   const climate_window_t *window, uint32_t i, uint32_t j, const climate_cell_t *cell,
                                   float32_t day, climate_rain_walk_t *out_walk);

/* Fills rain_mm_week of a tile's 256 cells, whose phase 03 and 04 fields are set: the rain on the 12 fit days, fitted to
 * two waves. hooks may be nullptr. Pure, allocates nothing. */
void climate_rain_build_tile(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                             const climate_window_t *window, climate_cell_t *cells);

#endif
