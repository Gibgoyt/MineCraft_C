/* See run/src/climate/weather/week/README.md. */
#ifndef MC_RUN_CLIMATE_WEATHER_WEEK_INDEX_H
#define MC_RUN_CLIMATE_WEATHER_WEEK_INDEX_H

#include "climate/heat/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 07: one day's weather and one week's state, as plain numbers, and the pure rules that turn seven days into
 * a week (master plan 7.4 and 7.5). Nothing here reads the climate: the tests feed it the master plan's numbers by hand. One
 * home for each number below (vision rule 5); CLIMATE_MELT_MM_PER_DEGREE_DAY stays in climate/season, phase 05's. */

static const float32_t CLIMATE_EVAPORATION_MM_PER_DEGREE_DAY = 0.2f32; /* 7.5 */
static const float32_t CLIMATE_SOIL_MM_MAX = 150.0f32;                 /* 7.5 */
static const float32_t CLIMATE_ICE_GROW_CM_PER_DEGREE_DAY = 1.5f32;    /* 7.5 */
static const float32_t CLIMATE_ICE_MELT_CM_PER_DEGREE_DAY = 2.0f32;    /* 7.5 */
static const float32_t CLIMATE_DAY_RAIN_MM = 1.0f32;                   /* 7.4: a day counts as wet over this */
static const float32_t CLIMATE_STORM_DEPTH_HPA = 20.0f32;              /* 7.4 */
static const float32_t CLIMATE_STORM_WIND = 15.0f32;                   /* 7.4: blocks a second */
static const float32_t CLIMATE_DUST_WIND = 12.0f32;                    /* 7.4: blocks a second */
static const float32_t CLIMATE_DUST_WEEK_RAIN_MM = 2.0f32;             /* 7.4 */
static const float32_t CLIMATE_HEAT_WAVE_SIGMAS = 1.5f32;              /* 7.4: of sigma_T */
static const float32_t CLIMATE_FOG_CALM_WIND = 3.0f32;                 /* phase 07: blocks a second at 06:00 */
static const float32_t CLIMATE_FOG_COAST_BLOCKS = 2000.0f32;           /* phase 07: from a cold-current sea */
static const float32_t CLIMATE_FOG_VALLEY_BLOCKS = 30.0f32;            /* phase 07: below its 8 neighbours' mean */
static const float32_t CLIMATE_FOG_VALLEY_SOIL_MM = 100.0f32;          /* phase 07 */
static const float32_t CLIMATE_CLOUDY_WEEK_RAIN_MM = 10.0f32;          /* phase 07: a wet week's normal */
static const float32_t CLIMATE_CLOUDY_LOW_RADII = 1.5f32;              /* phase 07: near a low */
static const float32_t CLIMATE_ICE_EVENT_CM =
    5.0f32; /* phase 07: ice this thick is "ice on lakes"; phase 08's ice blocks */

enum : uint32_t {
    CLIMATE_WEEK_DAYS = 7u,
};

typedef enum climate_day_kind_t : uint8_t {
    CLIMATE_DAY_CLEAR = 0,
    CLIMATE_DAY_CLOUDY = 1,
    CLIMATE_DAY_RAIN = 2,
    CLIMATE_DAY_SLEET = 3,
    CLIMATE_DAY_SNOW = 4,
    CLIMATE_DAY_THUNDERSTORM = 5,
    CLIMATE_DAY_STORM = 6,
    CLIMATE_DAY_HURRICANE = 7,
    CLIMATE_DAY_FOG = 8,
    CLIMATE_DAY_HEAT_WAVE = 9,
    CLIMATE_DAY_DUST_STORM = 10,
    CLIMATE_DAY_KIND_COUNT = 11,
} climate_day_kind_t;

/* One day's weather at one place: pure, from the seed and the time. */
typedef struct climate_day_t {
    float32_t t_mean;   /* 0: deg C */
    float32_t t_min;    /* 4 */
    float32_t t_max;    /* 8 */
    float32_t anomaly;  /* 12: T'_day, deg C */
    float32_t sigma_t;  /* 16: deg C */
    float32_t rain_mm;  /* 20: rain and snow as water */
    float32_t snow_mm;  /* 24: the part that fell as snow */
    float32_t wind[2];  /* 28: u, v at 12:00, blocks a second */
    float32_t wind_max; /* 36: the strongest hour, blocks a second */
    uint8_t kind;       /* 40: climate_day_kind_t */
    uint8_t _pad0[3];   /* 41 */
} climate_day_t;
static_assert(sizeof(climate_day_t) == 44);
static_assert(alignof(climate_day_t) == 4);
static_assert(offsetof(climate_day_t, t_mean) == 0);
static_assert(offsetof(climate_day_t, t_min) == 4);
static_assert(offsetof(climate_day_t, t_max) == 8);
static_assert(offsetof(climate_day_t, anomaly) == 12);
static_assert(offsetof(climate_day_t, sigma_t) == 16);
static_assert(offsetof(climate_day_t, rain_mm) == 20);
static_assert(offsetof(climate_day_t, snow_mm) == 24);
static_assert(offsetof(climate_day_t, wind) == 28);
static_assert(offsetof(climate_day_t, wind_max) == 36);
static_assert(offsetof(climate_day_t, kind) == 40);
static_assert(offsetof(climate_day_t, _pad0) == 41);

