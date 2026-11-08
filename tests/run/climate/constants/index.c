/* ctest run_climate_constants. See README.md. Plan 004 phase 11: every constant of the climate system, from sections 4 to 8
 * of the master plan and the phases' own, pinned to the value in the phase 11 file's table (its Tuned column if Ahmed
 * changed one, else the plan's, or the phase's where a phase chose it); the derived ones checked against their sources;
 * every knot of every knot table; and a sweep of run/src/calendar/ and run/src/climate/ for numbers with no name.
 *
 * A deliberate change to a value changes three things together: the constant, its line here, and the table's row. */
#include "harness/index.h"

#include "calendar/index.h"
#include "climate/air/index.h"
#include "climate/cell/index.h"
#include "climate/harmonic/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/label/index.h"
#include "climate/layer/index.h"
#include "climate/normals/index.h"
#include "climate/outlook/index.h"
#include "climate/rain/index.h"
#include "climate/sea/index.h"
#include "climate/season/index.h"
#include "climate/sun/index.h"
#include "climate/systems/index.h"
#include "climate/warning/index.h"
#include "climate/weather/flow/index.h"
#include "climate/weather/index.h"
#include "climate/weather/week/index.h"
#include "file/index.h"
#include "game/cover/index.h"
#include "game/index.h"
#include "machine/index.h"
#include "player/movement/index.h"
#include "ui/atlas/draw/index.h"
#include "ui/atlas/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Pinning ----------------------------------------------------------------------------------------------------------- */

