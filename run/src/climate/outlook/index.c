/* See README.md. Master plan 8's outlook, in two halves: climate_outlook_input_fill reads the climate as it is at the census
 * moment (the systems alive then and their steered tracks, the normals, the spawn rates) into a plain block, and
 * climate_outlook_run turns that block into chances without a climate to read. Allocates only from the arena it is given. */
#include "climate/outlook/index.h"

#include "climate/systems/field/index.h"
#include "climate/systems/index.h"
#include "climate/systems/name/index.h"
#include "climate/weather/index.h"
#include "terrain/rng/index.h"

#include <math.h>
#include <string.h>

static const float64_t OUTLOOK_PI = 3.14159265358979323846f64;
static const float64_t OUTLOOK_SQRT2 = 1.41421356237309504880f64;
/* A spawn square's start points about its centre: a uniform square of side q has a spread of q / sqrt(12) on each axis. */
static const float64_t OUTLOOK_SQRT12 = 3.46410161513775458705f64;
/* Wind in blocks a second to blocks a game hour: 1200 real seconds a day, over 24 hours. */
static const float32_t OUTLOOK_SECONDS_AN_HOUR = 50.0f32;
static const float64_t OUTLOOK_KM2_A_SQUARE = 1.048576f64; /* a 1024-block square */

float64_t climate_outlook_sigma(float64_t days) {
    return CLIMATE_OUTLOOK_SIGMA_START + CLIMATE_OUTLOOK_SIGMA_PER_DAY * days;
}

float64_t climate_outlook_disk_chance(float64_t d, float64_t sigma, float64_t radius) {
    if (!(radius > 0.0f64)) {
        return 0.0f64;
    }
    if (!(sigma > 1.0e-9f64)) {
        return fabs(d) < radius ? 1.0f64 : 0.0f64;
    }
    /* The centre at (d, 0): over x = R sin(phi) across the disk, the x density times the chance |y| < R cos(phi). The
     * substitution leaves no square root at the ends, so Simpson's rule on a fixed grid is accurate and the same everywhere. */
    const uint32_t steps = CLIMATE_OUTLOOK_DISK_STEPS;
    const float64_t h = OUTLOOK_PI / (float64_t)steps;
    float64_t sum = 0.0f64;
    for (uint32_t i = 0u; i <= steps; i++) {
        const float64_t phi = -0.5f64 * OUTLOOK_PI + h * (float64_t)i;
        const float64_t across = radius * cos(phi);
        const float64_t z = (radius * sin(phi) - d) / sigma;
        const float64_t density = exp(-0.5f64 * z * z) / (sigma * sqrt(2.0f64 * OUTLOOK_PI));
        const float64_t f = density * erf(across / (sigma * OUTLOOK_SQRT2)) * across;
        const float64_t weight = (i == 0u || i == steps) ? 1.0f64 : ((i % 2u) == 1u ? 4.0f64 : 2.0f64);
        sum += weight * f;
    }
    const float64_t p = sum * h / 3.0f64;
    return p < 0.0f64 ? 0.0f64 : (p > 1.0f64 ? 1.0f64 : p);
}

float64_t climate_outlook_footprint(float64_t d, float64_t sigma, float64_t radius, uint8_t footprint) {
    if (footprint == CLIMATE_OUTLOOK_FOOTPRINT_EXACT) {
        return climate_outlook_disk_chance(d, sigma, radius);
    }
    return exp(-(d * d) / (2.0f64 * (sigma * sigma + radius * radius)));
}

float32_t climate_outlook_background_rain(float32_t rain_mm_day) {
    if (!(rain_mm_day > 0.0f32)) {
        return 0.0f32;
    }
    return 1.0f32 - expf(-rain_mm_day / CLIMATE_OUTLOOK_RAIN_MM);
}

float64_t climate_outlook_at_least_one(const float64_t *chances, uint32_t count, float64_t lambda) {
    float64_t none = exp(-lambda);
    for (uint32_t i = 0u; i < count; i++) {
        none *= 1.0f64 - chances[i];
    }
    return 1.0f64 - none;
}

