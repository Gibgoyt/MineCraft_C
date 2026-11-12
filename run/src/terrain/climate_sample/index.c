/* See README.md. Plan 006 phase 02: the climate sample's reads, its warp and its fallback. Everything that decides a value
 * is integer arithmetic on the sample, or det_* and + - * / on float64 in a written order, so the same bits on any machine. */
#include "terrain/climate_sample/index.h"

#include "machine/index.h"
#include "math/det/index.h"
#include "terrain/density/index.h"
#include "terrain/rng/index.h"

#include <math.h> /* floor and fabs only: exact, IEEE 754 */
#include <string.h>

/* ---- The climate's numbers the fallback needs ------------------------------------------------------------------------ */

/* Copies of plan 004's tables in climate/normals and the latitude in climate/index.h: terrain may not include climate, so
 * each lives here too, and run_terrain_climate_sample holds every one equal to the climate's own. */
#define SAMPLE_TILT_DEG 23.5f64
#define SAMPLE_TROPIC_Y 10000.0f64
#define SAMPLE_POLE_Y (SAMPLE_TROPIC_Y * 90.0f64 / SAMPLE_TILT_DEG)
#define SAMPLE_LATITUDE_PERIOD (4.0f64 * SAMPLE_POLE_Y)
#define SAMPLE_PI 3.14159265358979323846f64
#define SAMPLE_POLE_DEG 90.0f64
#define SAMPLE_KAPPA 0.5f64    /* master 2.2 */
#define SAMPLE_MOISTURE 0.6f64 /* master 2.2: M */
#define SAMPLE_OROGRAPHY 1.0f64

enum : uint32_t {
    SAMPLE_LAT_KNOTS = 7u,
    SAMPLE_SWING_KNOTS = 4u,
    SAMPLE_RAIN_KNOT_COUNT = 9u,
    SAMPLE_MONTHS = 12u,
};
static const float64_t SAMPLE_KNOTS_LAT_DEG[SAMPLE_LAT_KNOTS] = {0.0f64,  15.0f64, 30.0f64, 45.0f64,
                                                                 60.0f64, 75.0f64, 90.0f64};
static const float64_t SAMPLE_TEMPERATURE_KNOTS[SAMPLE_LAT_KNOTS] = {27.0f64, 26.0f64,  20.0f64, 11.0f64,
                                                                     0.0f64,  -14.0f64, -24.0f64};
static const float64_t SAMPLE_SWING_KNOTS_LAT_DEG[SAMPLE_SWING_KNOTS] = {0.0f64, 30.0f64, 60.0f64, 90.0f64};
static const float64_t SAMPLE_SWING_SEA_KNOTS[SAMPLE_SWING_KNOTS] = {1.0f64, 3.0f64, 6.0f64, 10.0f64};
static const float64_t SAMPLE_SWING_LAND_KNOTS[SAMPLE_SWING_KNOTS] = {2.0f64, 9.0f64, 18.0f64, 22.0f64};
static const float64_t SAMPLE_RAIN_KNOTS_LAT_DEG[SAMPLE_RAIN_KNOT_COUNT] = {0.0f64,  10.0f64, 20.0f64, 30.0f64, 40.0f64,
                                                                            50.0f64, 60.0f64, 75.0f64, 90.0f64};
static const float64_t SAMPLE_RAIN_KNOTS[SAMPLE_RAIN_KNOT_COUNT] = {2400.0f64, 1800.0f64, 700.0f64, 300.0f64, 800.0f64,
                                                                    1000.0f64, 800.0f64,  350.0f64, 150.0f64};

/* The warp: two octaves of value noise, 256 and 128 blocks (CLIMATE_WARP_FREQUENCY's 1/256, lacunarity 2), in 1/65536ths. */
enum : int64_t {
    WARP_ONE = 65536,
    WARP_HALF_ONE = 32768,
    WARP_CELL_1 = 256,
    WARP_CELL_2 = 128,
};

