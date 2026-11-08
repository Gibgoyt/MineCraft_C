/* See README.md. Plan 004's knot tables (one home: the header, since phase 11), joined with the terrain's own spline (master plan 6.6: "the same
 * spline code terrain/density already has"). Changing a number here changes every world's climate. */
#include "climate/normals/index.h"

#include <math.h>
#include <string.h>

static_assert((uint32_t)CLIMATE_RAIN_KNOT_COUNT <= (uint32_t)TERRAIN_DENSITY_SPLINE_KNOTS_MAX);

void climate_normals_fill(climate_normals_t *normals) {
    memset(normals, 0, sizeof(*normals)); /* knots past each count too, so two builds compare as bytes */
    terrain_density_spline_build(&normals->p_belt, CLIMATE_KNOTS_LAT_DEG, CLIMATE_PRESSURE_KNOTS,
                                 CLIMATE_LAT_KNOT_COUNT);
    terrain_density_spline_build(&normals->t_mean, CLIMATE_KNOTS_LAT_DEG, CLIMATE_TEMPERATURE_KNOTS,
                                 CLIMATE_LAT_KNOT_COUNT);
    terrain_density_spline_build(&normals->a_sea, CLIMATE_SWING_KNOTS_LAT_DEG, CLIMATE_SWING_SEA_KNOTS,
                                 CLIMATE_SWING_KNOT_COUNT);
    terrain_density_spline_build(&normals->a_land, CLIMATE_SWING_KNOTS_LAT_DEG, CLIMATE_SWING_LAND_KNOTS,
                                 CLIMATE_SWING_KNOT_COUNT);
    terrain_density_spline_build(&normals->rain_belt, CLIMATE_RAIN_KNOTS_LAT_DEG, CLIMATE_RAIN_KNOTS,
                                 CLIMATE_RAIN_KNOT_COUNT);
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

float64_t climate_normals_rain_belt(const climate_normals_t *normals, float64_t psi_deg) {
    return terrain_density_spline_at(&normals->rain_belt, fabs(psi_deg));
}
