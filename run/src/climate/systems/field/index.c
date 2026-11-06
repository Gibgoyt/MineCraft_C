/* See README.md. Pressure, wind and rain of the weather systems of a day list at any point and moment: master plan 7.3,
 * and the phase 06 file's "Fields: pressure, wind, rain". Read-only, no allocation, any thread. Sums add in list order, so
 * the same list gives the same bytes however it was found. */
#include "climate/systems/field/index.h"

#include "climate/air/index.h"

#include <math.h>

static const float64_t FIELD_TWO_PI = 6.28318530717958647692f64;

float32_t climate_systems_rain_share(float32_t lat_deg) {
    const float64_t lo = (float64_t)CLIMATE_SYSTEM_RAIN_SHARE_BLEND_DEG[0];
    const float64_t hi = (float64_t)CLIMATE_SYSTEM_RAIN_SHARE_BLEND_DEG[1];
    const float64_t a = fabs((float64_t)lat_deg);
    const float64_t s = climate_systems_smoothstep((a - lo) / (hi - lo));
    const float64_t mid = (float64_t)CLIMATE_SYSTEM_RAIN_SHARE_MID;
    const float64_t share =
        (float64_t)CLIMATE_SYSTEM_RAIN_SHARE_TROPICS + (mid - (float64_t)CLIMATE_SYSTEM_RAIN_SHARE_TROPICS) * s;
    /* And down again toward the poles, past the band where lows form (Issues found 12). */
    const float64_t p_lo = (float64_t)CLIMATE_SYSTEM_RAIN_SHARE_POLAR_DEG[0];
    const float64_t p_hi = (float64_t)CLIMATE_SYSTEM_RAIN_SHARE_POLAR_DEG[1];
    const float64_t p = climate_systems_smoothstep((a - p_lo) / (p_hi - p_lo));
    return (float32_t)(share + ((float64_t)CLIMATE_SYSTEM_RAIN_SHARE_POLAR - mid) * p);
}

float32_t climate_systems_rain_share_today(float32_t lat_deg, float32_t day, float32_t t_day_c) {
    if (t_day_c > CLIMATE_THUNDER_HOT_C) {
        return climate_systems_rain_share(lat_deg); /* cells can form */
    }
    const float32_t psi = fabsf(climate_air_wind_latitude((float64_t)lat_deg, day));
    const float32_t margin = CLIMATE_SYSTEM_RAIN_SEASON_MARGIN_DEG;
    if (psi >= CLIMATE_LOW_PSI_MIN_DEG - margin && psi <= CLIMATE_LOW_PSI_MAX_DEG + margin) {
        return climate_systems_rain_share(lat_deg); /* lows can come by */
    }
    return 0.0f32;
}

float32_t climate_systems_wetness(float32_t rain_mm_day) {
    return rain_mm_day > 0.0f32 ? rain_mm_day / CLIMATE_SYSTEM_RAIN_WET_MM_DAY : 0.0f32;
}

float32_t climate_systems_spin(uint8_t kind, float64_t centre_y) {
    const float64_t phi = climate_latitude_deg(centre_y);
    const float32_t hemisphere = phi >= 0.0f64 ? 1.0f32 : -1.0f32;
    const float32_t sigma = hemisphere * climate_air_mirror_sign(centre_y);
    return kind == CLIMATE_SYSTEM_HIGH ? -sigma : sigma;
}

float32_t climate_systems_wind_speed(float32_t wind_max, float32_t core_radius, float32_t radius, float64_t r) {
    const float64_t reach = (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)radius;
    if (r < 1.0f64 || r > reach || !(core_radius > 0.0f32)) {
        return 0.0f32;
    }
    const float64_t rm = (float64_t)core_radius;
    float64_t v = r < rm ? (float64_t)wind_max * r / rm
                         : (float64_t)wind_max * pow(rm / r, (float64_t)CLIMATE_WIND_PROFILE_POWER);
    const float64_t taper_from = (float64_t)CLIMATE_SYSTEM_WIND_TAPER_RADII * (float64_t)radius;
    if (r > taper_from) {
        v *= climate_systems_smoothstep((reach - r) / (reach - taper_from));
    }
    return (float32_t)v;
}

/* The squared distance from (x, y) to the segment a-b. */
static float64_t field_segment_distance2(float64_t x, float64_t y, const float64_t a[2], const float64_t b[2]) {
    const float64_t dx = b[0] - a[0], dy = b[1] - a[1];
    const float64_t length2 = dx * dx + dy * dy;
    float64_t s = 0.0f64;
    if (length2 > 0.0f64) {
        s = ((x - a[0]) * dx + (y - a[1]) * dy) / length2;
        s = s < 0.0f64 ? 0.0f64 : (s > 1.0f64 ? 1.0f64 : s);
    }
    const float64_t ex = a[0] + s * dx - x, ey = a[1] + s * dy - y;
    return ex * ex + ey * ey;
}

