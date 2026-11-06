/* See run/src/climate/air/README.md. */
#ifndef MC_RUN_CLIMATE_AIR_INDEX_H
#define MC_RUN_CLIMATE_AIR_INDEX_H

#include "arena/index.h"
#include "climate/cell/index.h"
#include "climate/harmonic/index.h"
#include "climate/normals/index.h"
#include "types/index.h"

#include <stdint.h>

/* Currents, master plan 6.3 */
static const float32_t CLIMATE_CURRENT_REACH = 600.0f32; /* blocks offshore a current is felt */
static const float32_t CLIMATE_CURRENT_FULL_WITHIN =
    32.0f32; /* phase 04: a sea cell next to land, half a cell, is full */
static const float32_t CLIMATE_CURRENT_LAND_SCALE = 600.0f32; /* blocks: e^(-d_s / 600) on land */
static const float32_t CLIMATE_CURRENT_SIDE = 0.5f32;         /* |n_x| over this picks a west or east coast */
static const float32_t CLIMATE_CURRENT_WARM_C = 4.0f32;       /* deg C */
static const float32_t CLIMATE_CURRENT_COLD_C = -5.0f32;      /* deg C */
static const float32_t CLIMATE_CURRENT_WEAK = 0.5f32;         /* phase 04: "warm, weak" is half strength */
static const float32_t CLIMATE_CURRENT_BANDS_DEG[3] = {15.0f32, 45.0f32, 65.0f32};
static const float32_t CLIMATE_SEA_PEAK_DAY = 215.0f32; /* the day the sea is warmest in the north */

/* Pressure, master plan 6.4 */
static const float32_t CLIMATE_HEAT_LOW_HPA_PER_C = -0.6f32; /* hPa per deg C of seasonal swing, times kappa */

/* Swing and lag, master plan 6.6 (phase 05's terms, worked out here first because heat lows need them) */
static const float32_t CLIMATE_SUMMER_DAY = 170.0f32; /* the swing's peak before the lag */
static const float32_t CLIMATE_LAG_BASE_DAYS = 25.0f32;
static const float32_t CLIMATE_LAG_SEA_DAYS = 20.0f32; /* times 1 - kappa */

/* Winds, master plan 6.5 */
static const float32_t CLIMATE_WIND_LAG_DAYS = 30.0f32; /* psi uses delta(d - 30) */
static const float32_t CLIMATE_WIND_SHIFT = 0.5f32;     /* psi moves half as far as the sun */
static const float32_t CLIMATE_TRADE_SPEED = 6.0f32;    /* blocks a second */
static const float32_t CLIMATE_WESTERLY_SPEED = 8.0f32;
static const float32_t CLIMATE_POLAR_SPEED = 5.0f32;
static const float32_t CLIMATE_TRADE_DIR[2] = {-1.0f32, -0.4f32}; /* normalised at use: Issues found 2 */
static const float32_t CLIMATE_WESTERLY_DIR[2] = {1.0f32, 0.3f32};
static const float32_t CLIMATE_POLAR_DIR[2] = {-1.0f32, -0.2f32};
static const float32_t CLIMATE_BELT_EDGES_DEG[2] = {30.0f32, 60.0f32};
static const float32_t CLIMATE_BELT_BLEND_DEG = 6.0f32;
static const float32_t CLIMATE_HEMISPHERE_BLEND_DEG = 3.0f32; /* phase 04: sigma(psi) over +-3 degrees */
static const float32_t CLIMATE_DOLDRUMS_DEG = 5.0f32;
static const float32_t CLIMATE_DOLDRUMS_SPEED = 0.4f32;
static const float32_t CLIMATE_HORSE_DEG = 2.0f32; /* phase 04: full calm within 2 degrees of 30 */
static const float32_t CLIMATE_HORSE_SPEED = 0.5f32;
static const float32_t CLIMATE_CALM_RAMP_DEG = 3.0f32; /* phase 04 */
static const float32_t CLIMATE_PRESSURE_TURN_DEG = 70.0f32;
static const float32_t CLIMATE_PRESSURE_WIND_K = 900.0f32; /* blocks^2 per hPa per second */
static const float32_t CLIMATE_ALTITUDE_SCALE = 800.0f32;  /* blocks */
static const float32_t CLIMATE_ALTITUDE_MAX = 2.5f32;
static const float32_t CLIMATE_STEER = 0.7f32;

/* The three kinds of current, master plan 6.3. */
enum : uint8_t {
    CLIMATE_CURRENT_NONE = 0, /* neutral, the master plan's third kind */
    CLIMATE_CURRENT_WARM = 1,
    CLIMATE_CURRENT_COLD = 2,
};

/* Air at a block and a day, blended between cells as phase 03 blends (the same warp, the same four cells). */
typedef struct climate_air_t {
    float32_t wind[2];           /* 0: u east, v north, blocks a second, from the stored waves */
    float32_t pressure_hpa;      /* 8: p_belt + dp_heat, no weather systems (phase 06 adds them) */
    float32_t pressure_belt_hpa; /* 12: at this exact y and day */
    float32_t pressure_heat_hpa; /* 16: from the stored wave */
    float32_t current_c;         /* 20: C x strength / 255, blended: what phase 05 adds to temperature */
    uint8_t current;             /* 24: the nearest cell's kind, to the warped point */
    uint8_t current_strength;    /* 25: the same cell's */
    uint8_t _pad0[2];            /* 26 */
} climate_air_t;
static_assert(sizeof(climate_air_t) == 28);

/* What climate_air_build_tile worked out on the 12 fit days for each of a tile's cells, before the fit: for tests and the
 * bench. steer_off is read: non-zero builds with steering turned off (a test's hook). */