void climate_outlook_error(uint64_t seed, uint64_t id, uint64_t week, uint32_t day, float64_t out[2]) {
    const uint64_t base = terrain_rng_derive(seed, id ^ (week * 0x9E3779B97F4A7C15ull));
    const uint64_t a = terrain_rng_hash3(base, (int32_t)day, 0, 1);
    const uint64_t b = terrain_rng_hash3(base, (int32_t)day, 0, 2);
    /* Box-Muller from two uniforms; u1 in (0, 1] so the logarithm is finite. */
    const float64_t u1 = ((float64_t)(a >> 11) + 1.0f64) * 0x1.0p-53f64;
    const float64_t u2 = (float64_t)(b >> 11) * 0x1.0p-53f64;
    const float64_t r = sqrt(-2.0f64 * log(u1));
    out[0] = r * cos(2.0f64 * OUTLOOK_PI * u2);
    out[1] = r * sin(2.0f64 * OUTLOOK_PI * u2);
}

/* ---- The input: what is known now ---------------------------------------------------------------------------------- */

static float64_t outlook_range_mid(const float32_t range[2]) {
    return 0.5f64 * ((float64_t)range[0] + (float64_t)range[1]);
}

/* The chance a system of this kind is still alive age days after its birth: its life is uniform on the kind's range. */
static float64_t outlook_life_share(uint8_t kind, float64_t age) {
    const float32_t *const life =
        kind == CLIMATE_SYSTEM_HURRICANE ? CLIMATE_HURRICANE_LIFE_DAYS : CLIMATE_LOW_LIFE_DAYS;
    const float64_t lo = (float64_t)life[0], hi = (float64_t)life[1];
    const float64_t share = (hi - age) / (hi - lo);
    return share < 0.0f64 ? 0.0f64 : (share > 1.0f64 ? 1.0f64 : share);
}

/* A typical system of the kind born at (x, y) at t0: the middle of every range, so its track is what the kind usually does. */
static void outlook_typical(uint8_t kind, float64_t x, float64_t y, float64_t t0, climate_system_t *out) {
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    out->spawn_day = t0;
    out->start_x = x;
    out->start_y = y;
    if (kind == CLIMATE_SYSTEM_HURRICANE) {
        out->radius = (float32_t)outlook_range_mid(CLIMATE_HURRICANE_RADIUS);
        out->core_radius = (float32_t)outlook_range_mid(CLIMATE_HURRICANE_EYE);
        out->peak_hpa = (float32_t)outlook_range_mid(CLIMATE_HURRICANE_HPA);
        out->life_days = CLIMATE_HURRICANE_LIFE_DAYS[1];
        out->speed = 1.0f32;
        out->turn_psi_deg = (float32_t)outlook_range_mid(CLIMATE_HURRICANE_TURN_PSI_DEG);
    } else {
        out->radius = (float32_t)outlook_range_mid(CLIMATE_LOW_RADIUS);
        out->core_radius = CLIMATE_SYSTEM_CORE_SHARE * out->radius;
        out->peak_hpa = (float32_t)outlook_range_mid(CLIMATE_LOW_HPA);
        out->life_days = CLIMATE_LOW_LIFE_DAYS[1];
        out->speed = (float32_t)outlook_range_mid(CLIMATE_LOW_SPEED);
        out->front_length = (float32_t)outlook_range_mid(CLIMATE_LOW_FRONT_BLOCKS);
        out->shield_mm_hour = (float32_t)outlook_range_mid(CLIMATE_LOW_SHIELD_MM_HOUR);
    }
}

/* Lambda for a kind at each instant: over every square in reach and every day from the census day to the day before the
 * instant, the square's rate times the chance a system born there covers here at the instant. */
