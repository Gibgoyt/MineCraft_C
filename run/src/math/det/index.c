/* See README.md. Range reduction, then a fixed polynomial by Horner's rule, every operation written in the order it runs.
 * -std=c23 keeps GCC from contracting a * b + c into an FMA, so these are the same bits in every preset. The constants are
 * written as hexadecimal floats so no decimal-to-binary rounding stands between the source and the bits. */
#include "math/det/index.h"

#include <math.h> /* sqrt only: IEEE 754 rounds it correctly, like + - * /, so it leans on no libm's choices */
#include <stdint.h>
#include <string.h>

#define DET_PI 0x1.921fb54442d18p+1f64
#define DET_HALF_PI 0x1.921fb54442d18p+0f64
#define DET_TWO_OVER_PI 0x1.45f306dc9c883p-1f64
/* pi / 2 in three parts of 33 bits each (fdlibm's pio2_1, pio2_2, pio2_3): n times a part is exact for n under 2^20. */
#define DET_HALF_PI_1 0x1.921fb54400000p+0f64
#define DET_HALF_PI_2 0x1.0b4611a600000p-34f64
#define DET_HALF_PI_3 0x1.3198a2e000000p-69f64
/* ln 2 in two parts (fdlibm's ln2_hi, ln2_lo): k times the high part is exact for |k| under 2^11. */
#define DET_LN2_HI 0x1.62e42fee00000p-1f64
#define DET_LN2_LO 0x1.a39ef35793c76p-33f64
#define DET_INV_LN2 0x1.71547652b82fep+0f64
#define DET_EXP_OVER 0x1.62e42fefa39efp+9f64     /* 709.78: e^x past this is not a finite float64 */
#define DET_EXP_UNDER (-0x1.74910d52d3051p+9f64) /* -745.13: e^x below this rounds to 0 */
#define DET_TRIG_LIMIT 0x1p+40f64

enum : uint32_t {
    DET_EXP_TERMS = 14u,  /* 1 / n! for n = 0 to 13: |r| <= ln 2 / 2 leaves r^14 / 14! under 5e-18 */
    DET_SIN_TERMS = 9u,   /* (-1)^k / (2k + 1)!, k = 0 to 8, for |r| <= pi / 4 */
    DET_COS_TERMS = 10u,  /* (-1)^k / (2k)!, k = 0 to 9 */
    DET_ASIN_TERMS = 22u, /* the asin series' x^3 to x^45 terms, for |x| <= 1/2 */
    DET_ATAN_TERMS = 14u, /* (-1)^k / (2k + 1), k = 0 to 13, for |u| <= tan(pi / 16) */
    DET_QUADRANTS = 4u,
};