typedef struct climate_air_trace_t {
    float32_t belt[CLIMATE_TILE_CELL_COUNT][CLIMATE_FIT_DAYS][2];          /* 0: the belt wind alone */
    float32_t pressure_wind[CLIMATE_TILE_CELL_COUNT][CLIMATE_FIT_DAYS][2]; /* 24576: the pressure wind alone */
    float32_t wind[CLIMATE_TILE_CELL_COUNT][CLIMATE_FIT_DAYS][2];          /* 49152: after steering and altitude */
    float32_t heat_low[CLIMATE_TILE_CELL_COUNT][CLIMATE_FIT_DAYS];         /* 73728: dp_heat, hPa */
    uint32_t steer_off;                                                    /* 86016 */
} climate_air_trace_t;
static_assert(sizeof(climate_air_trace_t) == 86020);

/* ---- Pieces, each with its worked example in the phase 04 file ------------------------------------------------------- */

/* e(y) = sgn cos(pi y / 2Y): +1 where latitude rises with y, -1 in the strips mirrored past a pole. +1 where cos is 0. */
float32_t climate_air_mirror_sign(float64_t y);
/* Toward the pole as a world direction, (0, e sgn phi); (0, 0) on the equator itself. */
void climate_air_poleward(float64_t y, float32_t out[2]);
/* 3t^2 - 2t^3 for t in 0 to 1, 0 below and 1 above. */
float32_t climate_air_smoothstep(float32_t t);
/* f_dol and f_horse: the calms' speed factors at a wind latitude. */
float32_t climate_air_doldrums(float32_t psi_deg);
float32_t climate_air_horse(float32_t psi_deg);
/* psi = phi - 0.5 delta(d - 30). */
float32_t climate_air_wind_latitude(float64_t phi_deg, float32_t day);
/* The belt wind at a wind latitude, in a strip of mirror sign e. */
void climate_air_belt_wind(float32_t psi_deg, float32_t mirror, float32_t out[2]);
/* The pressure wind's turn in degrees, anticlockwise positive: -70 sgn phi e; 0 on the equator. */
float32_t climate_air_turn_deg(float64_t phi_deg, float32_t mirror);
/* R_theta v. */
void climate_air_rotate(const float32_t in[2], float32_t degrees, float32_t out[2]);
/* k_p R_theta(-gradient), gradient in hPa a block; (0, 0) when the turn is 0, on the equator. */
void climate_air_pressure_wind(const float32_t gradient[2], float32_t turn_deg, float32_t out[2]);
/* w - 0.7 s (w . g^) g^, s = min(1, |g|); unchanged when |g| < 1e-6. Never faster. */
void climate_air_steer(const float32_t wind[2], const float32_t slope[2], float32_t out[2]);
/* min(2.5, 1 + max(0, z) / 800). */
float32_t climate_air_altitude_factor(float32_t z);

/* The kind a sea cell's current has by the master plan's table, from |phi| and n_x toward its nearest land; *out_weight is
 * 0.5 for "warm, weak" and 1 otherwise. */
uint8_t climate_air_current_kind(float32_t lat_deg, float32_t n_x, float32_t *out_weight);
/* round(255 clamp((600 - d_l) / 568, 0, 1) weight): a sea cell's strength. */
uint8_t climate_air_sea_strength(float32_t land_distance, float32_t weight);
/* round(255 e^(-d_s / 600) weight): a land cell's. */
uint8_t climate_air_land_strength(float32_t sea_distance, float32_t weight);
/* C: +4 warm, -5 cold, 0 none. */
float32_t climate_air_current_c(uint8_t kind);
/* A current's flow for the Atlas, along the coast: warm toward the pole, cold toward the equator; due north or south coasts
 * by rule, warm east and cold west. n is the unit vector from the sea toward its land. (0, 0) for NONE. */
void climate_air_flow(uint8_t kind, const float32_t n[2], float64_t y, float32_t out[2]);

/* t_swing = (A_sea + kappa (A_land - A_sea)) s_h, s_h = +1 at phi = 0. */
float32_t climate_air_swing(const climate_normals_t *normals, float64_t phi_deg, float32_t kappa);
/* l = 25 + 20 (1 - kappa). */
float32_t climate_air_lag_days(float32_t kappa);
/* dp_heat = -0.6 kappa t_swing cos(2 pi (d - 170 - l) / 365), hPa; exactly 0 at kappa 0. */
float32_t climate_air_heat_low(float32_t kappa, float32_t t_swing, float32_t lag_days, float32_t day);
/* T_sea = T_lat(phi) + current_c + A_sea(phi) s_h cos(2 pi (d - 215) / 365), deg C. */
float32_t climate_air_sea_temperature(const climate_normals_t *normals, float64_t phi_deg, float32_t current_c,
                                      float32_t day);

/* The current and strength of window sea cell sea_at, from its nearest land, as that cell's own tile stores them: NONE and
 * 0 more than 600 blocks from land or with none in reach. Phase 05 reads it for the water an upwind walk meets and for a
 * coast's nearest sea. Pure. */
uint8_t climate_air_window_current(const climate_window_t *window, uint32_t sea_at, uint8_t *out_strength);

/* Fills current, current_strength, t_swing, t_lag_days, wind[6] and pressure[3] of a tile's 256 cells, whose phase 03 fields
 * are already set, from its window. Pure: the same bytes from the same window. Its scratch, about 40 KiB, comes from arena
 * (the worker's Job arena). trace may be nullptr. */
void climate_air_build_tile(const climate_normals_t *normals, arena_t *arena, const climate_window_t *window,
                            climate_cell_t *cells, climate_air_trace_t *trace);

#endif