void climate_outlook_unborn(const climate_t *climate, arena_t *scratch, climate_systems_mask_t *mask, uint8_t kind,
                            float64_t x, float64_t y, float64_t census_t, const float64_t *after, uint32_t count,
                            float64_t *out) {
    const float64_t q = climate_systems_square(kind);
    const float64_t reach = climate_systems_reach(kind);
    const float64_t spread = q / OUTLOOK_SQRT12;
    const int64_t census_day = (int64_t)floor(census_t);
    const int32_t x0 = (int32_t)floor((x - reach) / q), x1 = (int32_t)floor((x + reach) / q);
    const int32_t y0 = (int32_t)floor((y - reach) / q), y1 = (int32_t)floor((y + reach) / q);
    /* Births up to the last instant: the week's 7 days for the outlook's own instants, a day more for one past them. */
    float64_t last = 0.0f64;
    for (uint32_t i = 0u; i < count; i++) {
        out[i] = 0.0f64;
        last = after[i] > last ? after[i] : last;
    }
    const int64_t through = (int64_t)ceil(census_t + last) - census_day;
    const int64_t span = through > (int64_t)CLIMATE_OUTLOOK_DAYS ? through : (int64_t)CLIMATE_OUTLOOK_DAYS;
    climate_system_t *const typical = arena_alloc(scratch, sizeof(climate_system_t), alignof(climate_system_t));
    for (int32_t sy = y0; sy <= y1; sy++) {
        for (int32_t sx = x0; sx <= x1; sx++) {
            const float64_t cx = ((float64_t)sx + 0.5f64) * q, cy = ((float64_t)sy + 0.5f64) * q;
            for (int64_t d = census_day; d < census_day + span; d++) {
                const float64_t born = (float64_t)d + 0.5f64;
                const float32_t rate = kind == CLIMATE_SYSTEM_HURRICANE
                                           ? climate_systems_hurricane_rate(climate, scratch, cx, cy, born)
                                           : climate_systems_low_rate(climate, cy, born);
                if (!(rate > 0.0f32)) {
                    continue;
                }
                outlook_typical(kind, cx, cy, born, typical);
                climate_systems_track(climate, mask, typical);
                for (uint32_t i = 0u; i < count; i++) {
                    const float64_t instant = census_t + after[i];
                    if (born >= instant) {
                        continue; /* born after the instant: it cannot be here then */
                    }
                    climate_system_state_t state;
                    climate_system_state_at(typical, instant, &state);
                    if (state.alive == 0u) {
                        continue;
                    }
                    const float64_t dx = state.x - x, dy = state.y - y;
                    const float64_t cover =
                        climate_outlook_disk_chance(sqrt(dx * dx + dy * dy), spread, (float64_t)typical->radius);
                    out[i] += (float64_t)rate * cover * outlook_life_share(kind, instant - born);
                }
            }
        }
    }
}

/* Lambda at the outlook's own instants, 1 to 7 days after the census, as the input keeps it. */
static void outlook_unborn_week(const climate_t *climate, arena_t *scratch, climate_systems_mask_t *mask, uint8_t kind,
                                float64_t x, float64_t y, float64_t census_t, float32_t out[CLIMATE_OUTLOOK_DAYS]) {
    float64_t after[CLIMATE_OUTLOOK_DAYS], lambda[CLIMATE_OUTLOOK_DAYS];
    for (uint32_t t = 0u; t < CLIMATE_OUTLOOK_DAYS; t++) {
        after[t] = (float64_t)(t + 1u);
    }
    climate_outlook_unborn(climate, scratch, mask, kind, x, y, census_t, after, CLIMATE_OUTLOOK_DAYS, lambda);
    for (uint32_t t = 0u; t < CLIMATE_OUTLOOK_DAYS; t++) {
        out[t] = (float32_t)lambda[t];
    }
}

/* Known systems in order of how near they are now, then by id: the input keeps the nearest CLIMATE_OUTLOOK_KNOWN_MAX. */
static float64_t outlook_known_distance(const climate_system_t *system, float64_t t, float64_t x, float64_t y) {
    climate_system_state_t state;
    climate_system_state_at(system, t, &state);
    const float64_t dx = state.x - x, dy = state.y - y;
    return sqrt(dx * dx + dy * dy);
}

uint64_t climate_outlook_seed(const climate_t *climate) {
    return terrain_rng_derive(climate_seed(climate), terrain_rng_salt("climate_outlook/error"));
}

bool climate_outlook_known_list(const climate_t *climate, float64_t x, float64_t y, float64_t census_t,
                                arena_t *scratch, climate_systems_day_t *list, uint32_t **out_order,
                                uint32_t *out_count) {
    const int64_t census_day = (int64_t)floor(census_t);
    /* The systems alive at the census moment: born by then and not yet dead. None born after is ever looked at. */
    if (!climate_systems_day_build(climate, scratch, x, y, (float32_t)CLIMATE_OUTLOOK_REACH, 0.0f32, census_day,
                                   CLIMATE_SYSTEMS_BIG, list)) {
        return false;
    }
    uint32_t *const order = arena_alloc_array(scratch, list->system_count + 1u, sizeof(uint32_t), 4u);
    uint32_t alive = 0u;
    for (uint32_t i = 0u; i < list->system_count; i++) {
        const climate_system_t *const system = &list->borrowed_systems[i];
        climate_system_state_t state;
        climate_system_state_at(system, census_t, &state);
        if (system->spawn_day <= census_t && state.alive != 0u) {
            order[alive++] = i;
        }
    }
    /* Insertion sort by distance now, then id: at most a few dozen. */
    for (uint32_t i = 1u; i < alive; i++) {
        const uint32_t moving = order[i];
        const climate_system_t *const m = &list->borrowed_systems[moving];
        const float64_t dm = outlook_known_distance(m, census_t, x, y);
        uint32_t j = i;
        while (j > 0u) {
            const climate_system_t *const o = &list->borrowed_systems[order[j - 1u]];
            const float64_t d_o = outlook_known_distance(o, census_t, x, y);
            if (d_o < dm || (d_o == dm && o->id < m->id)) {
                break;
            }
            order[j] = order[j - 1u];
            j--;
        }
        order[j] = moving;
    }
    *out_order = order;
    *out_count = alive;
    return true;
}

