/* ctest run_math_det. See README.md. */
#include "harness/index.h"

#include "math/det/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    DET_TEST_POINTS = 1000000u, /* per function against libm: the phase 02 file's Tests table */
    DET_PIN_POINTS = 65536u,    /* per function in the digest */
};
static const float64_t DET_TEST_TOLERANCE = 1.0e-12f64;

/* Plan 006 phase 02: FNV-1a over the result bits of DET_PIN_POINTS fixed inputs, the same in debug and release. A change
 * here means a flag, the compiler or a coefficient moved a bit that decides blocks. */
static const uint64_t DET_PIN_EXP = 0xFF47A3D46F25E0C3ull;
static const uint64_t DET_PIN_SIN = 0x6B1F6EC48B269886ull;
static const uint64_t DET_PIN_COS = 0xCF6A2953BAB3CA4Bull;
static const uint64_t DET_PIN_ASIN = 0xFC2AADD8F64D44F8ull;
static const uint64_t DET_PIN_ATAN2 = 0xD07238729BBC52BAull;

static uint64_t det_test_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27u)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31u);
}

/* Uniform in [low, high). */
static float64_t det_test_uniform(uint64_t *state, float64_t low, float64_t high) {
    const float64_t unit = (float64_t)(det_test_next(state) >> 11u) * 0x1p-53f64;
    return low + (high - low) * unit;
}