static const float64_t DET_EXP_C[DET_EXP_TERMS] = {
    0x1.0000000000000p+0f64,  0x1.0000000000000p+0f64,  0x1.0000000000000p-1f64,  0x1.5555555555555p-3f64,
    0x1.5555555555555p-5f64,  0x1.1111111111111p-7f64,  0x1.6c16c16c16c17p-10f64, 0x1.a01a01a01a01ap-13f64,
    0x1.a01a01a01a01ap-16f64, 0x1.71de3a556c734p-19f64, 0x1.27e4fb7789f5cp-22f64, 0x1.ae64567f544e4p-26f64,
    0x1.1eed8eff8d898p-29f64, 0x1.6124613a86d09p-33f64,
};
static const float64_t DET_SIN_C[DET_SIN_TERMS] = {
    0x1.0000000000000p+0f64,   -0x1.5555555555555p-3f64,  0x1.1111111111111p-7f64,
    -0x1.a01a01a01a01ap-13f64, 0x1.71de3a556c734p-19f64,  -0x1.ae64567f544e4p-26f64,
    0x1.6124613a86d09p-33f64,  -0x1.ae7f3e733b81fp-41f64, 0x1.952c77030ad4ap-49f64,
};
static const float64_t DET_COS_C[DET_COS_TERMS] = {
    0x1.0000000000000p+0f64,  -0x1.0000000000000p-1f64,  0x1.5555555555555p-5f64,  -0x1.6c16c16c16c17p-10f64,
    0x1.a01a01a01a01ap-16f64, -0x1.27e4fb7789f5cp-22f64, 0x1.1eed8eff8d898p-29f64, -0x1.93974a8c07c9dp-37f64,
    0x1.ae7f3e733b81fp-45f64, -0x1.6827863b97d97p-53f64,
};
/* (2n)! / (4^n (n!)^2 (2n + 1)) for n = 1 to 22. */
static const float64_t DET_ASIN_C[DET_ASIN_TERMS] = {
    0x1.5555555555555p-3f64, 0x1.3333333333333p-4f64, 0x1.6db6db6db6db7p-5f64, 0x1.f1c71c71c71c7p-6f64,
    0x1.6e8ba2e8ba2e9p-6f64, 0x1.1c4ec4ec4ec4fp-6f64, 0x1.c99999999999ap-7f64, 0x1.7a87878787878p-7f64,
    0x1.3fde50d79435ep-7f64, 0x1.12ef3cf3cf3cfp-7f64, 0x1.df3bd37a6f4dfp-8f64, 0x1.a6863d70a3d71p-8f64,
    0x1.782dda12f684cp-8f64, 0x1.51ba308d3dcb1p-8f64, 0x1.31683bdef7bdfp-8f64, 0x1.15ee9d45d1746p-8f64,
    0x1.fcaf8fb6db6dbp-9f64, 0x1.d3d2a8e0dd67dp-9f64, 0x1.b026f57b13b14p-9f64, 0x1.90cb77f60c7cep-9f64,
    0x1.750de64d7d05fp-9f64, 0x1.5c5f56efaaaabp-9f64,
};
static const float64_t DET_ATAN_C[DET_ATAN_TERMS] = {
    0x1.0000000000000p+0f64, -0x1.5555555555555p-2f64, 0x1.999999999999ap-3f64, -0x1.2492492492492p-3f64,
    0x1.c71c71c71c71cp-4f64, -0x1.745d1745d1746p-4f64, 0x1.3b13b13b13b14p-4f64, -0x1.1111111111111p-4f64,
    0x1.e1e1e1e1e1e1ep-5f64, -0x1.af286bca1af28p-5f64, 0x1.8618618618618p-5f64, -0x1.642c8590b2164p-5f64,
    0x1.47ae147ae147bp-5f64, -0x1.2f684bda12f68p-5f64,
};

static bool det_sign_bit(float64_t x) {
    uint64_t bits = 0u;
    memcpy(&bits, &x, sizeof(bits));
    return (bits >> 63u) != 0u;
}

