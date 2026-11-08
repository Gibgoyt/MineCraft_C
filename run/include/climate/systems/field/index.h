/* See run/src/climate/systems/field/README.md. */
#ifndef MC_RUN_CLIMATE_SYSTEMS_FIELD_INDEX_H
#define MC_RUN_CLIMATE_SYSTEMS_FIELD_INDEX_H

#include "climate/systems/index.h"
#include "types/index.h"

#include <stdint.h>

/* Sum of every system's bell at (x, y) at time t, hPa. Phase 04's belt and heat-low pressure are not included. */
float32_t climate_systems_pressure_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* Sum of every system's circling wind, blocks a second, u east and v north. */
void climate_systems_wind_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t, float32_t out[2]);
/* Rain from hurricanes, lows, fronts and cells, mm an hour, before the high's cut. */
float32_t climate_systems_rain_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* 0.2 under any high's radius, else 1: what background and system rain are multiplied by (7.2, 7.3). */
float32_t climate_systems_rain_factor(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* Whether (x, y) is on a low's cold front at t, and under a high: 7.2.1's f_front and f_high. */
bool climate_systems_on_front(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
bool climate_systems_under_high(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* 7.4's f_sys at a latitude: 0.3 + 0.2 S((|phi| - 20) / 15). */
float32_t climate_systems_rain_share(float32_t lat_deg);
/* f_sys on a day: climate_systems_rain_share where some system can bring rain that day, cells (t_day_c over 18) or lows
 * (|psi| within 5 degrees of their 35 to 65), else 0, when the background brings the whole normal (Issues found 12); plus
 * CLIMATE_SYSTEM_RAIN_SHARE_HURRICANE times the day's hurricane_s (climate_systems_hurricane_s at the place), the share
 * hurricanes bring in their season (Issues found 22). */
float32_t climate_systems_rain_share_today(float32_t lat_deg, float32_t day, float32_t t_day_c, float32_t hurricane_s);
/* A place's wetness: its normal rain on the day over CLIMATE_SYSTEM_RAIN_WET_MM_DAY. The rain systems bring there is
 * climate_systems_rain_at times this (Issues found 11 of the phase 06 file). Never negative. */
float32_t climate_systems_wetness(float32_t rain_mm_day);

/* One system's parts, for the layers and tests. */
/* sigma: s_h e at the centre (x, y), negated for a high: +1 anticlockwise. */
float32_t climate_systems_spin(uint8_t kind, float64_t centre_y);
/* 7.3's speed profile at r, with the 2 R to 3 R taper: blocks a second. */
float32_t climate_systems_wind_speed(float32_t wind_max, float32_t core_radius, float32_t radius, float64_t r);
/* A low's cold front at t: its two ends; false if the low is not alive. */
bool climate_systems_front(const climate_system_t *low, float64_t t, float64_t out_from[2], float64_t out_to[2]);
/* A cell at t: its centre and strength share g(tau); false if not alive. */
bool climate_systems_thunder_at(const climate_thunder_t *cell, float64_t t, float64_t out_centre[2],
                                float64_t *out_share);

#endif
