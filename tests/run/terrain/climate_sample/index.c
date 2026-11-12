/* ctest run_terrain_climate_sample. See README.md. */
#include "harness/index.h"

#include "climate/air/index.h"
#include "climate/cell/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/normals/index.h"
#include "climate/rain/index.h"
#include "climate/season/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "terrain/climate_sample/index.h"
#include "thread/index.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16); /* the phase file's 16 */

enum : uint32_t {
    SAMPLE_TEST_FILLS = 4096u, /* column samples filled on 1 worker and on 16 */
    SAMPLE_TEST_WORKERS = 16u,
    SAMPLE_TEST_POINTS = 100000u,
};
static const uint64_t SAMPLE_TEST_SEED = 0x006002C11ull;

/* Every mirrored number equal to the climate's, so the seam cannot drift. */
TEST(the_sample_sizes_are_the_master_s) {
    CHECK_EQ_U64(sizeof(terrain_climate_cell_t), 24u);
    CHECK_EQ_U64(sizeof(terrain_climate_sample_t), 888u);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CELL_BLOCKS, CLIMATE_CELL_BLOCKS);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CELL_SHIFT, CLIMATE_CELL_SHIFT);
    CHECK_EQ_I64(TERRAIN_CLIMATE_WARP_BLOCKS, CLIMATE_WARP_BLOCKS);
    CHECK_EQ_I64(TERRAIN_CLIMATE_NO_SNOW_LINE, (int64_t)CLIMATE_SNOW_BALANCE_NONE);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CLASS_COUNT, CLIMATE_CLASS_COUNT);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CLASS_ICE, CLIMATE_CLASS_ICE);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CLASS_DESERT, CLIMATE_CLASS_DESERT);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CLASS_RAINFOREST, CLIMATE_CLASS_RAINFOREST);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CLASS_MEDITERRANEAN, CLIMATE_CLASS_MEDITERRANEAN);
    CHECK_EQ_I64(TERRAIN_CLIMATE_CLASS_BOREAL, CLIMATE_CLASS_BOREAL);
    CHECK_EQ_I64(TERRAIN_CLIMATE_TUNDRA_MAX_CC, (int64_t)(CLIMATE_TUNDRA_MAX_C * 100.0f32));
    CHECK_EQ_I64(TERRAIN_CLIMATE_TROPICAL_MIN_CC, (int64_t)(CLIMATE_TROPICAL_MIN_C * 100.0f32));
    CHECK_EQ_I64(TERRAIN_CLIMATE_TEMPERATE_MIN_CC, (int64_t)(CLIMATE_TEMPERATE_MIN_C * 100.0f32));
    CHECK_EQ_I64(TERRAIN_CLIMATE_CONTINENTAL_MAX_CC, (int64_t)(CLIMATE_CONTINENTAL_MAX_C * 100.0f32));
    CHECK_EQ_I64(TERRAIN_CLIMATE_WET_MONTH_MM, (int64_t)CLIMATE_WET_MONTH_MM);
    CHECK_EQ_I64(TERRAIN_CLIMATE_DRY_PER_C, (int64_t)CLIMATE_DRY_THRESHOLD_PER_C);
    CHECK_EQ_I64(TERRAIN_CLIMATE_DRY_BASE_MM, (int64_t)CLIMATE_DRY_THRESHOLD_BASE_MM);
    CHECK((float32_t)TERRAIN_CLIMATE_LAPSE_CC_PER_10 / 1000.0f32 == CLIMATE_LAPSE_C_PER_BLOCK);
    CHECK((float32_t)TERRAIN_CLIMATE_BASIN_CC_PER_10 / 1000.0f32 == CLIMATE_BASIN_C_PER_BLOCK);
    CHECK((float32_t)TERRAIN_CLIMATE_MONTH_WAVE_NUM / (float32_t)TERRAIN_CLIMATE_MONTH_WAVE_DEN == CLIMATE_MONTH_WAVE1);
    /* The read reach the column gate checks covers every cell a column's features read (phase 02 step 7). */
    CHECK_EQ_I64(CLIMATE_READ_REACH_BLOCKS, TERRAIN_CLIMATE_WEST_BLOCKS);
}

