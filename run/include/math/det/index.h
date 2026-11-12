/* See run/src/math/det/README.md. */
#ifndef MC_RUN_MATH_DET_INDEX_H
#define MC_RUN_MATH_DET_INDEX_H

#include "types/index.h"

/* Deterministic float64 maths for anything that decides a block (plan 006 master 2.3): range-reduced polynomials with
 * fixed coefficients and a written operation order, no libm transcendental, so the same bits on any glibc. Each is within
 * 1e-12 of libm over the range its README gives, and pinned by digest in run_math_det. Pure; any thread. */

/* e^x. Relative error under 1e-12 over [-700, 700]. Above 709.78 gives +infinity; below -745.2 gives 0; NaN stays NaN. */
float64_t det_exp(float64_t x);
/* sin x and cos x. Absolute error under 1e-12 for |x| up to 8e5 (2^19 quarter turns); still deterministic beyond, and
 * 0 for |x| at or above 2^40. det_sin(0) == 0 and det_cos(0) == 1 exactly. NaN or infinity gives NaN. */
float64_t det_sin(float64_t x);
float64_t det_cos(float64_t x);
/* asin x on [-1, 1], absolute error under 1e-12; outside gives NaN. */
float64_t det_asin(float64_t x);
/* The angle of (x, y) in (-pi, pi], absolute error under 1e-12. A zero y gives y itself (keeping its sign) when x is +0 or
 * more, and +-pi with y's sign when x is -0 or less. NaN in gives NaN. */
float64_t det_atan2(float64_t y, float64_t x);

#endif
