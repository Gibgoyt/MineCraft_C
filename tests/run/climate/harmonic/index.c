/* ctest run_climate_harmonic. See README.md. */
#include "harness/index.h"

#include "climate/harmonic/index.h"
#include "climate/normals/index.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define HARMONIC_TEST_TWO_PI 6.28318530717958647692f64

static float64_t harmonic_theta(uint32_t k) {
    return HARMONIC_TEST_TWO_PI * (float64_t)k / 12.0f64;
}

TEST(the_fit_days_are_spread_evenly_through_the_year) {
    CHECK_NEAR(climate_harmonic_day(0u), 0.0f32, 1.0e-6f32);
    CHECK_NEAR(climate_harmonic_day(1u), 30.416666f32, 1.0e-4f32);
    CHECK_NEAR(climate_harmonic_day(6u), 182.5f32, 1.0e-4f32);
    CHECK_NEAR(climate_harmonic_day(11u), 334.58334f32, 1.0e-3f32);
}

TEST(a_half_year_step_fits_to_the_worked_example) {
    const float32_t values[CLIMATE_FIT_DAYS] = {1.0f32, 1.0f32, 1.0f32, 1.0f32, 1.0f32, 1.0f32,
                                                0.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32};
    float32_t fit[3];
    climate_harmonic_fit1(values, fit);
    printf("step: a0 %.4f a1 %.4f b1 %.4f\n", (libc_double_t)fit[0], (libc_double_t)fit[1], (libc_double_t)fit[2]);
    CHECK_NEAR(fit[0], 0.5000f32, 1.0e-4f32);
    CHECK_NEAR(fit[1], 0.1667f32, 1.0e-4f32);
    CHECK_NEAR(fit[2], 0.6220f32, 1.0e-4f32);
}

TEST(a_pure_wave_comes_back_exactly) {
    float32_t values[CLIMATE_FIT_DAYS];
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        values[k] = (float32_t)(3.0f64 + 2.0f64 * cos(harmonic_theta(k)) - 5.0f64 * sin(harmonic_theta(k)));
    }
    float32_t fit[3];
    climate_harmonic_fit1(values, fit);
    CHECK_NEAR(fit[0], 3.0f32, 1.0e-5f32);
    CHECK_NEAR(fit[1], 2.0f32, 1.0e-5f32);
    CHECK_NEAR(fit[2], -5.0f32, 1.0e-5f32);
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        CHECK_NEAR(climate_harmonic_at1(fit, climate_harmonic_day(k)), values[k], 1.0e-5f32);
    }
}

TEST(a_constant_has_no_wave) {
    float32_t values[CLIMATE_FIT_DAYS];
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        values[k] = 1013.25f32;
    }
    float32_t fit[3];
    climate_harmonic_fit1(values, fit);
    CHECK_NEAR(fit[0], 1013.25f32, 1.0e-4f32);
    CHECK_NEAR(fit[1], 0.0f32, 1.0e-5f32);
    CHECK_NEAR(fit[2], 0.0f32, 1.0e-5f32);
}

/* The phase 04 file's 45 degrees north cell, 3450 blocks from the sea: dp = -0.6 kappa A cos(2 pi (d - 170 - l) / 365). */
TEST(the_heat_low_wave_is_the_worked_example) {
    climate_normals_t normals;
    climate_normals_fill(&normals);
    const float64_t kappa = 1.0f64 - exp(-3450.0f64 / 1500.0f64);
    const float64_t a_sea = climate_normals_a_sea(&normals, 45.0f64);
    const float64_t swing = a_sea + kappa * (climate_normals_a_land(&normals, 45.0f64) - a_sea);
    const float64_t lag = 25.0f64 + 20.0f64 * (1.0f64 - kappa);
    float32_t values[CLIMATE_FIT_DAYS];
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        const float64_t d = (float64_t)climate_harmonic_day(k);
        values[k] = (float32_t)(-0.6f64 * kappa * swing * cos(HARMONIC_TEST_TWO_PI * (d - 170.0f64 - lag) / 365.0f64));
    }
    float32_t fit[3];
    climate_harmonic_fit1(values, fit);
    const float32_t day_197 = climate_harmonic_at1(fit, 197.0f32);
    printf("kappa %.4f, A %.3f, lag %.2f: p0 %.5f p1c %.4f p1s %.4f, day 197 %.4f hPa\n", (libc_double_t)kappa,
           (libc_double_t)swing, (libc_double_t)lag, (libc_double_t)fit[0], (libc_double_t)fit[1],
           (libc_double_t)fit[2], (libc_double_t)day_197);
    CHECK_NEAR(fit[0], 0.0f32, 1.0e-4f32);
    CHECK_NEAR(fit[1], 6.721f32, 0.002f32);
    CHECK_NEAR(fit[2], 1.714f32, 0.002f32);
    CHECK_NEAR(day_197, -6.936f32, 0.002f32);
}

