/* ctest run_climate_season. See README.md. The phase 05 file's tests for run/src/climate/season/: hurricane seasons and
 * their packing, the hurricane coast, monsoons, the snow balance height and the climate classes. */
#include "harness/index.h"

#include "climate/air/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/season/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "thread/index.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct season_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
} season_rig_t;
static_assert(sizeof(season_rig_t) == 24);

static void *season_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool season_rig_start(test_context_t *t, season_rig_t *rig) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = season_aligned(t, sizeof(job_system_t));
    rig->climate = season_aligned(t, sizeof(climate_t));
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, MC_THREADS_WORKERS)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return true;
}

static void season_rig_stop(season_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

static climate_normals_t *season_normals(test_context_t *t) {
    climate_normals_t *const normals = season_aligned(t, sizeof(climate_normals_t));
    climate_normals_fill(normals);
    return normals;
}

/* ---- Hurricanes ------------------------------------------------------------------------------------------------------- */

TEST(hurricane_season_at_15_north_over_a_warm_current) {
    const climate_normals_t *const normals = season_normals(t);
    const float32_t peak = climate_air_sea_temperature(normals, 15.0f64, 4.0f32, 215.0f32);
    const float32_t coldest = climate_air_sea_temperature(normals, 15.0f64, 4.0f32, 32.0f32);
    const float32_t bare = climate_air_sea_temperature(normals, 15.0f64, 0.0f32, 32.0f32);
    printf("15 N: T_sea day 215 %.3f, day 32 %.3f (s %.3f), without the current %.3f\n", (libc_double_t)peak,
           (libc_double_t)coldest, (libc_double_t)climate_season_hurricane_s(15.0f32, coldest), (libc_double_t)bare);
    CHECK_NEAR(peak, 31.95f32, 0.01f32);
    CHECK_NEAR(climate_season_hurricane_s(15.0f32, peak), 1.0f32, 0.0f32);
    CHECK_NEAR(coldest, 28.05f32, 0.01f32);
    CHECK_NEAR(climate_season_hurricane_s(15.0f32, coldest), 0.775f32, 0.005f32);
    CHECK(fabsf(climate_season_hurricane_s(15.0f32, coldest) - 0.75f32) <= 0.075f32);
    CHECK_NEAR(bare, 24.05f32, 0.01f32);
    CHECK_NEAR(climate_season_hurricane_s(15.0f32, bare), 0.0f32, 0.0f32);
    uint8_t days[2];
    climate_season_hurricane_days(normals, 15.0f32, 0.0f32, days);
    printf("stored season without the current {%u, %u}\n", days[0], days[1]);
    CHECK_EQ_U64(days[0], 39u);
    CHECK_EQ_U64(days[1], 68u);
    climate_season_hurricane_days(normals, 15.0f32, 4.0f32, days);
    CHECK_EQ_U64(days[0], 0u);
    CHECK_EQ_U64(days[1], 91u);
}

TEST(no_hurricane_season_within_5_degrees_or_past_25) {
    for (float32_t t_sea = 20.0f32; t_sea <= 40.0f32; t_sea += 0.5f32) {
        CHECK_NEAR(climate_season_hurricane_s(4.99f32, t_sea), 0.0f32, 0.0f32);
        CHECK_NEAR(climate_season_hurricane_s(-25.01f32, t_sea), 0.0f32, 0.0f32);
        CHECK_NEAR(climate_season_hurricane_s(25.01f32, t_sea), 0.0f32, 0.0f32);
    }
    CHECK(climate_season_hurricane_s(5.0f32, 30.0f32) > 0.0f32);
    CHECK(climate_season_hurricane_s(25.0f32, 30.0f32) > 0.0f32);
    CHECK(climate_season_hurricane_s(-5.0f32, 30.0f32) > 0.0f32);
}

TEST(hurricane_days_pack_into_quarter_days) {
    const climate_normals_t *const normals = season_normals(t);
    const uint8_t all_year[2] = {0u, 91u};
    CHECK(climate_season_hurricane_today(all_year, 364.0f32));
    CHECK(climate_season_hurricane_today(all_year, 0.0f32));
    uint8_t days[2];
    climate_season_hurricane_days(normals, -15.0f32, 0.0f32, days);
    printf("15 S without a current: {%u, %u} (days %u to %u)\n", days[0], days[1], 4u * days[0], 4u * days[1] + 3u);
    CHECK(days[0] != CLIMATE_DAY_NONE && days[0] > days[1]);
    CHECK(climate_season_hurricane_today(days, 0.0f32));
    CHECK(climate_season_hurricane_today(days, 350.0f32));
    CHECK(!climate_season_hurricane_today(days, 182.0f32));
    const uint8_t none[2] = {CLIMATE_DAY_NONE, CLIMATE_DAY_NONE};
    for (uint32_t d = 0u; d < 365u; d++) {
        CHECK(!climate_season_hurricane_today(none, (float32_t)d));
    }
    climate_season_hurricane_days(normals, 40.0f32, 4.0f32, days);
    CHECK_EQ_U64(days[0], CLIMATE_DAY_NONE);
    CHECK_EQ_U64(days[1], CLIMATE_DAY_NONE);
}

TEST(a_coast_within_3_km_of_a_hurricane_sea_is_flagged) {
    season_rig_t rig;
    REQUIRE(season_rig_start(t, &rig));
    /* Land west of x = 0, the sea east of it: an east coast at 15 N, warm. */
    const ground_t land = ground_continent(-60000, -60000, 0, 60000);
    REQUIRE(climate_open_ground(rig.climate, 9u, ground_fn, &land));
    const int32_t y = 6400 + 32; /* a cell centre near 15 N */
    climate_prepare_area(rig.climate, rig.job, -4100, y - 10, 100, y + 10, nullptr, nullptr);
    climate_cell_t sea, near, far;
    REQUIRE(climate_cell_get(rig.climate, 32, y, &sea));
    REQUIRE(climate_cell_get(rig.climate, -2016, y, &near));
    REQUIRE(climate_cell_get(rig.climate, -4000, y, &far));
    printf("sea {%u, %u} current %u; land 2016 blocks in: distance %.0f, flags 0x%02X, {%u, %u}; 4000 in: %.0f, "
           "flags 0x%02X\n",
           sea.hurricane_days[0], sea.hurricane_days[1], sea.current, (libc_double_t)near.sea_distance, near.flags,
           near.hurricane_days[0], near.hurricane_days[1], (libc_double_t)far.sea_distance, far.flags);
    CHECK((sea.flags & CLIMATE_CELL_SEA) != 0u && sea.hurricane_days[0] != CLIMATE_DAY_NONE);
    CHECK((sea.flags & CLIMATE_CELL_HURRICANE_COAST) == 0u);
    CHECK((near.flags & CLIMATE_CELL_HURRICANE_COAST) != 0u);
    CHECK_EQ_U64(near.hurricane_days[0], sea.hurricane_days[0]);
    CHECK_EQ_U64(near.hurricane_days[1], sea.hurricane_days[1]);
    CHECK((far.flags & CLIMATE_CELL_HURRICANE_COAST) == 0u);
    CHECK_EQ_U64(far.hurricane_days[0], CLIMATE_DAY_NONE);
    CHECK_EQ_U64(far.hurricane_days[1], CLIMATE_DAY_NONE);
    season_rig_stop(&rig);
}

/* ---- Monsoons ---------------------------------------------------------------------------------------------------------- */

/* The Background's worked example: a coast facing south, summer wind (-1.0, 1.5) from the sea on fit day 6, winter wind
 * (-4.0, -1.2) from the land on fit day 0, and rain P0 - a cos theta, whose wet half is 6 P0 + 3.732 a. */
static climate_cell_t season_monsoon_cell(float32_t winter_v, float32_t six_p0, float32_t wave_sum) {
    climate_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    cell.lat_deg = 20.0f32;
    cell.sea_direction[1] = -1.0f32;
    cell.wind[0] = -2.5f32; /* u(0) = -4, u(pi) = -1 */
    cell.wind[1] = -1.5f32;
    cell.wind[3] = 0.5f32 * (winter_v + 1.5f32); /* v(0) = winter_v, v(pi) = 1.5 */
    cell.wind[4] = 0.5f32 * (winter_v - 1.5f32);
    cell.rain_mm_week[0] = six_p0 / 6.0f32;
    cell.rain_mm_week[1] = -wave_sum / 3.7320508f32;
    return cell;
}

TEST(a_monsoon_coast_is_flagged_and_a_steady_one_is_not) {
    const climate_cell_t monsoon = season_monsoon_cell(-1.2f32, 110.0f32, 70.0f32);
    CHECK(climate_season_monsoon_flag(&monsoon, nullptr));
    const climate_cell_t steady = season_monsoon_cell(1.2f32, 110.0f32, 70.0f32);
    CHECK(!climate_season_monsoon_flag(&steady, nullptr));
    const climate_cell_t damp = season_monsoon_cell(-1.2f32, 125.0f32, 55.0f32); /* halves 180 and 70 */
    CHECK(!climate_season_monsoon_flag(&damp, nullptr));
    /* A fixed wind never reverses. */
    const climate_test_hooks_t hooks = {.wind = {-1.0f32, 1.5f32}};
    CHECK(!climate_season_monsoon_flag(&monsoon, &hooks));
}

TEST(monsoon_today_is_the_wet_onshore_half) {
    climate_cell_t cell = season_monsoon_cell(-1.2f32, 110.0f32, 70.0f32);
    CHECK(!climate_season_monsoon_today(&cell, 182.0f32)); /* not flagged yet */
    cell.flags = CLIMATE_CELL_MONSOON;
    CHECK(climate_season_monsoon_today(&cell, 182.0f32));
    CHECK(!climate_season_monsoon_today(&cell, 0.0f32));
}

/* ---- Snow balance ------------------------------------------------------------------------------------------------------- */

static climate_cell_t season_snow_cell(const climate_normals_t *normals, float32_t lat, float32_t kappa,
                                       float32_t mm_year) {
    climate_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    cell.lat_deg = lat;
    cell.kappa = kappa;
    cell.t_mean = (float32_t)climate_normals_t_mean(normals, (float64_t)lat);
    cell.t_swing = climate_air_swing(normals, (float64_t)lat, kappa);
    cell.t_lag_days = climate_air_lag_days(kappa);
    cell.rain_mm_week[0] = mm_year * 7.0f32 / 365.0f32;
    return cell;
}

TEST(snow_balance_is_the_worked_examples) {
    const climate_normals_t *const normals = season_normals(t);
    const climate_cell_t peak = season_snow_cell(normals, 45.0f32, 0.5507f32, 900.0f32);
    const climate_cell_t coast60 = season_snow_cell(normals, 60.0f32, 0.0f32, 800.0f32);
    const climate_cell_t coast75 = season_snow_cell(normals, 75.0f32, 0.0f32, 350.0f32);
    const climate_cell_t equator = season_snow_cell(normals, 0.0f32, 0.5f32, 2000.0f32);
    const float32_t b45 = climate_season_snow_balance(&peak, true);
    const float32_t b60 = climate_season_snow_balance(&coast60, true);
    const float32_t b75 = climate_season_snow_balance(&coast75, true);
    const float32_t b0 = climate_season_snow_balance(&equator, true);
    printf("snow balance: 45 N %.1f, 60 coast %.1f, 75 coast %.1f, equator %.1f\n", (libc_double_t)b45,
           (libc_double_t)b60, (libc_double_t)b75, (libc_double_t)b0);
    CHECK_NEAR(b45, 1484.0f32, 16.0f32);
    CHECK_NEAR(b60, 325.0f32, 16.0f32);
    /* The phase 05 file's -250 for the 75 degree coast leaves out B: at -250 on land, B adds 6.25 deg C and the summer
     * peaks at +3.2, enough to melt 350 mm. With B, as its design says for land, the balance is -203; without (a lake),
     * -250 (Issues found in the phase file). */
    CHECK_NEAR(b75, -203.0f32, 2.0f32);
    CHECK(climate_season_snow_balance(&coast75, false) == -250.0f32);
    CHECK(b0 == CLIMATE_SNOW_BALANCE_NONE);
    /* Above the balance it outlasts, below it does not. */
    CHECK(climate_season_snow_outlasts(&coast60, b60 + 2.0f32, true));
    CHECK(!climate_season_snow_outlasts(&coast60, b60 - 2.0f32, true));
    /* And it falls with latitude. */
    float32_t last = 1.0e9f32;
    bool falling = true;
    for (float32_t lat = 45.0f32; lat <= 75.0f32; lat += 5.0f32) {
        const climate_cell_t cell = season_snow_cell(normals, lat, 0.0f32, 800.0f32);
        const float32_t b = climate_season_snow_balance(&cell, true);
        printf("  coast at %.0f with 800 mm: %.1f\n", (libc_double_t)lat, (libc_double_t)b);
        falling = falling && b <= last;
        last = b;
    }
    CHECK(falling);
}

/* ---- Classes ------------------------------------------------------------------------------------------------------------ */

typedef struct season_case_t {
    float32_t t_mean;   /* 0 */
    float32_t t_max;    /* 4 */
    float32_t t_min;    /* 8 */
    float32_t rain;     /* 12 */
    float32_t driest;   /* 16 */
    uint8_t dry_summer; /* 20 */
    uint8_t monsoon;    /* 21 */
    uint8_t want;       /* 22 */
    uint8_t _pad0[1];   /* 23 */
} season_case_t;
static_assert(sizeof(season_case_t) == 24);

TEST(classes_follow_the_first_match) {
    /* P_th = 20 T + 140: at T = 25, 640 (desert under 320); at T = 12, 380 (desert under 190); at T = 2, 180. */
    static const season_case_t CASES[14] = {
        {-20.0f32, -5.0f32, -35.0f32, 200.0f32, 5.0f32, 0u, 0u, CLIMATE_CLASS_ICE, {0}},
        {-8.0f32, 6.0f32, -22.0f32, 300.0f32, 10.0f32, 0u, 0u, CLIMATE_CLASS_TUNDRA, {0}},
        {25.0f32, 32.0f32, 18.0f32, 200.0f32, 0.0f32, 0u, 0u, CLIMATE_CLASS_DESERT, {0}},
        {25.0f32, 32.0f32, 18.0f32, 500.0f32, 10.0f32, 0u, 0u, CLIMATE_CLASS_STEPPE, {0}},
        {26.0f32, 27.0f32, 25.0f32, 2500.0f32, 120.0f32, 0u, 0u, CLIMATE_CLASS_RAINFOREST, {0}},
        {26.0f32, 29.0f32, 23.0f32, 2000.0f32, 20.0f32, 0u, 1u, CLIMATE_CLASS_MONSOON, {0}},
        {26.0f32, 29.0f32, 23.0f32, 1200.0f32, 10.0f32, 0u, 0u, CLIMATE_CLASS_SAVANNA, {0}},
        {16.0f32, 24.0f32, 9.0f32, 600.0f32, 5.0f32, 1u, 0u, CLIMATE_CLASS_MEDITERRANEAN, {0}},
        {11.0f32, 18.0f32, 4.0f32, 900.0f32, 50.0f32, 0u, 0u, CLIMATE_CLASS_TEMPERATE, {0}},
        {8.0f32, 24.0f32, -10.0f32, 700.0f32, 30.0f32, 0u, 0u, CLIMATE_CLASS_CONTINENTAL, {0}},
        {0.0f32, 16.0f32, -18.0f32, 500.0f32, 20.0f32, 0u, 0u, CLIMATE_CLASS_BOREAL, {0}},
        /* The order: Ice before Rainforest, Tundra before Desert, Desert before Savanna. */
        {-15.0f32, -1.0f32, -30.0f32, 3000.0f32, 200.0f32, 0u, 0u, CLIMATE_CLASS_ICE, {0}},
        {-5.0f32, 8.0f32, -20.0f32, 50.0f32, 1.0f32, 0u, 0u, CLIMATE_CLASS_TUNDRA, {0}},
        {25.0f32, 30.0f32, 20.0f32, 300.0f32, 0.0f32, 0u, 0u, CLIMATE_CLASS_DESERT, {0}},
    };
    for (uint32_t i = 0u; i < 14u; i++) {
        const season_case_t *const c = &CASES[i];
        const uint8_t got = climate_season_classify(c->t_mean, c->t_max, c->t_min, c->rain, c->driest,
                                                    c->dry_summer != 0u, c->monsoon != 0u);
        if (!CHECK_EQ_U64(got, c->want)) {
            printf("case %u: class %u, wanted %u\n", i, got, c->want);
        }
    }
}

TEST(the_45_north_peak_is_tundra_and_the_plateau_is_desert) {
    season_rig_t rig;
    REQUIRE(season_rig_start(t, &rig));
    /* Land 1000 high north of y = 17,984, the sea south of it: the cell centred at 19,168 (45.05 N) is 19 cells from the
     * last sea cell, so 1184 blocks from the sea; an east-west coast has no current at 45 degrees. */
    const ground_t high = {.kind = GROUND_HIGH_COAST_Y, .coast = 17984, .radius = 1000};
    REQUIRE(climate_open_ground(rig.climate, 11u, ground_fn, &high));
    climate_prepare_area(rig.climate, rig.job, 0, 17800, 200, 19300, nullptr, nullptr);
    climate_cell_t peak, sea;
    REQUIRE(climate_cell_get(rig.climate, 96, 19168, &peak));
    REQUIRE(climate_cell_get(rig.climate, 96, 17952 - 64, &sea));
    climate_season_months_t months;
    climate_season_months(&peak, peak.z_ground, true, &months);
    const float32_t want = (float32_t)climate_normals_t_mean(climate_normals(rig.climate), (float64_t)peak.lat_deg) -
                           12.0f32 + 0.98862f32 * fabsf(peak.t_swing);
    printf("45 N peak: latitude %.3f, z %.1f, sea distance %.0f, current %u; warmest month %.3f (formula %.3f), "
           "class %u\n",
           (libc_double_t)peak.lat_deg, (libc_double_t)peak.z_ground, (libc_double_t)peak.sea_distance, peak.current,
           (libc_double_t)months.t_max_month_c, (libc_double_t)want, peak.climate_class);
    CHECK_NEAR(peak.z_ground, 1000.0f32, 0.0f32);
    CHECK_EQ_U64(peak.current, CLIMATE_CURRENT_NONE);
    CHECK_NEAR(months.t_max_month_c, want, 0.01f32);
    /* 8.45 at exactly 1200 blocks inland and 45.00; here 1184 and 45.05, which move it by -0.08. */
    CHECK_NEAR(months.t_max_month_c, 8.45f32, 0.15f32);
    CHECK_EQ_U64(peak.climate_class, CLIMATE_CLASS_TUNDRA);
    CHECK_EQ_U64(sea.climate_class, CLIMATE_CLASS_NONE);
    CHECK(sea.snow_balance_z == CLIMATE_SNOW_BALANCE_NONE);
    climate_close(rig.climate, rig.job);

    /* Ahmed's sketch over a real year: the plateau is a desert. */
    const ground_t sa = {.kind = GROUND_SOUTH_AFRICA};
    REQUIRE(climate_open_ground(rig.climate, 11u, ground_fn, &sa));
    climate_prepare_area(rig.climate, rig.job, 8900, -10700, 9100, -10600, nullptr, nullptr);
    climate_cell_t plateau;
    REQUIRE(climate_cell_get(rig.climate, 8992, -10656, &plateau));
    printf("plateau: %.1f mm a year, class %u\n", (libc_double_t)(plateau.rain_mm_week[0] * 365.0f32 / 7.0f32),
           plateau.climate_class);
    CHECK_EQ_U64(plateau.climate_class, CLIMATE_CLASS_DESERT);
    season_rig_stop(&rig);
}

TEST_MAIN()
