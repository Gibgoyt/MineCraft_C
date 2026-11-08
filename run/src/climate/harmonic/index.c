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

void climate_harmonic_fit1(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[CLIMATE_HARMONIC1_COEFFICIENTS]) {
    /* For 12 even angles the normal equations' matrix is diagonal, 12, 6, 6: each sum over its own diagonal entry. */
    float64_t sum = 0.0f64, sum_cos = 0.0f64, sum_sin = 0.0f64;
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        const float64_t f = (float64_t)values[k];
        sum += f;
        sum_cos += f * HARMONIC_COS[k];
        sum_sin += f * HARMONIC_SIN[k];
    }
    out[0] = (float32_t)(sum / (float64_t)CLIMATE_FIT_DAYS);
    out[1] = (float32_t)(sum_cos / CLIMATE_HARMONIC_HALF_DAYS);
    out[2] = (float32_t)(sum_sin / CLIMATE_HARMONIC_HALF_DAYS);
}

float32_t climate_harmonic_at1(const float32_t coefficients[CLIMATE_HARMONIC1_COEFFICIENTS], float32_t day) {
    const float64_t theta = HARMONIC_TWO_PI * (float64_t)day / CLIMATE_YEAR_DAYS;
    return (float32_t)((float64_t)coefficients[0] + (float64_t)coefficients[1] * cos(theta) +
                       (float64_t)coefficients[2] * sin(theta));
}

void climate_harmonic_fit2(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[CLIMATE_HARMONIC2_COEFFICIENTS]) {
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
    out[0] = (float32_t)(sum / (float64_t)CLIMATE_FIT_DAYS);
    out[1] = (float32_t)(sum_cos / CLIMATE_HARMONIC_HALF_DAYS);
    out[2] = (float32_t)(sum_sin / CLIMATE_HARMONIC_HALF_DAYS);
    out[3] = (float32_t)(sum_cos2 / CLIMATE_HARMONIC_HALF_DAYS);
    out[4] = (float32_t)(sum_sin2 / CLIMATE_HARMONIC_HALF_DAYS);
}

float32_t climate_harmonic_at2(const float32_t coefficients[CLIMATE_HARMONIC2_COEFFICIENTS], float32_t day) {
    const float64_t theta = HARMONIC_TWO_PI * (float64_t)day / CLIMATE_YEAR_DAYS;
    return (float32_t)((float64_t)coefficients[0] + (float64_t)coefficients[1] * cos(theta) +
                       (float64_t)coefficients[2] * sin(theta) + (float64_t)coefficients[3] * cos(2.0f64 * theta) +
                       (float64_t)coefficients[4] * sin(2.0f64 * theta));
}

/* cos theta, sin theta, cos 2 theta and sin 2 theta for theta = 2 pi d / 365 on the days d = 0, 5, ..., 360, as
 * climate_harmonic_at2 works them out (glibc's cos and sin of the same float64_t theta), written out bit for bit so the
 * wettest day costs no trigonometry: plan 004 phase 09, Issues found 24. */
