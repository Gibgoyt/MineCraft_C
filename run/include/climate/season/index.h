/* See run/src/climate/season/README.md. */
#ifndef MC_RUN_CLIMATE_SEASON_INDEX_H
#define MC_RUN_CLIMATE_SEASON_INDEX_H

#include "climate/cell/index.h"
#include "climate/normals/index.h"
#include "climate/rain/index.h"
#include "types/index.h"

#include <stdint.h>

/* Master plan 6.8, 6.10 and 7.5, and the numbers the phase 05 file adds (each says why). One home each. */
static const float32_t CLIMATE_HURRICANE_SEA_C = 26.5f32;          /* 6.8 */
static const float32_t CLIMATE_HURRICANE_RAMP_C = 2.0f32;          /* 6.8: (T_sea - 26.5) / 2 */
static const float32_t CLIMATE_HURRICANE_LAT_MIN_DEG = 5.0f32;     /* 6.8 */
static const float32_t CLIMATE_HURRICANE_LAT_MAX_DEG = 25.0f32;    /* 6.8 */
static const float32_t CLIMATE_HURRICANE_SEASON_S = 0.25f32;       /* 6.8: "season while s_hur > 0.25" */
static const float32_t CLIMATE_HURRICANE_COAST_BLOCKS = 3000.0f32; /* 6.8: "a coast within 3 km" */
static const float32_t CLIMATE_MONSOON_RAIN_RATIO = 3.0f32;        /* 6.8 */
static const float32_t CLIMATE_MELT_MM_PER_DEGREE_DAY = 4.0f32;    /* 7.5; the one home: phase 07 includes this */
static const float32_t CLIMATE_SNOW_BALANCE_LOW = -250.0f32;       /* this phase: bisection range, the world's floor */
static const float32_t CLIMATE_SNOW_BALANCE_HIGH = 2000.0f32;      /* this phase: above 003 phase 11's +1250 */
static const float32_t CLIMATE_SNOW_BALANCE_NONE = 9999.0f32;      /* this phase: snow never outlasts summer */
enum : uint32_t {
    CLIMATE_SNOW_BALANCE_HALVINGS = 12u, /* this phase: 2250 / 4096 = 0.55 blocks */
    CLIMATE_SNOW_BALANCE_YEARS = 2u,     /* this phase: spin-up year, then the year tested */
    CLIMATE_SNOW_WEEKS = 52u,            /* weeks of a model year, week w at day 7 w + 3.5 */
    CLIMATE_MONTHS = 12u,
    CLIMATE_SEASON_DAYS = CALENDAR_DAYS_PER_YEAR,
    CLIMATE_SEASON_QUARTERS_LAST = 91u,                   /* day 364 as a quarter day */
    CLIMATE_HURRICANE_DAY_SCALE = 4u,                     /* 6.9: hurricane_days holds day / 4, so a day fits a byte */
    CLIMATE_SEASON_HALF_FIT_DAYS = CLIMATE_FIT_DAYS / 2u, /* phase 11: half a year of fit days: the monsoon's halves */
};
static const float32_t CLIMATE_DRY_THRESHOLD_PER_C = 20.0f32;      /* 6.10: P_th = 20 T + 140 */
static const float32_t CLIMATE_DRY_THRESHOLD_BASE_MM = 140.0f32;   /* 6.10 */
static const float32_t CLIMATE_TROPICAL_MIN_C = 18.0f32;           /* 6.10 */
static const float32_t CLIMATE_TEMPERATE_MIN_C = -3.0f32;          /* 6.10 */
static const float32_t CLIMATE_CONTINENTAL_MAX_C = 22.0f32;        /* 6.10 */
static const float32_t CLIMATE_TUNDRA_MAX_C = 10.0f32;             /* 6.10 */
static const float32_t CLIMATE_WET_MONTH_MM = 60.0f32;             /* 6.10 */
static const float32_t CLIMATE_DRY_SUMMER_MM = 40.0f32;            /* this phase, Koppen's Cs */
static const float32_t CLIMATE_DRY_SUMMER_SHARE = 1.0f32 / 3.0f32; /* this phase, Koppen's Cs */
static const float32_t CLIMATE_MONTH_WAVE1 = 0.98862f32;           /* sin(pi/12) / (pi/12): a month's mean of wave 1 */
static const float32_t CLIMATE_MONTH_WAVE2 = 0.95493f32;           /* sin(pi/6) / (pi/6): of wave 2 */
/* Phase 11: the driest month's search starts above any month's rain, mm. */
static const float32_t CLIMATE_SEASON_DRIEST_START_MM = 1.0e30f32;

