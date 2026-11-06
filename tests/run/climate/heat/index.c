/* ctest run_climate_heat. See README.md. The phase 05 file's tests for run/src/climate/heat/ and the Owner's
 * climate_normals_at. */
#include "harness/index.h"

#include "climate/heat/index.h"
#include "climate/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "thread/index.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct heat_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
} heat_rig_t;
static_assert(sizeof(heat_rig_t) == 24);

static void *heat_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool heat_rig_start(test_context_t *t, heat_rig_t *rig) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = heat_aligned(t, sizeof(job_system_t));
    rig->climate = heat_aligned(t, sizeof(climate_t));
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, MC_THREADS_WORKERS)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return true;
}

static void heat_rig_stop(heat_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

static climate_normals_t *heat_normals(test_context_t *t) {
    climate_normals_t *const normals = heat_aligned(t, sizeof(climate_normals_t));
    climate_normals_fill(normals);
    return normals;
}

static bool heat_within_ten_percent(float32_t value, float32_t plan) {
    return fabsf(value - plan) <= 0.1f32 * fabsf(plan);
}

/* ---- The worked examples of 6.6 -------------------------------------------------------------------------------------- */

TEST(the_equator_beach_is_26_to_28_all_year) {
    const climate_normals_t *const normals = heat_normals(t);
    float32_t low = 1.0e9f32, high = -1.0e9f32;
    for (uint32_t d = 0u; d < 365u; d++) {
        const float32_t v = climate_heat_from_place(normals, 0.0f32, 3.0f32, 0.05f32, 0.0f32, true, (float32_t)d);
        low = fminf(low, v);
        high = fmaxf(high, v);
    }
    printf("equator beach: %.3f to %.3f deg C\n", (libc_double_t)low, (libc_double_t)high);
    CHECK(low >= 25.90f32 && high <= 28.02f32);
    CHECK_NEAR(low, 25.914f32, 0.01f32);
    CHECK_NEAR(high, 28.014f32, 0.01f32);
}

TEST(the_45_north_peak_is_minus_10_4_in_january_and_8_4_in_july) {
    const climate_normals_t *const normals = heat_normals(t);
    const float32_t kappa = 1.0f32 - expf(-0.8f32);
    const float32_t january = climate_heat_from_place(normals, 45.0f32, 1000.0f32, kappa, 0.0f32, true, 15.0f32);
    const float32_t july = climate_heat_from_place(normals, 45.0f32, 1000.0f32, kappa, 0.0f32, true, 196.0f32);
    uint32_t warmest = 0u;
    float32_t best = -1.0e9f32;
    for (uint32_t d = 0u; d < 365u; d++) {
        const float32_t v = climate_heat_from_place(normals, 45.0f32, 1000.0f32, kappa, 0.0f32, true, (float32_t)d);
        if (v > best) {
            best = v;
            warmest = d;
        }
    }
    printf("45 N peak: day 15 %.3f, day 196 %.3f, warmest day %u\n", (libc_double_t)january, (libc_double_t)july,
           warmest);
    CHECK_NEAR(january, -10.50f32, 0.02f32);
    CHECK(heat_within_ten_percent(january, -10.4f32));
    CHECK_NEAR(july, 8.47f32, 0.02f32);
    CHECK(heat_within_ten_percent(july, 8.4f32));
    CHECK(warmest >= 203u && warmest <= 205u);
}

TEST(land_150_below_sea_level_at_28_north_is_34_5) {
    const climate_normals_t *const normals = heat_normals(t);
    const float32_t lat = (float32_t)climate_latitude_deg(11915.0f64);
    const float32_t kappa = 1.0f32 - expf(-4000.0f32 / 1500.0f32);
    const float32_t land = climate_heat_from_place(normals, lat, -150.0f32, kappa, 0.0f32, true, 196.0f32);
    const float32_t high = land + 0.5f32 * climate_heat_daily_range(kappa, 0.0f32);
    const float32_t wet = climate_heat_from_place(normals, lat, -150.0f32, kappa, 0.0f32, false, 196.0f32);
    printf("28 N basin: latitude %.3f, mean %.3f, high %.3f, without B %.3f\n", (libc_double_t)lat, (libc_double_t)land,
           (libc_double_t)high, (libc_double_t)wet);
    CHECK_NEAR(land, 34.63f32, 0.05f32);
    CHECK(heat_within_ten_percent(land, 34.5f32));
    CHECK_NEAR(high, 40.35f32, 0.1f32);
    CHECK_NEAR(wet, 30.88f32, 0.05f32);
}

TEST(temperature_falls_twelve_degrees_per_thousand_blocks) {
    const climate_normals_t *const normals = heat_normals(t);
    static const float32_t Z[4] = {0.0f32, 250.0f32, 500.0f32, 1000.0f32};
    for (uint32_t i = 0u; i < 4u; i++) {
        const float32_t low = climate_heat_from_place(normals, 40.0f32, Z[i], 0.3f32, 0.0f32, true, 100.0f32);
        const float32_t high =
            climate_heat_from_place(normals, 40.0f32, Z[i] + 1000.0f32, 0.3f32, 0.0f32, true, 100.0f32);
        CHECK_NEAR(low - high, 12.000f32, 1.0e-3f32);
    }
    const float32_t deep = climate_heat_from_place(normals, 40.0f32, -200.0f32, 0.3f32, 0.0f32, true, 100.0f32);
    const float32_t level = climate_heat_from_place(normals, 40.0f32, 0.0f32, 0.3f32, 0.0f32, true, 100.0f32);
    CHECK_NEAR(deep - level, 7.40f32, 1.0e-3f32);
}

TEST(the_daily_range_is_the_worked_example) {
    CHECK_NEAR(climate_heat_daily_range(0.9305f32, 0.0f32), 11.444f32, 1.0e-3f32);
    CHECK_NEAR(climate_heat_daily_range(0.9305f32, 6.0f32), 11.444f32 * (1.0f32 - 0.6f32 * 0.63212f32), 1.0e-3f32);
    CHECK_NEAR(climate_heat_daily_range(0.9305f32, 6.0f32), 7.104f32, 1.0e-3f32);
}

TEST(snow_share_follows_its_ramp) {
    static const float32_t T[6] = {2.0f32, 1.0f32, 0.0f32, -1.0f32, -2.0f32, -3.0f32};
    static const float32_t WANT[6] = {0.0f32, 0.0f32, 1.0f32 / 3.0f32, 2.0f32 / 3.0f32, 1.0f32, 1.0f32};
    for (uint32_t i = 0u; i < 6u; i++) {
        CHECK_NEAR(climate_heat_snow_share(T[i]), WANT[i], 1.0e-6f32);
    }
}

/* ---- Through the Owner ------------------------------------------------------------------------------------------------ */

/* The south mirrors the north on a ground that depends on x alone: the same place at -y is half a year later. */
TEST(the_south_runs_six_months_from_the_north) {
    heat_rig_t rig;
    REQUIRE(heat_rig_start(t, &rig));
    const ground_t coast = {.kind = GROUND_COAST_X, .coast = 0};
    REQUIRE(climate_open_ground(rig.climate, 7u, ground_fn, &coast));
    climate_warp_set(rig.climate, false);
    climate_prepare_area(rig.climate, rig.job, -1500, 4000, 3000, 12000, nullptr, nullptr);
    climate_prepare_area(rig.climate, rig.job, -1500, -12000, 3000, -4000, nullptr, nullptr);
    uint64_t state = 0x2545F4914F6CDD1Dull;
    float32_t worst_t = 0.0f32, worst_rain = 0.0f32;
    uint32_t compared = 0u;
    for (uint32_t p = 0u; p < 100u; p++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const float64_t x = -1400.0f64 + (float64_t)(state >> 40) / 16777216.0f64 * 4300.0f64;
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const float64_t y = 4100.0f64 + (float64_t)(state >> 40) / 16777216.0f64 * 7800.0f64;
        for (uint32_t k = 0u; k < 12u; k++) {
            const float32_t day = 10.0f32 + 30.0f32 * (float32_t)k;
            float32_t later = day + 182.5f32;
            later = later >= 365.0f32 ? later - 365.0f32 : later;
            climate_normals_at_t north, south;
            if (!CHECK(climate_normals_at(rig.climate, x, y, nanf(""), day, &north)) ||
                !CHECK(climate_normals_at(rig.climate, x, -y, nanf(""), later, &south))) {
                continue;
            }
            worst_t = fmaxf(worst_t, fabsf(north.t_day_c - south.t_day_c));
            worst_rain = fmaxf(worst_rain, fabsf(north.rain_mm_day - south.rain_mm_day));
            compared++;
        }
    }
    printf("%u pairs: worst temperature gap %.6f deg C, worst rain gap %.6f mm a day\n", compared,
           (libc_double_t)worst_t, (libc_double_t)worst_rain);
    CHECK_EQ_U64(compared, 1200u);
    CHECK(worst_t <= 1.0e-3f32);
    CHECK(worst_rain <= 1.0e-3f32);
    heat_rig_stop(&rig);
}

/* Along x, then y, as the Owner blends. */
static float32_t heat_lerp4(const climate_cell_t c[4], uint64_t offset, float32_t tx, float32_t ty) {
    float32_t v[4];
    for (uint32_t i = 0u; i < 4u; i++) {
        memcpy(&v[i], (const uint8_t *)&c[i] + offset, sizeof(float32_t));
    }
    const float32_t south = v[0] + tx * (v[1] - v[0]);
    const float32_t north = v[2] + tx * (v[3] - v[2]);
    return south + ty * (north - south);
}

TEST(the_read_equals_the_formula_at_cell_centres) {
    heat_rig_t rig;
    REQUIRE(heat_rig_start(t, &rig));
    const ground_t coast = {.kind = GROUND_COAST_X, .coast = 300};
    REQUIRE(climate_open_ground(rig.climate, 3u, ground_fn, &coast));
    climate_warp_set(rig.climate, false);
    climate_prepare_area(rig.climate, rig.job, 0, 8000, 1023, 9023, nullptr, nullptr);
    uint64_t exact = 0u;
    for (int32_t cy = 125; cy < 133; cy++) {
        for (int32_t cx = 2; cx < 10; cx++) {
            climate_cell_t cell;
            REQUIRE(climate_cell_get(rig.climate, cx * 64 + 32, cy * 64 + 32, &cell));
            const bool land = (cell.flags & (CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE)) == 0u;
            climate_normals_at_t read;
            REQUIRE(climate_normals_at(rig.climate, cx * 64 + 32, cy * 64 + 32, cell.z_ground, 123.0f32, &read));
            const float32_t want =
                climate_heat_formula(cell.t_mean, cell.z_ground, land, cell.t_swing, cell.t_lag_days, 123.0f32);
            exact += memcmp(&want, &read.t_day_c, sizeof(want)) == 0 ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(exact, 64u);
    /* Between four cells, 0.3 of the way east and 0.7 north of cell (4, 128): their blend, evaluated. */
    climate_cell_t c[4];
    for (uint32_t k = 0u; k < 4u; k++) {
        REQUIRE(climate_cell_get(rig.climate, (4 + (int32_t)(k & 1u)) * 64 + 32, (128 + (int32_t)(k >> 1u)) * 64 + 32,
                                 &c[k]));
    }
    const float32_t tx = 0.3f32, ty = 0.7f32;
    const float64_t x = 4.0f64 * 64.0f64 + 32.0f64 + 0.3f64 * 64.0f64;
    const float64_t y = 128.0f64 * 64.0f64 + 32.0f64 + 0.7f64 * 64.0f64;
    climate_normals_at_t read;
    REQUIRE(climate_normals_at(rig.climate, x, y, 40.0f32, 200.0f32, &read));
    const bool land = (c[3].flags & (CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE)) == 0u; /* north-east is nearest */
    const float32_t want = climate_heat_formula(heat_lerp4(c, offsetof(climate_cell_t, t_mean), tx, ty), 40.0f32, land,
                                                heat_lerp4(c, offsetof(climate_cell_t, t_swing), tx, ty),
                                                heat_lerp4(c, offsetof(climate_cell_t, t_lag_days), tx, ty), 200.0f32);
    float32_t rain[5];
    for (uint32_t i = 0u; i < 5u; i++) {
        rain[i] = heat_lerp4(c, offsetof(climate_cell_t, rain_mm_week) + 4u * i, tx, ty);
    }
    const float32_t want_rain = fmaxf(0.0f32, climate_harmonic_at2(rain, 200.0f32)) / 7.0f32;
    printf("between four cells: %.5f against %.5f deg C, %.5f against %.5f mm\n", (libc_double_t)read.t_day_c,
           (libc_double_t)want, (libc_double_t)read.rain_mm_day, (libc_double_t)want_rain);
    CHECK_NEAR(read.t_day_c, want, 1.0e-5f32);
    CHECK_NEAR(read.rain_mm_day, want_rain, 1.0e-5f32);
    heat_rig_stop(&rig);
}

TEST(t_mean_is_stored_at_sea_level) {
    heat_rig_t rig;
    REQUIRE(heat_rig_start(t, &rig));
    const ground_t high = {.kind = GROUND_LEVEL, .radius = 600};
    REQUIRE(climate_open_ground(rig.climate, 5u, ground_fn, &high));
    climate_prepare_area(rig.climate, rig.job, 0, 0, 1023, 1023, nullptr, nullptr);
    climate_cell_t cell;
    REQUIRE(climate_cell_get(rig.climate, 500, 500, &cell));
    CHECK_NEAR(cell.z_ground, 600.0f32, 0.0f32);
    const float32_t want =
        (float32_t)(climate_normals_t_mean(climate_normals(rig.climate), (float64_t)cell.lat_deg) +
                    (float64_t)(climate_air_current_c(cell.current) * (float32_t)cell.current_strength / 255.0f32));
    CHECK(cell.t_mean == want);
    climate_normals_at_t at_ground, at_sea_level;
    REQUIRE(climate_normals_at(rig.climate, 500.0f64, 500.0f64, 600.0f32, 80.0f32, &at_ground));
    REQUIRE(climate_normals_at(rig.climate, 500.0f64, 500.0f64, 0.0f32, 80.0f32, &at_sea_level));
    printf("t_mean %.4f; read at 0 %.4f, at 600 %.4f\n", (libc_double_t)cell.t_mean,
           (libc_double_t)at_sea_level.t_day_c, (libc_double_t)at_ground.t_day_c);
    CHECK_NEAR(at_sea_level.t_day_c - at_ground.t_day_c, 7.2f32, 1.0e-3f32);
    CHECK_NEAR(at_sea_level.t_mean_c - at_ground.t_mean_c, 7.2f32, 1.0e-3f32);
    /* A mine 200 below sea level under land is warm: B and Gamma together. */
    climate_normals_at_t mine;
    REQUIRE(climate_normals_at(rig.climate, 500.0f64, 500.0f64, -200.0f32, 80.0f32, &mine));
    CHECK_NEAR(mine.t_day_c - at_sea_level.t_day_c, 7.4f32, 1.0e-3f32);
    heat_rig_stop(&rig);
}

TEST_MAIN()
