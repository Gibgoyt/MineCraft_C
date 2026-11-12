/* See README.md. Storm seasons, monsoons, the snow balance height and the climate classes of master plan 6.8 to 6.10, by
 * the phase 05 file's "Seasons and classes, in climate/season". Labels worked out once from a cell's normals. */
#include "climate/season/index.h"

#include "calendar/index.h"
#include "climate/air/index.h"
#include "climate/harmonic/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/sea/index.h"
#include "math/det/index.h"

#include <math.h>
#include <string.h>

static const float32_t SEASON_TWO_PI = 6.28318530717958647692f32;
static const float32_t SEASON_YEAR_DAYS = 365.0f32;

/* ---- Hurricanes ------------------------------------------------------------------------------------------------------ */

float32_t climate_season_hurricane_s(float32_t lat_deg, float32_t t_sea_c) {
    const float32_t a = fabsf(lat_deg);
    if (a < CLIMATE_HURRICANE_LAT_MIN_DEG || a > CLIMATE_HURRICANE_LAT_MAX_DEG) {
        return 0.0f32; /* too near the equator for the spin to turn a storm, or too cool a sea */
    }
    const float32_t s = (t_sea_c - CLIMATE_HURRICANE_SEA_C) / CLIMATE_HURRICANE_RAMP_C;
    return s < 0.0f32 ? 0.0f32 : (s > 1.0f32 ? 1.0f32 : s);
}

void climate_season_hurricane_days(const climate_normals_t *normals, float32_t lat_deg, float32_t current_c,
                                   uint8_t out[2]) {
    out[0] = CLIMATE_DAY_NONE;
    out[1] = CLIMATE_DAY_NONE;
    const float32_t a = fabsf(lat_deg);
    if (a < CLIMATE_HURRICANE_LAT_MIN_DEG || a > CLIMATE_HURRICANE_LAT_MAX_DEG) {
        return; /* s_hur is 0 every day: the same answer without the 365 cosines */
    }
    bool in[CLIMATE_SEASON_DAYS];
    uint32_t count = 0u;
    for (uint32_t d = 0u; d < CLIMATE_SEASON_DAYS; d++) {
        const float32_t t_sea = climate_air_sea_temperature(normals, (float64_t)lat_deg, current_c, (float32_t)d);
        in[d] = climate_season_hurricane_s(lat_deg, t_sea) > CLIMATE_HURRICANE_SEASON_S;
        count += in[d] ? 1u : 0u;
    }
    if (count == 0u) {
        return;
    }
    if (count == CLIMATE_SEASON_DAYS) {
        out[0] = 0u;
        out[1] = (uint8_t)CLIMATE_SEASON_QUARTERS_LAST;
        return;
    }
    /* The longest run round the year: every run starts on an in-season day after one out of it. */
    uint32_t best_start = 0u, best_length = 0u;
    for (uint32_t d = 0u; d < CLIMATE_SEASON_DAYS; d++) {
        if (!in[d] || in[(d + CLIMATE_SEASON_DAYS - 1u) % CLIMATE_SEASON_DAYS]) {
            continue;
        }
        uint32_t length = 0u;
        while (in[(d + length) % CLIMATE_SEASON_DAYS]) {
            length++;
        }
        if (length > best_length) { /* strictly longer: ties stay with the earliest start */
            best_start = d;
            best_length = length;
        }
    }
    out[0] = (uint8_t)(best_start / CLIMATE_HURRICANE_DAY_SCALE);
    out[1] = (uint8_t)(((best_start + best_length - 1u) % CLIMATE_SEASON_DAYS) / CLIMATE_HURRICANE_DAY_SCALE);
}

bool climate_season_hurricane_today(const uint8_t hurricane_days[2], float32_t day) {
    if (hurricane_days[0] == CLIMATE_DAY_NONE || hurricane_days[1] == CLIMATE_DAY_NONE) {
        return false;
    }
    float32_t whole = floorf(day);
    whole = whole < 0.0f32
                ? 0.0f32
                : (whole > (float32_t)(CLIMATE_SEASON_DAYS - 1u) ? (float32_t)(CLIMATE_SEASON_DAYS - 1u) : whole);
    const uint32_t quarter = (uint32_t)whole / CLIMATE_HURRICANE_DAY_SCALE;
    const uint32_t first = hurricane_days[0], last = hurricane_days[1];
    return first <= last ? (quarter >= first && quarter <= last) : (quarter >= first || quarter <= last);
}

