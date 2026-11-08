/* See run/src/climate/normals/README.md. */
#ifndef MC_RUN_CLIMATE_NORMALS_INDEX_H
#define MC_RUN_CLIMATE_NORMALS_INDEX_H

#include "arena/index.h"
#include "terrain/density/index.h"
#include "types/index.h"

#include <stdint.h>

/* Every knot table of plan 004, the one home of each (phase 11 moved them here from the source, so a test can pin every
 * knot). Changing a number here changes every world's climate. */
enum : uint32_t {
    CLIMATE_LAT_KNOT_COUNT = 7u,   /* the pressure and temperature tables: every 15 degrees */
    CLIMATE_SWING_KNOT_COUNT = 4u, /* the swing tables: every 30 degrees */
    CLIMATE_RAIN_KNOT_COUNT = 9u,  /* the rain table */
};
/* Master plan 6.4 and 6.6: the latitudes the seven-knot tables are given at, degrees. */
static const float64_t CLIMATE_KNOTS_LAT_DEG[CLIMATE_LAT_KNOT_COUNT] = {0.0f64,  15.0f64, 30.0f64, 45.0f64,
                                                                        60.0f64, 75.0f64, 90.0f64};
/* 6.4: background pressure in hPa, by the wind latitude |psi|. */
static const float64_t CLIMATE_PRESSURE_KNOTS[CLIMATE_LAT_KNOT_COUNT] = {1008.0f64, 1012.0f64, 1020.0f64, 1013.0f64,
                                                                         1000.0f64, 1010.0f64, 1020.0f64};
/* 6.6: T_lat, the yearly mean temperature at sea level, deg C, by |phi|. */
static const float64_t CLIMATE_TEMPERATURE_KNOTS[CLIMATE_LAT_KNOT_COUNT] = {27.0f64, 26.0f64,  20.0f64, 11.0f64,
                                                                            0.0f64,  -14.0f64, -24.0f64};
/* 6.6: the latitudes of the swing tables, degrees. */
static const float64_t CLIMATE_SWING_KNOTS_LAT_DEG[CLIMATE_SWING_KNOT_COUNT] = {0.0f64, 30.0f64, 60.0f64, 90.0f64};
/* 6.6: A_sea and A_land, half the yearly swing, deg C, over the sea and deep inland. */
static const float64_t CLIMATE_SWING_SEA_KNOTS[CLIMATE_SWING_KNOT_COUNT] = {1.0f64, 3.0f64, 6.0f64, 10.0f64};
static const float64_t CLIMATE_SWING_LAND_KNOTS[CLIMATE_SWING_KNOT_COUNT] = {2.0f64, 9.0f64, 18.0f64, 22.0f64};
/* 6.7 (phase 05): P_belt, yearly rain at sea level in mm, by the wind latitude |psi|, at these latitudes, degrees. */
static const float64_t CLIMATE_RAIN_KNOTS_LAT_DEG[CLIMATE_RAIN_KNOT_COUNT] = {
    0.0f64, 10.0f64, 20.0f64, 30.0f64, 40.0f64, 50.0f64, 60.0f64, 75.0f64, 90.0f64};
static const float64_t CLIMATE_RAIN_KNOTS[CLIMATE_RAIN_KNOT_COUNT] = {
    2400.0f64, 1800.0f64, 700.0f64, 300.0f64, 800.0f64, 1000.0f64, 800.0f64, 350.0f64, 150.0f64};

/* Every knot table of plan 004 as a curve (master plan rule 5: one home). Built once, read-only after, by any thread. */
typedef struct climate_normals_t {
    terrain_density_spline_t p_belt;    /* 0: hPa by |psi|, master plan 6.4 */
    terrain_density_spline_t t_mean;    /* 296: deg C, the yearly mean at sea level by |phi|, 6.6 */
    terrain_density_spline_t a_sea;     /* 592: deg C, half the yearly swing over the sea by |phi|, 6.6 */
    terrain_density_spline_t a_land;    /* 888: deg C, the same over land, 6.6 */
    terrain_density_spline_t rain_belt; /* 1184: mm a year by |psi|, 6.7 (phase 05) */
} climate_normals_t;
static_assert(sizeof(climate_normals_t) == 1480);

/* Takes the curves from arena (Process lifetime: the climate's normals arena) and builds them. Never fails: the arena's
 * budget is fixed, and running out aborts. */
const climate_normals_t *climate_normals_build(arena_t *arena);

/* The same into memory the caller holds, for tests. */
void climate_normals_fill(climate_normals_t *normals);

/* Each curve at a latitude in degrees, either sign: the tables are by |latitude|, and past 90 they stay at 90's value. */
float64_t climate_normals_p_belt(const climate_normals_t *normals, float64_t psi_deg);
float64_t climate_normals_t_mean(const climate_normals_t *normals, float64_t phi_deg);
float64_t climate_normals_a_sea(const climate_normals_t *normals, float64_t phi_deg);
float64_t climate_normals_a_land(const climate_normals_t *normals, float64_t phi_deg);
/* Phase 05: P_belt, yearly rain at sea level in mm by the wind latitude |psi|: a rate, mm a year if every day were like
 * this one. */
float64_t climate_normals_rain_belt(const climate_normals_t *normals, float64_t psi_deg);

#endif
