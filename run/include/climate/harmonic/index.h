/* See run/src/climate/harmonic/README.md. */
#ifndef MC_RUN_CLIMATE_HARMONIC_INDEX_H
#define MC_RUN_CLIMATE_HARMONIC_INDEX_H

#include "types/index.h"

#include <stdint.h>

/* Master plan 6.9: a yearly cycle is fitted from values on this many days, d_k = 365 k / 12. */
enum : uint32_t { CLIMATE_FIT_DAYS = 12u };

#define CLIMATE_YEAR_DAYS 365.0f64

/* d_k = 365 k / 12, the k-th fit day, a fraction of a day. */
float32_t climate_harmonic_day(uint32_t k);

/* The least-squares fit of a0 + a1 cos theta + b1 sin theta to values on the CLIMATE_FIT_DAYS days d_k, theta = 2 pi d / 365.
 * Sums in float64_t, in k order. out = {a0, a1, b1}. Pure. */
void climate_harmonic_fit1(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[3]);

/* The wave on any day, d from 0 to below 365, fractions allowed. Pure. */
float32_t climate_harmonic_at1(const float32_t coefficients[3], float32_t day);

#endif