enum : uint8_t {
    CLIMATE_CLASS_NONE = 0, /* sea */
    CLIMATE_CLASS_ICE = 1,
    CLIMATE_CLASS_TUNDRA = 2,
    CLIMATE_CLASS_DESERT = 3,
    CLIMATE_CLASS_STEPPE = 4,
    CLIMATE_CLASS_RAINFOREST = 5,
    CLIMATE_CLASS_MONSOON = 6,
    CLIMATE_CLASS_SAVANNA = 7,
    CLIMATE_CLASS_MEDITERRANEAN = 8,
    CLIMATE_CLASS_TEMPERATE = 9,
    CLIMATE_CLASS_CONTINENTAL = 10,
    CLIMATE_CLASS_BOREAL = 11,
    CLIMATE_CLASS_COUNT = 12,
};

/* What a place's months are, for its class: worked out from a cell's stored numbers at a height. */
typedef struct climate_season_months_t {
    float32_t rain_mm[CLIMATE_MONTHS]; /* 0: each calendar month's rain, centred on 365 (m + 0.5) / 12, at least 0 */
    float32_t t_mean_c;                /* 48: the yearly mean at the height */
    float32_t t_max_month_c;           /* 52: the warmest month's mean: the month centred on the warmest day */
    float32_t t_min_month_c;           /* 56: the coldest month's */
    float32_t rain_mm_year;            /* 60: P0 x 365 / 7 */
    float32_t driest_month_mm;         /* 64 */
    uint8_t dry_summer; /* 68: the driest summer month under 40 mm and a third of the wettest winter one */
    uint8_t _pad0[3];   /* 69 */
} climate_season_months_t;
static_assert(sizeof(climate_season_months_t) == 72);

/* 6.8's s_hur from latitude and sea temperature: clamp((T_sea - 26.5) / 2, 0, 1), 0 outside 5 to 25 degrees. Pure. */
float32_t climate_season_hurricane_s(float32_t lat_deg, float32_t t_sea_c);

/* The season a sea at lat_deg with current_c (C x strength / 255) has, packed: the longest run of whole days with
 * s_hur > 0.25, counting round the year, ties to the earliest start, as quarter days {first, last}. {255, 255} for none,
 * {0, 91} for all year, first > last when it wraps past day 364. Pure. */
void climate_season_hurricane_days(const climate_normals_t *normals, float32_t lat_deg, float32_t current_c,
                                   uint8_t out[2]);

/* Whether day d is inside a stored season: first <= floor(d / 4) <= last, or the wrapped form when first > last. False for
 * CLIMATE_DAY_NONE. Pure. */
bool climate_season_hurricane_today(const uint8_t hurricane_days[2], float32_t day);

/* 6.8's monsoon test on the 12 fit days, for a land cell with a sea direction: some day's wind from the sea (w . n < 0)
 * with the wind half a year later from the land (w . n > 0), and the wettest 6 days in a row (round the year) over 3
 * times the other 6. The wind is the stored waves, or the hooks' (hooks may be nullptr). Pure. */
bool climate_season_monsoon_flag(const climate_cell_t *cell, const climate_test_hooks_t *hooks);

/* Monsoon today: the flag, the wind on day d (the stored waves) from the sea, and P(d) > P0. Pure. */
bool climate_season_monsoon_today(const climate_cell_t *cell, float32_t day);

/* The class from the numbers, by 6.10's first match. Pure. */
uint8_t climate_season_classify(float32_t t_mean_ground, float32_t t_max_month, float32_t t_min_month,
                                float32_t rain_mm_year, float32_t driest_month_mm, bool dry_summer, bool monsoon);

/* A cell's months at height z (land: B below 0), from its t_mean, t_swing, t_lag_days and rain_mm_week. Pure. */
void climate_season_months(const climate_cell_t *cell, float32_t z, bool land, climate_season_months_t *out);

/* Whether snow outlasts the summer at height z by the degree-day model on the cell's normals: two years of 52 weeks from no
 * snow, S += f_s(T) P - 7 x 4 x max(0, T), and the second year never back to 0. Pure. */
bool climate_season_snow_outlasts(const climate_cell_t *cell, float32_t z, bool land);

/* The height above which snow outlasts the summer, by 12 halvings of -250 to 2000: -250 if it outlasts even there,
 * CLIMATE_SNOW_BALANCE_NONE if not even at 2000. land as for climate_season_snow_outlasts. Blocks. Pure. */
float32_t climate_season_snow_balance(const climate_cell_t *cell, bool land);

/* Fills hurricane_days, the monsoon and hurricane-coast flags, snow_balance_z and climate_class of a tile's 256 cells, whose
 * t_mean (heat), rain (rain) and phase 03 and 04 fields are set. hooks may be nullptr. Pure, allocates nothing. */
void climate_season_build_tile(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                               const climate_window_t *window, climate_cell_t *cells);

#endif
