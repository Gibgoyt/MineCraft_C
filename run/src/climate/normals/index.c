/* See README.md. The one home of plan 004's knot tables, joined with the terrain's own spline (master plan 6.6: "the same
 * spline code terrain/density already has"). Changing a number here changes every world's climate. */
#include "climate/normals/index.h"

#include <math.h>
#include <string.h>

/* Master plan 6.4 and 6.6: the latitudes the seven-knot tables are given at. */
static const float64_t NORMALS_LAT_DEG[] = {0.0f64, 15.0f64, 30.0f64, 45.0f64, 60.0f64, 75.0f64, 90.0f64};
/* 6.4: background pressure in hPa, by the wind latitude |psi|. */
static const float64_t NORMALS_P_BELT_HPA[] = {1008.0f64, 1012.0f64, 1020.0f64, 1013.0f64,
                                               1000.0f64, 1010.0f64, 1020.0f64};
/* 6.6: the yearly mean temperature at sea level, deg C, by |phi|. */
static const float64_t NORMALS_T_MEAN_C[] = {27.0f64, 26.0f64, 20.0f64, 11.0f64, 0.0f64, -14.0f64, -24.0f64};
/* 6.6: the latitudes of the swing tables. */
static const float64_t NORMALS_SWING_LAT_DEG[] = {0.0f64, 30.0f64, 60.0f64, 90.0f64};
/* 6.6: half the yearly swing, deg C, over the sea and over land. */
static const float64_t NORMALS_A_SEA_C[] = {1.0f64, 3.0f64, 6.0f64, 10.0f64};
static const float64_t NORMALS_A_LAND_C[] = {2.0f64, 9.0f64, 18.0f64, 22.0f64};

enum : uint32_t {
    NORMALS_LAT_KNOTS = sizeof(NORMALS_LAT_DEG) / sizeof(NORMALS_LAT_DEG[0]),
    NORMALS_SWING_KNOTS = sizeof(NORMALS_SWING_LAT_DEG) / sizeof(NORMALS_SWING_LAT_DEG[0]),
};
static_assert(sizeof(NORMALS_P_BELT_HPA) == sizeof(NORMALS_LAT_DEG));
static_assert(sizeof(NORMALS_T_MEAN_C) == sizeof(NORMALS_LAT_DEG));
static_assert(sizeof(NORMALS_A_SEA_C) == sizeof(NORMALS_SWING_LAT_DEG));
static_assert(sizeof(NORMALS_A_LAND_C) == sizeof(NORMALS_SWING_LAT_DEG));

void climate_normals_fill(climate_normals_t *normals) {
    memset(normals, 0, sizeof(*normals)); /* knots past each count too, so two builds compare as bytes */
    terrain_density_spline_build(&normals->p_belt, NORMALS_LAT_DEG, NORMALS_P_BELT_HPA, NORMALS_LAT_KNOTS);
    terrain_density_spline_build(&normals->t_mean, NORMALS_LAT_DEG, NORMALS_T_MEAN_C, NORMALS_LAT_KNOTS);
    terrain_density_spline_build(&normals->a_sea, NORMALS_SWING_LAT_DEG, NORMALS_A_SEA_C, NORMALS_SWING_KNOTS);
    terrain_density_spline_build(&normals->a_land, NORMALS_SWING_LAT_DEG, NORMALS_A_LAND_C, NORMALS_SWING_KNOTS);
}

const climate_normals_t *climate_normals_build(arena_t *arena) {
    climate_normals_t *const normals = arena_alloc(arena, sizeof(climate_normals_t), alignof(climate_normals_t));
    climate_normals_fill(normals);
    return normals;
}

float64_t climate_normals_p_belt(const climate_normals_t *normals, float64_t psi_deg) {
    return terrain_density_spline_at(&normals->p_belt, fabs(psi_deg));
}

float64_t climate_normals_t_mean(const climate_normals_t *normals, float64_t phi_deg) {
    return terrain_density_spline_at(&normals->t_mean, fabs(phi_deg));
}

float64_t climate_normals_a_sea(const climate_normals_t *normals, float64_t phi_deg) {
    return terrain_density_spline_at(&normals->a_sea, fabs(phi_deg));
}

float64_t climate_normals_a_land(const climate_normals_t *normals, float64_t phi_deg) {
    return terrain_density_spline_at(&normals->a_land, fabs(phi_deg));
}