/* ---- Monsoons -------------------------------------------------------------------------------------------------------- */

bool climate_season_monsoon_flag(const climate_cell_t *cell, const climate_test_hooks_t *hooks) {
    const float32_t nx = cell->sea_direction[0], ny = cell->sea_direction[1];
    if (nx == 0.0f32 && ny == 0.0f32) {
        return false; /* no sea in reach */
    }
    float32_t onshore[CLIMATE_FIT_DAYS], rain[CLIMATE_FIT_DAYS];
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        const float32_t day = climate_harmonic_day(k);
        float32_t wind[2];
        climate_rain_wind(cell, hooks, day, wind);
        onshore[k] = wind[0] * nx + wind[1] * ny; /* below 0: the wind points away from the sea, so comes from it */
        rain[k] = fmaxf(0.0f32, climate_harmonic_at2(cell->rain_mm_week, day));
    }
    bool reverses = false;
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        reverses = reverses ||
                   (onshore[k] < 0.0f32 && onshore[(k + CLIMATE_SEASON_HALF_FIT_DAYS) % CLIMATE_FIT_DAYS] > 0.0f32);
    }
    if (!reverses) {
        return false;
    }
    /* The wet half: the 6 days in a row, round the year, with the most rain, ties to the earliest start. */
    float32_t total = 0.0f32;
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        total += rain[k];
    }
    float32_t wet = -1.0f32;
    for (uint32_t start = 0u; start < CLIMATE_FIT_DAYS; start++) {
        float32_t sum = 0.0f32;
        for (uint32_t k = 0u; k < CLIMATE_SEASON_HALF_FIT_DAYS; k++) {
            sum += rain[(start + k) % CLIMATE_FIT_DAYS];
        }
        wet = sum > wet ? sum : wet;
    }
    return wet > CLIMATE_MONSOON_RAIN_RATIO * (total - wet);
}

bool climate_season_monsoon_today(const climate_cell_t *cell, float32_t day) {
    if ((cell->flags & CLIMATE_CELL_MONSOON) == 0u) {
        return false;
    }
    float32_t wind[2];
    climate_rain_wind(cell, nullptr, day, wind);
    const bool from_sea = wind[0] * cell->sea_direction[0] + wind[1] * cell->sea_direction[1] < 0.0f32;
    return from_sea && climate_harmonic_at2(cell->rain_mm_week, day) > cell->rain_mm_week[0];
}

/* ---- Classes --------------------------------------------------------------------------------------------------------- */

uint8_t climate_season_classify(float32_t t_mean_ground, float32_t t_max_month, float32_t t_min_month,
                                float32_t rain_mm_year, float32_t driest_month_mm, bool dry_summer, bool monsoon) {
    if (t_max_month < 0.0f32) {
        return CLIMATE_CLASS_ICE;
    }
    if (t_max_month < CLIMATE_TUNDRA_MAX_C) {
        return CLIMATE_CLASS_TUNDRA;
    }
    const float32_t threshold = CLIMATE_DRY_THRESHOLD_PER_C * t_mean_ground + CLIMATE_DRY_THRESHOLD_BASE_MM;
    if (rain_mm_year < 0.5f32 * threshold) {
        return CLIMATE_CLASS_DESERT;
    }
    if (rain_mm_year < threshold) {
        return CLIMATE_CLASS_STEPPE;
    }
    if (t_min_month >= CLIMATE_TROPICAL_MIN_C) {
        if (driest_month_mm >= CLIMATE_WET_MONTH_MM) {
            return CLIMATE_CLASS_RAINFOREST;
        }
        return monsoon ? CLIMATE_CLASS_MONSOON : CLIMATE_CLASS_SAVANNA;
    }
    if (t_min_month > CLIMATE_TEMPERATE_MIN_C) {
        return dry_summer ? CLIMATE_CLASS_MEDITERRANEAN : CLIMATE_CLASS_TEMPERATE;
    }
    return t_max_month >= CLIMATE_CONTINENTAL_MAX_C ? CLIMATE_CLASS_CONTINENTAL : CLIMATE_CLASS_BOREAL;
}

/* cos and sin of month m's centre angle, 15 + 30 m degrees, and of twice it, 30 + 60 m: exact values, so no month's rain leans
 * on how a libm rounds (plan 006 phase 02). Before, cosf and sinf of the same float32_t angles. */
