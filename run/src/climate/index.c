/* See README.md. Latitude from y alone: plan 004 section 5.1, and its phase 02 file. A pure function, so any thread may ask, and
 * the same y gives the same bits on each. */
#include "climate/index.h"

#include <math.h>

float64_t climate_latitude_deg(float64_t y) {
    /* Into one period first, [-2Y, 2Y): sin of an angle in the millions keeps fewer digits than sin of one under 2 pi. */
    const float64_t period = CLIMATE_LATITUDE_PERIOD;
    const float64_t near = y - period * floor((y + 0.5f64 * period) / period);
    /* asin undoes sin while the angle is within a quarter turn, and folds it back past one: a triangle wave. */
    const float64_t degrees = asin(sin(CLIMATE_PI * near / (2.0f64 * CLIMATE_POLE_Y))) * (180.0f64 / CLIMATE_PI);
    return degrees > 90.0f64 ? 90.0f64 : (degrees < -90.0f64 ? -90.0f64 : degrees);
}