static uint64_t det_test_fnv(uint64_t hash, float64_t value) {
    uint8_t bytes[sizeof(value)];
    memcpy(bytes, &value, sizeof(value));
    for (uint32_t i = 0u; i < (uint32_t)sizeof(value); i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

/* The fixed input i of DET_PIN_POINTS, evenly over [low, high]. */
static float64_t det_test_even(uint32_t i, float64_t low, float64_t high) {
    return low + (high - low) * (float64_t)i / (float64_t)(DET_PIN_POINTS - 1u);
}

TEST(det_functions_match_libm) {
    uint64_t state = 0x006002ull;
    float64_t worst_exp = 0.0f64, worst_sin = 0.0f64, worst_cos = 0.0f64, worst_asin = 0.0f64, worst_atan2 = 0.0f64;
    for (uint32_t i = 0u; i < DET_TEST_POINTS; i++) {
        const float64_t xe = det_test_uniform(&state, -700.0f64, 700.0f64);
        const float64_t want_exp = exp(xe);
        worst_exp = fmax(worst_exp, fabs(det_exp(xe) - want_exp) / want_exp);
        /* Most points where the climate's angles live, one in ten out to the documented 8e5. */
        const float64_t reach = i % 10u == 0u ? 8.0e5f64 : 1.0e3f64;
        const float64_t xt = det_test_uniform(&state, -reach, reach);
        worst_sin = fmax(worst_sin, fabs(det_sin(xt) - sin(xt)));
        worst_cos = fmax(worst_cos, fabs(det_cos(xt) - cos(xt)));
        const float64_t xa = det_test_uniform(&state, -1.0f64, 1.0f64);
        worst_asin = fmax(worst_asin, fabs(det_asin(xa) - asin(xa)));
        const float64_t scale = i % 2u == 0u ? 1.0f64 : 1.0e4f64;
        const float64_t y = det_test_uniform(&state, -scale, scale);
        const float64_t x = det_test_uniform(&state, -scale, scale);
        worst_atan2 = fmax(worst_atan2, fabs(det_atan2(y, x) - atan2(y, x)));
    }
    printf("worst against libm over %" PRIu32
           " points: exp %.3e (relative), sin %.3e, cos %.3e, asin %.3e, atan2 %.3e\n",
           DET_TEST_POINTS, (libc_double_t)worst_exp, (libc_double_t)worst_sin, (libc_double_t)worst_cos,
           (libc_double_t)worst_asin, (libc_double_t)worst_atan2);
    CHECK(worst_exp < DET_TEST_TOLERANCE);
    CHECK(worst_sin < DET_TEST_TOLERANCE);
    CHECK(worst_cos < DET_TEST_TOLERANCE);
    CHECK(worst_asin < DET_TEST_TOLERANCE);
    CHECK(worst_atan2 < DET_TEST_TOLERANCE);
}

TEST(the_edges_are_exact) {
    CHECK(det_sin(0.0f64) == 0.0f64);
    CHECK(det_cos(0.0f64) == 1.0f64);
    CHECK(det_exp(0.0f64) == 1.0f64);
    CHECK(det_asin(0.0f64) == 0.0f64);
    CHECK(det_asin(1.0f64) == 0x1.921fb54442d18p+0f64);
    CHECK(det_asin(-1.0f64) == -0x1.921fb54442d18p+0f64);
    CHECK(det_atan2(0.0f64, 1.0f64) == 0.0f64);
    CHECK(det_atan2(0.0f64, -1.0f64) == 0x1.921fb54442d18p+1f64);
    CHECK(det_atan2(-0.0f64, -1.0f64) == -0x1.921fb54442d18p+1f64);
    CHECK(det_atan2(1.0f64, 0.0f64) == 0x1.921fb54442d18p+0f64);
    CHECK(det_atan2(-1.0f64, 0.0f64) == -0x1.921fb54442d18p+0f64);
    CHECK(det_exp(710.0f64) == INFINITY);
    CHECK(det_exp(-746.0f64) == 0.0f64);
    /* Below -708 the result is subnormal: still within a relative 1e-12 down to about -720, and positive to -745. */
    CHECK_NEAR(det_exp(-710.0f64) / exp(-710.0f64), 1.0f64, 1.0e-12f64);
    CHECK(det_exp(-745.0f64) > 0.0f64);
    CHECK(isnan(det_sin(INFINITY)));
    CHECK(isnan(det_cos(NAN)));
    CHECK(isnan(det_asin(1.5f64)));
    CHECK(isnan(det_exp(NAN)));
    CHECK(isnan(det_atan2(NAN, 1.0f64)));
    CHECK(det_sin(0x1p+41f64) == 0.0f64);
}

TEST(det_functions_are_pinned) {
    uint64_t exp_hash = 0xCBF29CE484222325ull, sin_hash = exp_hash, cos_hash = exp_hash, asin_hash = exp_hash,
             atan2_hash = exp_hash;
    for (uint32_t i = 0u; i < DET_PIN_POINTS; i++) {
        exp_hash = det_test_fnv(exp_hash, det_exp(det_test_even(i, -700.0f64, 700.0f64)));
        const float64_t angle = det_test_even(i, -1000.0f64, 1000.0f64);
        sin_hash = det_test_fnv(sin_hash, det_sin(angle));
        cos_hash = det_test_fnv(cos_hash, det_cos(angle));
        asin_hash = det_test_fnv(asin_hash, det_asin(det_test_even(i, -1.0f64, 1.0f64)));
        /* A spiral round the origin, every quadrant and every ratio, its inputs from det_* too so no libm reaches the pin. */
        const float64_t turn = det_test_even(i, -50.0f64, 50.0f64);
        const float64_t radius = 1.0f64 + (float64_t)(i % 97u);
        atan2_hash = det_test_fnv(atan2_hash, det_atan2(radius * det_sin(turn), radius * det_cos(turn)));
    }
    printf("digests: exp 0x%016" PRIX64 " sin 0x%016" PRIX64 " cos 0x%016" PRIX64 " asin 0x%016" PRIX64
           " atan2 0x%016" PRIX64 "\n",
           exp_hash, sin_hash, cos_hash, asin_hash, atan2_hash);
    CHECK_EQ_U64(exp_hash, DET_PIN_EXP);
    CHECK_EQ_U64(sin_hash, DET_PIN_SIN);
    CHECK_EQ_U64(cos_hash, DET_PIN_COS);
    CHECK_EQ_U64(asin_hash, DET_PIN_ASIN);
    CHECK_EQ_U64(atan2_hash, DET_PIN_ATAN2);
}

TEST_MAIN()