#define SEASON_COS_15 0.96592582628906828675f32
#define SEASON_SIN_15 0.25881904510252076235f32
#define SEASON_HALF_ROOT_2 0.70710678118654752440f32
#define SEASON_HALF_ROOT_3 0.86602540378443864676f32
static const float32_t SEASON_MONTH_COS[CLIMATE_MONTHS] = {
    SEASON_COS_15,  SEASON_HALF_ROOT_2,  SEASON_SIN_15,  -SEASON_SIN_15, -SEASON_HALF_ROOT_2, -SEASON_COS_15,
    -SEASON_COS_15, -SEASON_HALF_ROOT_2, -SEASON_SIN_15, SEASON_SIN_15,  SEASON_HALF_ROOT_2,  SEASON_COS_15,
};
static const float32_t SEASON_MONTH_SIN[CLIMATE_MONTHS] = {
    SEASON_SIN_15,  SEASON_HALF_ROOT_2,  SEASON_COS_15,  SEASON_COS_15,  SEASON_HALF_ROOT_2,  SEASON_SIN_15,
    -SEASON_SIN_15, -SEASON_HALF_ROOT_2, -SEASON_COS_15, -SEASON_COS_15, -SEASON_HALF_ROOT_2, -SEASON_SIN_15,
};
static const float32_t SEASON_MONTH_COS2[CLIMATE_MONTHS] = {
    SEASON_HALF_ROOT_3, 0.0f32, -SEASON_HALF_ROOT_3, -SEASON_HALF_ROOT_3, 0.0f32, SEASON_HALF_ROOT_3,
    SEASON_HALF_ROOT_3, 0.0f32, -SEASON_HALF_ROOT_3, -SEASON_HALF_ROOT_3, 0.0f32, SEASON_HALF_ROOT_3,
};
static const float32_t SEASON_MONTH_SIN2[CLIMATE_MONTHS] = {
    0.5f32, 1.0f32, 0.5f32, -0.5f32, -1.0f32, -0.5f32, 0.5f32, 1.0f32, 0.5f32, -0.5f32, -1.0f32, -0.5f32,
};

float32_t climate_season_month_rain_mm(const float32_t rain_mm_week[CLIMATE_RAIN_COEFFICIENTS], uint32_t m) {
    const float32_t *const p = rain_mm_week;
    const float32_t weeks_a_month = CLIMATE_DAYS_A_YEAR / (float32_t)CLIMATE_MONTHS / CLIMATE_DAYS_A_WEEK;
    const float32_t week = p[0] + CLIMATE_MONTH_WAVE1 * (p[1] * SEASON_MONTH_COS[m] + p[2] * SEASON_MONTH_SIN[m]) +
                           CLIMATE_MONTH_WAVE2 * (p[3] * SEASON_MONTH_COS2[m] + p[4] * SEASON_MONTH_SIN2[m]);
    return fmaxf(0.0f32, week) * weeks_a_month;
}

float32_t climate_season_driest_month_mm(const float32_t rain_mm_week[CLIMATE_RAIN_COEFFICIENTS]) {
    float32_t driest = CLIMATE_SEASON_DRIEST_START_MM;
    for (uint32_t m = 0u; m < CLIMATE_MONTHS; m++) {
        driest = fminf(driest, climate_season_month_rain_mm(rain_mm_week, m));
    }
    return driest;
}