bool climate_outlook_input_fill(const climate_t *climate, float64_t x, float64_t y, float64_t census_t, uint64_t week,
                                arena_t *scratch, climate_outlook_input_t *out) {
    memset(out, 0, sizeof(*out));
    out->x = x;
    out->y = y;
    out->census_t = census_t;
    out->week = week;
    out->seed = climate_outlook_seed(climate);
    out->footprint = CLIMATE_OUTLOOK_FOOTPRINT;
    climate_cell_t cell;
    if (!climate_cell_get(climate, (int32_t)floor(x), (int32_t)floor(y), &cell)) {
        return false;
    }
    out->sigma_t = CLIMATE_SIGMA_T_COAST + CLIMATE_SIGMA_T_INLAND_EXTRA * cell.kappa;
    const int64_t census_day = (int64_t)floor(census_t);

    climate_systems_day_t list;
    uint32_t *order = nullptr;
    uint32_t alive = 0u;
    if (!climate_outlook_known_list(climate, x, y, census_t, scratch, &list, &order, &alive)) {
        return false;
    }
    const uint32_t kept = alive < CLIMATE_OUTLOOK_KNOWN_MAX ? alive : CLIMATE_OUTLOOK_KNOWN_MAX;
    float64_t push = 0.0f64;
    for (uint32_t k = 0u; k < kept; k++) {
        const climate_system_t *const system = &list.borrowed_systems[order[k]];
        climate_outlook_known_t *const known = &out->known[k];
        known->id = system->id;
        known->kind = system->kind;
        known->radius = system->radius;
        climate_system_title(system, census_t, known->name);
        for (uint32_t t = 0u; t < CLIMATE_OUTLOOK_DAYS; t++) {
            climate_system_state_t state;
            climate_system_state_at(system, census_t + (float64_t)(t + 1u), &state);
            known->alive[t] = state.alive;
            known->x[t] = state.x;
            known->y[t] = state.y;
            known->pressure[t] = state.alive != 0u ? state.pressure_hpa : 0.0f32;
        }
        if (system->kind == CLIMATE_SYSTEM_HIGH) {
            const float64_t r = outlook_known_distance(system, census_t, x, y);
            const float64_t big = (float64_t)system->radius;
            push += (float64_t)(CLIMATE_HIGH_ANOMALY_SIGMAS * out->sigma_t) * exp(-(r * r) / (big * big));
        }
    }
    out->known_count = kept;

    /* The normals of each day ahead, the heat today's spell leaves, and the cells it makes. */
    climate_normals_at_t today;
    if (!climate_normals_at(climate, x, y, nanf(""), (float32_t)(census_day % 365), &today)) {
        return false;
    }
    const float32_t spell = out->sigma_t * climate_weather_spell(climate, x, y, census_day);
    out->anomaly = spell + (float32_t)(today.t_day_c > today.t_mean_c ? push : -push);
    for (uint32_t t = 0u; t < CLIMATE_OUTLOOK_DAYS; t++) {
        const int64_t day = census_day + (int64_t)t;
        const float32_t d = (float32_t)(((day % 365) + 365) % 365);
        climate_normals_at_t n;
        climate_air_t air;
        if (!climate_normals_at(climate, x, y, nanf(""), d, &n) || !climate_air_at(climate, x, y, d + 0.25f32, &air)) {
            return false;
        }
        out->high_c[t] = n.t_high_c;
        out->low_c[t] = n.t_low_c;
        out->rain_mm[t] = n.rain_mm_day;
        const float32_t heat = n.t_day_c + out->anomaly * expf(-(float32_t)(t + 1u) / (float32_t)CLIMATE_SPELL_DAYS);
        const float32_t share = n.rain_max_mm_day > 0.0f32 ? n.rain_mm_day / n.rain_max_mm_day : 0.0f32;
        out->thunder_rate[t] = (float32_t)((float64_t)climate_systems_thunder_base(heat, share, n.monsoon_today != 0u) /
                                           OUTLOOK_KM2_A_SQUARE);
        out->wind_blocks_hour[t] =
            sqrtf(air.wind[0] * air.wind[0] + air.wind[1] * air.wind[1]) * OUTLOOK_SECONDS_AN_HOUR;
    }

    /* Not born yet: the spawn rates over every square in reach, thinned by the chance of reaching here. */
    climate_systems_mask_t mask;
    climate_systems_mask_start(&mask, climate, scratch, nullptr);
    outlook_unborn_week(climate, scratch, &mask, CLIMATE_SYSTEM_HURRICANE, x, y, census_t, out->unborn_hurricane);
    outlook_unborn_week(climate, scratch, &mask, CLIMATE_SYSTEM_LOW, x, y, census_t, out->unborn_low);
    return true;
}

