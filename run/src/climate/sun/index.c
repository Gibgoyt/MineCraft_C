/* See README.md. Where the sun is: plan 004 section 5.2, and the derivation in its phase 02 file. Depth 2, and allocates nothing.
 * Every function is pure, in float64_t throughout. */
#include "climate/sun/index.h"

#include "climate/index.h"
#include "math/det/index.h"

#include <math.h>

static float64_t climate_sun_radians(float64_t degrees) {
    return degrees * (CLIMATE_PI / 180.0f64);
}

static float64_t climate_sun_degrees(float64_t radians) {
    return radians * (180.0f64 / CLIMATE_PI);
}

float64_t climate_sun_declination_deg(float64_t day_of_year) {
    return CLIMATE_TILT_DEG *
           det_sin(2.0f64 * CLIMATE_PI * (day_of_year - CLIMATE_SUN_EQUINOX_DAY) / (float64_t)CALENDAR_DAYS_PER_YEAR);
}

float64_t climate_sun_sunset_hour_angle(float64_t latitude_deg, float64_t declination_deg) {
    const float64_t phi = climate_sun_radians(latitude_deg);
    const float64_t delta = climate_sun_radians(declination_deg);
    const float64_t cos_phi = cos(phi);
    if (cos_phi < CLIMATE_SUN_POLE_COS) {
        /* At a pole tan(phi) is infinite: the sun is up all day if it is on the pole's side of the equator, never if not. */
        if (declination_deg == 0.0f64) {
            return 0.5f64 * CLIMATE_PI;
        }
        return (latitude_deg > 0.0f64) == (declination_deg > 0.0f64) ? CLIMATE_PI : 0.0f64;
    }
    /* -tan(phi) tan(delta), divided out so that it stays finite right up to the pole. */
    const float64_t c = -sin(phi) * sin(delta) / (cos_phi * cos(delta));
    return acos(c < -1.0f64 ? -1.0f64 : (c > 1.0f64 ? 1.0f64 : c));
}

float64_t climate_sun_day_hours(float64_t latitude_deg, float64_t declination_deg) {
    return CALENDAR_HOURS_PER_DAY_F64 * climate_sun_sunset_hour_angle(latitude_deg, declination_deg) / CLIMATE_PI;
}

void climate_sun_at(float64_t latitude_deg, float64_t day_of_year, float64_t clock_hour, climate_sun_t *out) {
    const float64_t declination_deg = climate_sun_declination_deg(day_of_year);
    const float64_t phi = climate_sun_radians(latitude_deg);
    const float64_t delta = climate_sun_radians(declination_deg);
    const float64_t hour = 2.0f64 * CLIMATE_PI * (clock_hour - CLIMATE_SUN_NOON_HOUR) / CALENDAR_HOURS_PER_DAY_F64;
    const float64_t sin_phi = sin(phi), cos_phi = cos(phi);
    const float64_t sin_delta = sin(delta), cos_delta = cos(delta);
    const float64_t sin_hour = sin(hour), cos_hour = cos(hour);
    /* s = sin(delta) P + cos(delta) (cos(H) M - sin(H) E), with P toward the celestial pole, M up and toward the equator, and E
     * east: the boxed formula of the phase 02 file. */
    out->direction[0] = -cos_delta * sin_hour;
    out->direction[1] = sin_delta * cos_phi - cos_delta * cos_hour * sin_phi;
    out->direction[2] = sin_delta * sin_phi + cos_delta * cos_hour * cos_phi;
    out->declination_deg = declination_deg;
    out->hour_angle = hour;
    const float64_t height = out->direction[2];
    out->elevation_deg = climate_sun_degrees(asin(height < -1.0f64 ? -1.0f64 : (height > 1.0f64 ? 1.0f64 : height)));
    /* Clockwise from north: atan2 of east over north. Into [0, 360), where -0 and a rounding up to 360 both mean north. */
    float64_t azimuth = climate_sun_degrees(atan2(out->direction[0], out->direction[1]));
    if (azimuth < 0.0f64) {
        azimuth += 360.0f64;
    }
    out->azimuth_deg = azimuth >= 360.0f64 || azimuth == 0.0f64 ? 0.0f64 : azimuth;
    out->day_hours = climate_sun_day_hours(latitude_deg, declination_deg);
}

void climate_sun_of_clock(uint32_t day_zero, calendar_clock_t clock, float64_t y, climate_sun_t *out) {
    /* The hour of the clock itself, not of the calendar day, which starts at 06:00. */
    const float64_t hour =
        CALENDAR_HOURS_PER_DAY_F64 * (float64_t)(clock & (CALENDAR_CLOCK_DAY - 1u)) / (float64_t)CALENDAR_CLOCK_DAY;
    climate_sun_at(climate_latitude_deg(y), calendar_year_position(day_zero, clock), hour, out);
}

float64_t climate_sun_height_of_clock(uint32_t day_zero, calendar_clock_t clock, float64_t y) {
    /* All of it, and only the height kept: the same code is what makes it the same bits as the picture's sun. */
    climate_sun_t sun;
    climate_sun_of_clock(day_zero, clock, y, &sun);
    return sun.direction[2];
}