void climate_season_months(const climate_cell_t *cell, float32_t z, bool land, climate_season_months_t *out) {
    memset(out, 0, sizeof(*out));
    const float32_t mean = climate_heat_base(cell->t_mean, z, land);
    /* The month centred on the warmest (coldest) day: the wave's peak times a month's mean of a cosine. */
    const float32_t swing = CLIMATE_MONTH_WAVE1 * fabsf(cell->t_swing);
    out->t_mean_c = mean;
    out->t_max_month_c = mean + swing;
    out->t_min_month_c = mean - swing;
    const float32_t *const p = cell->rain_mm_week;
    out->rain_mm_year = fmaxf(0.0f32, p[0]) * CLIMATE_DAYS_A_YEAR / CLIMATE_DAYS_A_WEEK;
    const float32_t hemisphere = cell->lat_deg < 0.0f32 ? -1.0f32 : 1.0f32;
    float32_t driest = CLIMATE_SEASON_DRIEST_START_MM, driest_summer = CLIMATE_SEASON_DRIEST_START_MM,
              wettest_winter = 0.0f32;
    for (uint32_t m = 0u; m < CLIMATE_MONTHS; m++) {
        const float32_t centre = SEASON_YEAR_DAYS * ((float32_t)m + 0.5f32) / (float32_t)CLIMATE_MONTHS;
        const float32_t month = climate_season_month_rain_mm(p, m);
        out->rain_mm[m] = month;
        driest = fminf(driest, month);
        const float32_t summer_phase = SEASON_TWO_PI * (centre - CLIMATE_SOLSTICE_DAY) / SEASON_YEAR_DAYS;
        const bool summer = hemisphere * (float32_t)det_cos((float64_t)summer_phase) > 0.0f32;
        if (summer) {
            driest_summer = fminf(driest_summer, month);
        } else {
            wettest_winter = fmaxf(wettest_winter, month);
        }
    }
    out->driest_month_mm = driest;
    out->dry_summer =
        (driest_summer < CLIMATE_DRY_SUMMER_MM && driest_summer < CLIMATE_DRY_SUMMER_SHARE * wettest_winter) ? 1u : 0u;
}

/* ---- Snow balance ---------------------------------------------------------------------------------------------------- */

/* The model's weeks: the temperature wave and the rain, neither of which depends on the height. */
typedef struct season_weeks_t {
    float32_t wave[CLIMATE_SNOW_WEEKS]; /* 0 */
    float32_t rain[CLIMATE_SNOW_WEEKS]; /* 208: mm a week, at least 0 */
} season_weeks_t;
static_assert(sizeof(season_weeks_t) == 416);

static void season_weeks_fill(const climate_cell_t *cell, season_weeks_t *weeks) {
    for (uint32_t w = 0u; w < CLIMATE_SNOW_WEEKS; w++) {
        const float32_t day =
            (float32_t)CALENDAR_DAYS_PER_WEEK * (float32_t)w + 0.5f32 * (float32_t)CALENDAR_DAYS_PER_WEEK;
        weeks->wave[w] = climate_heat_wave(cell->t_swing, cell->t_lag_days, day);
        weeks->rain[w] = fmaxf(0.0f32, climate_harmonic_at2(cell->rain_mm_week, day));
    }
}

static bool season_outlasts(const climate_cell_t *cell, const season_weeks_t *weeks, float32_t z, bool land) {
    const float32_t base = climate_heat_base(cell->t_mean, z, land);
    const float32_t melt_a_week = CLIMATE_DAYS_A_WEEK * CLIMATE_MELT_MM_PER_DEGREE_DAY;
    float32_t snow = 0.0f32;
    for (uint32_t year = 0u; year < CLIMATE_SNOW_BALANCE_YEARS; year++) {
        for (uint32_t w = 0u; w < CLIMATE_SNOW_WEEKS; w++) {
            const float32_t t = base + weeks->wave[w]; /* climate_heat_formula's bits at day 7 w + 3.5 */
            snow = fmaxf(0.0f32, snow + climate_heat_snow_share(t) * weeks->rain[w] - melt_a_week * fmaxf(0.0f32, t));
            if (year + 1u == CLIMATE_SNOW_BALANCE_YEARS && snow == 0.0f32) {
                return false;
            }
        }
    }
    return true;
}

bool climate_season_snow_outlasts(const climate_cell_t *cell, float32_t z, bool land) {
    season_weeks_t weeks;
    season_weeks_fill(cell, &weeks);
    return season_outlasts(cell, &weeks, z, land);
}

float32_t climate_season_snow_balance(const climate_cell_t *cell, bool land) {
    season_weeks_t weeks;
    season_weeks_fill(cell, &weeks);
    float32_t low = CLIMATE_SNOW_BALANCE_LOW, high = CLIMATE_SNOW_BALANCE_HIGH;
    if (season_outlasts(cell, &weeks, low, land)) {
        return low;
    }
    if (!season_outlasts(cell, &weeks, high, land)) {
        return CLIMATE_SNOW_BALANCE_NONE;
    }
    /* Higher is colder, so it outlasts above some height: keep the half where the answer changes. */
    for (uint32_t h = 0u; h < CLIMATE_SNOW_BALANCE_HALVINGS; h++) {
        const float32_t middle = 0.5f32 * (low + high);
        if (season_outlasts(cell, &weeks, middle, land)) {
            high = middle;
        } else {
            low = middle;
        }
    }
    return 0.5f32 * (low + high);
}