static void sample_test_flat(terrain_climate_sample_t *sample, int32_t t_mean_cc) {
    memset(sample, 0, sizeof(*sample));
    sample->warp_off = 1u;
    for (int32_t j = 0; j < TERRAIN_CLIMATE_SIDE; j++) {
        for (int32_t i = 0; i < TERRAIN_CLIMATE_SIDE; i++) {
            sample->cells[j][i].t_mean_cc = t_mean_cc;
            sample->cells[j][i].snow_line_z = TERRAIN_CLIMATE_NO_SNOW_LINE;
        }
    }
}

/* Cell centres at 64 i + 32. Between the centres of cells (1, 1), (2, 1), (1, 2), (2, 2), a quarter across and three
 * quarters up: 1000, 2000, 3000, 4000 give 1250 along the south, 3250 along the north, and 2750 three quarters up, exactly,
 * in 64ths. */
TEST(the_blend_is_exact_with_the_warp_off) {
    terrain_climate_sample_t sample;
    sample_test_flat(&sample, 0);
    sample.origin_x = -128;
    sample.origin_y = 256;
    sample.cells[1][1].t_mean_cc = 1000;
    sample.cells[1][2].t_mean_cc = 2000;
    sample.cells[2][1].t_mean_cc = 3000;
    sample.cells[2][2].t_mean_cc = 4000;
    sample.cells[1][1].rain_mm_year = 400;
    sample.cells[1][2].rain_mm_year = 800;
    sample.cells[2][1].rain_mm_year = 400;
    sample.cells[2][2].rain_mm_year = 800;
    sample.cells[2][1].climate_class = TERRAIN_CLIMATE_CLASS_BOREAL;
    sample.cells[1][1].snow_line_z = 900;
    sample.cells[1][2].snow_line_z = 1000;
    sample.cells[2][1].snow_line_z = 500;
    sample.cells[2][2].flags = TERRAIN_CLIMATE_SEA; /* its 9999 left out of the snow line */
    const int64_t x = sample.origin_x + 64 + 32 + 16, y = sample.origin_y + 64 + 32 + 48;
    const terrain_climate_point_t at = terrain_climate_at(&sample, x, y, 0);
    CHECK_EQ_I64(at.t_mean_cc, 2750);
    CHECK_EQ_I64(at.rain_mm_year, 500);                           /* a quarter of the way from 400 to 800 */
    CHECK_EQ_I64(at.climate_class, TERRAIN_CLIMATE_CLASS_BOREAL); /* nearest: west, north */
    /* Snow line over the three land corners: (768 x 900 + 256 x 1000 + 2304 x 500) / 3328 = 2099200 / 3328 = 630.8, so 631. */
    CHECK_EQ_I64(at.snow_line_z, 631);
    /* At a centre, the cell's own numbers. */
    CHECK_EQ_I64(terrain_climate_at(&sample, sample.origin_x + 64 + 32, sample.origin_y + 128 + 32, 0).t_mean_cc, 3000);
    /* Past the cells, the edge. */
    sample.cells[0][0].t_mean_cc = -500;
    CHECK_EQ_I64(terrain_climate_at(&sample, sample.origin_x - 4000, sample.origin_y - 4000, 0).t_mean_cc, -500);
    /* All four sea: no snow line. */
    for (int32_t j = 0; j < TERRAIN_CLIMATE_SIDE; j++) {
        for (int32_t i = 0; i < TERRAIN_CLIMATE_SIDE; i++) {
            sample.cells[j][i].flags = TERRAIN_CLIMATE_SEA;
        }
    }
    CHECK_EQ_I64(terrain_climate_at(&sample, x, y, 0).snow_line_z, TERRAIN_CLIMATE_NO_SNOW_LINE);
}

/* Plan 004's 12 degrees a 1000 blocks: 27 degrees at sea level is 12 at the world's top. */
TEST(the_lapse_is_twelve_degrees_a_thousand_blocks) {
    terrain_climate_sample_t sample;
    sample_test_flat(&sample, 2700);
    for (int32_t j = 0; j < TERRAIN_CLIMATE_SIDE; j++) {
        for (int32_t i = 0; i < TERRAIN_CLIMATE_SIDE; i++) {
            sample.cells[j][i].t_swing_cc = -500; /* south of the equator: the size counts, not the sign */
        }
    }
    CHECK_EQ_I64(terrain_climate_at(&sample, 100, 100, 0).t_mean_cc, 2700);
    CHECK_EQ_I64(terrain_climate_at(&sample, 100, 100, 1250).t_mean_cc, 1200);
    CHECK_EQ_I64(terrain_climate_at(&sample, 100, 100, 1).t_mean_cc, 2699); /* 1.2 off, rounded down to 1 */
    CHECK_EQ_I64(terrain_climate_at(&sample, 100, 100, 9).t_mean_cc, 2690); /* 10.8 off, rounded down to 10 */
    /* Below sea level on land: plus 1.2 a block back, and the basin's 2.5. */
    CHECK_EQ_I64(terrain_climate_at(&sample, 100, 100, -100).t_mean_cc, 2700 + 120 + 250);
    /* The months: 5 degrees of swing is 4.94 for a month's mean. */
    const terrain_climate_point_t at = terrain_climate_at(&sample, 100, 100, 0);
    CHECK_EQ_I64(at.t_warm_cc, 2700 + 494);
    CHECK_EQ_I64(at.t_cold_cc, 2700 - 494);
}

