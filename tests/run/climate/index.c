/* ctest run_climate. See README.md. */
#include "harness/index.h"

#include "climate/index.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>

enum : uint32_t {
    LAT_THREADS = 16u,
    LAT_SAMPLES = 100000u,
    LAT_FAR_SAMPLES = 10000u,
};

/* splitmix64, seeded the same every run. */
static uint64_t lat_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* A y from -3e7 to 3e7, the world's edge either way. */
static float64_t lat_random_y(uint64_t *state) {
    const float64_t unit = (float64_t)(lat_next(state) >> 11) / 9007199254740992.0f64;
    return (unit * 2.0f64 - 1.0f64) * 3.0e7f64;
}

TEST(latitude_is_0_at_the_equator_23_5_at_the_tropic_and_90_at_each_pole) {
    CHECK_NEAR(climate_latitude_deg(0.0f64), 0.0f64, 0.0f64);
    CHECK_NEAR(climate_latitude_deg(10000.0f64), 23.5f64, 1e-9f64);
    CHECK_NEAR(climate_latitude_deg(28298.0f64), 66.5f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(38298.0f64), 90.0f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(-38298.0f64), -90.0f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(76596.0f64), 0.0f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(-10000.0f64), -23.5f64, 1e-9f64);
    /* The south pole the other way round, and the second equator. */
    CHECK_NEAR(climate_latitude_deg(114894.0f64), -90.0f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(153191.0f64), 0.0f64, 0.002f64);
    /* Exactly at the pole, never past 90. */
    CHECK_NEAR(climate_latitude_deg(CLIMATE_POLE_Y), 90.0f64, 1e-9f64);
    CHECK(climate_latitude_deg(CLIMATE_POLE_Y) <= 90.0f64);
    CHECK(climate_latitude_deg(-CLIMATE_POLE_Y) >= -90.0f64);
    printf("  Y = %.4f, period = %.4f\n", (libc_double_t)CLIMATE_POLE_Y, (libc_double_t)CLIMATE_LATITUDE_PERIOD);
}

TEST(latitude_at_19149_is_45_north_and_at_minus_19149_is_45_south) {
    CHECK_NEAR(climate_latitude_deg(19149.0f64), 45.0f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(-19149.0f64), -45.0f64, 0.001f64);
}

TEST(latitude_folds_back_past_a_pole) {
    CHECK_NEAR(climate_latitude_deg(30000.0f64), 70.5f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(50000.0f64), 62.5f64, 0.001f64);
    CHECK_NEAR(climate_latitude_deg(100000.0f64), -55.0f64, 0.001f64);
    /* The same distance either side of a pole is the same latitude. */
    CHECK_NEAR(climate_latitude_deg(CLIMATE_POLE_Y - 1234.5f64), climate_latitude_deg(CLIMATE_POLE_Y + 1234.5f64),
               1e-9f64);
}

TEST(latitude_repeats_every_period) {
    uint64_t state = 0x243F6A8885A308D3ull;
    float64_t worst_whole = 0.0f64, worst_exact = 0.0f64;
    for (uint32_t i = 0u; i < LAT_FAR_SAMPLES; i++) {
        const float64_t y = lat_random_y(&state);
        const float64_t here = climate_latitude_deg(y);
        const float64_t whole = fabs(climate_latitude_deg(y + 153191.0f64) - here);
        const float64_t exact = fabs(climate_latitude_deg(y + CLIMATE_LATITUDE_PERIOD) - here);
        worst_whole = whole > worst_whole ? whole : worst_whole;
        worst_exact = exact > worst_exact ? exact : worst_exact;
    }
    printf("  worst gap: %.3g degrees for 153191 blocks, %.3g for the exact period\n", (libc_double_t)worst_whole,
           (libc_double_t)worst_exact);
    CHECK(worst_whole <= 0.002f64);
    CHECK(worst_exact <= 1e-6f64);
}

TEST(latitude_rises_evenly_between_the_poles) {
    const float64_t step = 90.0f64 / CLIMATE_POLE_Y;
    float64_t worst = 0.0f64;
    float64_t before = climate_latitude_deg(-38297.0f64);
    for (int32_t y = -38296; y <= 38297; y++) {
        const float64_t here = climate_latitude_deg((float64_t)y);
        const float64_t gap = fabs(here - before - step);
        worst = gap > worst ? gap : worst;
        before = here;
    }
    printf("  step %.7f, worst off by %.3g\n", (libc_double_t)step, (libc_double_t)worst);
    CHECK_NEAR(step, 0.0023500f64, 1e-7f64);
    CHECK(worst <= 1e-7f64);
}

/* latitude_does_not_care_about_x: by construction. climate_latitude_deg takes only y, and the header has no x to give it. */

typedef struct lat_run_t {
    const float64_t *borrowed_ys; /* 0 */
    float64_t *borrowed_out;      /* 8 */
} lat_run_t;
static_assert(sizeof(lat_run_t) == 16);

static void *lat_worker(void *argument) {
    const lat_run_t *const run = argument;
    for (uint32_t i = 0u; i < LAT_SAMPLES; i++) {
        run->borrowed_out[i] = climate_latitude_deg(run->borrowed_ys[i]);
    }
    return nullptr;
}

TEST(latitude_is_the_same_on_any_thread) {
    float64_t *const ys = test_alloc(t, LAT_SAMPLES * sizeof(float64_t));
    float64_t *const expected = test_alloc(t, LAT_SAMPLES * sizeof(float64_t));
    float64_t *const got = test_alloc(t, (uint64_t)LAT_THREADS * LAT_SAMPLES * sizeof(float64_t));
    uint64_t state = 0x13198A2E03707344ull;
    for (uint32_t i = 0u; i < LAT_SAMPLES; i++) {
        ys[i] = lat_random_y(&state);
        expected[i] = climate_latitude_deg(ys[i]);
    }
    pthread_t threads[LAT_THREADS];
    lat_run_t runs[LAT_THREADS];
    for (uint32_t w = 0u; w < LAT_THREADS; w++) {
        runs[w] = (lat_run_t){.borrowed_ys = ys, .borrowed_out = got + (uint64_t)w * LAT_SAMPLES};
        REQUIRE(pthread_create(&threads[w], nullptr, lat_worker, &runs[w]) == 0);
    }
    for (uint32_t w = 0u; w < LAT_THREADS; w++) {
        REQUIRE(pthread_join(threads[w], nullptr) == 0);
    }
    for (uint32_t w = 0u; w < LAT_THREADS; w++) {
        CHECK_MEM_EQ(got + (uint64_t)w * LAT_SAMPLES, expected, LAT_SAMPLES * sizeof(float64_t));
    }
}

TEST_MAIN()