bool climate_systems_front(const climate_system_t *low, float64_t t, float64_t out_from[2], float64_t out_to[2]) {
    if (low->kind != CLIMATE_SYSTEM_LOW) {
        return false;
    }
    climate_system_state_t state;
    climate_system_state_at(low, t, &state);
    if (state.alive == 0u) {
        return false;
    }
    /* Equatorward turned 30 degrees toward the west: (-0.5, -0.866 e s_h). */
    const float64_t phi = climate_latitude_deg(state.y);
    const float64_t equatorward = -(float64_t)climate_air_mirror_sign(state.y) * (phi >= 0.0f64 ? 1.0f64 : -1.0f64);
    const float64_t length = (float64_t)low->front_length;
    out_from[0] = state.x;
    out_from[1] = state.y;
    out_to[0] = state.x + length * (float64_t)CLIMATE_FRONT_TURN_X;
    out_to[1] = state.y + length * (float64_t)CLIMATE_FRONT_TURN_Y * equatorward;
    return true;
}

bool climate_systems_thunder_at(const climate_thunder_t *cell, float64_t t, float64_t out_centre[2],
                                float64_t *out_share) {
    const float64_t age = t - cell->spawn_day;
    if (age < 0.0f64 || age > (float64_t)cell->life_days) {
        return false;
    }
    out_centre[0] = cell->start_x + (float64_t)cell->velocity[0] * age;
    out_centre[1] = cell->start_y + (float64_t)cell->velocity[1] * age;
    *out_share = climate_systems_life(age / (float64_t)cell->life_days);
    return true;
}

float32_t climate_systems_pressure_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t) {
    float64_t sum = 0.0f64;
    const float64_t reach = (float64_t)CLIMATE_SYSTEM_REACH_RADII;
    for (uint32_t i = 0u; i < day->system_count; i++) {
        const climate_system_t *const system = &day->borrowed_systems[i];
        climate_system_state_t state;
        climate_system_state_at(system, t, &state);
        if (state.alive == 0u) {
            continue;
        }
        const float64_t dx = x - state.x, dy = y - state.y, r2 = dx * dx + dy * dy;
        const float64_t big = (float64_t)system->radius * (float64_t)system->radius;
        if (r2 > reach * reach * big) {
            continue;
        }
        sum += (float64_t)state.pressure_hpa * exp(-r2 / big);
    }
    for (uint32_t i = 0u; i < day->thunder_count; i++) {
        const climate_thunder_t *const cell = &day->borrowed_thunder[i];
        float64_t centre[2], share = 0.0f64;
        if (!climate_systems_thunder_at(cell, t, centre, &share)) {
            continue;
        }
        const float64_t dx = x - centre[0], dy = y - centre[1], r2 = dx * dx + dy * dy;
        const float64_t big = (float64_t)cell->radius * (float64_t)cell->radius;
        if (r2 > reach * reach * big) {
            continue;
        }
        sum += (float64_t)cell->peak_hpa * share * exp(-r2 / big);
    }
    return (float32_t)sum;
}

void climate_systems_wind_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t,
                             float32_t out[2]) {
    float64_t u = 0.0f64, v = 0.0f64;
    for (uint32_t i = 0u; i < day->system_count; i++) {
        const climate_system_t *const system = &day->borrowed_systems[i];
        climate_system_state_t state;
        climate_system_state_at(system, t, &state);
        if (state.alive == 0u) {
            continue;
        }
        const float64_t dx = x - state.x, dy = y - state.y;
        const float64_t r = sqrt(dx * dx + dy * dy);
        const float32_t speed = climate_systems_wind_speed(state.wind_max, system->core_radius, system->radius, r);
        if (speed == 0.0f32) {
            continue;
        }
        /* The unit vector out from the centre turned 90 degrees anticlockwise, times the turning sense. */
        const float64_t along = (float64_t)climate_systems_spin(system->kind, state.y) * (float64_t)speed / r;
        u += -dy * along;
        v += dx * along;
    }
    out[0] = (float32_t)u;
    out[1] = (float32_t)v;
}