TEST(the_rounding_is_half_to_even) {
    CHECK_EQ_I64(terrain_climate_round(2.5f64), 2);
    CHECK_EQ_I64(terrain_climate_round(3.5f64), 4);
    CHECK_EQ_I64(terrain_climate_round(-2.5f64), -2);
    CHECK_EQ_I64(terrain_climate_round(-2.6f64), -3);
    CHECK_EQ_I64(terrain_climate_round(1.0e20f64), INT32_MAX);
    CHECK_EQ_I64(terrain_climate_round(0.0f64 / 0.0f64), 0);
    terrain_climate_cell_t cell;
    terrain_climate_cell_make(27.004f64, -3.0f64, 1440.4f64, 9999.0f64, 120.0f64, 5u, 0u, &cell);
    CHECK_EQ_I64(cell.t_mean_cc, 2700);
    CHECK_EQ_I64(cell.t_swing_cc, -300);
    CHECK_EQ_I64(cell.rain_mm_year, 1440);
    CHECK_EQ_I64(cell.snow_line_z, TERRAIN_CLIMATE_NO_SNOW_LINE);
}

/* The class in integers against the climate's own in floats, over a grid of places. */
TEST(the_class_is_the_climate_s) {
    uint32_t differ = 0u, tried = 0u;
    for (int32_t mean = -3000; mean <= 3000; mean += 250) {
        for (int32_t month = 0; month <= 2000; month += 250) {
            for (int32_t rain = 0; rain <= 3000; rain += 125) {
                for (int32_t dry = 0; dry <= 200; dry += 50) {
                    const uint8_t mine = terrain_climate_classify(mean, mean + month, mean - month, rain, dry, false);
                    const uint8_t theirs = climate_season_classify(
                        (float32_t)mean / 100.0f32, (float32_t)(mean + month) / 100.0f32,
                        (float32_t)(mean - month) / 100.0f32, (float32_t)rain, (float32_t)dry, false, false);
                    differ += mine == theirs ? 0u : 1u;
                    tried++;
                }
            }
        }
    }
    printf("class: %" PRIu32 " of %" PRIu32 " places differ\n", differ, tried);
    CHECK_EQ_U64(differ, 0u);
}

TEST(the_fallback_is_pure_and_defined_everywhere) {
    terrain_climate_sample_t *const first = test_alloc(t, sizeof(terrain_climate_sample_t));
    terrain_climate_sample_t *const again = test_alloc(t, sizeof(terrain_climate_sample_t));
    uint64_t state = 0x5EEDull;
    uint32_t bad = 0u, differ = 0u;
    for (uint32_t i = 0u; i < SAMPLE_TEST_POINTS; i++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        /* Poles and their neighbours, then the world's edge, then anywhere in between. */
        int64_t x = (int64_t)(state >> 40) - (1 << 23);
        int64_t y = (int64_t)((state >> 16) & 0xFFFFFFull) - (1 << 23);
        if (i % 4u == 0u) {
            y = (int64_t)(CLIMATE_POLE_Y) + (int64_t)(i % 129u) - 64;
        } else if (i % 4u == 1u) {
            x = (i & 8u) != 0u ? 29999000 : -29999000;
            y = (i & 16u) != 0u ? 29999000 : -29999000;
        }
        terrain_climate_fallback(SAMPLE_TEST_SEED, x, y, first);
        terrain_climate_fallback(SAMPLE_TEST_SEED, x, y, again);
        differ += memcmp(first, again, sizeof(*first)) == 0 ? 0u : 1u;
        const terrain_climate_point_t at = terrain_climate_at(first, x, y, (int32_t)(i % 1500u) - 250);
        const bool sane = at.climate_class > 0u && at.climate_class < TERRAIN_CLIMATE_CLASS_COUNT &&
                          at.t_mean_cc > -6000 && at.t_mean_cc < 4500 && at.rain_mm_year >= 0 &&
                          at.t_warm_cc >= at.t_cold_cc && first->from_climate == 0u;
        bad += sane ? 0u : 1u;
    }
    CHECK_EQ_U64(differ, 0u);
    CHECK_EQ_U64(bad, 0u);
}