static void pin_u64(test_context_t *t, const char *name, uint64_t found, uint64_t expected, int32_t line) {
    if (found != expected) {
        printf("  %s is %" PRIu64 ", the table says %" PRIu64 "\n", name, found, expected);
    }
    test_check(t, found == expected, __FILE__, line, name);
}
static void pin_i64(test_context_t *t, const char *name, int64_t found, int64_t expected, int32_t line) {
    if (found != expected) {
        printf("  %s is %" PRId64 ", the table says %" PRId64 "\n", name, found, expected);
    }
    test_check(t, found == expected, __FILE__, line, name);
}
/* Exact: a constant is the bits its literal makes, so a float is pinned in its own width. */
static void pin_f32(test_context_t *t, const char *name, float32_t found, float32_t expected, int32_t line) {
    if (found != expected) {
        printf("  %s is %.9g, the table says %.9g\n", name, (libc_double_t)found, (libc_double_t)expected);
    }
    test_check(t, found == expected, __FILE__, line, name);
}
static void pin_f64(test_context_t *t, const char *name, float64_t found, float64_t expected, int32_t line) {
    if (found != expected) {
        printf("  %s is %.17g, the table says %.17g\n", name, (libc_double_t)found, (libc_double_t)expected);
    }
    test_check(t, found == expected, __FILE__, line, name);
}
#define PIN_U(name, expected) pin_u64(t, #name, (uint64_t)(name), (uint64_t)(expected), __LINE__)
#define PIN_I(name, expected) pin_i64(t, #name, (int64_t)(name), (int64_t)(expected), __LINE__)
#define PIN_F32(name, expected) pin_f32(t, #name, (name), (expected), __LINE__)
#define PIN_F64(name, expected) pin_f64(t, #name, (name), (expected), __LINE__)
/* A {lo, hi} range or a 2-vector. */
#define PIN_F32_2(name, a, b)                                                                                          \
    do {                                                                                                               \
        pin_f32(t, #name "[0]", (name)[0], (a), __LINE__);                                                             \
        pin_f32(t, #name "[1]", (name)[1], (b), __LINE__);                                                             \
    } while (false)

/* ---- Section 4: the calendar ------------------------------------------------------------------------------------------- */

TEST(every_constant_has_the_value_its_table_gives_calendar) {
    PIN_U((uint64_t)(GAME_DAY_SECONDS * (float64_t)MC_GAME_TICK_HZ),
          24000u); /* CALENDAR_TICKS_PER_DAY: 1200 s at 20 Hz */
    PIN_F64(GAME_DAY_SECONDS, 1200.0f64);
    PIN_F32(CLIMATE_REAL_SECONDS_A_DAY, (float32_t)GAME_DAY_SECONDS); /* the climate's copy, which cannot see game/ */
    PIN_U(CALENDAR_DAYS_PER_YEAR, 365u);
    PIN_U(CALENDAR_DAYS_PER_WEEK, 7u);
    PIN_U(CALENDAR_DAY_ZERO, 79u);               /* D0: the spring equinox, Q2 */
    PIN_U(CALENDAR_CLOCK_DAY_START, 1ull << 30); /* t0: 06:00, a quarter of the clock's 2^32 a day */
    PIN_U(CALENDAR_CLOCK_DAY, 1ull << 32);
    PIN_U(CALENDAR_CENSUS_HOUR, 6u);
    PIN_U(CALENDAR_FULL_WEEKS_MAX, 52u); /* CLIMATE_WEEK_CATCHUP_FULL */
    PIN_U(CALENDAR_SPRING_FROM, 79u);
    PIN_U(CALENDAR_SUMMER_FROM, 170u);
    PIN_U(CALENDAR_AUTUMN_FROM, 262u);
    PIN_U(CALENDAR_WINTER_FROM, 353u);
    PIN_U(CALENDAR_SEASON_PARTS, 3u);
    PIN_U(CALENDAR_SEASONS, 4u);
    PIN_U(CALENDAR_HOURS_PER_DAY, 24u);
    PIN_U(CALENDAR_NOON_HOUR, 12u);
}

/* ---- Section 5: latitude and the sun ----------------------------------------------------------------------------------- */

TEST(every_constant_has_the_value_its_table_gives_sun) {
    PIN_F64(CLIMATE_TILT_DEG, 23.5f64); /* the tropic and the declination's amplitude: one constant */
    PIN_F64(CLIMATE_TROPIC_Y, 10000.0f64);
    PIN_F64(CLIMATE_POLE_DEG, 90.0f64);
    PIN_F64(CLIMATE_SUN_EQUINOX_DAY, 79.0f64);
    PIN_F64(CLIMATE_SUN_NOON_HOUR, 12.0f64);
    PIN_F64(CLIMATE_PI, 3.14159265358979323846f64);
}

TEST(derived_constants_follow_their_sources) {
    /* CLIMATE_POLE_Y = round(CLIMATE_TROPIC_Y 90 / CLIMATE_TILT_DEG) to the block, and exactly the formula. */
    CHECK_EQ_I64((int64_t)llround(CLIMATE_POLE_Y), 38298);
    PIN_F64(CLIMATE_POLE_Y, CLIMATE_TROPIC_Y * 90.0f64 / CLIMATE_TILT_DEG);
    CHECK_EQ_I64((int64_t)llround(CLIMATE_LATITUDE_PERIOD), 153191);
    PIN_F64(CLIMATE_LATITUDE_PERIOD, 4.0f64 * CLIMATE_POLE_Y);
    PIN_F64(CLIMATE_POLAR_CIRCLE_DEG, 66.5f64);
    PIN_F64(CLIMATE_POLAR_CIRCLE_DEG, CLIMATE_POLE_DEG - CLIMATE_TILT_DEG);
    PIN_F64(CLIMATE_TWO_PI, 2.0f64 * CLIMATE_PI);
    /* One year, one week and the calendar's days, whatever form each part keeps them in. */
    PIN_F64(CLIMATE_YEAR_DAYS, (float64_t)CALENDAR_DAYS_PER_YEAR);
    PIN_F64(CLIMATE_SYSTEMS_YEAR_DAYS, (float64_t)CALENDAR_DAYS_PER_YEAR);
    PIN_F32(CLIMATE_HEAT_YEAR_DAYS, (float32_t)CALENDAR_DAYS_PER_YEAR);
    PIN_F32(CLIMATE_DAYS_A_YEAR, (float32_t)CALENDAR_DAYS_PER_YEAR);
    PIN_U(CLIMATE_SEASON_DAYS, CALENDAR_DAYS_PER_YEAR);
    PIN_U(CLIMATE_WEEK_DAYS, CALENDAR_DAYS_PER_WEEK);
    PIN_F32(CLIMATE_DAYS_A_WEEK, (float32_t)CALENDAR_DAYS_PER_WEEK);
    PIN_U(CLIMATE_HOURS_A_DAY, CALENDAR_HOURS_PER_DAY);
    PIN_F64(CLIMATE_SUN_EQUINOX_DAY, (float64_t)CALENDAR_SPRING_FROM); /* CLIMATE_EQUINOX_DAY */
    PIN_F32(CLIMATE_SOLSTICE_DAY, (float32_t)CALENDAR_SUMMER_FROM);    /* 6.6's 170 */
    PIN_F32(CLIMATE_SUMMER_DAY, (float32_t)CALENDAR_SUMMER_FROM);      /* 6.4's swing, the same 170 */
    PIN_F32(CLIMATE_DAY_START_HOUR, (float32_t)CALENDAR_CENSUS_HOUR);
    PIN_U(CALENDAR_CLOCK_DAY_START, CALENDAR_CLOCK_DAY * CALENDAR_CENSUS_HOUR / CALENDAR_HOURS_PER_DAY);
    PIN_F64(CALENDAR_NOON_INTO_DAY_F64, 0.25f64);
    PIN_U(CLIMATE_MARGIN_CELLS * CLIMATE_CELL_BLOCKS, 8192u); /* the 8 km margin */
    PIN_U(CLIMATE_TILE_BLOCKS, CLIMATE_TILE_CELLS * CLIMATE_CELL_BLOCKS);
    PIN_F32(CLIMATE_WALK_NONE_BLOCKS, CLIMATE_WALK_STEP_BLOCKS * (float32_t)CLIMATE_WALK_STEPS_MAX); /* 8 km */
    PIN_F32(CLIMATE_WALK_STEP_BLOCKS * (float32_t)CLIMATE_WALK_STEPS_WINDOW, 2048.0f32);             /* 2 km */
    PIN_U(CLIMATE_SEASON_HALF_FIT_DAYS, CLIMATE_FIT_DAYS / 2u);
    PIN_U(CLIMATE_SEASON_QUARTERS_LAST, (CALENDAR_DAYS_PER_YEAR - 1u) / CLIMATE_HURRICANE_DAY_SCALE);
    PIN_U(CLIMATE_TRACK_SAMPLES_MAX, (uint64_t)(CLIMATE_HIGH_LIFE_DAYS[1] / (float32_t)CLIMATE_TRACK_STEP_DAYS) + 1u);
    PIN_F32(CLIMATE_THUNDER_HIGH, CLIMATE_HIGH_RAIN_SHARE); /* 0.2 for cells and for rain under a high */
    PIN_F32(CLIMATE_OUTLOOK_RAIN_MM, CLIMATE_WET_DAY_SCALE_MM);
    PIN_F64(CLIMATE_LIFE_GROW_SHARE, 1.0f64 / CLIMATE_LIFE_THIRDS);
    PIN_F64(CLIMATE_LIFE_FADE_SHARE, 1.0f64 / CLIMATE_LIFE_THIRDS);
    PIN_U(CLIMATE_HARMONIC2_COEFFICIENTS, 1u + 2u * 2u); /* CLIMATE_HARMONICS: two waves, each a cos and a sin */
    /* The float copies of the calendar's whole numbers, which the parts multiply by. */
    PIN_F64(CALENDAR_HOURS_PER_DAY_F64, (float64_t)CALENDAR_HOURS_PER_DAY);
    PIN_F32(CALENDAR_HOURS_PER_DAY_F32, (float32_t)CALENDAR_HOURS_PER_DAY);
    PIN_F64(CALENDAR_DAYS_PER_YEAR_F64, (float64_t)CALENDAR_DAYS_PER_YEAR);
    PIN_F32(CALENDAR_DAYS_PER_YEAR_F32, (float32_t)CALENDAR_DAYS_PER_YEAR);
    PIN_F64(CALENDAR_DAYS_PER_WEEK_F64, (float64_t)CALENDAR_DAYS_PER_WEEK);
    PIN_F32(CALENDAR_DAYS_PER_WEEK_F32, (float32_t)CALENDAR_DAYS_PER_WEEK);
    PIN_F64(CALENDAR_CENSUS_HOUR_F64, (float64_t)CALENDAR_CENSUS_HOUR);
    PIN_F32(CALENDAR_CENSUS_HOUR_F32, (float32_t)CALENDAR_CENSUS_HOUR);
    PIN_F32(CALENDAR_NOON_INTO_DAY_F32, 0.25f32);
    PIN_F32(CLIMATE_CURRENT_STRENGTH_MAX_F32, (float32_t)CLIMATE_CURRENT_STRENGTH_MAX);
    PIN_F64(CLIMATE_HARMONIC_HALF_DAYS, (float64_t)CLIMATE_FIT_DAYS / 2.0f64);
    PIN_U(CLIMATE_WEEK_SPINUP_WEEKS, CLIMATE_WEEK_SPINUP_YEARS * CALENDAR_FULL_WEEKS_MAX);
    PIN_U(CLIMATE_SNOW_WEEKS, CALENDAR_FULL_WEEKS_MAX);                                   /* 52 weeks of a model year */
    PIN_F64(CLIMATE_WARNING_STEP_DAYS * (float64_t)(CLIMATE_WARNING_STEPS - 1u), 1.0f64); /* now to one day ahead */
    /* The front's turn is a unit vector, 30 degrees off the equatorward line. */
    CHECK(fabsf(CLIMATE_FRONT_TURN_X * CLIMATE_FRONT_TURN_X + CLIMATE_FRONT_TURN_Y * CLIMATE_FRONT_TURN_Y - 1.0f32) <
          1.0e-6f32);
}

/* ---- Section 6: normals ------------------------------------------------------------------------------------------------ */

TEST(every_constant_has_the_value_its_table_gives_cells) {
    PIN_I(CLIMATE_CELL_BLOCKS, 64);
    PIN_I(CLIMATE_TILE_CELLS, 16);
    PIN_U(CLIMATE_LAKE_LARGE_CELLS, 489u);
    PIN_I(CLIMATE_MARGIN_CELLS, 128);
    PIN_F32(CLIMATE_SEA_DISTANCE_MAX, 8192.0f32);
    PIN_F32(CLIMATE_KAPPA_SCALE, 1500.0f32);
    PIN_U(CLIMATE_SEA_AREA_MIN_CELLS, 8192u);
    PIN_F32(CLIMATE_SEA_FLOOR_BLOCKS, -30.0f32);
    PIN_F32(CLIMATE_WARP_FREQUENCY, 1.0f32 / 256.0f32);
    PIN_I(CLIMATE_WARP_BLOCKS, 32);
    PIN_F64(CLIMATE_WARP_LIMIT, 0.35f64);
    PIN_F64(CLIMATE_WARP_CUBIC, 1.5f64);
    PIN_I(CLIMATE_GROUND_SAMPLES_SIDE, 4);
    PIN_U(CLIMATE_CURRENT_STRENGTH_MAX, 255u);
}

TEST(every_constant_has_the_value_its_table_gives_currents_and_wind) {
    PIN_F32(CLIMATE_CURRENT_REACH, 600.0f32);
    PIN_F32(CLIMATE_CURRENT_SIDE, 0.5f32);
    pin_f32(t, "CLIMATE_CURRENT_BANDS_DEG[0]", CLIMATE_CURRENT_BANDS_DEG[0], 15.0f32, __LINE__);
    pin_f32(t, "CLIMATE_CURRENT_BANDS_DEG[1]", CLIMATE_CURRENT_BANDS_DEG[1], 45.0f32, __LINE__);
    pin_f32(t, "CLIMATE_CURRENT_BANDS_DEG[2]", CLIMATE_CURRENT_BANDS_DEG[2], 65.0f32, __LINE__);
    PIN_F32(CLIMATE_CURRENT_WARM_C, 4.0f32);
    PIN_F32(CLIMATE_CURRENT_COLD_C, -5.0f32);
    PIN_F32(CLIMATE_CURRENT_LAND_SCALE, 600.0f32);
    PIN_F32(CLIMATE_SEA_PEAK_DAY, 215.0f32);
    PIN_F32(CLIMATE_CURRENT_WEAK, 0.5f32);
    PIN_F32(CLIMATE_CURRENT_FULL_WITHIN, 32.0f32);
    PIN_F32(CLIMATE_HEAT_LOW_HPA_PER_C, -0.6f32);
    PIN_F32(CLIMATE_WIND_SHIFT, 0.5f32);
    PIN_F32(CLIMATE_WIND_LAG_DAYS, 30.0f32);
    PIN_F32_2(CLIMATE_TRADE_DIR, -1.0f32, -0.4f32);
    PIN_F32(CLIMATE_TRADE_SPEED, 6.0f32);
    PIN_F32_2(CLIMATE_WESTERLY_DIR, 1.0f32, 0.3f32);
    PIN_F32(CLIMATE_WESTERLY_SPEED, 8.0f32);
    PIN_F32_2(CLIMATE_POLAR_DIR, -1.0f32, -0.2f32);
    PIN_F32(CLIMATE_POLAR_SPEED, 5.0f32);
    PIN_F32_2(CLIMATE_BELT_EDGES_DEG, 30.0f32, 60.0f32);
    PIN_F32(CLIMATE_BELT_BLEND_DEG, 6.0f32);
    PIN_F32(CLIMATE_HEMISPHERE_BLEND_DEG, 3.0f32);
    PIN_F32(CLIMATE_DOLDRUMS_DEG, 5.0f32);
    PIN_F32(CLIMATE_DOLDRUMS_SPEED, 0.4f32);
    PIN_F32(CLIMATE_HORSE_SPEED, 0.5f32);
    PIN_F32(CLIMATE_HORSE_DEG, 2.0f32);
    PIN_F32(CLIMATE_CALM_RAMP_DEG, 3.0f32);
    PIN_F32(CLIMATE_PRESSURE_WIND_K, 900.0f32);
    PIN_F32(CLIMATE_PRESSURE_TURN_DEG, 70.0f32);
    PIN_F32(CLIMATE_ALTITUDE_SCALE, 800.0f32);
    PIN_F32(CLIMATE_ALTITUDE_MAX, 2.5f32);
    PIN_F32(CLIMATE_STEER, 0.7f32);
    PIN_U(CLIMATE_FIT_DAYS, 12u);
    PIN_U(CLIMATE_AIR_DAY_VALUES, 3u);
}

TEST(every_constant_has_the_value_its_table_gives_temperature_and_rain) {
    PIN_F32(CLIMATE_LAPSE_C_PER_BLOCK, 0.012f32);
    PIN_F32(CLIMATE_BASIN_C_PER_BLOCK, 0.025f32);
    PIN_F32(CLIMATE_LAG_BASE_DAYS, 25.0f32);
    PIN_F32(CLIMATE_LAG_SEA_DAYS, 20.0f32);
    PIN_F32(CLIMATE_RANGE_COAST_C, 4.0f32);
    PIN_F32(CLIMATE_RANGE_INLAND_EXTRA_C, 8.0f32);
    PIN_F32(CLIMATE_RANGE_RAIN_CUT, 0.6f32);
    PIN_F32(CLIMATE_WET_DAY_SCALE_MM, 6.0f32);
    PIN_U(CLIMATE_WALK_STEPS_WINDOW, 16u);
    PIN_F32(CLIMATE_WALK_STEP_BLOCKS, 128.0f32);
    PIN_F32(CLIMATE_WALK_NONE_BLOCKS, 8192.0f32);
    PIN_U(CLIMATE_WALK_STEPS_MAX, 64u);
    PIN_F32(CLIMATE_WALK_CALM, 1.0e-3f32);
    PIN_F32(CLIMATE_MOISTURE_MIN, 0.15f32);
    PIN_F32(CLIMATE_MOISTURE_SCALE_BLOCKS, 3000.0f32);
    PIN_F32(CLIMATE_MOISTURE_WARM, 1.25f32);
    PIN_F32(CLIMATE_MOISTURE_COLD, 0.4f32);
    PIN_F32(CLIMATE_MOISTURE_COLD_BLOCKS, 800.0f32);
    PIN_F32(CLIMATE_OROGRAPHIC_GAIN, 8.0f32);
    PIN_F32(CLIMATE_OROGRAPHIC_RISE_BLOCKS, 256.0f32);
    PIN_F32(CLIMATE_SHADOW_BLOCKS, 400.0f32);
    PIN_F32(CLIMATE_OROGRAPHIC_MIN, 0.15f32);
    PIN_F32(CLIMATE_OROGRAPHIC_MAX, 2.5f32);
    PIN_F32(CLIMATE_SNOW_ALL_RAIN_C, 1.0f32);
    PIN_F32(CLIMATE_SNOW_ALL_SNOW_C, -2.0f32);
}

TEST(every_constant_has_the_value_its_table_gives_seasons_and_classes) {
    PIN_F32(CLIMATE_HURRICANE_COAST_BLOCKS, 3000.0f32);
    PIN_F32(CLIMATE_HURRICANE_SEA_C, 26.5f32);
    PIN_F32(CLIMATE_HURRICANE_RAMP_C, 2.0f32);
    PIN_F32(CLIMATE_HURRICANE_LAT_MIN_DEG, 5.0f32);
    PIN_F32(CLIMATE_HURRICANE_LAT_MAX_DEG, 25.0f32);
    PIN_F32(CLIMATE_HURRICANE_SEASON_S, 0.25f32);
    PIN_F32(CLIMATE_MONSOON_RAIN_RATIO, 3.0f32);
    PIN_U(CLIMATE_HURRICANE_DAY_SCALE, 4u);
    PIN_F32(CLIMATE_DRY_THRESHOLD_PER_C, 20.0f32);
    PIN_F32(CLIMATE_DRY_THRESHOLD_BASE_MM, 140.0f32);
    PIN_F32(CLIMATE_TUNDRA_MAX_C, 10.0f32);
    PIN_F32(CLIMATE_TROPICAL_MIN_C, 18.0f32);
    PIN_F32(CLIMATE_WET_MONTH_MM, 60.0f32);
    PIN_F32(CLIMATE_TEMPERATE_MIN_C, -3.0f32);
    PIN_F32(CLIMATE_CONTINENTAL_MAX_C, 22.0f32);
    PIN_F32(CLIMATE_DRY_SUMMER_MM, 40.0f32);
    PIN_F32(CLIMATE_DRY_SUMMER_SHARE, 1.0f32 / 3.0f32);
    PIN_F32(CLIMATE_MONTH_WAVE1, 0.98862f32);
    PIN_F32(CLIMATE_MONTH_WAVE2, 0.95493f32);
    PIN_F32(CLIMATE_SNOW_BALANCE_LOW, -250.0f32);
    PIN_F32(CLIMATE_SNOW_BALANCE_HIGH, 2000.0f32);
    PIN_F32(CLIMATE_SNOW_BALANCE_NONE, 9999.0f32);
    PIN_U(CLIMATE_SNOW_BALANCE_HALVINGS, 12u);
    PIN_U(CLIMATE_SNOW_BALANCE_YEARS, 2u);
    PIN_F32(CLIMATE_MELT_MM_PER_DEGREE_DAY, 4.0f32);
}

/* ---- Section 7: weather ------------------------------------------------------------------------------------------------ */

TEST(every_constant_has_the_value_its_table_gives_systems) {
    PIN_F64(CLIMATE_SQUARE_HURRICANE, 16384.0f64);
    PIN_F32(CLIMATE_HURRICANE_RATE, 0.034f32); /* tuned by phase 06 from 7.2's 0.02: the table's Tuned column */
    PIN_F32_2(CLIMATE_HURRICANE_RADIUS, 1500.0f32, 4000.0f32);
    PIN_F32_2(CLIMATE_HURRICANE_EYE, 80.0f32, 150.0f32);
    PIN_F32_2(CLIMATE_HURRICANE_HPA, -30.0f32, -90.0f32);
    PIN_F32_2(CLIMATE_HURRICANE_LIFE_DAYS, 4.0f32, 10.0f32);
    PIN_F32(CLIMATE_HURRICANE_HALF_LIFE_DAYS, 1.0f32); /* 7.2's "by half each game day": 0.5 a day */
    PIN_F32(CLIMATE_HURRICANE_COOL_SEA_C, 26.0f32);
    PIN_F64(CLIMATE_LIFE_THIRDS, 3.0f64);
    PIN_F64(CLIMATE_SQUARE_LOW, 16384.0f64);
    PIN_F32(CLIMATE_LOW_PSI_MIN_DEG, 35.0f32);
    PIN_F32(CLIMATE_LOW_PSI_MAX_DEG, 65.0f32);
    PIN_F32(CLIMATE_LOW_RATE_WINTER, 0.18f32); /* tuned by phase 06 from 0.08 */
    PIN_F32(CLIMATE_LOW_RATE_SUMMER, 0.09f32); /* tuned by phase 06 from 0.04 */
    PIN_F32_2(CLIMATE_LOW_RADIUS, 3000.0f32, 7000.0f32);
    PIN_F32_2(CLIMATE_LOW_HPA, -10.0f32, -30.0f32);
    PIN_F32_2(CLIMATE_LOW_LIFE_DAYS, 3.0f32, 6.0f32);
    PIN_F32_2(CLIMATE_LOW_SPEED, 5000.0f32, 7000.0f32); /* "about 6 km a day" */
    PIN_F32_2(CLIMATE_LOW_FRONT_BLOCKS, 4000.0f32, 8000.0f32);
    PIN_F32(CLIMATE_LOW_POLEWARD, 0.08f32);
    PIN_F32(CLIMATE_LOW_PEAK_DAY, 200.0f32);
    PIN_F64(CLIMATE_SQUARE_HIGH, 24576.0f64);
    PIN_F32(CLIMATE_HIGH_PSI_MIN_DEG, 20.0f32);
    PIN_F32(CLIMATE_HIGH_PSI_MAX_DEG, 45.0f32);
    PIN_F32(CLIMATE_HIGH_INTERIOR_PSI_MAX_DEG, 70.0f32);
    PIN_F32(CLIMATE_HIGH_INTERIOR_BLOCKS, 3000.0f32);
    PIN_F32(CLIMATE_HIGH_RATE, 0.03f32);
    PIN_F32_2(CLIMATE_HIGH_RADIUS, 6000.0f32, 12000.0f32);
    PIN_F32_2(CLIMATE_HIGH_HPA, 8.0f32, 20.0f32);
    PIN_F32_2(CLIMATE_HIGH_LIFE_DAYS, 6.0f32, 14.0f32);
    PIN_F32_2(CLIMATE_HIGH_SPEED, 1500.0f32, 2500.0f32); /* "about 2 km a day" */
    PIN_F32(CLIMATE_HIGH_EQUATORWARD, 0.1f32);
    PIN_F32(CLIMATE_HIGH_RAIN_SHARE, 0.2f32);
    PIN_F64(CLIMATE_SQUARE_THUNDER, 1024.0f64);
    PIN_F32_2(CLIMATE_THUNDER_RADIUS, 150.0f32, 400.0f32);
    PIN_F32_2(CLIMATE_THUNDER_HPA, -1.0f32, -3.0f32);
    PIN_F32_2(CLIMATE_THUNDER_LIFE_HOURS, 1.0f32, 4.0f32);
    PIN_F32(CLIMATE_THUNDER_RATE, 3.0f32);
    PIN_F32(CLIMATE_THUNDER_RATE_GAIN, 0.22f32); /* tuned by phase 06 from 1 */
    PIN_F32(CLIMATE_THUNDER_HOT_C, 18.0f32);
    PIN_F32(CLIMATE_THUNDER_HOT_RANGE_C, 10.0f32);
    PIN_F32(CLIMATE_THUNDER_MONSOON, 2.0f32);
    PIN_F32(CLIMATE_THUNDER_FRONT, 4.0f32); /* CLIMATE_FRONT_CELL_FACTOR 5 is 1 + this */
    PIN_F32(CLIMATE_THUNDER_HOT_FLOOR, 0.2f32);
    PIN_F32(CLIMATE_THUNDER_RAIN_SCALE_BASE, 0.333333333f32);
    PIN_F32_2(CLIMATE_THUNDER_START_HOURS, 13.0f32, 19.0f32);
    PIN_F32_2(CLIMATE_THUNDER_MM_HOUR, 20.0f32, 60.0f32);
    PIN_F32(CLIMATE_THUNDER_SPEED_MAX, 15.0f32);
    PIN_F64(CLIMATE_TRACK_STEP_DAYS, 0.25f64);
    PIN_F32(CLIMATE_HURRICANE_SPEED_WEST, 3500.0f32);
    PIN_F32_2(CLIMATE_HURRICANE_TURN_PSI_DEG, 20.0f32, 28.0f32);
    PIN_F32(CLIMATE_HURRICANE_TURN_DAYS, 2.0f32);
    PIN_F32(CLIMATE_HURRICANE_SPEED_EAST, 6000.0f32);
    PIN_F32(CLIMATE_HURRICANE_POLEWARD_WEST, 0.15f32);
    PIN_F32(CLIMATE_HURRICANE_POLEWARD_EAST, 0.3f32);
    PIN_F32_2(CLIMATE_SPEED_SCALE, 0.85f32, 1.15f32);
    PIN_F32(CLIMATE_HURRICANE_OPEN_SEA_BLOCKS, 2048.0f32);
    PIN_F32(CLIMATE_WIND_PROFILE_POWER, 0.6f32);
    PIN_F32(CLIMATE_HURRICANE_VMAX_AT_30, 33.0f32);
    PIN_F32(CLIMATE_HURRICANE_VMAX_AT_90, 70.0f32);
    PIN_F32(CLIMATE_HURRICANE_STORM_VMAX, 33.0f32);
    PIN_F32(CLIMATE_SYSTEM_CORE_SHARE, 0.5f32);
    PIN_F32(CLIMATE_SYSTEM_VMAX_PER_HPA, 0.4f32);
    PIN_F32(CLIMATE_SYSTEM_REACH_RADII, 3.0f32);
    PIN_F32(CLIMATE_SYSTEM_WIND_TAPER_RADII, 2.0f32);
    PIN_F32(CLIMATE_SYSTEM_DEAD_HPA, 5.0f32);
    PIN_F32(CLIMATE_HURRICANE_EYEWALL_MM_HOUR, 40.0f32);
    PIN_F32(CLIMATE_HURRICANE_EYEWALL_HPA, 90.0f32);
    PIN_F32_2(CLIMATE_LOW_SHIELD_MM_HOUR, 1.0f32, 4.0f32);
    PIN_F32(CLIMATE_FRONT_MM_HOUR, 3.0f32);
    PIN_F32(CLIMATE_FRONT_HALF_WIDTH, 400.0f32);
    PIN_F32(CLIMATE_SYSTEM_RAIN_SHARE_TROPICS, 0.3f32);
    PIN_F32(CLIMATE_SYSTEM_RAIN_SHARE_MID, 0.5f32);
    PIN_F32_2(CLIMATE_SYSTEM_RAIN_SHARE_BLEND_DEG, 25.0f32, 50.0f32);
    PIN_F32(CLIMATE_SYSTEM_RAIN_SHARE_POLAR, 0.4f32);
    PIN_F32(CLIMATE_SYSTEM_RAIN_WET_MM_DAY, 5.0f32);
    PIN_F32_2(CLIMATE_SYSTEM_RAIN_SHARE_POLAR_DEG, 60.0f32, 70.0f32);
    PIN_F32(CLIMATE_SYSTEM_RAIN_SEASON_MARGIN_DEG, 5.0f32);
    PIN_F32(CLIMATE_DRIFT_POLE_STOP_DEG, 85.0f32);
    PIN_F32(CLIMATE_DRIFT_EQUATOR_STOP_DEG, 5.0f32);
    PIN_F32(CLIMATE_HURRICANE_BAND_ARMS, 3.0f32);
    PIN_F32(CLIMATE_HURRICANE_BAND_WIND, 2.5f32);
    PIN_F32(CLIMATE_LOW_SHIELD_MEAN, 2.5f32);
    PIN_F32(CLIMATE_LOW_SHIELD_AHEAD, 0.5f32);
    PIN_F32(CLIMATE_LOW_SHIELD_WIDTH, 0.6f32);
    PIN_F32(CLIMATE_FRONT_TURN_X, -0.5f32);
    PIN_F32(CLIMATE_FRONT_TURN_Y, 0.8660254f32);
    PIN_F32(CLIMATE_THUNDER_BIG_MARGIN, 1250.0f32);
    /* The longest life and the reach the list looks back over: 7.3's 14 days, and the hurricane's ~35 km. */
    PIN_F32(CLIMATE_HIGH_LIFE_DAYS[1], 14.0f32);
    CHECK(climate_systems_reach(CLIMATE_SYSTEM_HURRICANE) > 35000.0f64);
}

TEST(every_constant_has_the_value_its_table_gives_daily_weather_and_week) {
    PIN_F64(CLIMATE_SPELL_BLOCKS, 2500.0f64);
    PIN_F64(CLIMATE_SPELL_DAYS, 10.0f64);
    PIN_F32(CLIMATE_SIGMA_T_COAST, 2.0f32);
    PIN_F32(CLIMATE_SIGMA_T_INLAND_EXTRA, 3.0f32);
    PIN_F32(CLIMATE_HIGH_ANOMALY_SIGMAS, 0.5f32);
    PIN_F32(CLIMATE_CLOUD_RANGE_CUT, 0.6f32);
    PIN_F32(CLIMATE_RAIN_NOISE_GAIN, 0.9f32);
    PIN_F32(CLIMATE_DAY_RAIN_MM, 1.0f32);
    PIN_F32(CLIMATE_STORM_DEPTH_HPA, 20.0f32);
    PIN_F32(CLIMATE_STORM_WIND, 15.0f32);
    PIN_F32(CLIMATE_HEAT_WAVE_SIGMAS, 1.5f32);
    PIN_F32(CLIMATE_DUST_WIND, 12.0f32);
    PIN_F32(CLIMATE_DUST_WEEK_RAIN_MM, 2.0f32);
    PIN_F32(CLIMATE_FOG_CALM_WIND, 3.0f32);
    PIN_F32(CLIMATE_FOG_COAST_BLOCKS, 2000.0f32);
    PIN_F32(CLIMATE_EVAPORATION_MM_PER_DEGREE_DAY, 0.2f32);
    PIN_F32(CLIMATE_SOIL_MM_MAX, 150.0f32);
    PIN_F32(CLIMATE_ICE_GROW_CM_PER_DEGREE_DAY, 1.5f32);
    PIN_F32(CLIMATE_ICE_MELT_CM_PER_DEGREE_DAY, 2.0f32);
    PIN_F32(CLIMATE_FLOOD_BANKFULL, 3.0f32);
    PIN_F32(CLIMATE_FLOOD_CHANNEL_MIN, 20.0f32);
    PIN_F32(CLIMATE_CLOUD_UNDER_HIGH, 0.05f32);
    PIN_F32(CLIMATE_CLOUD_CLEAR, 0.2f32);
    PIN_F32(CLIMATE_CLOUD_CLOUDY, 0.6f32);
    PIN_F32(CLIMATE_CLOUD_STORM, 0.75f32);
    PIN_F32(CLIMATE_CLOUD_PER_MM_HOUR, 0.25f32);
    PIN_F64(CLIMATE_FOG_MORNING_HOURS, 4.0f64);
    PIN_F32(CLIMATE_DUST_FULL_WIND, 20.0f32);
    PIN_F64(CLIMATE_RAIN_AMOUNT_BLOCKS, 2000.0f64);
    PIN_F64(CLIMATE_RAIN_AMOUNT_DAYS, 3.0f64);
    PIN_F64(CLIMATE_RAIN_HOURS_BLOCKS, 1500.0f64);
    PIN_F64(CLIMATE_RAIN_HOURS_HOURS, 6.0f64);
    PIN_F32(CLIMATE_SPELL_CLAMP, 3.0f32);
    PIN_F32(CLIMATE_SPELL_RAW_SD, 0.2013f32); /* measured by phase 07; run_climate_weather holds them within 3% */
    PIN_F32(CLIMATE_RAIN_AMOUNT_RAW_SD, 0.2008f32);
    PIN_F32(CLIMATE_RAIN_HOURS_RAW_SD, 0.2015f32);
    PIN_F64(CLIMATE_WEATHER_SHARE_BLOCKS, 8192.0f64);
    PIN_F32(CLIMATE_FOG_VALLEY_BLOCKS, 30.0f32);
    PIN_F32(CLIMATE_FOG_VALLEY_SOIL_MM, 100.0f32);
    PIN_F32(CLIMATE_CLOUDY_WEEK_RAIN_MM, 10.0f32);
    PIN_F32(CLIMATE_CLOUDY_LOW_RADII, 1.5f32);
    PIN_F32(CLIMATE_ICE_EVENT_CM, 5.0f32);
    PIN_U(CLIMATE_WEEK_SPINUP_YEARS, 2u);
}

/* ---- Section 8 and phases 08 to 10 ----------------------------------------------------------------------------------- */

TEST(every_constant_has_the_value_its_table_gives_outlook_and_later_phases) {
    PIN_U(CLIMATE_OUTLOOK_DAYS, 7u);
    PIN_F64(CLIMATE_OUTLOOK_SIGMA_START, 200.0f64);
    PIN_F64(CLIMATE_OUTLOOK_SIGMA_PER_DAY, 400.0f64);
    PIN_F32(CLIMATE_OUTLOOK_SPREAD_SIGMAS, 1.5f32);
    PIN_F64(CLIMATE_OUTLOOK_REACH, 50000.0f64);
    PIN_F32(CLIMATE_OUTLOOK_CELL_RADIUS, 275.0f32);
    PIN_F32(CLIMATE_OUTLOOK_CELL_HOURS, 2.5f32);
    PIN_F32(CLIMATE_OUTLOOK_DEEP_LOW_SHARE, 0.5f32);
    PIN_U(CLIMATE_OUTLOOK_FOOTPRINT,
          CLIMATE_OUTLOOK_FOOTPRINT_MASTER); /* master plan 8's footprint, not the exact one */
    PIN_F32(GAME_COVER_ICE_ON_CM, 5.0f32);
    PIN_F32(GAME_COVER_SNOW_ON_MM, 20.0f32);
    PIN_F64(PLAYER_WIND_SHARE, 0.04f64);
    PIN_F64(PLAYER_WIND_THRESHOLD, 10.0f64);
    PIN_F64(CLIMATE_WARNING_THRESHOLD, 0.5f64);
    PIN_U(CLIMATE_WARNING_TICKS, 200u);
    PIN_U(UI_ATLAS_NOW_REDRAW_MS, 500u);
    PIN_F32(UI_ATLAS_SHADE_EXAGGERATION, 1.0f32);
    PIN_U(CLIMATE_WARNING_STEPS, 5u);
    PIN_F64(CLIMATE_WARNING_STEP_DAYS, 0.25f64);
    /* The Atlas's scales (phases 05 and 09, master plan 9.3): what each colour means. */
    PIN_F32(CLIMATE_LAYER_PRESSURE_LOW_HPA, 990.0f32);
    PIN_F32(CLIMATE_LAYER_PRESSURE_MID_HPA, 1013.0f32);
    PIN_F32(CLIMATE_LAYER_PRESSURE_HIGH_HPA, 1030.0f32);
    PIN_F32(CLIMATE_LAYER_PRESSURE_LINE_HPA, 4.0f32);
    PIN_F32(CLIMATE_LAYER_WIND_TOP, 15.0f32);
    PIN_F32(CLIMATE_LAYER_LAND_TOP, 454.0f32);
    PIN_F32(CLIMATE_LAYER_SNOW_NEAR_BLOCKS, 50.0f32);
    PIN_F32(CLIMATE_LAYER_SNOW_FAR_BLOCKS, 2000.0f32);
    static const float32_t TEMPERATURE_C[CLIMATE_LAYER_TEMPERATURE_STOPS] = {-30.0f32, -10.0f32, 0.0f32,
                                                                             15.0f32,  30.0f32,  45.0f32};
    static const float32_t RAIN_MM[CLIMATE_LAYER_RAIN_STOPS] = {0.0f32,    250.0f32,  500.0f32,
                                                                1000.0f32, 2000.0f32, 3000.0f32};
    static const float32_t RADAR_MM_HOUR[CLIMATE_LAYER_RADAR_STOPS] = {0.1f32,  1.0f32,  4.0f32,
                                                                       10.0f32, 25.0f32, 50.0f32};
    for (uint32_t i = 0u; i < CLIMATE_LAYER_TEMPERATURE_STOPS; i++) {
        pin_f32(t, "CLIMATE_LAYER_TEMPERATURE_C[i]", CLIMATE_LAYER_TEMPERATURE_C[i], TEMPERATURE_C[i], __LINE__);
    }
    for (uint32_t i = 0u; i < CLIMATE_LAYER_RAIN_STOPS; i++) {
        pin_f32(t, "CLIMATE_LAYER_RAIN_MM[i]", CLIMATE_LAYER_RAIN_MM[i], RAIN_MM[i], __LINE__);
    }
    for (uint32_t i = 0u; i < CLIMATE_LAYER_RADAR_STOPS; i++) {
        pin_f32(t, "CLIMATE_LAYER_RADAR_MM_HOUR[i]", CLIMATE_LAYER_RADAR_MM_HOUR[i], RADAR_MM_HOUR[i], __LINE__);
    }
}

/* ---- Knot tables ------------------------------------------------------------------------------------------------------- */

static void knots_check(test_context_t *t, const char *name, const float64_t *latitudes, const float64_t *values,
                        const float64_t *expected_latitudes, const float64_t *expected_values, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        const bool rises = i == 0u || latitudes[i] > latitudes[i - 1u];
        const bool same = latitudes[i] == expected_latitudes[i] && values[i] == expected_values[i];
        if (!rises || !same) {
            printf("  %s knot %u: (%g, %g), the table says (%g, %g)%s\n", name, i, (libc_double_t)latitudes[i],
                   (libc_double_t)values[i], (libc_double_t)expected_latitudes[i], (libc_double_t)expected_values[i],
                   rises ? "" : ", and the latitudes do not rise");
        }
        test_check(t, rises && same, __FILE__, __LINE__, name);
    }
}

TEST(knot_tables_have_every_knot_in_order) {
    static const float64_t LAT_15[CLIMATE_LAT_KNOT_COUNT] = {0.0f64,  15.0f64, 30.0f64, 45.0f64,
                                                             60.0f64, 75.0f64, 90.0f64};
    static const float64_t PRESSURE[CLIMATE_LAT_KNOT_COUNT] = {1008.0f64, 1012.0f64, 1020.0f64, 1013.0f64,
                                                               1000.0f64, 1010.0f64, 1020.0f64};
    static const float64_t TEMPERATURE[CLIMATE_LAT_KNOT_COUNT] = {27.0f64, 26.0f64,  20.0f64, 11.0f64,
                                                                  0.0f64,  -14.0f64, -24.0f64};
    static const float64_t LAT_30[CLIMATE_SWING_KNOT_COUNT] = {0.0f64, 30.0f64, 60.0f64, 90.0f64};
    static const float64_t SWING_SEA[CLIMATE_SWING_KNOT_COUNT] = {1.0f64, 3.0f64, 6.0f64, 10.0f64};
    static const float64_t SWING_LAND[CLIMATE_SWING_KNOT_COUNT] = {2.0f64, 9.0f64, 18.0f64, 22.0f64};
    static const float64_t LAT_RAIN[CLIMATE_RAIN_KNOT_COUNT] = {0.0f64,  10.0f64, 20.0f64, 30.0f64, 40.0f64,
                                                                50.0f64, 60.0f64, 75.0f64, 90.0f64};
    static const float64_t RAIN[CLIMATE_RAIN_KNOT_COUNT] = {2400.0f64, 1800.0f64, 700.0f64, 300.0f64, 800.0f64,
                                                            1000.0f64, 800.0f64,  350.0f64, 150.0f64};
    knots_check(t, "CLIMATE_PRESSURE_KNOTS", CLIMATE_KNOTS_LAT_DEG, CLIMATE_PRESSURE_KNOTS, LAT_15, PRESSURE,
                CLIMATE_LAT_KNOT_COUNT);
    knots_check(t, "CLIMATE_TEMPERATURE_KNOTS", CLIMATE_KNOTS_LAT_DEG, CLIMATE_TEMPERATURE_KNOTS, LAT_15, TEMPERATURE,
                CLIMATE_LAT_KNOT_COUNT);
    knots_check(t, "CLIMATE_SWING_SEA_KNOTS", CLIMATE_SWING_KNOTS_LAT_DEG, CLIMATE_SWING_SEA_KNOTS, LAT_30, SWING_SEA,
                CLIMATE_SWING_KNOT_COUNT);
    knots_check(t, "CLIMATE_SWING_LAND_KNOTS", CLIMATE_SWING_KNOTS_LAT_DEG, CLIMATE_SWING_LAND_KNOTS, LAT_30,
                SWING_LAND, CLIMATE_SWING_KNOT_COUNT);
    knots_check(t, "CLIMATE_RAIN_KNOTS", CLIMATE_RAIN_KNOTS_LAT_DEG, CLIMATE_RAIN_KNOTS, LAT_RAIN, RAIN,
                CLIMATE_RAIN_KNOT_COUNT);
    /* And the curves the climate reads are built from these very tables: each passes through every knot. */
    climate_normals_t normals;
    climate_normals_fill(&normals);
    for (uint32_t i = 0u; i < CLIMATE_LAT_KNOT_COUNT; i++) {
        CHECK_NEAR(climate_normals_p_belt(&normals, LAT_15[i]), PRESSURE[i], 1.0e-12f64);
        CHECK_NEAR(climate_normals_t_mean(&normals, LAT_15[i]), TEMPERATURE[i], 1.0e-12f64);
    }
    for (uint32_t i = 0u; i < CLIMATE_SWING_KNOT_COUNT; i++) {
        CHECK_NEAR(climate_normals_a_sea(&normals, LAT_30[i]), SWING_SEA[i], 1.0e-12f64);
        CHECK_NEAR(climate_normals_a_land(&normals, LAT_30[i]), SWING_LAND[i], 1.0e-12f64);
    }
    for (uint32_t i = 0u; i < CLIMATE_RAIN_KNOT_COUNT; i++) {
        CHECK_NEAR(climate_normals_rain_belt(&normals, LAT_RAIN[i]), RAIN[i], 1.0e-12f64);
    }
}

/* ---- The literal sweep ------------------------------------------------------------------------------------------------- */

/* The allowlist, each entry with why. Anything else in a function body, or as an array's size, is a number with no name.
 *  - 0, 1, 2 (and -1, as the sign is not part of a literal) and 0.5: counting, halves, doubling, signs.
 *  - 3, only in the smoothstep 3t^2 - 2t^3 written as t t (3 - 2 t): pure maths.
 *  - 180 and 360: degrees in a half and a whole turn.
 *  - pi, 2 pi, pi / 2 and pi / 4 written out: pure maths.
 *  - An array subscript such as w[3]: an element's place in an array whose layout its header documents. Not an array's
 *    size: a declaration's [N] is checked.
 *  - Outside function bodies, the numbers of named definitions: a file's static const tables and scalars, enum members,
 *    #defines. Each is a named constant, which is the point; a header holds the ones other files or this test need.
 *  - static_assert lines and _padN[] members: the coding standard requires their numbers (sizes, offsets, padding). */
enum : uint32_t {
    SWEEP_FILES_MAX = 128u,
    SWEEP_PATH_BYTES = 256u,
    SWEEP_FILE_BYTES = 1u << 20,
    SWEEP_LINE_BYTES = 4096u,
    SWEEP_REPORT_MAX = 64u,
};

typedef struct sweep_result_t {
    uint32_t files;    /* 0 */
    uint32_t literals; /* 4: numbers that are not on the allowlist */
} sweep_result_t;
static_assert(sizeof(sweep_result_t) == 8);

static bool sweep_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* Comments to spaces and the insides of strings and characters to spaces, line breaks kept, so a number inside either is
 * not seen and every line keeps its number. */
static void sweep_blank(char *text, uint64_t bytes) {
    uint64_t i = 0u;
    while (i < bytes) {
        if (text[i] == '/' && i + 1u < bytes && text[i + 1u] == '*') {
            while (i < bytes && !(text[i] == '*' && i + 1u < bytes && text[i + 1u] == '/')) {
                text[i] = text[i] == '\n' ? '\n' : ' ';
                i++;
            }
            if (i < bytes) {
                text[i] = ' ';
                text[i + 1u] = ' ';
                i += 2u;
            }
        } else if (text[i] == '/' && i + 1u < bytes && text[i + 1u] == '/') {
            while (i < bytes && text[i] != '\n') {
                text[i++] = ' ';
            }
        } else if (text[i] == '"' || text[i] == '\'') {
            const char quote = text[i++];
            while (i < bytes && text[i] != quote && text[i] != '\n') {
                if (text[i] == '\\' && i + 1u < bytes) {
                    text[i++] = ' ';
                }
                text[i++] = ' ';
            }
            i++;
        } else {
            i++;
        }
    }
}

static bool sweep_pi(float64_t v) {
    static const float64_t PI_FORMS[] = {0.25f64, 0.5f64, 1.0f64, 2.0f64};
    for (uint32_t k = 0u; k < sizeof(PI_FORMS) / sizeof(PI_FORMS[0]); k++) {
        if (fabs(v - PI_FORMS[k] * CLIMATE_PI) < 1.0e-6f64) {
            return true;
        }
    }
    return false;
}

/* Whether the text before position end (the start of an identifier followed by [) ends in a type: a declaration. */
static bool sweep_declares(const char *line, uint64_t end) {
    uint64_t i = end;
    while (i > 0u && line[i - 1u] == ' ') {
        i--;
    }
    /* const, then *, may come between the type and the name. */
    if (i >= 5u && strncmp(line + i - 5u, "const", 5u) == 0 && (i == 5u || !sweep_ident(line[i - 6u]))) {
        const uint64_t before = i - 5u;
        uint64_t j = before;
        while (j > 0u && line[j - 1u] == ' ') {
            j--;
        }
        if (j > 0u && line[j - 1u] == '*') {
            i = j - 1u;
            while (i > 0u && line[i - 1u] == ' ') {
                i--;
            }
        } else {
            return true; /* "const float32_t NAME[" reads as type, then const: either way a declaration */
        }
    } else if (i > 0u && line[i - 1u] == '*') {
        i--;
        while (i > 0u && line[i - 1u] == ' ') {
            i--;
        }
    }
    uint64_t word = i;
    while (word > 0u && sweep_ident(line[word - 1u])) {
        word--;
    }
    const uint64_t length = i - word;
    if (length == 0u) {
        return false;
    }
    const char *w = line + word;
    return (length >= 2u && w[length - 2u] == '_' && w[length - 1u] == 't') ||
           (length == 4u && !strncmp(w, "char", 4u)) || (length == 4u && !strncmp(w, "bool", 4u)) ||
           (length == 5u && !strncmp(w, "const", 5u));
}

/* One file's text, already blanked: counts, and prints up to SWEEP_REPORT_MAX of, the numbers not allowed. */
static uint32_t sweep_text(const char *path, const char *text, uint64_t bytes, uint32_t *reported) {
    uint32_t found = 0u;
    int32_t depth = 0;
    bool in_function = false;
    char last = '\0';
    uint32_t line_number = 1u;
    uint64_t start = 0u;
    char line[SWEEP_LINE_BYTES];
    bool at[SWEEP_LINE_BYTES]; /* in a function body, at each character */
    while (start < bytes) {
        uint64_t end = start;
        while (end < bytes && text[end] != '\n') {
            end++;
        }
        uint64_t length = end - start;
        length = length >= SWEEP_LINE_BYTES ? SWEEP_LINE_BYTES - 1u : length;
        memcpy(line, text + start, length);
        line[length] = '\0';
        for (uint64_t i = 0u; i < length; i++) {
            at[i] = in_function;
            if (line[i] == '{') {
                in_function = in_function || (depth == 0 && last == ')');
                depth++;
            } else if (line[i] == '}') {
                depth--;
                in_function = in_function && depth > 0;
            }
            last = line[i] != ' ' && line[i] != '\t' ? line[i] : last;
        }
        const char *first = line;
        while (*first == ' ' || *first == '\t') {
            first++;
        }
        const bool skip = *first == '#' || strncmp(first, "static_assert", 13u) == 0 || strstr(line, "_pad") != nullptr;
        const bool smoothstep =
            strstr(line, "3.0f32 - 2.0f32") != nullptr || strstr(line, "3.0f64 - 2.0f64") != nullptr;
        for (uint64_t i = 0u; !skip && i < length; i++) {
            const bool digit = line[i] >= '0' && line[i] <= '9';
            const bool dot_digit = line[i] == '.' && i + 1u < length && line[i + 1u] >= '0' && line[i + 1u] <= '9';
            if ((!digit && !dot_digit) || (i > 0u && (sweep_ident(line[i - 1u]) || line[i - 1u] == '.'))) {
                continue;
            }
            /* The number, then its suffix: f32, f64, u, ull. strtod reads decimal and hex, integers and floats. */
            char *after = nullptr;
            const float64_t value = strtod(line + i, &after);
            uint64_t stop = (uint64_t)(after - line);
            stop = stop <= i ? i + 1u : stop;
            while (stop < length && sweep_ident(line[stop])) {
                stop++;
            }
            const uint64_t token = i;
            i = stop - 1u;
            /* A subscript or a declaration's size: [N] with N alone. */
            uint64_t open = token;
            while (open > 0u && line[open - 1u] == ' ') {
                open--;
            }
            uint64_t close = stop;
            while (close < length && line[close] == ' ') {
                close++;
            }
            bool subscript = false, declaration = false;
            if (open > 0u && line[open - 1u] == '[' && close < length && line[close] == ']') {
                uint64_t name_end = open - 1u;
                while (name_end > 0u && line[name_end - 1u] == ' ') {
                    name_end--;
                }
                uint64_t name = name_end;
                while (name > 0u && sweep_ident(line[name - 1u])) {
                    name--;
                }
                subscript = name < name_end;
                declaration = subscript && sweep_declares(line, name);
            }
            if (!at[token] && !declaration) {
                continue; /* a named definition's number */
            }
            const bool allowed = value == 0.0f64 || value == 1.0f64 || value == 2.0f64 || value == 0.5f64 ||
                                 value == 180.0f64 || value == 360.0f64 || sweep_pi(value) ||
                                 (value == 3.0f64 && smoothstep) || (subscript && !declaration);
            if (allowed) {
                continue;
            }
            found++;
            if (*reported < SWEEP_REPORT_MAX) {
                printf("  %s:%u: %.*s\n", path, line_number, (int32_t)(stop - token), line + token);
                (*reported)++;
            }
        }
        line_number++;
        start = end + 1u;
    }
    return found;
}

/* Every index.c under root, by an explicit stack of directories (no recursion). */
static uint32_t sweep_list(const char *root, char (*paths)[SWEEP_PATH_BYTES], uint32_t count,
                           char (*stack)[SWEEP_PATH_BYTES]) {
    uint32_t depth = 0u;
    snprintf(stack[depth++], SWEEP_PATH_BYTES, "%s", root);
    while (depth > 0u) {
        char directory[SWEEP_PATH_BYTES];
        snprintf(directory, sizeof(directory), "%s", stack[--depth]);
        DIR *const listing = opendir(directory);
        if (listing == nullptr) {
            continue;
        }
        for (struct dirent *entry = readdir(listing); entry != nullptr; entry = readdir(listing)) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            char path[SWEEP_PATH_BYTES];
            if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int32_t)sizeof(path)) {
                continue; /* no path in this repository is this long */
            }
            if (entry->d_type == DT_DIR && depth < SWEEP_FILES_MAX) {
                snprintf(stack[depth++], SWEEP_PATH_BYTES, "%s", path);
            } else if (strcmp(entry->d_name, "index.c") == 0 && count < SWEEP_FILES_MAX) {
                snprintf(paths[count++], SWEEP_PATH_BYTES, "%s", path);
            }
        }
        closedir(listing);
    }
    return count;
}