/* One cell's weather state, for every cell with generated land in it, saved with the world (master plan 7.5). */
typedef struct climate_week_t {
    float32_t t_mean;    /* 0: deg C, last week */
    float32_t t_min;     /* 4 */
    float32_t t_max;     /* 8 */
    float32_t rain_mm;   /* 12: rain and snow together, as water */
    float32_t snow_mm;   /* 16: snow on the ground, as water */
    float32_t soil_mm;   /* 20: water in the soil, 0 to CLIMATE_SOIL_MM_MAX (150) */
    float32_t runoff_mm; /* 24: what ran off last week */
    float32_t wind[2];   /* 28: last week's prevailing u and v */
    float32_t ice_cm;    /* 36: on still water */
    float32_t flood;     /* 40: runoff from upstream over channel size, 1 = banks full */
    uint8_t day_kind[7]; /* 44: CLIMATE_DAY_* for each day */
    uint8_t _pad0[13];   /* 51 */
} climate_week_t;
static_assert(sizeof(climate_week_t) == 64);
static_assert(alignof(climate_week_t) == 4);
static_assert(offsetof(climate_week_t, t_mean) == 0);
static_assert(offsetof(climate_week_t, t_min) == 4);
static_assert(offsetof(climate_week_t, t_max) == 8);
static_assert(offsetof(climate_week_t, rain_mm) == 12);
static_assert(offsetof(climate_week_t, snow_mm) == 16);
static_assert(offsetof(climate_week_t, soil_mm) == 20);
static_assert(offsetof(climate_week_t, runoff_mm) == 24);
static_assert(offsetof(climate_week_t, wind) == 28);
static_assert(offsetof(climate_week_t, ice_cm) == 36);
static_assert(offsetof(climate_week_t, flood) == 40);
static_assert(offsetof(climate_week_t, day_kind) == 44);
static_assert(offsetof(climate_week_t, _pad0) == 51);

/* What decides a day's kind: everything the table in the phase 07 file reads, worked out by whoever knows the day. */
typedef struct climate_day_facts_t {
    float32_t t_mean;         /* 0: the day's mean, deg C */
    float32_t anomaly;        /* 4: T'_day */
    float32_t sigma_t;        /* 8 */
    float32_t rain_mm;        /* 12: the day's rain and snow */
    float32_t wind_max;       /* 16: the strongest hourly wind */
    float32_t wind_morning;   /* 20: the wind's speed at 06:00 */
    float32_t week_rain_mm;   /* 24: rain in the 7 days ending today */
    float32_t normal_week_mm; /* 28: this week's normal rain */
    float32_t sea_distance;   /* 32: blocks */
    float32_t valley_depth;   /* 36: the mean of the 8 neighbours' ground less this cell's, blocks */
    float32_t soil_mm;        /* 40: last week's soil water */
    uint8_t in_hurricane;     /* 44: within a hurricane's radius at some hour */
    uint8_t in_deep_low;      /* 45: within the radius of a low over 20 hPa deep at some hour */
    uint8_t under_thunder;    /* 46: a thunderstorm cell's footprint at some hour */
    uint8_t near_low;         /* 47: within 1.5 radii of a low at 12:00 */
    uint8_t desert;           /* 48: the cell's climate class is desert */
    uint8_t cold_current;     /* 49: the cell's current is cold */
    uint8_t _pad0[2];         /* 50 */
} climate_day_facts_t;
static_assert(sizeof(climate_day_facts_t) == 52);

/* The day's kind: the first row of the phase 07 file's table that matches, in its order, so there is exactly one. Pure. */
uint8_t climate_week_day_kind(const climate_day_facts_t *facts);

/* Master plan 7.5 for one cell, in this order: the 7 days' kinds; T_w the mean of the day means, t_min and t_max the lowest
 * low and highest high, P_w the sum of the rain, f_s P_w the sum of the snow parts; ice day by day; melt, snow, evaporation,
 * the soil bucket and runoff; the mean of the 12:00 winds. flood is left as before was: flow accumulation sets it. Every sum
 * in float32_t in a fixed order. after may be before. Pure. */
void climate_week_update(const climate_week_t *before, const climate_day_t days[CLIMATE_WEEK_DAYS],
                         climate_week_t *after);

/* Snow, soil and runoff only, from one week's mean temperature t_w, precipitation p_w and snow share f_s: the same formulas.
 * The other fields of after are before's (fast mode leaves ice, flood and the day kinds alone). after may be before. Pure. */
void climate_week_update_water(const climate_week_t *before, float32_t t_w, float32_t p_w, float32_t f_s,
                               climate_week_t *after);

/* Ice on still water through the days, from each day's mean: I += 1.5 max(0, -T) - 2 max(0, T), never below 0, cm. Pure. */
float32_t climate_week_ice(float32_t ice_cm, const float32_t *day_means, uint32_t count);

/* The evaporation 7.5's clamp at 0 let happen: what water conservation counts. Pure. */
float32_t climate_week_evaporation_actual(const climate_week_t *before, float32_t t_w, float32_t p_w, float32_t f_s);

#endif