/* ---- Integer helpers ------------------------------------------------------------------------------------------------- */

/* a / b rounded toward minus infinity, b > 0. */
static int64_t sample_floor_div(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

/* a / b rounded to the nearest, halves up, b > 0. */
static int64_t sample_round_div(int64_t a, int64_t b) {
    return sample_floor_div(2 * a + b, 2 * b);
}

static int64_t sample_abs(int64_t v) {
    return v < 0 ? -v : v;
}

/* ---- Small public pieces --------------------------------------------------------------------------------------------- */

void terrain_climate_sample_origin(int64_t west_x, int64_t south_y, int64_t *out_x, int64_t *out_y) {
    *out_x = sample_floor_div(west_x - TERRAIN_CLIMATE_WEST_BLOCKS, TERRAIN_CLIMATE_CELL_BLOCKS) *
             TERRAIN_CLIMATE_CELL_BLOCKS;
    *out_y = sample_floor_div(south_y - TERRAIN_CLIMATE_WEST_BLOCKS, TERRAIN_CLIMATE_CELL_BLOCKS) *
             TERRAIN_CLIMATE_CELL_BLOCKS;
}

bool terrain_climate_sample_holds(const terrain_climate_sample_t *sample, int64_t x_low, int64_t y_low, int64_t x_high,
                                  int64_t y_high) {
    /* A read at u = at - origin - 32 stays on the cells for u from 0 to 64 (SIDE - 1); the warp moves it up to 32. */
    const int64_t low = TERRAIN_CLIMATE_HALF_CELL + TERRAIN_CLIMATE_WARP_BLOCKS;
    const int64_t high = TERRAIN_CLIMATE_HALF_CELL + TERRAIN_CLIMATE_CELL_BLOCKS * (TERRAIN_CLIMATE_SIDE - 1) -
                         TERRAIN_CLIMATE_WARP_BLOCKS;
    return x_low - sample->origin_x >= low && y_low - sample->origin_y >= low && x_high - sample->origin_x <= high &&
           y_high - sample->origin_y <= high;
}

uint32_t terrain_climate_warp_seed(uint64_t seed) {
    return (uint32_t)terrain_rng_derive(seed, terrain_rng_salt("terrain/climate_sample/warp"));
}

int32_t terrain_climate_round(float64_t v) {
    if (v != v) {
        return 0;
    }
    if (v >= (float64_t)INT32_MAX) {
        return INT32_MAX;
    }
    if (v <= (float64_t)INT32_MIN) {
        return INT32_MIN;
    }
    /* Adding 1.5 * 2^52 leaves no fraction bits, so the add itself rounds, to nearest with halves to even. */
    const float64_t big = 0x1.8p52f64;
    const float64_t rounded = (v + big) - big;
    return (int32_t)rounded;
}

void terrain_climate_cell_make(float64_t t_mean_c, float64_t t_swing_c, float64_t rain_mm_year,
                               float64_t snow_balance_z, float64_t dry_month_mm, uint8_t climate_class, uint8_t flags,
                               terrain_climate_cell_t *out) {
    memset(out, 0, sizeof(*out));
    out->t_mean_cc = terrain_climate_round(t_mean_c * 100.0f64);
    out->t_swing_cc = terrain_climate_round(t_swing_c * 100.0f64);
    out->rain_mm_year = terrain_climate_round(rain_mm_year);
    const int32_t line = terrain_climate_round(snow_balance_z);
    out->snow_line_z = line >= TERRAIN_CLIMATE_NO_SNOW_LINE ? TERRAIN_CLIMATE_NO_SNOW_LINE : line;
    out->dry_month_mm = terrain_climate_round(dry_month_mm);
    out->climate_class = climate_class;
    out->flags = flags;
}

uint8_t terrain_climate_classify(int32_t t_mean_cc, int32_t t_warm_cc, int32_t t_cold_cc, int32_t rain_mm_year,
                                 int32_t dry_month_mm, bool monsoon) {
    if (t_warm_cc < 0) {
        return TERRAIN_CLIMATE_CLASS_ICE;
    }
    if (t_warm_cc < TERRAIN_CLIMATE_TUNDRA_MAX_CC) {
        return TERRAIN_CLIMATE_CLASS_TUNDRA;
    }
    /* P_th = 20 T + 140 mm, in hundredths of a mm so T stays in centi-degrees. */
    const int64_t threshold_x100 =
        (int64_t)TERRAIN_CLIMATE_DRY_PER_C * t_mean_cc + (int64_t)TERRAIN_CLIMATE_DRY_BASE_MM * 100;
    const int64_t rain_x100 = (int64_t)rain_mm_year * 100;
    if (2 * rain_x100 < threshold_x100) {
        return TERRAIN_CLIMATE_CLASS_DESERT;
    }
    if (rain_x100 < threshold_x100) {
        return TERRAIN_CLIMATE_CLASS_STEPPE;
    }
    if (t_cold_cc >= TERRAIN_CLIMATE_TROPICAL_MIN_CC) {
        if (dry_month_mm >= TERRAIN_CLIMATE_WET_MONTH_MM) {
            return TERRAIN_CLIMATE_CLASS_RAINFOREST;
        }
        return monsoon ? TERRAIN_CLIMATE_CLASS_MONSOON : TERRAIN_CLIMATE_CLASS_SAVANNA;
    }
    if (t_cold_cc > TERRAIN_CLIMATE_TEMPERATE_MIN_CC) {
        return TERRAIN_CLIMATE_CLASS_TEMPERATE; /* the sample keeps no dry summer: Mediterranean is the climate's call */
    }
    return t_warm_cc >= TERRAIN_CLIMATE_CONTINENTAL_MAX_CC ? TERRAIN_CLIMATE_CLASS_CONTINENTAL
                                                           : TERRAIN_CLIMATE_CLASS_BOREAL;
}

/* ---- The warp -------------------------------------------------------------------------------------------------------- */

/* A lattice point's value in [-32768, 32767]. */
static int64_t warp_lattice(uint64_t seed, int64_t ix, int64_t iy) {
    return (int64_t)(terrain_rng_hash2(seed, (int32_t)ix, (int32_t)iy) >> 48u) - WARP_HALF_ONE;
}

/* 3f^2 - 2f^3 of f in 65536ths. */
static int64_t warp_smooth(int64_t f) {
    return ((f * f) >> 16) * (3 * WARP_ONE - 2 * f) >> 16; /* f in [0, 65536]: every step is at least 0 */
}

/* One octave of value noise at (x, y) with lattice cells of cell blocks, in [-32768, 32767]. */
static int64_t warp_octave(uint64_t seed, int64_t x, int64_t y, int64_t cell) {
    const int64_t ix = sample_floor_div(x, cell), iy = sample_floor_div(y, cell);
    const int64_t sx = warp_smooth((x - ix * cell) * WARP_ONE / cell);
    const int64_t sy = warp_smooth((y - iy * cell) * WARP_ONE / cell);
    const int64_t a = warp_lattice(seed, ix, iy), b = warp_lattice(seed, ix + 1, iy);
    const int64_t c = warp_lattice(seed, ix, iy + 1), d = warp_lattice(seed, ix + 1, iy + 1);
    const int64_t south = a + sample_floor_div((b - a) * sx, WARP_ONE);
    const int64_t north = c + sample_floor_div((d - c) * sx, WARP_ONE);
    return south + sample_floor_div((north - south) * sy, WARP_ONE);
}

/* The climate's spread of a raw field, unit (1.5 - 0.5 unit^2) of the clamped unit, in 65536ths of 32 blocks, to blocks. */
static int64_t warp_blocks(uint64_t seed, int64_t x, int64_t y) {
    const int64_t raw = (2 * warp_octave(seed, x, y, WARP_CELL_1) + warp_octave(seed, x, y, WARP_CELL_2)) / 3;
    int64_t unit = 2 * raw * 2; /* 32768ths to 65536ths, then doubled: half the field's reach is the warp's whole */
    unit = unit > WARP_ONE ? WARP_ONE : (unit < -WARP_ONE ? -WARP_ONE : unit);
    const int64_t spread = sample_floor_div(unit * (3 * WARP_HALF_ONE - ((unit * unit) >> 17)), WARP_ONE);
    return sample_round_div(spread * TERRAIN_CLIMATE_WARP_BLOCKS, WARP_ONE);
}

void terrain_climate_warp(uint32_t warp_seed, int64_t x, int64_t y, int64_t *out_x, int64_t *out_y) {
    const uint64_t seed_x = (uint64_t)warp_seed | (1ull << 32u);
    const uint64_t seed_y = (uint64_t)warp_seed | (2ull << 32u);
    *out_x = x + warp_blocks(seed_x, x, y);
    *out_y = y + warp_blocks(seed_y, x, y);
}

/* ---- The read -------------------------------------------------------------------------------------------------------- */

/* The cell index and the fraction in 64ths along one axis, kept on the sample. */
static void sample_axis(int64_t at, int64_t origin, int64_t *out_cell, int64_t *out_fraction) {
    const int64_t u = at - origin - TERRAIN_CLIMATE_HALF_CELL;
    int64_t cell = sample_floor_div(u, TERRAIN_CLIMATE_CELL_BLOCKS);
    int64_t fraction = u - cell * TERRAIN_CLIMATE_CELL_BLOCKS;
    if (cell < 0) {
        cell = 0;
        fraction = 0;
    } else if (cell > TERRAIN_CLIMATE_SIDE - 2) {
        cell = TERRAIN_CLIMATE_SIDE - 2;
        fraction = TERRAIN_CLIMATE_CELL_BLOCKS;
    }
    *out_cell = cell;
    *out_fraction = fraction;
}

terrain_climate_point_t terrain_climate_at(const terrain_climate_sample_t *sample, int64_t x, int64_t y, int32_t z) {
    int64_t read_x = x, read_y = y;
    if (sample->warp_off == 0u) {
        terrain_climate_warp(sample->warp_seed, x, y, &read_x, &read_y);
    }
    int64_t i = 0, j = 0, fx = 0, fy = 0;
    sample_axis(read_x, sample->origin_x, &i, &fx);
    sample_axis(read_y, sample->origin_y, &j, &fy);
    const terrain_climate_cell_t *const corner[4] = {&sample->cells[j][i], &sample->cells[j][i + 1],
                                                     &sample->cells[j + 1][i], &sample->cells[j + 1][i + 1]};
    const int64_t side = TERRAIN_CLIMATE_CELL_BLOCKS;
    const int64_t weight[4] = {(side - fx) * (side - fy), fx * (side - fy), (side - fx) * fy, fx * fy};
    const int64_t total = side * side;
#define SAMPLE_BLEND(field)                                                                                            \
    sample_round_div((int64_t)corner[0]->field *weight[0] + (int64_t)corner[1]->field * weight[1] +                    \
                         (int64_t)corner[2]->field * weight[2] + (int64_t)corner[3]->field * weight[3],                \
                     total)
    const int64_t t_sea = SAMPLE_BLEND(t_mean_cc);
    const int64_t swing = sample_abs(SAMPLE_BLEND(t_swing_cc));
    terrain_climate_point_t out;
    memset(&out, 0, sizeof(out));
    out.rain_mm_year = (int32_t)SAMPLE_BLEND(rain_mm_year);
    out.dry_month_mm = (int32_t)SAMPLE_BLEND(dry_month_mm);
#undef SAMPLE_BLEND
    /* The snow line over the corners that are not sea, as climate_snow_balance_blend has it: a sea cell's 9999 means no
     * height, not a height. */
    int64_t line_sum = 0, line_weight = 0;
    for (uint32_t k = 0u; k < 4u; k++) {
        if ((corner[k]->flags & TERRAIN_CLIMATE_SEA) == 0u) {
            line_sum += (int64_t)corner[k]->snow_line_z * weight[k];
            line_weight += weight[k];
        }
    }
    const int64_t line = line_weight == 0 ? TERRAIN_CLIMATE_NO_SNOW_LINE : sample_round_div(line_sum, line_weight);
    out.snow_line_z = (int32_t)(line >= TERRAIN_CLIMATE_NO_SNOW_LINE ? TERRAIN_CLIMATE_NO_SNOW_LINE : line);
    /* The nearest cell: the climate's t >= 0.5. */
    const terrain_climate_cell_t *const nearest =
        corner[(fx >= TERRAIN_CLIMATE_HALF_CELL ? 1u : 0u) + (fy >= TERRAIN_CLIMATE_HALF_CELL ? 2u : 0u)];
    out.climate_class = nearest->climate_class;
    out.flags = nearest->flags;
    out.from_climate = sample->from_climate;
    /* Plan 004 6.6 at z: minus 1.2 centi-degrees a block, and on land below sea level plus 2.5, each rounded down. */
    const bool land = (nearest->flags & (TERRAIN_CLIMATE_SEA | TERRAIN_CLIMATE_LAKE)) == 0u;
    int64_t t = t_sea - sample_floor_div((int64_t)z * TERRAIN_CLIMATE_LAPSE_CC_PER_10, 10);
    if (land && z < 0) {
        t += sample_floor_div(-(int64_t)z * TERRAIN_CLIMATE_BASIN_CC_PER_10, 10);
    }
    const int64_t month = sample_round_div(swing * TERRAIN_CLIMATE_MONTH_WAVE_NUM, TERRAIN_CLIMATE_MONTH_WAVE_DEN);
    out.t_mean_cc = (int32_t)t;
    out.t_warm_cc = (int32_t)(t + month);
    out.t_cold_cc = (int32_t)(t - month);
    return out;
}

/* ---- The fallback ---------------------------------------------------------------------------------------------------- */

/* The four curves the fallback reads: built once a sample. */
typedef struct sample_curves_t {
    terrain_density_spline_t t_mean; /* 0 */
    terrain_density_spline_t a_sea;  /* 296 */
    terrain_density_spline_t a_land; /* 592 */
    terrain_density_spline_t rain;   /* 888 */
} sample_curves_t;
static_assert(sizeof(sample_curves_t) == 1184);

static void sample_curves_build(sample_curves_t *curves) {
    memset(curves, 0, sizeof(*curves));
    terrain_density_spline_build(&curves->t_mean, SAMPLE_KNOTS_LAT_DEG, SAMPLE_TEMPERATURE_KNOTS, SAMPLE_LAT_KNOTS);
    terrain_density_spline_build(&curves->a_sea, SAMPLE_SWING_KNOTS_LAT_DEG, SAMPLE_SWING_SEA_KNOTS,
                                 SAMPLE_SWING_KNOTS);
    terrain_density_spline_build(&curves->a_land, SAMPLE_SWING_KNOTS_LAT_DEG, SAMPLE_SWING_LAND_KNOTS,
                                 SAMPLE_SWING_KNOTS);
    terrain_density_spline_build(&curves->rain, SAMPLE_RAIN_KNOTS_LAT_DEG, SAMPLE_RAIN_KNOTS, SAMPLE_RAIN_KNOT_COUNT);
}

/* climate_latitude_deg, the same operations in the same order. */
static float64_t sample_latitude_deg(float64_t y) {
    const float64_t period = SAMPLE_LATITUDE_PERIOD;
    const float64_t near = y - period * floor((y + 0.5f64 * period) / period);
    const float64_t degrees = det_asin(det_sin(SAMPLE_PI * near / (2.0f64 * SAMPLE_POLE_Y))) * (180.0f64 / SAMPLE_PI);
    return degrees > SAMPLE_POLE_DEG ? SAMPLE_POLE_DEG : (degrees < -SAMPLE_POLE_DEG ? -SAMPLE_POLE_DEG : degrees);
}

static void sample_fallback_cell(const sample_curves_t *curves, int64_t centre_y, terrain_climate_cell_t *out) {
    const float64_t phi = sample_latitude_deg((float64_t)centre_y);
    const float64_t a = fabs(phi);
    const float64_t t_mean = terrain_density_spline_at(&curves->t_mean, a);
    const float64_t sea = terrain_density_spline_at(&curves->a_sea, a);
    const float64_t land = terrain_density_spline_at(&curves->a_land, a);
    /* climate_air_swing's order: sea + kappa (land - sea), times the hemisphere, +1 at exactly 0. */
    const float64_t swing = (sea + SAMPLE_KAPPA * (land - sea)) * (phi < 0.0f64 ? -1.0f64 : 1.0f64);
    const float64_t rain = terrain_density_spline_at(&curves->rain, a) * SAMPLE_MOISTURE * SAMPLE_OROGRAPHY;
    terrain_climate_cell_make(t_mean, swing, rain, (float64_t)TERRAIN_CLIMATE_NO_SNOW_LINE,
                              rain / (float64_t)SAMPLE_MONTHS, TERRAIN_CLIMATE_CLASS_NONE, 0u, out);
    /* The warmest month at sea level, as terrain_climate_at works it out; snow lasts the summer above the height where it
     * reaches 0 with the lapse. Below the world's floor it lasts everywhere; above the world's top, nowhere. */
    const int64_t month =
        sample_round_div(sample_abs(out->t_swing_cc) * TERRAIN_CLIMATE_MONTH_WAVE_NUM, TERRAIN_CLIMATE_MONTH_WAVE_DEN);
    const int64_t warm = (int64_t)out->t_mean_cc + month;
    const int64_t cold = (int64_t)out->t_mean_cc - month;
    const int64_t line = sample_floor_div(warm * 10, TERRAIN_CLIMATE_LAPSE_CC_PER_10);
    out->snow_line_z =
        line > MC_WORLD_Z_MAX ? TERRAIN_CLIMATE_NO_SNOW_LINE : (int32_t)(line < MC_WORLD_Z_MIN ? MC_WORLD_Z_MIN : line);
    out->climate_class = terrain_climate_classify(out->t_mean_cc, (int32_t)warm, (int32_t)cold, out->rain_mm_year,
                                                  out->dry_month_mm, false);
}

void terrain_climate_fallback_cell(int64_t centre_y, terrain_climate_cell_t *out) {
    sample_curves_t curves;
    sample_curves_build(&curves);
    sample_fallback_cell(&curves, centre_y, out);
}

void terrain_climate_fallback(uint64_t seed, int64_t west_x, int64_t south_y, terrain_climate_sample_t *out) {
    memset(out, 0, sizeof(*out));
    terrain_climate_sample_origin(west_x, south_y, &out->origin_x, &out->origin_y);
    out->warp_seed = terrain_climate_warp_seed(seed);
    sample_curves_t curves;
    sample_curves_build(&curves);
    /* The fallback knows no x: one cell a row, copied across. */
    for (int32_t j = 0; j < TERRAIN_CLIMATE_SIDE; j++) {
        terrain_climate_cell_t row;
        sample_fallback_cell(
            &curves, out->origin_y + (int64_t)j * TERRAIN_CLIMATE_CELL_BLOCKS + TERRAIN_CLIMATE_HALF_CELL, &row);
        for (int32_t i = 0; i < TERRAIN_CLIMATE_SIDE; i++) {
            out->cells[j][i] = row;
        }
    }
}