/* The fallback's temperatures against the climate's own formulas at kappa 0.5 and no current, pole to pole. */
TEST(the_fallback_matches_the_climate_at_kappa_half) {
    climate_normals_t normals;
    climate_normals_fill(&normals);
    int64_t worst_mean = 0, worst_swing = 0, worst_rain = 0;
    for (int64_t y = -80000; y <= 80000; y += 97) {
        terrain_climate_cell_t cell;
        terrain_climate_fallback_cell(y, &cell);
        const float64_t phi = climate_latitude_deg((float64_t)y);
        const int64_t mean = terrain_climate_round(climate_normals_t_mean(&normals, phi) * 100.0f64);
        const int64_t swing = terrain_climate_round((float64_t)climate_air_swing(&normals, phi, 0.5f32) * 100.0f64);
        const int64_t rain = terrain_climate_round(climate_normals_rain_belt(&normals, phi) * 0.6f64);
        const int64_t dm = cell.t_mean_cc - mean, ds = cell.t_swing_cc - swing, dr = cell.rain_mm_year - rain;
        worst_mean = dm < 0 ? (-dm > worst_mean ? -dm : worst_mean) : (dm > worst_mean ? dm : worst_mean);
        worst_swing = ds < 0 ? (-ds > worst_swing ? -ds : worst_swing) : (ds > worst_swing ? ds : worst_swing);
        worst_rain = dr < 0 ? (-dr > worst_rain ? -dr : worst_rain) : (dr > worst_rain ? dr : worst_rain);
    }
    printf("fallback against the climate's formulas: mean %" PRId64 " cc, swing %" PRId64 " cc, rain %" PRId64 " mm\n",
           worst_mean, worst_swing, worst_rain);
    CHECK(worst_mean <= 50); /* the phase file's 0.5 degrees; the same formula, so in fact a rounding at most */
    CHECK(worst_swing <= 50);
    CHECK(worst_rain <= 1);
}

/* Plan 004: 27 degrees at the equator at sea level, 12 at the top, so no snow ever lasts the summer there, in the fallback
 * and in the climate's own snow balance at kappa 0.5. */
TEST(the_equator_has_no_snow_line) {
    terrain_climate_sample_t sample;
    terrain_climate_fallback(SAMPLE_TEST_SEED, -8, -8, &sample);
    for (int32_t z = -250; z <= 1250; z += 50) {
        const terrain_climate_point_t at = terrain_climate_at(&sample, 0, 0, z);
        CHECK_EQ_I64(at.snow_line_z, TERRAIN_CLIMATE_NO_SNOW_LINE);
    }
    const terrain_climate_point_t sea = terrain_climate_at(&sample, 0, 0, 0);
    printf("fallback at the equator: %d cc yearly, %d warmest, %d coldest, %d mm, class %u\n", sea.t_mean_cc,
           sea.t_warm_cc, sea.t_cold_cc, sea.rain_mm_year, (uint32_t)sea.climate_class);
    CHECK(sea.t_mean_cc >= 2690 && sea.t_mean_cc <= 2700);
    CHECK_EQ_I64(terrain_climate_at(&sample, 0, 0, 1250).t_mean_cc, sea.t_mean_cc - 1500);

    climate_normals_t normals;
    climate_normals_fill(&normals);
    climate_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    cell.t_mean = (float32_t)climate_normals_t_mean(&normals, 0.0f64);
    cell.t_swing = climate_air_swing(&normals, 0.0f64, 0.5f32);
    cell.t_lag_days = climate_air_lag_days(0.5f32);
    cell.rain_mm_week[0] =
        (float32_t)(climate_normals_rain_belt(&normals, 0.0f64) * 0.6f64) * CLIMATE_DAYS_A_WEEK / CLIMATE_DAYS_A_YEAR;
    CHECK(climate_season_snow_balance(&cell, true) == CLIMATE_SNOW_BALANCE_NONE);
}