static sweep_result_t sweep_run(test_context_t *t) {
    char (*const paths)[SWEEP_PATH_BYTES] = test_alloc(t, (uint64_t)SWEEP_FILES_MAX * SWEEP_PATH_BYTES);
    char (*const stack)[SWEEP_PATH_BYTES] = test_alloc(t, (uint64_t)SWEEP_FILES_MAX * SWEEP_PATH_BYTES);
    char *const text = test_alloc(t, SWEEP_FILE_BYTES);
    uint32_t count = sweep_list("run/src/calendar", paths, 0u, stack);
    count = sweep_list("run/src/climate", paths, count, stack);
    sweep_result_t result = {.files = count};
    uint32_t reported = 0u;
    for (uint32_t f = 0u; f < count; f++) {
        uint64_t bytes = 0u;
        if (file_read_whole(paths[f], text, SWEEP_FILE_BYTES - 1u, &bytes) != FILE_OK) {
            printf("  could not read %s\n", paths[f]);
            result.literals++;
            continue;
        }
        sweep_blank(text, bytes);
        result.literals += sweep_text(paths[f], text, bytes, &reported);
    }
    return result;
}

TEST(no_unnamed_number_in_the_climate_code) {
    const sweep_result_t result = sweep_run(t);
    printf("swept %u files: %u numbers with no name\n", result.files, result.literals);
    CHECK(result.files >= 20u); /* the calendar and every climate part: an empty sweep proves nothing */
    CHECK_EQ_U64(result.literals, 0u);
}