/* ---- Phase 05: the two-wave fit ------------------------------------------------------------------------------------- */

TEST(two_pure_waves_come_back_exactly) {
    float32_t values[CLIMATE_FIT_DAYS];
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        const float64_t th = harmonic_theta(k);
        values[k] = (float32_t)(5.0f64 + 2.0f64 * cos(th) - 1.0f64 * sin(th) + 3.0f64 * cos(2.0f64 * th) +
                                4.0f64 * sin(2.0f64 * th));
    }
    float32_t fit[5];
    climate_harmonic_fit2(values, fit);
    static const float32_t WANT[5] = {5.0f32, 2.0f32, -1.0f32, 3.0f32, 4.0f32};
    for (uint32_t i = 0u; i < 5u; i++) {
        CHECK_NEAR(fit[i], WANT[i], 1.0e-5f32);
    }
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        CHECK_NEAR(climate_harmonic_at2(fit, climate_harmonic_day(k)), values[k], 1.0e-5f32);
    }
}

TEST(the_worked_example_rain_fits_within_two_mm) {
    const float32_t values[CLIMATE_FIT_DAYS] = {40.0f32, 55.0f32, 62.0f32, 50.0f32, 35.0f32, 30.0f32,
                                                38.0f32, 52.0f32, 60.0f32, 48.0f32, 34.0f32, 31.0f32};
    float32_t fit[5];
    climate_harmonic_fit2(values, fit);
    printf("rain fit: P0 %.3f P1c %.3f P1s %.3f P2c %.3f P2s %.3f\n", (libc_double_t)fit[0], (libc_double_t)fit[1],
           (libc_double_t)fit[2], (libc_double_t)fit[3], (libc_double_t)fit[4]);
    static const float32_t WANT[5] = {44.583f32, 0.994f32, 0.933f32, -5.250f32, 14.289f32};
    for (uint32_t i = 0u; i < 5u; i++) {
        CHECK_NEAR(fit[i], WANT[i], 1.0e-3f32);
    }
    float32_t worst = 0.0f32;
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        worst = fmaxf(worst, fabsf(climate_harmonic_at2(fit, climate_harmonic_day(k)) - values[k]));
    }
    printf("worst miss %.3f mm\n", (libc_double_t)worst);
    CHECK(worst <= 1.8f32);
}

TEST(the_two_wave_fit_agrees_with_the_one_wave_fit_on_wave_one) {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    for (uint32_t round = 0u; round < 100u; round++) {
        float32_t values[CLIMATE_FIT_DAYS];
        for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            values[k] = (float32_t)(state >> 40) / 16777216.0f32 * 100.0f32 - 20.0f32;
        }
        float32_t one[3], two[5];
        climate_harmonic_fit1(values, one);
        climate_harmonic_fit2(values, two);
        for (uint32_t i = 0u; i < 3u; i++) {
            CHECK_NEAR(two[i], one[i], 1.0e-6f32);
        }
    }
}

/* The wettest day's table is the 73 calls' own trigonometry: the same bits, for any coefficients (plan 004 phase 09). */
TEST(the_largest_of_every_fifth_day_is_the_same_bits_as_the_calls) {
    uint64_t state = 0xD1B54A32D192ED03ull;
    uint32_t differ = 0u;
    for (uint32_t round = 0u; round < 20000u; round++) {
        float32_t coefficients[5];
        for (uint32_t i = 0u; i < 5u; i++) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            coefficients[i] = (float32_t)(state >> 40) / 16777216.0f32 * 400.0f32 - (i == 0u ? 50.0f32 : 200.0f32);
        }
        float32_t want = 0.0f32;
        for (uint32_t k = 0u; k < CLIMATE_HARMONIC_MAX_SAMPLES; k++) {
            want = fmaxf(want, climate_harmonic_at2(coefficients, (float32_t)(k * CLIMATE_HARMONIC_MAX_STEP_DAYS)));
        }
        const float32_t got = climate_harmonic_max2(coefficients);
        differ += memcmp(&got, &want, sizeof(got)) == 0 ? 0u : 1u;
    }
    CHECK_EQ_U64(differ, 0u);
}

TEST_MAIN()
