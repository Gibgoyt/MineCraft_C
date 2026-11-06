/* See run/src/climate/heat/README.md. */
#ifndef MC_RUN_CLIMATE_HEAT_INDEX_H
#define MC_RUN_CLIMATE_HEAT_INDEX_H

#include "climate/cell/index.h"
#include "climate/normals/index.h"
#include "types/index.h"

#include <stdint.h>

/* Master plan 6.6, 6.7 and 8: the temperature formula's constants. One home each. */
static const float32_t CLIMATE_LAPSE_C_PER_BLOCK = 0.012f32;  /* Gamma, 6.6: Ahmed's "12 deg C per 1000 blocks" */
static const float32_t CLIMATE_BASIN_C_PER_BLOCK = 0.025f32;  /* B, 6.6 and 2.1: below sea level, on land */
static const float32_t CLIMATE_SOLSTICE_DAY = 170.0f32;       /* 6.6: the cosine's zero lag */
static const float32_t CLIMATE_RANGE_COAST_C = 4.0f32;        /* 6.6: daily range on a coast */
static const float32_t CLIMATE_RANGE_INLAND_EXTRA_C = 8.0f32; /* 6.6: + 8 kappa */
static const float32_t CLIMATE_RANGE_RAIN_CUT = 0.6f32;       /* 6.6: x (1 - 0.6 r) */
static const float32_t CLIMATE_WET_DAY_SCALE_MM = 6.0f32;     /* 8: r = 1 - e^(-P_day / 6) */
static const float32_t CLIMATE_SNOW_ALL_RAIN_C = 1.0f32;      /* 6.7: all rain above */
static const float32_t CLIMATE_SNOW_ALL_SNOW_C = -2.0f32;     /* 6.7: all snow below */
static const float32_t CLIMATE_HEAT_TWO_PI = 6.28318530717958647692f32;
static const float32_t CLIMATE_HEAT_YEAR_DAYS = 365.0f32;

/* t_mean - Gamma z + B, with B = 0.025 max(0, -z) only when land: the part of 6.6 that does not change through the year. */
float32_t climate_heat_base(float32_t t_mean_sea_level, float32_t z, bool land);

/* A s_h cos(2 pi (d - 170 - l) / 365): the yearly wave, t_swing_signed already carrying s_h. */
float32_t climate_heat_wave(float32_t t_swing_signed, float32_t lag_days, float32_t day);

/* 6.6, the normal daily mean temperature, deg C: climate_heat_base + climate_heat_wave, added in that order, so a caller
 * that keeps the two parts apart gets the same bits. day from 0 to below 365, fractions allowed. land is false for sea and
 * lakes: then B = 0. Pure. */
float32_t climate_heat_formula(float32_t t_mean_sea_level, float32_t z, bool land, float32_t t_swing_signed,
                               float32_t lag_days, float32_t day);

/* The same from latitude, height, kappa and the current's deg C, working out t_mean, A and l itself (phase 04's swing and
 * lag). For the tests of 6.6's worked examples, which give these inputs. Pure. */
float32_t climate_heat_from_place(const climate_normals_t *normals, float32_t lat_deg, float32_t z, float32_t kappa,
                                  float32_t current_c, bool land, float32_t day);

/* 6.6: (4 + 8 kappa)(1 - 0.6 r), with r = 1 - e^(-P_day / 6) and P_day the day's normal rain in mm. deg C. Pure. */
float32_t climate_heat_daily_range(float32_t kappa, float32_t rain_mm_day);

/* 6.7: clamp((1 - T) / 3, 0, 1), the share of precipitation that falls as snow. Pure. */
float32_t climate_heat_snow_share(float32_t t_c);

/* A cell's yearly mean at sea level: T_lat(|phi|) + C x strength / 255. What t_mean holds. Pure. */
float32_t climate_heat_t_mean(const climate_normals_t *normals, const climate_cell_t *cell);

/* Fills t_mean of a tile's 256 cells, whose phase 03 and 04 fields are set. Pure, allocates nothing. */
void climate_heat_build_tile(const climate_normals_t *normals, climate_cell_t *cells);

#endif
