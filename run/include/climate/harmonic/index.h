/* See run/src/climate/harmonic/README.md. */
#ifndef MC_RUN_CLIMATE_HARMONIC_INDEX_H
#define MC_RUN_CLIMATE_HARMONIC_INDEX_H

#include "calendar/index.h"
#include "types/index.h"

#include <stdint.h>

/* Master plan 6.9: a yearly cycle is fitted from values on this many days, d_k = 365 k / 12. */
enum : uint32_t {
    CLIMATE_FIT_DAYS = 12u,
    CLIMATE_HARMONIC1_COEFFICIENTS = 3u, /* phase 11: a0, a1, b1: a mean and one wave, 6.9 */
    CLIMATE_HARMONIC2_COEFFICIENTS = 5u, /* phase 11: P0, P1c, P1s, P2c, P2s: a mean and two waves, 6.9 */
    CLIMATE_HARMONIC_TRIG = 4u,          /* phase 11: cos, sin, cos 2, sin 2 of one day */
};
/* Phase 11: least squares on 12 evenly spaced days divides each wave's sum by half their count. */
#define CLIMATE_HARMONIC_HALF_DAYS ((float64_t)CLIMATE_FIT_DAYS / 2.0f64)

#define CLIMATE_YEAR_DAYS CALENDAR_DAYS_PER_YEAR_F64 /* days: the calendar's year, one home since phase 11 */

/* d_k = 365 k / 12, the k-th fit day, a fraction of a day. */
float32_t climate_harmonic_day(uint32_t k);

/* The least-squares fit of a0 + a1 cos theta + b1 sin theta to values on the CLIMATE_FIT_DAYS days d_k, theta = 2 pi d / 365.
 * Sums in float64_t, in k order. out = {a0, a1, b1}. Pure. */
void climate_harmonic_fit1(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[CLIMATE_HARMONIC1_COEFFICIENTS]);

/* The wave on any day, d from 0 to below 365, fractions allowed. Pure. */
float32_t climate_harmonic_at1(const float32_t coefficients[CLIMATE_HARMONIC1_COEFFICIENTS], float32_t day);

/* Phase 05: the least-squares fit of P0 + P1c cos theta + P1s sin theta + P2c cos 2 theta + P2s sin 2 theta to values on
 * the CLIMATE_FIT_DAYS days d_k. Sums in float64_t, in k order. out = {P0, P1c, P1s, P2c, P2s}. Pure. */
void climate_harmonic_fit2(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[CLIMATE_HARMONIC2_COEFFICIENTS]);

/* The two-wave curve on any day, from 0 to below 365, fractions allowed. Not clamped: callers clamp rain at 0. Pure. */
float32_t climate_harmonic_at2(const float32_t coefficients[CLIMATE_HARMONIC2_COEFFICIENTS], float32_t day);

/* Days 0, 5, ..., 360: the samples climate_harmonic_max2 takes. */
enum : uint32_t { CLIMATE_HARMONIC_MAX_SAMPLES = 73u, CLIMATE_HARMONIC_MAX_STEP_DAYS = 5u };

/* The largest of 0 and climate_harmonic_at2(coefficients, d) over d = 0, 5, ..., 360: the same bits as those 73 calls
 * folded with fmaxf from 0, without their trigonometry. Pure. */
float32_t climate_harmonic_max2(const float32_t coefficients[CLIMATE_HARMONIC2_COEFFICIENTS]);

#endif