/* Phase 02 step 7: every block whose cell terrain_climate_at may blend, for any point a column's features read (16 past its
 * edges), warped, lies inside the rectangle the column gate asks climate_area_ready about: the column and
 * CLIMATE_READ_REACH_BLOCKS round it. Bilinear between centres reads the cells holding the point plus and minus half a cell. */
TEST(a_feature_on_the_column_edge_reads_a_ready_cell) {
    const uint32_t seed = terrain_climate_warp_seed(SAMPLE_TEST_SEED);
    int64_t worst_out = 0, worst_reach = 0;
    for (int64_t column = -300; column < 300; column += 7) {
        const int64_t west = column * 16, south = -column * 16 + 40000;
        for (int64_t dy = -16; dy < 32; dy += 3) {
            for (int64_t dx = -16; dx < 32; dx++) {
                int64_t rx = 0, ry = 0;
                terrain_climate_warp(seed, west + dx, south + dy, &rx, &ry);
                const int64_t lows[2] = {rx - TERRAIN_CLIMATE_HALF_CELL - (west - CLIMATE_READ_REACH_BLOCKS),
                                         ry - TERRAIN_CLIMATE_HALF_CELL - (south - CLIMATE_READ_REACH_BLOCKS)};
                const int64_t highs[2] = {(west + 15 + CLIMATE_READ_REACH_BLOCKS) - (rx + TERRAIN_CLIMATE_HALF_CELL),
                                          (south + 15 + CLIMATE_READ_REACH_BLOCKS) - (ry + TERRAIN_CLIMATE_HALF_CELL)};
                for (uint32_t k = 0u; k < 2u; k++) {
                    worst_out = lows[k] < 0 && -lows[k] > worst_out ? -lows[k] : worst_out;
                    worst_out = highs[k] < 0 && -highs[k] > worst_out ? -highs[k] : worst_out;
                }
                const int64_t used = (rx - west) < 0 ? west - rx : rx - west;
                worst_reach = used > worst_reach ? used : worst_reach;
            }
        }
    }
    printf("edge reads: the warp moved a read at most %" PRId64 " blocks from the column's corner; %" PRId64
           " blocks past the gate\n",
           worst_reach, worst_out);
    CHECK_EQ_I64(worst_out, 0);
}

/* ---- 1 and 16 workers ----------------------------------------------------------------------------------------------- */

typedef struct sample_test_run_t {
    _Atomic uint32_t ticket;                    /* 0 */
    uint32_t workers;                           /* 4 */
    terrain_climate_sample_t *borrowed_samples; /* 8 */
} sample_test_run_t;
static_assert(sizeof(sample_test_run_t) == 16);

static void sample_test_fill(uint32_t index, terrain_climate_sample_t *out) {
    /* 64 by 64 columns, a chunk apart, round a place 45 degrees north. */
    const int64_t west = ((int64_t)(index % 64u) - 32) * 16;
    const int64_t south = 19149 + ((int64_t)(index / 64u) - 32) * 16;
    terrain_climate_fallback(SAMPLE_TEST_SEED, west, south, out);
}

static void sample_test_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    sample_test_run_t *const run = borrowed_argument;
    const uint32_t first = atomic_fetch_add_explicit(&run->ticket, 1u, memory_order_relaxed);
    for (uint32_t i = first; i < SAMPLE_TEST_FILLS; i += run->workers) {
        sample_test_fill(SAMPLE_TEST_FILLS - 1u - i, &run->borrowed_samples[SAMPLE_TEST_FILLS - 1u - i]);
    }
}

TEST(one_and_sixteen_workers_fill_the_same_samples) {
    const uint64_t bytes = (uint64_t)SAMPLE_TEST_FILLS * sizeof(terrain_climate_sample_t);
    terrain_climate_sample_t *const one = test_alloc(t, bytes);
    terrain_climate_sample_t *const many = test_alloc(t, bytes);
    for (uint32_t i = 0u; i < SAMPLE_TEST_FILLS; i++) {
        sample_test_fill(i, &one[i]);
    }
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));
    sample_test_run_t run = {.workers = SAMPLE_TEST_WORKERS, .borrowed_samples = many};
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < SAMPLE_TEST_WORKERS; i++) {
        job_submit(&system, &group, sample_test_job, &run);
    }
    job_group_wait(&system, &group);
    job_group_deinit(&group);
    job_deinit(&system);
    CHECK_MEM_EQ(one, many, bytes);
}

TEST_MAIN()