float32_t climate_systems_rain_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t) {
    float64_t rain = 0.0f64;
    const float64_t reach = (float64_t)CLIMATE_SYSTEM_REACH_RADII;
    for (uint32_t i = 0u; i < day->system_count; i++) {
        const climate_system_t *const system = &day->borrowed_systems[i];
        if (system->kind == CLIMATE_SYSTEM_HIGH) {
            continue; /* a high only cuts: climate_systems_rain_factor */
        }
        climate_system_state_t state;
        climate_system_state_at(system, t, &state);
        if (state.alive == 0u) {
            continue;
        }
        const float64_t dx = x - state.x, dy = y - state.y;
        const float64_t r = sqrt(dx * dx + dy * dy);
        const float64_t big = (float64_t)system->radius;
        if (system->kind == CLIMATE_SYSTEM_HURRICANE) {
            if (r > reach * big) {
                continue;
            }
            const float64_t peak = (float64_t)CLIMATE_HURRICANE_EYEWALL_MM_HOUR * fabs((float64_t)state.pressure_hpa) /
                                   (float64_t)CLIMATE_HURRICANE_EYEWALL_HPA;
            const float64_t rm = (float64_t)system->core_radius;
            if (r < rm) {
                rain += peak * (r / rm) * (r / rm); /* nearly dry in the eye */
                continue;
            }
            /* Three spiral arms winding inward in the storm's own turning sense, each storm's turned by its id. */
            const float64_t sigma = (float64_t)climate_systems_spin(system->kind, state.y);
            const float64_t theta = atan2(dy, dx);
            const float64_t turn = FIELD_TWO_PI * climate_systems_uniform(system->id);
            const float64_t band =
                0.5f64 + 0.5f64 * cos((float64_t)CLIMATE_HURRICANE_BAND_ARMS * theta -
                                      (float64_t)CLIMATE_HURRICANE_BAND_WIND * sigma * log(r / rm) + turn);
            float64_t taper = 1.0f64;
            const float64_t taper_from = (float64_t)CLIMATE_SYSTEM_WIND_TAPER_RADII * big;
            if (r > taper_from) {
                taper = climate_systems_smoothstep((reach * big - r) / (reach * big - taper_from));
            }
            rain += peak * sqrt(rm / r) * band * taper;
            continue;
        }
        /* A low: the shield ahead of the centre, and showers on the front. */
        const float64_t g = climate_systems_life((float64_t)state.life_share);
        const float64_t ahead = (float64_t)CLIMATE_LOW_SHIELD_AHEAD * big;
        const float64_t sx = state.x + ahead * (float64_t)state.heading[0] - x;
        const float64_t sy = state.y + ahead * (float64_t)state.heading[1] - y;
        const float64_t width = (float64_t)CLIMATE_LOW_SHIELD_WIDTH * big;
        const float64_t s2 = sx * sx + sy * sy;
        if (s2 <= reach * reach * width * width) {
            rain += (float64_t)system->shield_mm_hour * g * exp(-s2 / (width * width));
        }
        float64_t from[2], to[2];
        if (climate_systems_front(system, t, from, to)) {
            const float64_t half = (float64_t)CLIMATE_FRONT_HALF_WIDTH;
            const float64_t d2 = field_segment_distance2(x, y, from, to);
            if (d2 <= reach * reach * half * half) {
                /* In proportion to the low's shield, so a low's season scale applies to its front too. */
                const float64_t strength = (float64_t)system->shield_mm_hour / (float64_t)CLIMATE_LOW_SHIELD_MEAN;
                rain += (float64_t)CLIMATE_FRONT_MM_HOUR * strength * g * exp(-d2 / (half * half));
            }
        }
    }
    for (uint32_t i = 0u; i < day->thunder_count; i++) {
        const climate_thunder_t *const cell = &day->borrowed_thunder[i];
        float64_t centre[2], share = 0.0f64;
        if (!climate_systems_thunder_at(cell, t, centre, &share)) {
            continue;
        }
        const float64_t dx = x - centre[0], dy = y - centre[1];
        const float64_t q = (dx * dx + dy * dy) / ((float64_t)cell->radius * (float64_t)cell->radius);
        if (q < 1.0f64) {
            rain += (float64_t)cell->peak_mm_hour * share * (1.0f64 - q);
        }
    }
    return (float32_t)rain;
}

bool climate_systems_under_high(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t) {
    for (uint32_t i = 0u; i < day->system_count; i++) {
        const climate_system_t *const system = &day->borrowed_systems[i];
        if (system->kind != CLIMATE_SYSTEM_HIGH) {
            continue;
        }
        climate_system_state_t state;
        climate_system_state_at(system, t, &state);
        if (state.alive == 0u) {
            continue;
        }
        const float64_t dx = x - state.x, dy = y - state.y;
        if (dx * dx + dy * dy < (float64_t)system->radius * (float64_t)system->radius) {
            return true;
        }
    }
    return false;
}

float32_t climate_systems_rain_factor(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t) {
    return climate_systems_under_high(day, x, y, t) ? CLIMATE_HIGH_RAIN_SHARE : 1.0f32;
}

bool climate_systems_on_front(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t) {
    const float64_t half = (float64_t)CLIMATE_FRONT_HALF_WIDTH;
    for (uint32_t i = 0u; i < day->system_count; i++) {
        float64_t from[2], to[2];
        if (!climate_systems_front(&day->borrowed_systems[i], t, from, to)) {
            continue;
        }
        if (field_segment_distance2(x, y, from, to) <= half * half) {
            return true;
        }
    }
    return false;
}