/* The sweep itself, on a sample: it must catch what it is for and pass what the allowlist allows. */
TEST(the_sweep_catches_an_unnamed_number) {
    static const char SAMPLE[] =
        "static const float32_t NAMED = 0.012f32; /* 7.5 */\n"
        "static const uint32_t TABLE[4] = {1u, 2u, 3u, 4u};\n"
        "float32_t f(const float32_t w[CLIMATE_CORNERS], float32_t x) {\n"
        "    float32_t a[6];\n"                   /* caught: a size */
        "    const float32_t y = x * 0.012f32;\n" /* caught */
        "    const float32_t s = y * y * (3.0f32 - 2.0f32 * y) + w[3];\n"
        "    static_assert(sizeof(a) == 24);\n"
        "    return s * 0.5f32 + 2.0f32 * 3.14159265358979323846f32 + \"9.5\"[0] + 0x10u;\n" /* 0x10 */
        "}\n";
    char text[sizeof(SAMPLE)];
    memcpy(text, SAMPLE, sizeof(SAMPLE));
    sweep_blank(text, sizeof(SAMPLE) - 1u);
    uint32_t reported = 0u;
    CHECK_EQ_U64(sweep_text("sample", text, sizeof(SAMPLE) - 1u, &reported), 4u); /* TABLE[4], a[6], 0.012, 0x10u */
}

TEST_MAIN()
