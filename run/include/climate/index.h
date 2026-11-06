/* See run/src/climate/README.md. */
#ifndef MC_RUN_CLIMATE_INDEX_H
#define MC_RUN_CLIMATE_INDEX_H

#include "types/index.h"

/* Ahmed, 2026-11-04: the tropic, at 23.5 degrees, is 10 km from the equator, and latitude rises evenly with y. */
#define CLIMATE_TILT_DEG 23.5f64
#define CLIMATE_TROPIC_Y 10000.0f64
/* Y in master plan 5.1: blocks from the equator to a pole, 38297.87. */
#define CLIMATE_POLE_Y (CLIMATE_TROPIC_Y * 90.0f64 / CLIMATE_TILT_DEG)
/* Equator, north pole, equator, south pole, equator: 153191.49 blocks. */
#define CLIMATE_LATITUDE_PERIOD (4.0f64 * CLIMATE_POLE_Y)
#define CLIMATE_PI 3.14159265358979323846f64

/* phi(y) of master plan 5.1, in degrees: positive north, from -90 to 90, a triangle wave in y. Any finite y. Pure. */
float64_t climate_latitude_deg(float64_t y);

#endif