/* ---- One tile -------------------------------------------------------------------------------------------------------- */

/* Which sea gives a cell its hurricane season, and that sea's latitude and C x strength / 255. False for none. */
static bool season_sea_of(const climate_window_t *window, uint32_t at, const climate_cell_t *cell, float32_t *out_lat,
                          float32_t *out_current_c) {
    if ((cell->flags & CLIMATE_CELL_SEA) != 0u) {
        *out_lat = cell->lat_deg;
        *out_current_c =
            climate_air_current_c(cell->current) * (float32_t)cell->current_strength / CLIMATE_CURRENT_STRENGTH_MAX_F32;
        return true;
    }
    if (cell->sea_distance > CLIMATE_HURRICANE_COAST_BLOCKS || window->sea_distance2[at] == CLIMATE_SEA_FAR) {
        return false;
    }
    const uint32_t sea_at = window->sea_nearest[at];
    uint8_t strength = 0u;
    const uint8_t current = climate_air_window_current(window, sea_at, &strength);
    const int64_t row = (int64_t)(sea_at / (uint32_t)CLIMATE_WINDOW_CELLS);
    const int64_t centre_y = (int64_t)(window->tile_y - CLIMATE_WINDOW_TILES / 2) * CLIMATE_TILE_BLOCKS +
                             row * CLIMATE_CELL_BLOCKS + CLIMATE_CELL_BLOCKS / 2;
    *out_lat = (float32_t)climate_latitude_deg((float64_t)centre_y); /* as that sea cell's own tile has it */
    *out_current_c = climate_air_current_c(current) * (float32_t)strength / CLIMATE_CURRENT_STRENGTH_MAX_F32;
    return true;
}

void climate_season_build_tile(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                               const climate_window_t *window, climate_cell_t *cells) {
    const uint32_t tile = (uint32_t)CLIMATE_TILE_CELLS;
    const uint32_t margin = (uint32_t)CLIMATE_MARGIN_CELLS;
    const uint32_t side = (uint32_t)CLIMATE_WINDOW_CELLS;
    for (uint32_t iy = 0u; iy < tile; iy++) {
        for (uint32_t ix = 0u; ix < tile; ix++) {
            climate_cell_t *const cell = &cells[iy * tile + ix];
            const uint32_t at = (margin + iy) * side + margin + ix;
            const bool sea = (cell->flags & CLIMATE_CELL_SEA) != 0u;
            const bool lake = (cell->flags & CLIMATE_CELL_LAKE) != 0u;
            /* 1 to 3: hurricane days, and the coast flag. */
            float32_t sea_lat = 0.0f32, sea_current_c = 0.0f32;
            cell->hurricane_days[0] = CLIMATE_DAY_NONE;
            cell->hurricane_days[1] = CLIMATE_DAY_NONE;
            if (season_sea_of(window, at, cell, &sea_lat, &sea_current_c)) {
                climate_season_hurricane_days(normals, sea_lat, sea_current_c, cell->hurricane_days);
            }
            if (!sea && cell->hurricane_days[0] != CLIMATE_DAY_NONE) {
                cell->flags |= CLIMATE_CELL_HURRICANE_COAST;
            }
            if (sea) {
                cell->snow_balance_z = CLIMATE_SNOW_BALANCE_NONE;
                cell->climate_class = CLIMATE_CLASS_NONE;
                continue;
            }
            /* 4. Monsoon, for land. */
            if (!lake && climate_season_monsoon_flag(cell, hooks)) {
                cell->flags |= CLIMATE_CELL_MONSOON;
            }
            /* 5. Snow balance, B below 0 for land only. */
            cell->snow_balance_z = climate_season_snow_balance(cell, !lake);
            /* 6. The class, at the ground. */
            climate_season_months_t months;
            climate_season_months(cell, cell->z_ground, !lake, &months);
            cell->climate_class = climate_season_classify(
                months.t_mean_c, months.t_max_month_c, months.t_min_month_c, months.rain_mm_year,
                months.driest_month_mm, months.dry_summer != 0u, (cell->flags & CLIMATE_CELL_MONSOON) != 0u);
        }
    }
}
