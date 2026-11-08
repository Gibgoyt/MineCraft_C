/* See README.md. Temperature, master plan 6.6, by the phase 05 file's "The temperature formula, term by term": a formula of
 * latitude, height, the current, kappa and the day, and nothing simulated. */
#include "climate/heat/index.h"

#include "climate/air/index.h"

#include <math.h>

float32_t climate_heat_base(float32_t t_mean_sea_level, float32_t z, bool land) {
    const float32_t basin = land ? CLIMATE_BASIN_C_PER_BLOCK * fmaxf(0.0f32, -z) : 0.0f32;
    return t_mean_sea_level - CLIMATE_LAPSE_C_PER_BLOCK * z + basin;
}

float32_t climate_heat_wave(float32_t t_swing_signed, float32_t lag_days, float32_t day) {
    const float32_t phase = CLIMATE_HEAT_TWO_PI * (day - CLIMATE_SOLSTICE_DAY - lag_days) / CLIMATE_HEAT_YEAR_DAYS;
    return t_swing_signed * cosf(phase);
}

float32_t climate_heat_formula(float32_t t_mean_sea_level, float32_t z, bool land, float32_t t_swing_signed,
                               float32_t lag_days, float32_t day) {
    return climate_heat_base(t_mean_sea_level, z, land) + climate_heat_wave(t_swing_signed, lag_days, day);
}

float32_t climate_heat_from_place(const climate_normals_t *normals, float32_t lat_deg, float32_t z, float32_t kappa,
                                  float32_t current_c, bool land, float32_t day) {
    const float32_t t_mean = (float32_t)(climate_normals_t_mean(normals, (float64_t)lat_deg) + (float64_t)current_c);
    const float32_t swing = climate_air_swing(normals, (float64_t)lat_deg, kappa);
    return climate_heat_formula(t_mean, z, land, swing, climate_air_lag_days(kappa), day);
}

float32_t climate_heat_daily_range(float32_t kappa, float32_t rain_mm_day) {
    const float32_t wet = 1.0f32 - expf(-fmaxf(0.0f32, rain_mm_day) / CLIMATE_WET_DAY_SCALE_MM);
    return (CLIMATE_RANGE_COAST_C + CLIMATE_RANGE_INLAND_EXTRA_C * kappa) * (1.0f32 - CLIMATE_RANGE_RAIN_CUT * wet);
}

float32_t climate_heat_snow_share(float32_t t_c) {
    const float32_t share = (CLIMATE_SNOW_ALL_RAIN_C - t_c) / (CLIMATE_SNOW_ALL_RAIN_C - CLIMATE_SNOW_ALL_SNOW_C);
    return share < 0.0f32 ? 0.0f32 : (share > 1.0f32 ? 1.0f32 : share);
}

float32_t climate_heat_t_mean(const climate_normals_t *normals, const climate_cell_t *cell) {
    const float32_t current_c =
        climate_air_current_c(cell->current) * (float32_t)cell->current_strength / CLIMATE_CURRENT_STRENGTH_MAX_F32;
    return (float32_t)(climate_normals_t_mean(normals, (float64_t)cell->lat_deg) + (float64_t)current_c);
}

void climate_heat_build_tile(const climate_normals_t *normals, climate_cell_t *cells) {
    /* At sea level, without the height terms, so a read can put them back for any z (the phase 05 file, Design). */
    for (uint32_t i = 0u; i < (uint32_t)CLIMATE_TILE_CELL_COUNT; i++) {
        cells[i].t_mean = climate_heat_t_mean(normals, &cells[i]);
    }
}