static const float64_t HARMONIC_EVERY_5[CLIMATE_HARMONIC_MAX_SAMPLES][CLIMATE_HARMONIC_TRIG] = {
    {0x1p+0f64, 0x0p+0f64, 0x1p+0f64, 0x0p+0f64},                                                          /* day 0 */
    {0x1.fe1acb5cd137ap-1f64, 0x1.601c9ff2f3efap-4f64, 0x1.f86ec513c2006p-1f64, 0x1.5ecef0a24ec39p-3f64},  /* day 5 */
    {0x1.f86ec513c2006p-1f64, 0x1.5ecef0a24ec39p-3f64, 0x1.e1f457ebc093ap-1f64, 0x1.599f9a5da1988p-2f64},  /* day 10 */
    {0x1.ef06ad37499c5p-1f64, 0x1.057b558dcca8p-2f64, 0x1.bd3ad2086275dp-1f64, 0x1.f9a04c3e018bcp-2f64},   /* day 15 */
    {0x1.e1f457ebc093ap-1f64, 0x1.599f9a5da1988p-2f64, 0x1.8b581b9ba143ep-1f64, 0x1.455761ee4a6a8p-1f64},  /* day 20 */
    {0x1.d1508b9ccd64p-1f64, 0x1.ab34ccbd48023p-2f64, 0x1.4dc5b48730495p-1f64, 0x1.8440a8af90851p-1f64},   /* day 25 */
    {0x1.bd3ad2086275dp-1f64, 0x1.f9a04c3e018bcp-2f64, 0x1.06558bb4fe6cp-1f64, 0x1.b7afe8d78e60cp-1f64},   /* day 30 */
    {0x1.a5d93c784d2a8p-1f64, 0x1.2226bb8bb6546p-1f64, 0x1.6e4872768012ap-2f64, 0x1.de1fe9cb29d8dp-1f64},  /* day 35 */
    {0x1.8b581b9ba143ep-1f64, 0x1.455761ee4a6a8p-1f64, 0x1.8a240e1506823p-3f64, 0x1.f66dcd3a56ea9p-1f64},  /* day 40 */
    {0x1.6de9ab88c0903p-1f64, 0x1.661f66df34119p-1f64, 0x1.60850db4df375p-6f64, 0x1.ffe1a837262a7p-1f64},  /* day 45 */
    {0x1.4dc5b48730495p-1f64, 0x1.8440a8af90851p-1f64, -0x1.33503ea3d40a6p-3f64, 0x1.fa33f2f8ac071p-1f64}, /* day 50 */
    {0x1.2b292155b157bp-1f64, 0x1.9f820c2bb9f94p-1f64, -0x1.44cdcb0c43e67p-2f64, 0x1.e58fa625decddp-1f64}, /* day 55 */
    {0x1.06558bb4fe6cp-1f64, 0x1.b7afe8d78e60cp-1f64, -0x1.e65992e6e997fp-2f64, 0x1.c290f5a84b601p-1f64},  /* day 60 */
    {0x1.bf21802409a18p-2f64, 0x1.cc9c6ad741c0cp-1f64, -0x1.3cc27f87f73bep-1f64, 0x1.9240b2a35726fp-1f64}, /* day 65 */
    {0x1.6e4872768012ap-2f64, 0x1.de1fe9cb29d8dp-1f64, -0x1.7cfb310d24fd4p-1f64, 0x1.560c7780df7e6p-1f64}, /* day 70 */
    {0x1.1ab92a37e9b2ap-2f64, 0x1.ec1933f9e2db6p-1f64, -0x1.b1f0e243b1571p-1f64, 0x1.0fbbd94fb8531p-1f64}, /* day 75 */
    {0x1.8a240e1506823p-3f64, 0x1.f66dcd3a56ea9p-1f64, -0x1.da12cff935147p-1f64, 0x1.82c5e077f57d2p-2f64}, /* day 80 */
    {0x1.b7d580c1f7e2fp-4f64, 0x1.fd0a21266a089p-1f64, -0x1.f43148395cf13p-1f64, 0x1.b54a74269b748p-3f64}, /* day 85 */
    {0x1.60850db4df375p-6f64, 0x1.ffe1a837262a7p-1f64, -0x1.ff86a47549d6ep-1f64, 0x1.607029782bd54p-5f64}, /* day 90 */
    {-0x1.083a030b352d1p-4f64, 0x1.feeeff84116c7p-1f64, -0x1.fbbd21324e04dp-1f64,
     -0x1.07ad1fdca0a46p-3f64}, /* day 95 */
    {-0x1.33503ea3d40a6p-3f64, 0x1.fa33f2f8ac071p-1f64, -0x1.e8f16709bdbb7p-1f64,
     -0x1.2fd57c3c559adp-2f64}, /* day 100 */
    {-0x1.e03d058b3023dp-3f64, 0x1.f1b979ecbc93ep-1f64, -0x1.c7b1b1caa6706p-1f64,
     -0x1.d2d9344a95d6cp-2f64}, /* day 105 */
    {-0x1.44cdcb0c43e67p-2f64, 0x1.e58fa625decddp-1f64, -0x1.98f99c26885bep-1f64,
     -0x1.340811b8134abp-1f64}, /* day 110 */
    {-0x1.971576a869dc5p-2f64, 0x1.d5cd85628a564p-1f64, -0x1.5e2aafb9f4312p-1f64,
     -0x1.75889156b3d28p-1f64}, /* day 115 */
    {-0x1.e65992e6e997fp-2f64, 0x1.c290f5a84b601p-1f64, -0x1.1901f1b8f20e2p-1f64,
     -0x1.abfe6ca7fe095p-1f64}, /* day 120 */
    {-0x1.1901f1b8f20dap-1f64, 0x1.abfe6ca7fe09bp-1f64, -0x1.971576a869dd1p-2f64,
     -0x1.d5cd85628a561p-1f64}, /* day 125 */
    {-0x1.3cc27f87f73bep-1f64, 0x1.9240b2a35726fp-1f64, -0x1.e03d058b30255p-3f64,
     -0x1.f1b979ecbc93dp-1f64}, /* day 130 */
    {-0x1.5e2aafb9f4313p-1f64, 0x1.75889156b3d27p-1f64, -0x1.083a030b352d2p-4f64,
     -0x1.feeeff84116c7p-1f64}, /* day 135 */
    {-0x1.7cfb310d24fd4p-1f64, 0x1.560c7780df7e6p-1f64, 0x1.b7d580c1f7e2dp-4f64,
     -0x1.fd0a21266a089p-1f64}, /* day 140 */
    {-0x1.98f99c26885bfp-1f64, 0x1.340811b8134aap-1f64, 0x1.1ab92a37e9b2ap-2f64,
     -0x1.ec1933f9e2db6p-1f64}, /* day 145 */
    {-0x1.b1f0e243b1571p-1f64, 0x1.0fbbd94fb8531p-1f64, 0x1.bf21802409a14p-2f64,
     -0x1.cc9c6ad741c0dp-1f64}, /* day 150 */
    {-0x1.c7b1b1caa6707p-1f64, 0x1.d2d9344a95d68p-2f64, 0x1.2b292155b157ap-1f64,
     -0x1.9f820c2bb9f95p-1f64}, /* day 155 */
    {-0x1.da12cff935147p-1f64, 0x1.82c5e077f57d2p-2f64, 0x1.6de9ab88c0903p-1f64,
     -0x1.661f66df34119p-1f64}, /* day 160 */
    {-0x1.e8f16709bdbb5p-1f64, 0x1.2fd57c3c559b9p-2f64, 0x1.a5d93c784d2a3p-1f64,
     -0x1.2226bb8bb654dp-1f64}, /* day 165 */
    {-0x1.f43148395cf13p-1f64, 0x1.b54a74269b748p-3f64, 0x1.d1508b9ccd63fp-1f64,
     -0x1.ab34ccbd48026p-2f64}, /* day 170 */
    {-0x1.fbbd21324e04dp-1f64, 0x1.07ad1fdca0a5ep-3f64, 0x1.ef06ad37499c3p-1f64,
     -0x1.057b558dcca94p-2f64}, /* day 175 */
    {-0x1.ff86a47549d6ep-1f64, 0x1.607029782bd54p-5f64, 0x1.fe1acb5cd1379p-1f64,
     -0x1.601c9ff2f3f0ap-4f64},                                                                            /* day 180 */
    {-0x1.ff86a47549d6ep-1f64, -0x1.607029782bd3p-5f64, 0x1.fe1acb5cd137ap-1f64, 0x1.601c9ff2f3ee7p-4f64}, /* day 185 */
    {-0x1.fbbd21324e04dp-1f64, -0x1.07ad1fdca0a46p-3f64, 0x1.ef06ad37499c6p-1f64,
     0x1.057b558dcca7cp-2f64},                                                                             /* day 190 */
    {-0x1.f43148395cf13p-1f64, -0x1.b54a74269b74p-3f64, 0x1.d1508b9ccd641p-1f64, 0x1.ab34ccbd4801ep-2f64}, /* day 195 */
    {-0x1.e8f16709bdbb7p-1f64, -0x1.2fd57c3c559adp-2f64, 0x1.a5d93c784d2aap-1f64,
     0x1.2226bb8bb6543p-1f64}, /* day 200 */
    {-0x1.da12cff935148p-1f64, -0x1.82c5e077f57cep-2f64, 0x1.6de9ab88c0906p-1f64,
     0x1.661f66df34116p-1f64}, /* day 205 */
    {-0x1.c7b1b1caa6706p-1f64, -0x1.d2d9344a95d6cp-2f64, 0x1.2b292155b1577p-1f64,
     0x1.9f820c2bb9f97p-1f64}, /* day 210 */
    {-0x1.b1f0e243b1572p-1f64, -0x1.0fbbd94fb852fp-1f64, 0x1.bf21802409a1cp-2f64,
     0x1.cc9c6ad741c0bp-1f64}, /* day 215 */
    {-0x1.98f99c26885bep-1f64, -0x1.340811b8134abp-1f64, 0x1.1ab92a37e9b23p-2f64,
     0x1.ec1933f9e2db7p-1f64},                                                                             /* day 220 */
    {-0x1.7cfb310d24fd5p-1f64, -0x1.560c7780df7e4p-1f64, 0x1.b7d580c1f7e5p-4f64, 0x1.fd0a21266a088p-1f64}, /* day 225 */
    {-0x1.5e2aafb9f4312p-1f64, -0x1.75889156b3d28p-1f64, -0x1.083a030b352efp-4f64,
     0x1.feeeff84116c7p-1f64}, /* day 230 */
    {-0x1.3cc27f87f73bfp-1f64, -0x1.9240b2a35726dp-1f64, -0x1.e03d058b30244p-3f64,
     0x1.f1b979ecbc93ep-1f64}, /* day 235 */
    {-0x1.1901f1b8f20e2p-1f64, -0x1.abfe6ca7fe095p-1f64, -0x1.971576a869dabp-2f64,
     0x1.d5cd85628a56ap-1f64},                                                                             /* day 240 */
    {-0x1.e65992e6e9983p-2f64, -0x1.c290f5a84b6p-1f64, -0x1.1901f1b8f20dep-1f64, 0x1.abfe6ca7fe097p-1f64}, /* day 245 */
    {-0x1.971576a869dd1p-2f64, -0x1.d5cd85628a561p-1f64, -0x1.5e2aafb9f4308p-1f64,
     0x1.75889156b3d31p-1f64}, /* day 250 */
    {-0x1.44cdcb0c43e64p-2f64, -0x1.e58fa625decdep-1f64, -0x1.98f99c26885cp-1f64,
     0x1.340811b8134a8p-1f64},                                                                             /* day 255 */
    {-0x1.e03d058b30255p-3f64, -0x1.f1b979ecbc93dp-1f64, -0x1.c7b1b1caa67p-1f64, 0x1.d2d9344a95d82p-2f64}, /* day 260 */
    {-0x1.33503ea3d40a7p-3f64, -0x1.fa33f2f8ac07p-1f64, -0x1.e8f16709bdbb7p-1f64,
     0x1.2fd57c3c559aep-2f64}, /* day 265 */
    {-0x1.083a030b352d2p-4f64, -0x1.feeeff84116c7p-1f64, -0x1.fbbd21324e04dp-1f64,
     0x1.07ad1fdca0a47p-3f64}, /* day 270 */
    {0x1.60850db4df36fp-6f64, -0x1.ffe1a837262a7p-1f64, -0x1.ff86a47549d6ep-1f64,
     -0x1.607029782bd4dp-5f64}, /* day 275 */
    {0x1.b7d580c1f7e2dp-4f64, -0x1.fd0a21266a089p-1f64, -0x1.f43148395cf13p-1f64,
     -0x1.b54a74269b747p-3f64}, /* day 280 */
    {0x1.8a240e1506822p-3f64, -0x1.f66dcd3a56ea9p-1f64, -0x1.da12cff935147p-1f64,
     -0x1.82c5e077f57d2p-2f64}, /* day 285 */
    {0x1.1ab92a37e9b2ap-2f64, -0x1.ec1933f9e2db6p-1f64, -0x1.b1f0e243b1571p-1f64,
     -0x1.0fbbd94fb853p-1f64}, /* day 290 */
    {0x1.6e4872768012ap-2f64, -0x1.de1fe9cb29d8dp-1f64, -0x1.7cfb310d24fd4p-1f64,
     -0x1.560c7780df7e6p-1f64}, /* day 295 */
    {0x1.bf21802409a14p-2f64, -0x1.cc9c6ad741c0dp-1f64, -0x1.3cc27f87f73c1p-1f64,
     -0x1.9240b2a35726cp-1f64}, /* day 300 */
    {0x1.06558bb4fe6bep-1f64, -0x1.b7afe8d78e60ep-1f64, -0x1.e65992e6e9987p-2f64,
     -0x1.c290f5a84b5ffp-1f64}, /* day 305 */
    {0x1.2b292155b157ap-1f64, -0x1.9f820c2bb9f95p-1f64, -0x1.44cdcb0c43e68p-2f64,
     -0x1.e58fa625decddp-1f64}, /* day 310 */
    {0x1.4dc5b48730494p-1f64, -0x1.8440a8af90852p-1f64, -0x1.33503ea3d40afp-3f64,
     -0x1.fa33f2f8ac07p-1f64}, /* day 315 */
    {0x1.6de9ab88c0903p-1f64, -0x1.661f66df34119p-1f64, 0x1.60850db4df328p-6f64,
     -0x1.ffe1a837262a7p-1f64}, /* day 320 */
    {0x1.8b581b9ba143dp-1f64, -0x1.455761ee4a6a9p-1f64, 0x1.8a240e1506819p-3f64,
     -0x1.f66dcd3a56eaap-1f64}, /* day 325 */
    {0x1.a5d93c784d2a3p-1f64, -0x1.2226bb8bb654dp-1f64, 0x1.6e48727680108p-2f64,
     -0x1.de1fe9cb29d94p-1f64}, /* day 330 */
    {0x1.bd3ad2086275cp-1f64, -0x1.f9a04c3e018bfp-2f64, 0x1.06558bb4fe6bcp-1f64,
     -0x1.b7afe8d78e60fp-1f64}, /* day 335 */
    {0x1.d1508b9ccd63fp-1f64, -0x1.ab34ccbd48026p-2f64, 0x1.4dc5b48730492p-1f64,
     -0x1.8440a8af90854p-1f64}, /* day 340 */
    {0x1.e1f457ebc0939p-1f64, -0x1.599f9a5da198bp-2f64, 0x1.8b581b9ba143cp-1f64,
     -0x1.455761ee4a6abp-1f64}, /* day 345 */
    {0x1.ef06ad37499c3p-1f64, -0x1.057b558dcca94p-2f64, 0x1.bd3ad20862753p-1f64,
     -0x1.f9a04c3e018dfp-2f64}, /* day 350 */
    {0x1.f86ec513c2006p-1f64, -0x1.5ecef0a24ec41p-3f64, 0x1.e1f457ebc0938p-1f64,
     -0x1.599f9a5da198fp-2f64}, /* day 355 */
    {0x1.fe1acb5cd1379p-1f64, -0x1.601c9ff2f3f0ap-4f64, 0x1.f86ec513c2005p-1f64,
     -0x1.5ecef0a24ec49p-3f64}, /* day 360 */
};

float32_t climate_harmonic_max2(const float32_t coefficients[CLIMATE_HARMONIC2_COEFFICIENTS]) {
    float32_t most = 0.0f32;
    for (uint32_t k = 0u; k < CLIMATE_HARMONIC_MAX_SAMPLES; k++) {
        const float64_t *const w = HARMONIC_EVERY_5[k];
        const float32_t value = (float32_t)((float64_t)coefficients[0] + (float64_t)coefficients[1] * w[0] +
                                            (float64_t)coefficients[2] * w[1] + (float64_t)coefficients[3] * w[2] +
                                            (float64_t)coefficients[4] * w[3]);
        most = fmaxf(most, value);
    }
    return most;
}