/* ---- The chances, from the input alone ----------------------------------------------------------------------------- */

/* A known system's chance of covering (x, y), its centre at (cx, cy) when t days have passed since the census: the forecast
 * moves the centre by its error, drawn for each whole day and going evenly between them, and spreads it by sigma(t). At a
 * whole t the error is that day's draw itself. */
static float64_t outlook_forecast_chance(float64_t cx, float64_t cy, float64_t radius, uint64_t id, uint64_t seed,
                                         uint64_t week, float64_t t, float64_t x, float64_t y, bool error_off,
                                         uint8_t footprint) {
    const float64_t sigma = climate_outlook_sigma(t);
    float64_t error[2] = {0.0f64, 0.0f64};
    if (!error_off) {
        const float64_t whole = floor(t);
        const float64_t part = t - whole;
        climate_outlook_error(seed, id, week, (uint32_t)whole, error);
        if (part > 0.0f64) {
            float64_t next[2];
            climate_outlook_error(seed, id, week, (uint32_t)whole + 1u, next);
            error[0] += part * (next[0] - error[0]);
            error[1] += part * (next[1] - error[1]);
        }
    }
    const float64_t fx = cx + error[0] * sigma, fy = cy + error[1] * sigma;
    const float64_t dx = fx - x, dy = fy - y;
    return climate_outlook_footprint(sqrt(dx * dx + dy * dy), sigma, radius, footprint);
}

float64_t climate_outlook_system_chance(const climate_system_t *system, uint64_t seed, uint64_t week,
                                        float64_t census_t, float64_t t, float64_t x, float64_t y, bool error_off,
                                        uint8_t footprint) {
    climate_system_state_t state;
    climate_system_state_at(system, census_t + t, &state);
    if (state.alive == 0u) {
        return 0.0f64;
    }
    return outlook_forecast_chance(state.x, state.y, (float64_t)system->radius, system->id, seed, week, t, x, y,
                                   error_off, footprint);
}

static uint32_t outlook_fold_id(uint64_t id) {
    return (uint32_t)(id ^ (id >> 32));
}