/* 2^k for k in [-1022, 1023], built from its bits. */
static float64_t det_power_of_two(int64_t k) {
    const uint64_t bits = (uint64_t)(k + 1023) << 52u;
    float64_t out = 0.0f64;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

/* The nearest integer, halves away from 0; |x| must be under 2^62. */
static int64_t det_round(float64_t x) {
    return (int64_t)(x >= 0.0f64 ? x + 0.5f64 : x - 0.5f64);
}

/* Horner's rule, highest coefficient first: c[0] + z (c[1] + z (c[2] + ...)). */
static float64_t det_horner(const float64_t *c, uint32_t count, float64_t z) {
    float64_t p = c[count - 1u];
    for (uint32_t i = count - 1u; i > 0u; i--) {
        p = p * z + c[i - 1u];
    }
    return p;
}

float64_t det_exp(float64_t x) {
    if (x != x) {
        return x;
    }
    if (x > DET_EXP_OVER) {
        return INFINITY;
    }
    if (x < DET_EXP_UNDER) {
        return 0.0f64;
    }
    const int64_t k = det_round(x * DET_INV_LN2);
    const float64_t kf = (float64_t)k;
    const float64_t r = (x - kf * DET_LN2_HI) - kf * DET_LN2_LO;
    const float64_t p = det_horner(DET_EXP_C, DET_EXP_TERMS, r);
    /* 2^k in two halves, so a result near the bottom of the range rounds once, on the last multiply. */
    const int64_t k1 = k / 2;
    return p * det_power_of_two(k1) * det_power_of_two(k - k1);
}

/* sin r and cos r on |r| <= pi / 4. */
static float64_t det_sin_core(float64_t r) {
    const float64_t z = r * r;
    return r * det_horner(DET_SIN_C, DET_SIN_TERMS, z);
}

static float64_t det_cos_core(float64_t r) {
    return det_horner(DET_COS_C, DET_COS_TERMS, r * r);
}

/* x = n pi / 2 + r with |r| <= pi / 4 (a hair more at a tie), and n's quadrant. */
static float64_t det_reduce(float64_t x, uint32_t *quadrant) {
    const int64_t n = det_round(x * DET_TWO_OVER_PI);
    const float64_t nf = (float64_t)n;
    *quadrant = (uint32_t)((uint64_t)n & (DET_QUADRANTS - 1u));
    return ((x - nf * DET_HALF_PI_1) - nf * DET_HALF_PI_2) - nf * DET_HALF_PI_3;
}

float64_t det_sin(float64_t x) {
    if (x != x || x - x != 0.0f64) {
        return x - x; /* NaN for NaN and for either infinity */
    }
    if (fabs(x) >= DET_TRIG_LIMIT) {
        return 0.0f64;
    }
    uint32_t quadrant = 0u;
    const float64_t r = det_reduce(x, &quadrant);
    switch (quadrant) {
    case 0u:
        return det_sin_core(r);
    case 1u:
        return det_cos_core(r);
    case 2u:
        return -det_sin_core(r);
    default:
        return -det_cos_core(r);
    }
}

float64_t det_cos(float64_t x) {
    if (x != x || x - x != 0.0f64) {
        return x - x;
    }
    if (fabs(x) >= DET_TRIG_LIMIT) {
        return 0.0f64;
    }
    uint32_t quadrant = 0u;
    const float64_t r = det_reduce(x, &quadrant);
    switch (quadrant) {
    case 0u:
        return det_cos_core(r);
    case 1u:
        return -det_sin_core(r);
    case 2u:
        return -det_cos_core(r);
    default:
        return det_sin_core(r);
    }
}

/* asin x on |x| <= 1/2: x + x z P(z), z = x^2. */
static float64_t det_asin_core(float64_t x) {
    const float64_t z = x * x;
    return x + x * (z * det_horner(DET_ASIN_C, DET_ASIN_TERMS, z));
}

float64_t det_asin(float64_t x) {
    const float64_t a = fabs(x);
    if (!(a <= 1.0f64)) {
        return (x - x) / (x - x); /* NaN, for NaN and for |x| > 1 */
    }
    if (a <= 0.5f64) {
        return det_asin_core(x);
    }
    /* asin a = pi/2 - 2 asin(sqrt((1 - a) / 2)); 1 - a is exact for a in [1/2, 1]. */
    const float64_t s = sqrt((1.0f64 - a) * 0.5f64);
    const float64_t out = DET_HALF_PI - 2.0f64 * det_asin_core(s);
    return x < 0.0f64 ? -out : out;
}

/* atan t on |t| <= 1: two halvings, atan t = 2 atan(t / (1 + sqrt(1 + t^2))), bring it within tan(pi / 16), then the
 * series. */
static float64_t det_atan_core(float64_t t) {
    float64_t u = t / (1.0f64 + sqrt(1.0f64 + t * t));
    u = u / (1.0f64 + sqrt(1.0f64 + u * u));
    return 4.0f64 * (u * det_horner(DET_ATAN_C, DET_ATAN_TERMS, u * u));
}

float64_t det_atan2(float64_t y, float64_t x) {
    if (x != x || y != y) {
        return x + y;
    }
    if (y == 0.0f64) {
        if (!det_sign_bit(x)) {
            return y;
        }
        return det_sign_bit(y) ? -DET_PI : DET_PI;
    }
    if (x == 0.0f64) {
        return y > 0.0f64 ? DET_HALF_PI : -DET_HALF_PI;
    }
    if (fabs(y) <= fabs(x)) {
        const float64_t a = det_atan_core(y / x);
        if (x > 0.0f64) {
            return a;
        }
        return y > 0.0f64 ? a + DET_PI : a - DET_PI;
    }
    const float64_t a = det_atan_core(x / y);
    return (y > 0.0f64 ? DET_HALF_PI : -DET_HALF_PI) - a;
}
