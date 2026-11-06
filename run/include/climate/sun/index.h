/* See run/src/climate/sun/README.md. */
#ifndef MC_RUN_CLIMATE_SUN_INDEX_H
#define MC_RUN_CLIMATE_SUN_INDEX_H

#include "calendar/index.h"
#include "types/index.h"

/* The spring equinox, as a day of the year: delta is 0 here and rising. */
#define CLIMATE_SUN_EQUINOX_DAY 79.0f64
/* Below this cos(phi) a place is a pole, and day length is 0, 12 or 24 hours by the signs alone. */
#define CLIMATE_SUN_POLE_COS 1.0e-12f64

/* Where the sun is, seen from one place at one moment. */
typedef struct climate_sun_t {
    float64_t direction[3];    /* 0: toward the sun, unit: x east, y north, z up; [2] is sin(elevation) */
    float64_t declination_deg; /* 24: delta: the latitude where it is overhead at noon today */
    float64_t hour_angle;      /* 32: H, radians: 0 at noon, -pi/2 at 06:00 */
    float64_t elevation_deg;   /* 40: above the horizon; below 0 is night */
    float64_t azimuth_deg;     /* 48: clockwise from north (+y), 0 to 360: 90 is east */
    float64_t day_hours;       /* 56: sunrise to sunset today, 0 to 24 */
} climate_sun_t;
static_assert(sizeof(climate_sun_t) == 64);

/* delta(d) of master plan 5.2, for a day of the year that may have a fraction: 23.5 * sin(2 pi (d - 79) / 365). */
float64_t climate_sun_declination_deg(float64_t day_of_year);

/* h0 of master plan 5.2, radians from 0 (no sun) to pi (no night), with the clamp, and the poles by their signs. */
float64_t climate_sun_sunset_hour_angle(float64_t latitude_deg, float64_t declination_deg);

/* 24 h0 / pi. */
float64_t climate_sun_day_hours(float64_t latitude_deg, float64_t declination_deg);

/* All of it at a latitude, a day of the year with its fraction, and an hour on the clock (0 to 24). Pure. */
void climate_sun_at(float64_t latitude_deg, float64_t day_of_year, float64_t clock_hour, climate_sun_t *out);

/* The same from the world clock and a y: what the game's threads call. */
void climate_sun_of_clock(uint32_t day_zero, calendar_clock_t clock, float64_t y, climate_sun_t *out);

/* Only direction[2], which is all the bed and mob rules need: the same bits as climate_sun_of_clock gives there. */
float64_t climate_sun_height_of_clock(uint32_t day_zero, calendar_clock_t clock, float64_t y);

#endif