void climate_outlook_run(const climate_outlook_input_t *input, climate_outlook_t *out) {
    memset(out, 0, sizeof(*out));
    out->week = (uint32_t)input->week;
    float32_t chance[CLIMATE_OUTLOOK_KNOWN_MAX][CLIMATE_OUTLOOK_DAYS];
    memset(chance, 0, sizeof(chance));
    for (uint32_t k = 0u; k < input->known_count && k < CLIMATE_OUTLOOK_KNOWN_MAX; k++) {
        const climate_outlook_known_t *const known = &input->known[k];
        for (uint32_t t = 0u; t < CLIMATE_OUTLOOK_DAYS; t++) {
            if (known->alive[t] == 0u) {
                continue; /* died by then */
            }
            chance[k][t] = (float32_t)outlook_forecast_chance(
                known->x[t], known->y[t], (float64_t)known->radius, known->id, input->seed, input->week,
                (float64_t)(t + 1u), input->x, input->y, input->error_off != 0u, input->footprint);
        }
    }
    for (uint32_t t = 0u; t < CLIMATE_OUTLOOK_DAYS; t++) {
        float64_t hurricanes[CLIMATE_OUTLOOK_KNOWN_MAX], storms[CLIMATE_OUTLOOK_KNOWN_MAX];
        float64_t rainy[CLIMATE_OUTLOOK_KNOWN_MAX + 2u];
        uint32_t n_hurricane = 0u, n_storm = 0u, n_rain = 0u;
        for (uint32_t k = 0u; k < input->known_count && k < CLIMATE_OUTLOOK_KNOWN_MAX; k++) {
            const climate_outlook_known_t *const known = &input->known[k];
            const float64_t p = (float64_t)chance[k][t];
            if (known->kind == CLIMATE_SYSTEM_HURRICANE) {
                hurricanes[n_hurricane++] = p;
                storms[n_storm++] = p;
                rainy[n_rain++] = p;
            } else if (known->kind == CLIMATE_SYSTEM_LOW) {
                rainy[n_rain++] = p;
                if (known->pressure[t] <= -CLIMATE_STORM_DEPTH_HPA) {
                    storms[n_storm++] = p;
                }
            }
        }
        climate_outlook_day_t *const day = &out->days[t];
        const float64_t hurricane_lambda = (float64_t)input->unborn_hurricane[t];
        const float64_t low_lambda = (float64_t)input->unborn_low[t];
        day->hurricane = (float32_t)climate_outlook_at_least_one(hurricanes, n_hurricane, hurricane_lambda);
        day->storm = (float32_t)climate_outlook_at_least_one(
            storms, n_storm, hurricane_lambda + (float64_t)CLIMATE_OUTLOOK_DEEP_LOW_SHARE * low_lambda);
        /* A cell passes over a point if it starts in the strip its footprint sweeps, or its own disk. */
        const float64_t r = (float64_t)CLIMATE_OUTLOOK_CELL_RADIUS;
        const float64_t sweep =
            2.0f64 * r * (float64_t)input->wind_blocks_hour[t] * (float64_t)CLIMATE_OUTLOOK_CELL_HOURS;
        const float64_t area = (sweep + OUTLOOK_PI * r * r) / 1.0e6f64;
        day->thunder = (float32_t)(1.0f64 - exp(-(float64_t)input->thunder_rate[t] * area));
        rainy[n_rain++] = (float64_t)climate_outlook_background_rain(input->rain_mm[t]);
        rainy[n_rain++] = (float64_t)day->thunder;
        day->rain = (float32_t)climate_outlook_at_least_one(rainy, n_rain, hurricane_lambda + low_lambda);
        const float32_t decay = expf(-(float32_t)(t + 1u) / (float32_t)CLIMATE_SPELL_DAYS);
        day->high_c = input->high_c[t] + input->anomaly * decay;
        day->low_c = input->low_c[t] + input->anomaly * decay;
        day->spread_c = CLIMATE_OUTLOOK_SPREAD_SIGMAS * input->sigma_t;
    }
    /* The named systems: the highest chance on any day, then id. */
    float32_t best[CLIMATE_OUTLOOK_KNOWN_MAX];
    bool taken[CLIMATE_OUTLOOK_KNOWN_MAX];
    for (uint32_t k = 0u; k < CLIMATE_OUTLOOK_KNOWN_MAX; k++) {
        best[k] = 0.0f32;
        taken[k] = false;
        for (uint32_t t = 0u; t < CLIMATE_OUTLOOK_DAYS; t++) {
            best[k] = chance[k][t] > best[k] ? chance[k][t] : best[k];
        }
    }
    for (uint32_t slot = 0u; slot < CLIMATE_OUTLOOK_SYSTEMS_MAX; slot++) {
        uint32_t pick = CLIMATE_OUTLOOK_KNOWN_MAX;
        for (uint32_t k = 0u; k < input->known_count && k < CLIMATE_OUTLOOK_KNOWN_MAX; k++) {
            if (taken[k] || !(best[k] > 0.0f32)) {
                continue;
            }
            if (pick == CLIMATE_OUTLOOK_KNOWN_MAX || best[k] > best[pick] ||
                (best[k] == best[pick] && input->known[k].id < input->known[pick].id)) {
                pick = k;
            }
        }
        if (pick == CLIMATE_OUTLOOK_KNOWN_MAX) {
            break;
        }
        taken[pick] = true;
        climate_outlook_system_t *const system = &out->systems[out->system_count++];
        memcpy(system->name, input->known[pick].name, sizeof(system->name));
        system->name[sizeof(system->name) - 1u] = '\0';
        system->id = outlook_fold_id(input->known[pick].id);
        system->kind = input->known[pick].kind;
        memcpy(system->chance, chance[pick], sizeof(system->chance));
    }
}
