/* See README.md. One yearly wave fitted to 12 days by least squares: the phase 04 file's "Fitting a yearly wave". */
#include "climate/harmonic/index.h"

#include <math.h>

#define HARMONIC_HALF_ROOT_3 0.86602540378443864676f64
#define HARMONIC_TWO_PI 6.28318530717958647692f64

/* cos and sin of theta_k = 2 pi k / 12 = k * 30 degrees, written exactly so no cell calls cos: 0, +-1/2, +-root 3 / 2, +-1. */
static const float64_t HARMONIC_COS[CLIMATE_FIT_DAYS] = {
    1.0f64,  HARMONIC_HALF_ROOT_3,  0.5f64,  0.0f64, -0.5f64, -HARMONIC_HALF_ROOT_3,
    -1.0f64, -HARMONIC_HALF_ROOT_3, -0.5f64, 0.0f64, 0.5f64,  HARMONIC_HALF_ROOT_3,
};
static const float64_t HARMONIC_SIN[CLIMATE_FIT_DAYS] = {
    0.0f64, 0.5f64,  HARMONIC_HALF_ROOT_3,  1.0f64,  HARMONIC_HALF_ROOT_3,  0.5f64,
    0.0f64, -0.5f64, -HARMONIC_HALF_ROOT_3, -1.0f64, -HARMONIC_HALF_ROOT_3, -0.5f64,
};

/* cos and sin of 2 theta_k = k * 60 degrees, for the second wave (phase 05): the same exact values. */
static const float64_t HARMONIC_COS2[CLIMATE_FIT_DAYS] = {
    1.0f64, 0.5f64, -0.5f64, -1.0f64, -0.5f64, 0.5f64, 1.0f64, 0.5f64, -0.5f64, -1.0f64, -0.5f64, 0.5f64,
};
static const float64_t HARMONIC_SIN2[CLIMATE_FIT_DAYS] = {
    0.0f64, HARMONIC_HALF_ROOT_3, HARMONIC_HALF_ROOT_3, 0.0f64, -HARMONIC_HALF_ROOT_3, -HARMONIC_HALF_ROOT_3,
    0.0f64, HARMONIC_HALF_ROOT_3, HARMONIC_HALF_ROOT_3, 0.0f64, -HARMONIC_HALF_ROOT_3, -HARMONIC_HALF_ROOT_3,
};

float32_t climate_harmonic_day(uint32_t k) {
    return (float32_t)(CLIMATE_YEAR_DAYS * (float64_t)k / (float64_t)CLIMATE_FIT_DAYS);
}

void climate_harmonic_fit1(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[3]) {
    /* For 12 even angles the normal equations' matrix is diagonal, 12, 6, 6: each sum over its own diagonal entry. */
    float64_t sum = 0.0f64, sum_cos = 0.0f64, sum_sin = 0.0f64;
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        const float64_t f = (float64_t)values[k];
        sum += f;
        sum_cos += f * HARMONIC_COS[k];
        sum_sin += f * HARMONIC_SIN[k];
    }
    out[0] = (float32_t)(sum / 12.0f64);
    out[1] = (float32_t)(sum_cos / 6.0f64);
    out[2] = (float32_t)(sum_sin / 6.0f64);
}

float32_t climate_harmonic_at1(const float32_t coefficients[3], float32_t day) {
    const float64_t theta = HARMONIC_TWO_PI * (float64_t)day / CLIMATE_YEAR_DAYS;
    return (float32_t)((float64_t)coefficients[0] + (float64_t)coefficients[1] * cos(theta) +
                       (float64_t)coefficients[2] * sin(theta));
}

void climate_harmonic_fit2(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[5]) {
    /* Waves 1 and 2 on 12 even angles are orthogonal too: the matrix is diagonal, 12, 6, 6, 6, 6. */
    float64_t sum = 0.0f64, sum_cos = 0.0f64, sum_sin = 0.0f64, sum_cos2 = 0.0f64, sum_sin2 = 0.0f64;
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        const float64_t f = (float64_t)values[k];
        sum += f;
        sum_cos += f * HARMONIC_COS[k];
        sum_sin += f * HARMONIC_SIN[k];
        sum_cos2 += f * HARMONIC_COS2[k];
        sum_sin2 += f * HARMONIC_SIN2[k];
    }
    out[0] = (float32_t)(sum / 12.0f64);
    out[1] = (float32_t)(sum_cos / 6.0f64);
    out[2] = (float32_t)(sum_sin / 6.0f64);
    out[3] = (float32_t)(sum_cos2 / 6.0f64);
    out[4] = (float32_t)(sum_sin2 / 6.0f64);
}

float32_t climate_harmonic_at2(const float32_t coefficients[5], float32_t day) {
    const float64_t theta = HARMONIC_TWO_PI * (float64_t)day / CLIMATE_YEAR_DAYS;
    return (float32_t)((float64_t)coefficients[0] + (float64_t)coefficients[1] * cos(theta) +
                       (float64_t)coefficients[2] * sin(theta) + (float64_t)coefficients[3] * cos(2.0f64 * theta) +
                       (float64_t)coefficients[4] * sin(2.0f64 * theta));
}
