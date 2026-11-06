/* ctest run_climate_harmonic. See README.md. */
#include "harness/index.h"

#include "climate/harmonic/index.h"
#include "climate/normals/index.h"

#include <math.h>
#include <stdio.h>

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

TEST_MAIN()
