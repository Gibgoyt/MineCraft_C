/* See README.md. Master plan 7.5's weekly update and the phase 07 file's day-kind table, on plain numbers: no climate, no
 * noise, no allocation. Every sum is float32_t, added in one fixed order, so the same inputs give the same bits anywhere. */
#include "climate/weather/week/index.h"

#include "climate/season/index.h"

#include <string.h>

static float32_t week_max0(float32_t v) {
    return v > 0.0f32 ? v : 0.0f32;
}

uint8_t climate_week_day_kind(const climate_day_facts_t *f) {
    const bool wet = f->rain_mm > CLIMATE_DAY_RAIN_MM;
    if (f->in_hurricane != 0u) {
        return CLIMATE_DAY_HURRICANE;
    }
    if (f->in_deep_low != 0u || f->wind_max > CLIMATE_STORM_WIND) {
        return CLIMATE_DAY_STORM;
    }
    if (f->under_thunder != 0u) {
        return CLIMATE_DAY_THUNDERSTORM;
    }
    if (f->wind_max > CLIMATE_DUST_WIND && f->week_rain_mm < CLIMATE_DUST_WEEK_RAIN_MM && f->desert != 0u) {
        return CLIMATE_DAY_DUST_STORM;
    }
    if (wet && f->t_mean < CLIMATE_SNOW_ALL_SNOW_C) {
        return CLIMATE_DAY_SNOW;
    }
    if (wet && f->t_mean <= CLIMATE_SNOW_ALL_RAIN_C) {
        return CLIMATE_DAY_SLEET;
    }
    if (wet) {
        return CLIMATE_DAY_RAIN;
    }
    if (f->anomaly > CLIMATE_HEAT_WAVE_SIGMAS * f->sigma_t) {
        return CLIMATE_DAY_HEAT_WAVE;
    }
    const bool coast_fog = f->cold_current != 0u && f->sea_distance < CLIMATE_FOG_COAST_BLOCKS;
    const bool valley_fog = f->valley_depth >= CLIMATE_FOG_VALLEY_BLOCKS && f->soil_mm > CLIMATE_FOG_VALLEY_SOIL_MM;
    if (f->wind_morning < CLIMATE_FOG_CALM_WIND && (coast_fog || valley_fog)) {
        return CLIMATE_DAY_FOG;
    }
    if (f->normal_week_mm > CLIMATE_CLOUDY_WEEK_RAIN_MM || f->near_low != 0u) {
        return CLIMATE_DAY_CLOUDY;
    }
    return CLIMATE_DAY_CLEAR;
}

float32_t climate_week_ice(float32_t ice_cm, const float32_t *day_means, uint32_t count) {
    float32_t ice = ice_cm;
    for (uint32_t i = 0u; i < count; i++) {
        const float32_t t = day_means[i];
        ice = ice + CLIMATE_ICE_GROW_CM_PER_DEGREE_DAY * week_max0(-t) -
              CLIMATE_ICE_MELT_CM_PER_DEGREE_DAY * week_max0(t);
        ice = week_max0(ice);
    }
    return ice;
}

/* The pieces of 7.5, shared by the update and the conservation check so they cannot drift apart. */
typedef struct week_water_t {
    float32_t melt;     /* 0: M_w */
    float32_t snow;     /* 4: S_{w+1} */
    float32_t evap;     /* 8: E_w */
    float32_t soil_raw; /* 12: W' */
} week_water_t;
static_assert(sizeof(week_water_t) == 16);

/* snowfall is f_s P_w: the update passes the snow parts' sum, so it is that sum exactly. */
static week_water_t week_water(const climate_week_t *before, float32_t t_w, float32_t p_w, float32_t snowfall) {
    const float32_t days = (float32_t)CLIMATE_WEEK_DAYS;
    week_water_t w;
    w.melt = days * CLIMATE_MELT_MM_PER_DEGREE_DAY * week_max0(t_w);
    const float32_t pack = before->snow_mm + snowfall;
    w.snow = week_max0(pack - w.melt);
    w.evap = days * CLIMATE_EVAPORATION_MM_PER_DEGREE_DAY * week_max0(t_w);
    const float32_t melted = w.melt < pack ? w.melt : pack;
    w.soil_raw = before->soil_mm + (p_w - snowfall) + melted - w.evap;
    return w;
}

static void week_water_apply(const climate_week_t *before, float32_t t_w, float32_t p_w, float32_t snowfall,
                             climate_week_t *after) {
    const week_water_t w = week_water(before, t_w, p_w, snowfall);
    climate_week_t out = *before;
    out.snow_mm = w.snow;
    /* Runoff from W' before the clamp: clamping first would lose it (the spring-melt test holds this). */
    out.runoff_mm = week_max0(w.soil_raw - CLIMATE_SOIL_MM_MAX);
    out.soil_mm = w.soil_raw < 0.0f32 ? 0.0f32 : (w.soil_raw > CLIMATE_SOIL_MM_MAX ? CLIMATE_SOIL_MM_MAX : w.soil_raw);
    *after = out;
}

void climate_week_update_water(const climate_week_t *before, float32_t t_w, float32_t p_w, float32_t f_s,
                               climate_week_t *after) {
    week_water_apply(before, t_w, p_w, f_s * p_w, after);
}

float32_t climate_week_evaporation_actual(const climate_week_t *before, float32_t t_w, float32_t p_w, float32_t f_s) {
    const week_water_t w = week_water(before, t_w, p_w, f_s * p_w);
    return w.soil_raw < 0.0f32 ? w.evap + w.soil_raw : w.evap;
}

void climate_week_update(const climate_week_t *before, const climate_day_t days[CLIMATE_WEEK_DAYS],
                         climate_week_t *after) {
    climate_week_t out = *before;
    float32_t t_sum = 0.0f32, rain = 0.0f32, snow = 0.0f32, u = 0.0f32, v = 0.0f32;
    float32_t t_min = days[0].t_min, t_max = days[0].t_max;
    float32_t means[CLIMATE_WEEK_DAYS];
    for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
        out.day_kind[i] = days[i].kind;
        t_sum += days[i].t_mean;
        rain += days[i].rain_mm;
        snow += days[i].snow_mm;
        u += days[i].wind[0];
        v += days[i].wind[1];
        t_min = days[i].t_min < t_min ? days[i].t_min : t_min;
        t_max = days[i].t_max > t_max ? days[i].t_max : t_max;
        means[i] = days[i].t_mean;
    }
    const float32_t count = (float32_t)CLIMATE_WEEK_DAYS;
    const float32_t t_w = t_sum / count;
    out.t_mean = t_w;
    out.t_min = t_min;
    out.t_max = t_max;
    out.rain_mm = rain;
    out.ice_cm = climate_week_ice(before->ice_cm, means, CLIMATE_WEEK_DAYS);
    climate_week_t water;
    week_water_apply(before, t_w, rain, snow, &water);
    out.snow_mm = water.snow_mm;
    out.soil_mm = water.soil_mm;
    out.runoff_mm = water.runoff_mm;
    out.wind[0] = u / count;
    out.wind[1] = v / count;
    memset(out._pad0, 0, sizeof(out._pad0));
    *after = out;
}
