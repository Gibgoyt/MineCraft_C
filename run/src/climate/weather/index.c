/* See README.md. Plan 004 phase 07: each day's weather at a place (master plan 7.4), the store of every kept cell's weekly
 * state, a census week for a block of tiles, and flow accumulation over them (7.5). Daily weather is a pure function of the
 * seed, the place and the day; the census reads only pure functions and each tile's own previous state, so 1 worker and 16
 * give the same bytes. Allocates only from the arenas it is given. */
#include "climate/weather/index.h"

#include "assert/index.h"
#include "climate/air/index.h"
#include "climate/season/index.h"
#include "climate/systems/field/index.h"
#include "climate/systems/name/index.h"
#include "log/index.h"
#include "terrain/rng/index.h"
#include "time/index.h"

#include <math.h>
#include <string.h>

static const float64_t WEATHER_TWO_PI = 6.28318530717958647692f64;
static const float64_t WEATHER_HOUR_DAYS = 1.0f64 / 24.0f64;
/* The clock hour of the day's warmest moment: the daily cycle's peak. */
static const float64_t WEATHER_WARMEST_HOUR = 15.0f64;
/* Past a system's own reach, so a view never drops what could still touch a point by a rounding. */
static const float64_t WEATHER_VIEW_MARGIN = 64.0f64;
/* A front's showers reach this far either side of its line: 3 half-widths, as climate/systems/field cuts it. */
static const float64_t WEATHER_FRONT_REACH = 3.0f64 * 400.0f64;
/* A block's day lists are worked out in a child arena of this, reset each day. */
static const uint64_t WEATHER_DAY_ARENA_BYTES = 20ull << 20;

static const terrain_noise_shape_t WEATHER_SPELL_SHAPE = {
    .frequency_x = 1.0f64 / CLIMATE_SPELL_BLOCKS,
    .frequency_y = 1.0f64 / CLIMATE_SPELL_BLOCKS,
    .frequency_z = 1.0f64 / CLIMATE_SPELL_DAYS,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t WEATHER_AMOUNT_SHAPE = {
    .frequency_x = 1.0f64 / CLIMATE_RAIN_AMOUNT_BLOCKS,
    .frequency_y = 1.0f64 / CLIMATE_RAIN_AMOUNT_BLOCKS,
    .frequency_z = 1.0f64 / CLIMATE_RAIN_AMOUNT_DAYS,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t WEATHER_HOURS_SHAPE = {
    .frequency_x = 1.0f64 / CLIMATE_RAIN_HOURS_BLOCKS,
    .frequency_y = 1.0f64 / CLIMATE_RAIN_HOURS_BLOCKS,
    .frequency_z = 1.0f64 / CLIMATE_RAIN_HOURS_HOURS,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};

static int32_t weather_floor_div(int32_t value, int32_t by) {
    const int32_t q = value / by;
    return (value % by != 0 && value < 0) ? q - 1 : q;
}

/* d of a calendar day, 0 to 364, for any D. */
static float32_t weather_day_of_year(int64_t day) {
    const int64_t year = (int64_t)CALENDAR_DAYS_PER_YEAR;
    return (float32_t)(((day % year) + year) % year);
}

static float32_t weather_scaled(float32_t raw, float32_t raw_sd) {
    const float32_t v = raw / raw_sd;
    return v > CLIMATE_SPELL_CLAMP ? CLIMATE_SPELL_CLAMP : (v < -CLIMATE_SPELL_CLAMP ? -CLIMATE_SPELL_CLAMP : v);
}

/* ---- The weather's region ------------------------------------------------------------------------------------------ */

bool climate_weather_open(climate_weather_t *weather, uint8_t *region, uint64_t bytes, uint64_t seed) {
    ASSERT_ALWAYS(bytes >= CLIMATE_WEEK_REGION_BYTES && (void *)weather == (void *)region);
    memset(weather, 0, sizeof(*weather));
    arena_init(&weather->arena_week, "climate_week", region + sizeof(climate_weather_t),
               bytes - sizeof(climate_weather_t));
    arena_t *const arena = &weather->arena_week;
    terrain_noise_bank_init(&weather->bank, arena, seed, 3u);
    if (!terrain_noise_bank_add(&weather->bank, "climate_weather/spell", &WEATHER_SPELL_SHAPE, &weather->spell) ||
        !terrain_noise_bank_add(&weather->bank, "climate_weather/rain_amount", &WEATHER_AMOUNT_SHAPE,
                                &weather->amount) ||
        !terrain_noise_bank_add(&weather->bank, "climate_weather/rain_hours", &WEATHER_HOURS_SHAPE, &weather->hours)) {
        terrain_noise_bank_deinit(&weather->bank);
        return false; /* it has said why */
    }
    climate_week_store_t *const store = &weather->store;
    const uint64_t cells = (uint64_t)CLIMATE_WEEK_TILES_MAX * CLIMATE_TILE_CELL_COUNT;
    store->borrowed_tiles = arena_alloc_array(arena, CLIMATE_WEEK_TILES_MAX, sizeof(climate_week_tile_t), 64u);
    store->borrowed_sides = arena_alloc_array(arena, CLIMATE_WEEK_TILES_MAX, sizeof(climate_week_side_t), 64u);
    store->borrowed_table = arena_alloc_array(arena, CLIMATE_WEEK_TABLE_SLOTS, sizeof(uint32_t), 64u);
    store->borrowed_flow = arena_alloc_array(arena, cells, sizeof(climate_flow_cell_t), 64u);
    store->borrowed_flow_index = arena_alloc_array(arena, 2u * cells, sizeof(uint32_t), 64u);
    store->borrowed_pending = arena_alloc_array(arena, 2u * (uint64_t)CLIMATE_WEEK_PENDING_MAX, sizeof(int32_t), 64u);
    climate_week_store_empty(store);
    return true;
}

void climate_weather_close(climate_weather_t *weather) {
    if (weather == nullptr || weather->spell == nullptr) {
        return;
    }
    terrain_noise_bank_deinit(&weather->bank);
    weather->spell = nullptr;
    weather->amount = nullptr;
    weather->hours = nullptr;
    climate_week_store_empty(&weather->store);
    arena_deinit(&weather->arena_week);
}

climate_week_store_t *climate_week_store(const climate_t *climate) {
    return &climate->borrowed_weather->store;
}

/* ---- The noises ---------------------------------------------------------------------------------------------------- */

float64_t climate_weather_time_of_clock(uint32_t day_zero, calendar_clock_t clock) {
    const uint64_t day = calendar_day_of(day_zero, clock);
    /* The share of the calendar day since 06:00: the clock's units past the day's start, which is at 2^30 each day. */
    const uint64_t units = (clock + CALENDAR_CLOCK_DAY - CALENDAR_CLOCK_DAY_START) % CALENDAR_CLOCK_DAY;
    return (float64_t)day + (float64_t)units / (float64_t)CALENDAR_CLOCK_DAY;
}

static float32_t weather_noise_one(const terrain_noise_field_t *field, float32_t raw_sd, float64_t x, float64_t y,
                                   float64_t z) {
    float32_t raw = 0.0f32;
    terrain_noise_sample_points(field, &x, &y, &z, 1u, &raw);
    return weather_scaled(raw, raw_sd);
}

float32_t climate_weather_spell(const climate_t *climate, float64_t x, float64_t y, int64_t day) {
    return weather_noise_one(climate->borrowed_weather->spell, CLIMATE_SPELL_RAW_SD, x, y, (float64_t)day);
}

float32_t climate_weather_amount(const climate_t *climate, float64_t x, float64_t y, int64_t day) {
    return weather_noise_one(climate->borrowed_weather->amount, CLIMATE_RAIN_AMOUNT_RAW_SD, x, y, (float64_t)day);
}

float32_t climate_weather_hours(const climate_t *climate, float64_t x, float64_t y, int64_t day, uint32_t hour) {
    const float64_t z = 24.0f64 * (float64_t)day + (float64_t)hour;
    return weather_noise_one(climate->borrowed_weather->hours, CLIMATE_RAIN_HOURS_RAW_SD, x, y, z);
}

/* ---- One place, one day -------------------------------------------------------------------------------------------- */

/* What a place is, whatever the day. */
typedef struct weather_place_t {
    float64_t x;            /* 0 */
    float64_t y;            /* 8 */
    float32_t sigma_t;      /* 16 */
    float32_t lat_deg;      /* 20 */
    float32_t sea_distance; /* 24 */
    float32_t valley_depth; /* 28 */
    uint8_t desert;         /* 32 */
    uint8_t cold_current;   /* 33 */
    uint8_t south;          /* 34 */
    uint8_t _pad0[5];       /* 35 */
} weather_place_t;
static_assert(sizeof(weather_place_t) == 40);

static bool weather_place_fill(const climate_t *climate, float64_t x, float64_t y, weather_place_t *out) {
    const int32_t bx = (int32_t)floor(x), by = (int32_t)floor(y);
    climate_cell_t cell;
    if (!climate_cell_get(climate, bx, by, &cell)) {
        return false;
    }
    float32_t around = 0.0f32;
    for (uint32_t k = 0u; k < CLIMATE_FLOW_NEIGHBOURS; k++) {
        climate_cell_t near;
        if (!climate_cell_get(climate, bx + CLIMATE_FLOW_DX[k] * CLIMATE_CELL_BLOCKS,
                              by + CLIMATE_FLOW_DY[k] * CLIMATE_CELL_BLOCKS, &near)) {
            return false;
        }
        around += near.z_ground;
    }
    memset(out, 0, sizeof(*out));
    out->x = x;
    out->y = y;
    out->sigma_t = CLIMATE_SIGMA_T_COAST + CLIMATE_SIGMA_T_INLAND_EXTRA * cell.kappa;
    out->lat_deg = (float32_t)climate_latitude_deg(y);
    out->sea_distance = cell.sea_distance;
    out->valley_depth = around / (float32_t)CLIMATE_FLOW_NEIGHBOURS - cell.z_ground;
    out->desert = cell.climate_class == CLIMATE_CLASS_DESERT ? 1u : 0u;
    out->cold_current = cell.current == CLIMATE_CURRENT_COLD ? 1u : 0u;
    out->south = out->lat_deg < 0.0f32 ? 1u : 0u;
    return true;
}

/* What a place's day is, before weather systems. */
typedef struct weather_day_in_t {
    climate_normals_at_t normals;         /* 0: at the ground */
    int64_t day;                          /* 64: D */
    float32_t spell;                      /* 72: N_1, spread 1 */
    float32_t amount;                     /* 76: N_2 */
    float32_t hours[CLIMATE_HOURS_A_DAY]; /* 80: N_3 for each hour from 06:00 */
    float32_t belt_noon[2];               /* 176: phase 04's wind at 12:00 */
    float32_t belt_morning[2];            /* 184: and at 06:00 */
} weather_day_in_t;
static_assert(sizeof(weather_day_in_t) == 192);

/* Everything but the noises, which the census samples in bulk. */
static bool weather_day_in_climate(const climate_t *climate, const weather_place_t *place, int64_t day,
                                   weather_day_in_t *out) {
    const float32_t d = weather_day_of_year(day);
    climate_air_t noon, morning;
    if (!climate_normals_at(climate, place->x, place->y, nanf(""), d, &out->normals) ||
        !climate_air_at(climate, place->x, place->y, d + 0.25f32, &noon) ||
        !climate_air_at(climate, place->x, place->y, d, &morning)) {
        return false;
    }
    out->day = day;
    out->belt_noon[0] = noon.wind[0];
    out->belt_noon[1] = noon.wind[1];
    out->belt_morning[0] = morning.wind[0];
    out->belt_morning[1] = morning.wind[1];
    return true;
}

static bool weather_day_in_fill(const climate_t *climate, const weather_place_t *place, int64_t day,
                                weather_day_in_t *out) {
    if (!weather_day_in_climate(climate, place, day, out)) {
        return false;
    }
    out->spell = climate_weather_spell(climate, place->x, place->y, day);
    out->amount = climate_weather_amount(climate, place->x, place->y, day);
    for (uint32_t h = 0u; h < CLIMATE_HOURS_A_DAY; h++) {
        out->hours[h] = climate_weather_hours(climate, place->x, place->y, day, h);
    }
    return true;
}

/* The instants of a day, as t: the 24 half-past hours from 06:30, then 12:00, then 06:00. */
static float64_t weather_instant(int64_t day, uint32_t instant) {
    if (instant == CLIMATE_WEEK_INSTANT_NOON) {
        return (float64_t)day + 0.25f64;
    }
    if (instant == CLIMATE_WEEK_INSTANT_MORNING) {
        return (float64_t)day;
    }
    return (float64_t)day + ((float64_t)instant + 0.5f64) * WEATHER_HOUR_DAYS;
}

/* The rectangle of points a set of views serves, both ends included. */
typedef struct weather_box_t {
    float64_t x_min; /* 0 */
    float64_t y_min; /* 8 */
    float64_t x_max; /* 16 */
    float64_t y_max; /* 24 */
} weather_box_t;
static_assert(sizeof(weather_box_t) == 32);

static float64_t weather_box_distance2(const weather_box_t *box, float64_t x, float64_t y) {
    const float64_t dx = x < box->x_min ? box->x_min - x : (x > box->x_max ? x - box->x_max : 0.0f64);
    const float64_t dy = y < box->y_min ? box->y_min - y : (y > box->y_max ? y - box->y_max : 0.0f64);
    return dx * dx + dy * dy;
}

/* How far a big system can touch anything: its pressure and wind to 3 R, a low's front to its length plus the showers. */
static float64_t weather_system_reach(const climate_system_t *system) {
    const float64_t bell = (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)system->radius;
    const float64_t front = (float64_t)system->front_length + WEATHER_FRONT_REACH;
    return (bell > front ? bell : front) + WEATHER_VIEW_MARGIN;
}

/* The systems of a day list alive at t and able to touch the box: the same sums at any point of the box as the whole list
 * gives, as a field skips a system past its reach before adding anything. Arrays from arena; false if it is full. */
static bool weather_view_build(const climate_systems_day_t *list, const weather_box_t *box, float64_t t, arena_t *arena,
                               climate_systems_day_t *out, uint32_t *overflowed) {
    *out = *list;
    uint32_t big = 0u, cells = 0u;
    uint8_t keep_big[CLIMATE_SYSTEMS_LIST_MAX];
    for (uint32_t i = 0u; i < list->system_count && i < CLIMATE_SYSTEMS_LIST_MAX; i++) {
        climate_system_state_t state;
        climate_system_state_at(&list->borrowed_systems[i], t, &state);
        const float64_t reach = weather_system_reach(&list->borrowed_systems[i]);
        keep_big[i] = state.alive != 0u && weather_box_distance2(box, state.x, state.y) <= reach * reach ? 1u : 0u;
        big += keep_big[i];
    }
    for (uint32_t i = 0u; i < list->thunder_count; i++) {
        float64_t centre[2], share = 0.0f64;
        const climate_thunder_t *const cell = &list->borrowed_thunder[i];
        if (climate_systems_thunder_at(cell, t, centre, &share)) {
            const float64_t reach =
                (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)cell->radius + WEATHER_VIEW_MARGIN;
            cells += weather_box_distance2(box, centre[0], centre[1]) <= reach * reach ? 1u : 0u;
        }
    }
    if (big > CLIMATE_WEEK_VIEW_SYSTEMS || cells > CLIMATE_WEEK_VIEW_THUNDER) {
        (*overflowed)++;
    }
    void *systems = nullptr, *thunder = nullptr;
    if (!arena_alloc_try(arena, (uint64_t)(big > 0u ? big : 1u) * sizeof(climate_system_t), alignof(climate_system_t),
                         &systems) ||
        !arena_alloc_try(arena, (uint64_t)(cells > 0u ? cells : 1u) * sizeof(climate_thunder_t),
                         alignof(climate_thunder_t), &thunder)) {
        return false;
    }
    climate_system_t *const system_out = systems;
    climate_thunder_t *const thunder_out = thunder;
    uint32_t b = 0u, c = 0u;
    for (uint32_t i = 0u; i < list->system_count && i < CLIMATE_SYSTEMS_LIST_MAX; i++) {
        if (keep_big[i] != 0u) {
            memcpy(&system_out[b++], &list->borrowed_systems[i], sizeof(climate_system_t));
        }
    }
    for (uint32_t i = 0u; i < list->thunder_count; i++) {
        float64_t centre[2], share = 0.0f64;
        const climate_thunder_t *const cell = &list->borrowed_thunder[i];
        if (climate_systems_thunder_at(cell, t, centre, &share)) {
            const float64_t reach =
                (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)cell->radius + WEATHER_VIEW_MARGIN;
            if (weather_box_distance2(box, centre[0], centre[1]) <= reach * reach) {
                thunder_out[c++] = *cell;
            }
        }
    }
    out->borrowed_systems = system_out;
    out->borrowed_thunder = thunder_out;
    out->system_count = b;
    out->system_capacity = b;
    out->thunder_count = c;
    out->thunder_capacity = c;
    return true;
}

static bool weather_views_build(const climate_systems_day_t *list, const weather_box_t *box, int64_t day,
                                arena_t *arena, climate_systems_day_t views[CLIMATE_WEEK_INSTANTS],
                                uint32_t *overflowed) {
    for (uint32_t k = 0u; k < CLIMATE_WEEK_INSTANTS; k++) {
        if (!weather_view_build(list, box, weather_instant(day, k), arena, &views[k], overflowed)) {
            return false;
        }
    }
    return true;
}

/* A big system's centre at t, if alive; the radius test reads it. */
static bool weather_within(const climate_system_t *system, float64_t t, float64_t x, float64_t y, float64_t radii,
                           climate_system_state_t *state, float64_t *out_r2) {
    climate_system_state_at(system, t, state);
    if (state->alive == 0u) {
        return false;
    }
    const float64_t dx = x - state->x, dy = y - state->y;
    const float64_t big = radii * (float64_t)system->radius;
    *out_r2 = dx * dx + dy * dy;
    return *out_r2 < big * big;
}

/* What a day's computation hands back besides the day itself. */
typedef struct weather_day_out_t {
    float64_t hour_mm[CLIMATE_HOURS_A_DAY]; /* 0: each hour's rain and snow, mm: the HUD's rate */
    const climate_system_t *hurricane;      /* 192: the first hurricane over the place, at the first hour it was */
    float64_t hurricane_t;                  /* 200 */
    float32_t range;                        /* 208: the daily range used */
    uint8_t _pad0[4];                       /* 212 */
} weather_day_out_t;
static_assert(sizeof(weather_day_out_t) == 216);

static float32_t weather_speed(const float32_t w[2]) {
    return sqrtf(w[0] * w[0] + w[1] * w[1]);
}

/* The phase 07 file's "Daily weather": one place, one day, from its normals, noises and the day's views. */
static void weather_day_compute(const weather_place_t *place, const weather_day_in_t *in,
                                const climate_systems_day_t views[CLIMATE_WEEK_INSTANTS],
                                const climate_week_t *last_week, climate_day_t *out, weather_day_out_t *extra) {
    const climate_normals_at_t *const n = &in->normals;
    const float64_t x = place->x, y = place->y;
    const int64_t day = in->day;
    const float64_t noon = weather_instant(day, CLIMATE_WEEK_INSTANT_NOON);
    const climate_systems_day_t *const at_noon = &views[CLIMATE_WEEK_INSTANT_NOON];
    memset(out, 0, sizeof(*out));
    memset(extra, 0, sizeof(*extra));

    /* Temperature: the spell, a high's push (warmer in local summer, colder in winter), and clouds under lows. */
    const bool summer = n->t_day_c > n->t_mean_c;
    float64_t push = 0.0f64;
    bool cloudy_noon = false, near_low = false;
    for (uint32_t i = 0u; i < at_noon->system_count; i++) {
        const climate_system_t *const system = &at_noon->borrowed_systems[i];
        climate_system_state_t state;
        float64_t r2 = 0.0f64;
        if (system->kind == CLIMATE_SYSTEM_HIGH) {
            if (weather_within(system, noon, x, y, (float64_t)CLIMATE_SYSTEM_REACH_RADII, &state, &r2)) {
                const float64_t big = (float64_t)system->radius * (float64_t)system->radius;
                push += (float64_t)(CLIMATE_HIGH_ANOMALY_SIGMAS * place->sigma_t) * exp(-r2 / big);
            }
            continue;
        }
        if (weather_within(system, noon, x, y, 1.0f64, &state, &r2)) {
            cloudy_noon = true;
        }
        if (system->kind == CLIMATE_SYSTEM_LOW &&
            weather_within(system, noon, x, y, (float64_t)CLIMATE_CLOUDY_LOW_RADII, &state, &r2)) {
            near_low = true;
        }
    }
    const float32_t anomaly = place->sigma_t * in->spell + (float32_t)(summer ? push : -push);
    const float32_t t_mean = n->t_day_c + anomaly;
    const float32_t range = n->t_range_c * (cloudy_noon ? 1.0f32 - CLIMATE_CLOUD_RANGE_CUT : 1.0f32);

    /* Rain: the background in the hours its own noise says, and each system's, all cut under a high. */
    const float32_t f_sys = climate_systems_rain_share_today(place->lat_deg, weather_day_of_year(day), n->t_day_c);
    const float32_t g = fmaxf(0.0f32, 1.0f32 + CLIMATE_RAIN_NOISE_GAIN * in->amount);
    const float64_t background = (float64_t)n->rain_mm_day * (float64_t)(1.0f32 - f_sys) * (float64_t)g;
    uint32_t wet_hours = 0u;
    for (uint32_t h = 0u; h < CLIMATE_HOURS_A_DAY; h++) {
        wet_hours += in->hours[h] > 0.0f32 ? 1u : 0u;
    }
    const float64_t per_wet_hour = wet_hours > 0u ? background / (float64_t)wet_hours : 0.0f64;
    const float64_t wet = (float64_t)climate_systems_wetness(n->rain_mm_day);
    float64_t rain = 0.0f64, snow = 0.0f64;
    float32_t wind_max = 0.0f32;
    bool in_hurricane = false, in_deep_low = false, under_thunder = false;
    for (uint32_t h = 0u; h < CLIMATE_HOURS_A_DAY; h++) {
        const climate_systems_day_t *const view = &views[h];
        const float64_t t = weather_instant(day, h);
        const float64_t systems = (float64_t)climate_systems_rain_at(view, x, y, t);
        const float64_t cut = (float64_t)climate_systems_rain_factor(view, x, y, t);
        const float64_t own = in->hours[h] > 0.0f32 ? per_wet_hour : 0.0f64;
        const float64_t hour = (own + systems * wet) * cut;
        extra->hour_mm[h] = hour;
        rain += hour;
        const float64_t clock = 6.0f64 + (float64_t)h + 0.5f64;
        const float32_t t_hour =
            t_mean + 0.5f32 * range * (float32_t)cos(WEATHER_TWO_PI * (clock - WEATHER_WARMEST_HOUR) / 24.0f64);
        snow += hour * (float64_t)climate_heat_snow_share(t_hour);
        float32_t wind[2];
        climate_systems_wind_at(view, x, y, t, wind);
        wind[0] += in->belt_noon[0];
        wind[1] += in->belt_noon[1];
        const float32_t speed = weather_speed(wind);
        wind_max = speed > wind_max ? speed : wind_max;
        for (uint32_t i = 0u; i < view->system_count; i++) {
            const climate_system_t *const system = &view->borrowed_systems[i];
            climate_system_state_t state;
            float64_t r2 = 0.0f64;
            if (system->kind == CLIMATE_SYSTEM_HIGH || !weather_within(system, t, x, y, 1.0f64, &state, &r2)) {
                continue;
            }
            if (system->kind == CLIMATE_SYSTEM_HURRICANE) {
                if (!in_hurricane) {
                    extra->hurricane = system;
                    extra->hurricane_t = t;
                }
                in_hurricane = true;
            } else if (state.pressure_hpa <= -CLIMATE_STORM_DEPTH_HPA) {
                in_deep_low = true;
            }
        }
        for (uint32_t i = 0u; i < view->thunder_count && !under_thunder; i++) {
            float64_t centre[2], strength = 0.0f64;
            const climate_thunder_t *const cell = &view->borrowed_thunder[i];
            if (climate_systems_thunder_at(cell, t, centre, &strength)) {
                const float64_t dx = x - centre[0], dy = y - centre[1];
                under_thunder = dx * dx + dy * dy < (float64_t)cell->radius * (float64_t)cell->radius;
            }
        }
    }
    float32_t wind_noon[2], wind_morning[2];
    climate_systems_wind_at(at_noon, x, y, noon, wind_noon);
    wind_noon[0] += in->belt_noon[0];
    wind_noon[1] += in->belt_noon[1];
    climate_systems_wind_at(&views[CLIMATE_WEEK_INSTANT_MORNING], x, y, (float64_t)day, wind_morning);
    wind_morning[0] += in->belt_morning[0];
    wind_morning[1] += in->belt_morning[1];

    out->t_mean = t_mean;
    out->t_min = t_mean - 0.5f32 * range;
    out->t_max = t_mean + 0.5f32 * range;
    out->anomaly = anomaly;
    out->sigma_t = place->sigma_t;
    out->rain_mm = (float32_t)rain;
    out->snow_mm = (float32_t)snow;
    out->wind[0] = wind_noon[0];
    out->wind[1] = wind_noon[1];
    out->wind_max = wind_max;
    extra->range = range;

    /* The 7 days ending today: six of last week's, as a share of its total, and today. With no state, the normal. */
    const float32_t before = last_week != nullptr ? last_week->rain_mm : 7.0f32 * n->rain_mm_day;
    const climate_day_facts_t facts = {
        .t_mean = t_mean,
        .anomaly = anomaly,
        .sigma_t = place->sigma_t,
        .rain_mm = out->rain_mm,
        .wind_max = wind_max,
        .wind_morning = weather_speed(wind_morning),
        .week_rain_mm = before * (6.0f32 / 7.0f32) + out->rain_mm,
        .normal_week_mm = 7.0f32 * n->rain_mm_day,
        .sea_distance = place->sea_distance,
        .valley_depth = place->valley_depth,
        .soil_mm = last_week != nullptr ? last_week->soil_mm : 0.0f32,
        .in_hurricane = in_hurricane ? 1u : 0u,
        .in_deep_low = in_deep_low ? 1u : 0u,
        .under_thunder = under_thunder ? 1u : 0u,
        .near_low = near_low ? 1u : 0u,
        .desert = place->desert,
        .cold_current = place->cold_current,
        ._pad0 = {0},
    };
    out->kind = climate_week_day_kind(&facts);
}

/* The day list round a place, cells included: radius reaches every point of the box from its middle. */
static bool weather_list_build(const climate_t *climate, arena_t *arena, const weather_box_t *box, int64_t day,
                               climate_systems_day_t *out) {
    const float64_t cx = 0.5f64 * (box->x_min + box->x_max), cy = 0.5f64 * (box->y_min + box->y_max);
    const float64_t hx = 0.5f64 * (box->x_max - box->x_min), hy = 0.5f64 * (box->y_max - box->y_min);
    const float32_t radius = (float32_t)(sqrt(hx * hx + hy * hy) + 1.0f64);
    return climate_systems_day_build(climate, arena, cx, cy, radius, radius, day, CLIMATE_SYSTEMS_ALL, out);
}

/* One place's whole day: its list, views, inputs and the day. False if a tile is not built or the arena is too small. */
static bool weather_place_day(const climate_t *climate, const weather_place_t *place, int64_t day,
                              const climate_week_t *last_week, arena_t *scratch, climate_day_t *out,
                              weather_day_out_t *extra, weather_day_in_t *in,
                              climate_systems_day_t views[CLIMATE_WEEK_INSTANTS], climate_systems_day_t *list) {
    const weather_box_t box = {place->x, place->y, place->x, place->y};
    uint32_t overflowed = 0u;
    if (!weather_day_in_fill(climate, place, day, in) || !weather_list_build(climate, scratch, &box, day, list) ||
        list->thunder_complete == 0u || !weather_views_build(list, &box, day, scratch, views, &overflowed)) {
        return false;
    }
    weather_day_compute(place, in, views, last_week, out, extra);
    return true;
}

bool climate_weather_day(const climate_t *climate, int32_t x, int32_t y, int64_t day, const climate_week_t *last_week,
                         arena_t *scratch, climate_day_t *out) {
    weather_place_t place;
    if (!weather_place_fill(climate, (float64_t)x, (float64_t)y, &place)) {
        return false;
    }
    weather_day_in_t in;
    weather_day_out_t extra;
    climate_systems_day_t views[CLIMATE_WEEK_INSTANTS], list;
    return weather_place_day(climate, &place, day, last_week, scratch, out, &extra, &in, views, &list);
}

bool climate_weather_at(const climate_t *climate, float64_t x, float64_t y, float64_t z, float64_t t,
                        const climate_week_t *last_week, arena_t *scratch, climate_now_t *out) {
    weather_place_t place;
    if (!weather_place_fill(climate, x, y, &place)) {
        return false;
    }
    const int64_t day = (int64_t)floor(t);
    weather_day_in_t in;
    weather_day_out_t extra;
    climate_day_t today;
    climate_systems_day_t views[CLIMATE_WEEK_INSTANTS], list, now;
    climate_normals_at_t high;
    climate_air_t air;
    const float32_t d = weather_day_of_year(day);
    const float32_t part = (float32_t)(t - (float64_t)day);
    uint32_t overflowed = 0u;
    const weather_box_t box = {x, y, x, y};
    if (!weather_place_day(climate, &place, day, last_week, scratch, &today, &extra, &in, views, &list) ||
        !weather_view_build(&list, &box, t, scratch, &now, &overflowed) ||
        !climate_normals_at(climate, x, y, (float32_t)z, d, &high) || !climate_air_at(climate, x, y, d + part, &air)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const float64_t hours = (t - (float64_t)day) * 24.0f64;
    uint32_t hour = (uint32_t)hours;
    hour = hour >= CLIMATE_HOURS_A_DAY ? CLIMATE_HOURS_A_DAY - 1u : hour;
    const float64_t clock = 6.0f64 + hours;
    const float32_t t_mean_here = high.t_day_c + today.anomaly;
    out->temperature_c =
        t_mean_here + 0.5f32 * extra.range * (float32_t)cos(WEATHER_TWO_PI * (clock - WEATHER_WARMEST_HOUR) / 24.0f64);
    out->rain_mm_h = (float32_t)extra.hour_mm[hour];
    out->snow_share = climate_heat_snow_share(out->temperature_c);
    float32_t wind[2];
    climate_systems_wind_at(&now, x, y, t, wind);
    out->wind[0] = air.wind[0] + wind[0];
    out->wind[1] = air.wind[1] + wind[1];
    out->pressure_hpa = air.pressure_hpa + climate_systems_pressure_at(&now, x, y, t);
    bool under_storm = false, under_high = false;
    for (uint32_t i = 0u; i < now.system_count; i++) {
        climate_system_state_t state;
        float64_t r2 = 0.0f64;
        const climate_system_t *const system = &now.borrowed_systems[i];
        if (!weather_within(system, t, x, y, 1.0f64, &state, &r2)) {
            continue;
        }
        under_high = under_high || system->kind == CLIMATE_SYSTEM_HIGH;
        under_storm = under_storm || system->kind != CLIMATE_SYSTEM_HIGH;
        out->in_hurricane = system->kind == CLIMATE_SYSTEM_HURRICANE ? 1u : out->in_hurricane;
    }
    /* Cloud by what falls now, then by what is overhead: phase 08 draws it, phase 07 only says how much. */
    float32_t cloud = under_high ? 0.05f32 : 0.2f32;
    cloud = today.kind == CLIMATE_DAY_CLOUDY ? 0.6f32 : cloud;
    cloud = under_storm ? 0.75f32 : cloud;
    if (out->rain_mm_h > 0.0f32) {
        const float32_t raining = 0.5f32 + 0.25f32 * out->rain_mm_h;
        cloud = raining > 1.0f32 ? 1.0f32 : (raining > cloud ? raining : cloud);
    }
    out->cloud = cloud;
    float64_t nearest = -1.0f64;
    for (uint32_t i = 0u; i < now.thunder_count; i++) {
        float64_t centre[2], strength = 0.0f64;
        const climate_thunder_t *const cell = &now.borrowed_thunder[i];
        if (!climate_systems_thunder_at(cell, t, centre, &strength)) {
            continue;
        }
        const float64_t dx = x - centre[0], dy = y - centre[1];
        const float64_t r = sqrt(dx * dx + dy * dy);
        const float64_t reach = (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)cell->radius;
        if (r < reach && (nearest < 0.0f64 || r < nearest)) {
            nearest = r;
            out->thunder = (float32_t)(strength * (1.0f64 - r / reach));
            out->thunder_distance = r < 1.0f64 ? 1.0f32 : (float32_t)r;
            out->thunder_cell = (uint32_t)(cell->id ^ (cell->id >> 32)) | 1u;
        }
    }
    const float32_t morning = (float32_t)(hours < 4.0f64 ? 1.0f64 - hours / 4.0f64 : 0.0f64);
    out->fog = today.kind == CLIMATE_DAY_FOG ? morning : 0.0f32;
    const float32_t gust = weather_speed(out->wind) / 20.0f32;
    out->dust = today.kind == CLIMATE_DAY_DUST_STORM ? (gust > 1.0f32 ? 1.0f32 : gust) : 0.0f32;
    out->day_kind = today.kind;
    return true;
}

/* ---- Spin-up and fast weeks ---------------------------------------------------------------------------------------- */

/* One week from normals at its middle day: snow, soil and runoff. sigma_t 0 leaves the noises out. */
static bool weather_week_normal(const climate_t *climate, float64_t x, float64_t y, int64_t week, float32_t sigma_t,
                                bool noise, climate_week_t *state) {
    const int64_t middle = 7 * week - 4; /* days 7 (week - 1) to 7 week - 1: the fourth */
    climate_normals_at_t n;
    if (!climate_normals_at(climate, x, y, nanf(""), weather_day_of_year(middle), &n)) {
        return false;
    }
    float32_t t_w = n.t_day_c, p_w = (float32_t)CLIMATE_WEEK_DAYS * n.rain_mm_day;
    if (noise) {
        t_w += sigma_t * climate_weather_spell(climate, x, y, middle);
        p_w *= fmaxf(0.0f32, 1.0f32 + CLIMATE_RAIN_NOISE_GAIN * climate_weather_amount(climate, x, y, middle));
    }
    climate_week_update_water(state, t_w, p_w, climate_heat_snow_share(t_w), state);
    return true;
}

bool climate_week_spinup(const climate_t *climate, int32_t cell_x, int32_t cell_y, uint64_t week,
                         climate_week_t *out_state, float32_t *out_mean_runoff) {
    const float64_t x = (float64_t)cell_x * (float64_t)CLIMATE_CELL_BLOCKS + 32.0f64;
    const float64_t y = (float64_t)cell_y * (float64_t)CLIMATE_CELL_BLOCKS + 32.0f64;
    const int64_t weeks = (int64_t)CLIMATE_WEEK_SPINUP_WEEKS;
    if (out_state != nullptr) {
        climate_week_t state;
        memset(&state, 0, sizeof(state));
        for (int64_t w = (int64_t)week - weeks + 1; w <= (int64_t)week; w++) {
            if (!weather_week_normal(climate, x, y, w, 0.0f32, false, &state)) {
                return false;
            }
        }
        state.runoff_mm = 0.0f32; /* a starting state: nothing has run off yet */
        *out_state = state;
    }
    if (out_mean_runoff != nullptr) {
        climate_week_t state;
        memset(&state, 0, sizeof(state));
        float32_t sum = 0.0f32;
        for (int64_t w = 1; w <= weeks; w++) {
            if (!weather_week_normal(climate, x, y, w, 0.0f32, false, &state)) {
                return false;
            }
            sum += w > weeks / 2 ? state.runoff_mm : 0.0f32;
        }
        *out_mean_runoff = sum / (float32_t)(weeks / 2);
    }
    return true;
}

bool climate_week_fast(const climate_t *climate, int32_t cell_x, int32_t cell_y, uint64_t week, climate_week_t *state) {
    const float64_t x = (float64_t)cell_x * (float64_t)CLIMATE_CELL_BLOCKS + 32.0f64;
    const float64_t y = (float64_t)cell_y * (float64_t)CLIMATE_CELL_BLOCKS + 32.0f64;
    climate_cell_t cell;
    if (!climate_cell_get(climate, (int32_t)x, (int32_t)y, &cell)) {
        return false;
    }
    const float32_t sigma_t = CLIMATE_SIGMA_T_COAST + CLIMATE_SIGMA_T_INLAND_EXTRA * cell.kappa;
    return weather_week_normal(climate, x, y, (int64_t)week, sigma_t, true, state);
}

/* ---- The store ----------------------------------------------------------------------------------------------------- */

static uint32_t weather_tile_hash(int32_t tile_x, int32_t tile_y) {
    uint32_t h = (uint32_t)tile_x * 0x9E3779B1u ^ (uint32_t)tile_y * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h & (CLIMATE_WEEK_TABLE_SLOTS - 1u);
}

void climate_week_store_empty(climate_week_store_t *store) {
    memset(store->borrowed_table, 0, (uint64_t)CLIMATE_WEEK_TABLE_SLOTS * sizeof(uint32_t));
    store->week = 0u;
    store->tile_count = 0u;
    store->pending_count = 0u;
    store->busy = 0u;
    store->full = 0u;
    store->pending_lost = 0u;
}

climate_week_tile_t *climate_week_store_find(const climate_week_store_t *store, int32_t tile_x, int32_t tile_y,
                                             uint32_t *out_index) {
    for (uint32_t probe = weather_tile_hash(tile_x, tile_y);; probe = (probe + 1u) & (CLIMATE_WEEK_TABLE_SLOTS - 1u)) {
        const uint32_t entry = store->borrowed_table[probe];
        if (entry == 0u) {
            return nullptr;
        }
        climate_week_tile_t *const tile = &store->borrowed_tiles[entry - 1u];
        if (tile->tile_x == tile_x && tile->tile_y == tile_y) {
            if (out_index != nullptr) {
                *out_index = entry - 1u;
            }
            return tile;
        }
    }
}

climate_week_tile_t *climate_week_store_add(climate_week_store_t *store, int32_t tile_x, int32_t tile_y, uint32_t flags,
                                            uint32_t *out_index) {
    climate_week_tile_t *tile = climate_week_store_find(store, tile_x, tile_y, out_index);
    if (tile != nullptr) {
        tile->flags |= flags;
        return tile;
    }
    if (store->tile_count >= CLIMATE_WEEK_TILES_MAX) {
        if (store->full == 0u) {
            LOG_WARN("climate: the weekly state holds its most tiles, %u: tile %d, %d is not kept",
                     (uint32_t)CLIMATE_WEEK_TILES_MAX, tile_x, tile_y);
        }
        store->full = 1u;
        return nullptr;
    }
    const uint32_t index = store->tile_count++;
    tile = &store->borrowed_tiles[index];
    memset(tile, 0, sizeof(*tile));
    tile->tile_x = tile_x;
    tile->tile_y = tile_y;
    tile->week = (uint32_t)store->week;
    tile->flags = flags;
    climate_week_side_t *const side = &store->borrowed_sides[index];
    memset(side, 0, sizeof(*side));
    side->fresh = 1u;
    uint32_t probe = weather_tile_hash(tile_x, tile_y);
    while (store->borrowed_table[probe] != 0u) {
        probe = (probe + 1u) & (CLIMATE_WEEK_TABLE_SLOTS - 1u);
    }
    store->borrowed_table[probe] = index + 1u;
    if (out_index != nullptr) {
        *out_index = index;
    }
    return tile;
}

bool climate_week_store_adopt(climate_week_store_t *store) {
    if (store->tile_count >= CLIMATE_WEEK_TILES_MAX) {
        return false;
    }
    const uint32_t index = store->tile_count;
    const climate_week_tile_t *const tile = &store->borrowed_tiles[index];
    if (climate_week_store_find(store, tile->tile_x, tile->tile_y, nullptr) != nullptr) {
        return false;
    }
    store->tile_count = index + 1u;
    memset(&store->borrowed_sides[index], 0, sizeof(climate_week_side_t));
    uint32_t probe = weather_tile_hash(tile->tile_x, tile->tile_y);
    while (store->borrowed_table[probe] != 0u) {
        probe = (probe + 1u) & (CLIMATE_WEEK_TABLE_SLOTS - 1u);
    }
    store->borrowed_table[probe] = index + 1u;
    return true;
}

static void weather_mark_now(climate_week_store_t *store, int32_t cell_x, int32_t cell_y) {
    const int32_t tile_x = weather_floor_div(cell_x, CLIMATE_TILE_CELLS);
    const int32_t tile_y = weather_floor_div(cell_y, CLIMATE_TILE_CELLS);
    climate_week_tile_t *const found = climate_week_store_find(store, tile_x, tile_y, nullptr);
    const bool was_generated = found != nullptr && (found->flags & CLIMATE_WEEK_TILE_GENERATED) != 0u;
    climate_week_tile_t *const tile =
        climate_week_store_add(store, tile_x, tile_y, CLIMATE_WEEK_TILE_GENERATED, nullptr);
    if (tile == nullptr) {
        return;
    }
    const uint32_t local = (uint32_t)((cell_y - tile_y * CLIMATE_TILE_CELLS) * CLIMATE_TILE_CELLS +
                                      (cell_x - tile_x * CLIMATE_TILE_CELLS));
    tile->generated[local / 64u] |= 1ull << (local % 64u);
    if (was_generated) {
        return; /* its margin is there already */
    }
    const int32_t margin = (int32_t)CLIMATE_FLOOD_MARGIN_TILES;
    for (int32_t dy = -margin; dy <= margin; dy++) {
        for (int32_t dx = -margin; dx <= margin; dx++) {
            if (dx != 0 || dy != 0) {
                (void)climate_week_store_add(store, tile_x + dx, tile_y + dy, CLIMATE_WEEK_TILE_MARGIN, nullptr);
            }
        }
    }
}

void climate_week_store_mark(climate_week_store_t *store, int32_t x, int32_t y) {
    const int32_t cell_x = weather_floor_div(x, CLIMATE_CELL_BLOCKS);
    const int32_t cell_y = weather_floor_div(y, CLIMATE_CELL_BLOCKS);
    if (store->busy == 0u) {
        weather_mark_now(store, cell_x, cell_y);
        return;
    }
    const uint32_t n = store->pending_count;
    if (n > 0u && store->borrowed_pending[2u * (n - 1u)] == cell_x &&
        store->borrowed_pending[2u * (n - 1u) + 1u] == cell_y) {
        return;
    }
    if (n >= CLIMATE_WEEK_PENDING_MAX) {
        store->pending_lost = 1u;
        return;
    }
    store->borrowed_pending[2u * n] = cell_x;
    store->borrowed_pending[2u * n + 1u] = cell_y;
    store->pending_count = n + 1u;
}

void climate_week_store_pending_apply(climate_week_store_t *store) {
    ASSERT_ALWAYS(store->busy == 0u);
    for (uint32_t i = 0u; i < store->pending_count; i++) {
        weather_mark_now(store, store->borrowed_pending[2u * i], store->borrowed_pending[2u * i + 1u]);
    }
    store->pending_count = 0u;
}

static int32_t weather_tile_compare(const climate_week_tile_t *a, const climate_week_tile_t *b) {
    if (a->tile_y != b->tile_y) {
        return a->tile_y < b->tile_y ? -1 : 1;
    }
    return a->tile_x < b->tile_x ? -1 : (a->tile_x > b->tile_x ? 1 : 0);
}

void climate_week_store_sorted(const climate_week_store_t *store, uint32_t *out, uint32_t *scratch) {
    const uint32_t count = store->tile_count;
    for (uint32_t i = 0u; i < count; i++) {
        out[i] = i;
    }
    uint32_t *from = out, *to = scratch;
    for (uint32_t width = 1u; width < count; width *= 2u) {
        for (uint32_t left = 0u; left < count; left += 2u * width) {
            const uint32_t middle = left + width < count ? left + width : count;
            const uint32_t right = left + 2u * width < count ? left + 2u * width : count;
            uint32_t a = left, b = middle, o = left;
            while (a < middle && b < right) {
                to[o++] = weather_tile_compare(&store->borrowed_tiles[from[b]], &store->borrowed_tiles[from[a]]) < 0
                              ? from[b++]
                              : from[a++];
            }
            while (a < middle) {
                to[o++] = from[a++];
            }
            while (b < right) {
                to[o++] = from[b++];
            }
        }
        uint32_t *const swap = from;
        from = to;
        to = swap;
    }
    if (from != out) {
        memcpy(out, from, (uint64_t)count * sizeof(uint32_t));
    }
}

const climate_week_t *climate_week_store_cell(const climate_week_store_t *store, int32_t cell_x, int32_t cell_y) {
    const int32_t tile_x = weather_floor_div(cell_x, CLIMATE_TILE_CELLS);
    const int32_t tile_y = weather_floor_div(cell_y, CLIMATE_TILE_CELLS);
    const climate_week_tile_t *const tile = climate_week_store_find(store, tile_x, tile_y, nullptr);
    if (tile == nullptr) {
        return nullptr;
    }
    return &tile->cells[(cell_y - tile_y * CLIMATE_TILE_CELLS) * CLIMATE_TILE_CELLS +
                        (cell_x - tile_x * CLIMATE_TILE_CELLS)];
}

bool climate_week_first_snow(float32_t snow_before, float32_t snow_after, uint16_t day_of_year, bool south) {
    if (!(snow_before == 0.0f32 && snow_after > 0.0f32)) {
        return false;
    }
    const calendar_season_label_t label = calendar_season_temperate(day_of_year, south);
    return label.season == CALENDAR_SEASON_AUTUMN ||
           (label.season == CALENDAR_SEASON_WINTER && label.part == CALENDAR_PART_EARLY);
}

/* ---- A census week for a block of tiles ---------------------------------------------------------------------------- */

static int32_t weather_cell_x(const climate_week_tile_t *tile, uint32_t c) {
    return tile->tile_x * CLIMATE_TILE_CELLS + (int32_t)(c % (uint32_t)CLIMATE_TILE_CELLS);
}

static int32_t weather_cell_y(const climate_week_tile_t *tile, uint32_t c) {
    return tile->tile_y * CLIMATE_TILE_CELLS + (int32_t)(c / (uint32_t)CLIMATE_TILE_CELLS);
}

static float64_t weather_centre(int32_t cell) {
    return (float64_t)cell * (float64_t)CLIMATE_CELL_BLOCKS + 32.0f64;
}

/* A tile's side data where missing, and its cells at spin-up if fresh. False if a climate tile is not built. */
static bool weather_tile_ready(const climate_t *climate, climate_week_tile_t *tile, climate_week_side_t *side,
                               uint64_t week) {
    for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
        const int32_t cx = weather_cell_x(tile, c), cy = weather_cell_y(tile, c);
        if (side->ground_ready == 0u) {
            climate_cell_t cell;
            if (!climate_cell_get(climate, cx * CLIMATE_CELL_BLOCKS + 32, cy * CLIMATE_CELL_BLOCKS + 32, &cell)) {
                return false;
            }
            side->z_ground[c] = cell.z_ground;
            side->sea[c] = (cell.flags & CLIMATE_CELL_SEA) != 0u ? 1u : 0u;
        }
        if (!climate_week_spinup(climate, cx, cy, week - 1u, side->fresh != 0u ? &tile->cells[c] : nullptr,
                                 side->mean_ready == 0u ? &side->mean_runoff[c] : nullptr)) {
            return false;
        }
    }
    side->ground_ready = 1u;
    side->mean_ready = 1u;
    side->fresh = 0u;
    return true;
}

static void weather_hurricane_note(climate_week_side_t *side, const climate_system_t *hurricane, float64_t t) {
    for (uint32_t i = 0u; i < side->hurricane_count; i++) {
        if (side->hurricane_ids[i] == hurricane->id) {
            return;
        }
    }
    if (side->hurricane_count < CLIMATE_WEEK_HURRICANES) {
        side->hurricane_ids[side->hurricane_count] = hurricane->id;
        climate_system_title(hurricane, t, side->hurricanes[side->hurricane_count]);
        side->hurricane_count++;
    }
}

static uint8_t weather_events(const climate_week_t *before, const climate_week_t *after, uint16_t day_of_year,
                              bool south) {
    uint8_t events = 0u;
    events |= after->t_min < 0.0f32 ? CLIMATE_WEEK_EVENT_FROST : 0u;
    for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
        const uint8_t kind = after->day_kind[i];
        events |= kind == CLIMATE_DAY_HEAT_WAVE ? CLIMATE_WEEK_EVENT_HEAT_WAVE : 0u;
        events |= kind == CLIMATE_DAY_THUNDERSTORM ? CLIMATE_WEEK_EVENT_THUNDERSTORM : 0u;
        events |= kind == CLIMATE_DAY_STORM ? CLIMATE_WEEK_EVENT_STORM : 0u;
        events |= kind == CLIMATE_DAY_HURRICANE ? CLIMATE_WEEK_EVENT_HURRICANE : 0u;
    }
    events |= climate_week_first_snow(before->snow_mm, after->snow_mm, day_of_year, south)
                  ? CLIMATE_WEEK_EVENT_FIRST_SNOW
                  : 0u;
    events |= before->ice_cm < CLIMATE_ICE_EVENT_CM && after->ice_cm >= CLIMATE_ICE_EVENT_CM
                  ? CLIMATE_WEEK_EVENT_LAKE_ICE
                  : 0u;
    return events;
}

void climate_week_block_run(const climate_t *climate, climate_week_store_t *store, uint64_t week, bool fast,
                            bool record_events, arena_t *arena, climate_week_block_t *block) {
    ASSERT_ALWAYS(week >= 1u && block->count >= 1u && block->count <= CLIMATE_WEEK_BLOCK_TILES);
    const int64_t started = time_now_ns();
    climate_week_tile_t *tiles[CLIMATE_WEEK_BLOCK_TILES];
    climate_week_side_t *sides[CLIMATE_WEEK_BLOCK_TILES];
    weather_box_t box = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (uint32_t k = 0u; k < block->count; k++) {
        tiles[k] = &store->borrowed_tiles[block->slots[k]];
        sides[k] = &store->borrowed_sides[block->slots[k]];
        const float64_t x0 = weather_centre(tiles[k]->tile_x * CLIMATE_TILE_CELLS);
        const float64_t y0 = weather_centre(tiles[k]->tile_y * CLIMATE_TILE_CELLS);
        const float64_t span = (float64_t)((CLIMATE_TILE_CELLS - 1) * CLIMATE_CELL_BLOCKS);
        box.x_min = fmin(box.x_min, x0);
        box.y_min = fmin(box.y_min, y0);
        box.x_max = fmax(box.x_max, x0 + span);
        box.y_max = fmax(box.y_max, y0 + span);
        if (!weather_tile_ready(climate, tiles[k], sides[k], week)) {
            block->failed++;
        }
        if (record_events) {
            memset(sides[k]->events, 0, sizeof(sides[k]->events));
            sides[k]->hurricane_count = 0u;
        }
    }
    if (fast) {
        for (uint32_t k = 0u; k < block->count; k++) {
            for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
                if (!climate_week_fast(climate, weather_cell_x(tiles[k], c), weather_cell_y(tiles[k], c), week,
                                       &tiles[k]->cells[c])) {
                    block->failed++;
                }
            }
            tiles[k]->week = (uint32_t)week;
        }
        block->ns_cells += (uint64_t)(time_now_ns() - started);
        return;
    }

    const uint32_t cells = block->count * CLIMATE_TILE_CELL_COUNT;
    weather_place_t *const places = arena_alloc_array(arena, cells, sizeof(weather_place_t), alignof(weather_place_t));
    climate_day_t *const days =
        arena_alloc_array(arena, (uint64_t)cells * CLIMATE_WEEK_DAYS, sizeof(climate_day_t), alignof(climate_day_t));
    float64_t *const xs = arena_alloc_array(arena, (uint64_t)cells * CLIMATE_HOURS_A_DAY, sizeof(float64_t), 8u);
    float64_t *const ys = arena_alloc_array(arena, (uint64_t)cells * CLIMATE_HOURS_A_DAY, sizeof(float64_t), 8u);
    float64_t *const zs = arena_alloc_array(arena, (uint64_t)cells * CLIMATE_HOURS_A_DAY, sizeof(float64_t), 8u);
    float32_t *const raw = arena_alloc_array(arena, (uint64_t)cells * CLIMATE_HOURS_A_DAY, sizeof(float32_t), 4u);
    float32_t *const spell = arena_alloc_array(arena, cells, sizeof(float32_t), 4u);
    float32_t *const amount = arena_alloc_array(arena, cells, sizeof(float32_t), 4u);
    arena_t *const day_arena = arena_alloc(arena, sizeof(arena_t), alignof(arena_t));
    arena_init(day_arena, "climate_week_day", arena_alloc(arena, WEATHER_DAY_ARENA_BYTES, 64u),
               WEATHER_DAY_ARENA_BYTES);
    bool *const placed = arena_alloc_zeroed(arena, cells, 1u);
    for (uint32_t k = 0u; k < block->count; k++) {
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            const uint32_t i = k * CLIMATE_TILE_CELL_COUNT + c;
            placed[i] = weather_place_fill(climate, weather_centre(weather_cell_x(tiles[k], c)),
                                           weather_centre(weather_cell_y(tiles[k], c)), &places[i]);
            block->failed += placed[i] ? 0u : 1u;
        }
    }
    const climate_weather_t *const weather = climate->borrowed_weather;
    for (uint32_t i_day = 0u; i_day < CLIMATE_WEEK_DAYS; i_day++) {
        const int64_t day = 7 * ((int64_t)week - 1) + (int64_t)i_day;
        arena_reset(day_arena);
        const int64_t list_started = time_now_ns();
        climate_systems_day_t list, views[CLIMATE_WEEK_INSTANTS];
        if (!weather_list_build(climate, day_arena, &box, day, &list) ||
            !weather_views_build(&list, &box, day, day_arena, views, &block->overflowed)) {
            block->failed++;
            continue;
        }
        block->incomplete += list.thunder_complete == 0u ? 1u : 0u;
        block->overflowed += list.overflowed;
        block->ns_lists += (uint64_t)(time_now_ns() - list_started);
        /* The noises in bulk: the same bits as one at a time. */
        for (uint32_t i = 0u; i < cells; i++) {
            xs[i] = places[i].x;
            ys[i] = places[i].y;
            zs[i] = (float64_t)day;
        }
        terrain_noise_sample_points(weather->spell, xs, ys, zs, cells, spell);
        terrain_noise_sample_points(weather->amount, xs, ys, zs, cells, amount);
        for (uint32_t i = 0u; i < cells; i++) {
            for (uint32_t h = 0u; h < CLIMATE_HOURS_A_DAY; h++) {
                xs[i * CLIMATE_HOURS_A_DAY + h] = places[i].x;
                ys[i * CLIMATE_HOURS_A_DAY + h] = places[i].y;
                zs[i * CLIMATE_HOURS_A_DAY + h] = 24.0f64 * (float64_t)day + (float64_t)h;
            }
        }
        terrain_noise_sample_points(weather->hours, xs, ys, zs, (uint64_t)cells * CLIMATE_HOURS_A_DAY, raw);
        for (uint32_t k = 0u; k < block->count; k++) {
            for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
                const uint32_t i = k * CLIMATE_TILE_CELL_COUNT + c;
                climate_day_t *const out = &days[(uint64_t)i * CLIMATE_WEEK_DAYS + i_day];
                weather_day_in_t in;
                if (!placed[i] || !weather_day_in_climate(climate, &places[i], day, &in)) {
                    memset(out, 0, sizeof(*out));
                    block->failed++;
                    continue;
                }
                in.spell = weather_scaled(spell[i], CLIMATE_SPELL_RAW_SD);
                in.amount = weather_scaled(amount[i], CLIMATE_RAIN_AMOUNT_RAW_SD);
                for (uint32_t h = 0u; h < CLIMATE_HOURS_A_DAY; h++) {
                    in.hours[h] = weather_scaled(raw[i * CLIMATE_HOURS_A_DAY + h], CLIMATE_RAIN_HOURS_RAW_SD);
                }
                weather_day_out_t extra;
                weather_day_compute(&places[i], &in, views, &tiles[k]->cells[c], out, &extra);
                if (record_events && extra.hurricane != nullptr) {
                    weather_hurricane_note(sides[k], extra.hurricane, extra.hurricane_t);
                }
            }
        }
    }
    const uint16_t census_day = (uint16_t)weather_day_of_year(7 * (int64_t)week);
    for (uint32_t k = 0u; k < block->count; k++) {
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            const uint32_t i = k * CLIMATE_TILE_CELL_COUNT + c;
            const climate_week_t before = tiles[k]->cells[c];
            climate_week_update(&before, &days[(uint64_t)i * CLIMATE_WEEK_DAYS], &tiles[k]->cells[c]);
            if (record_events) {
                sides[k]->events[c] = weather_events(&before, &tiles[k]->cells[c], census_day, places[i].south != 0u);
            }
        }
        tiles[k]->week = (uint32_t)week;
    }
    const uint64_t total = (uint64_t)(time_now_ns() - started);
    block->ns_cells += total > block->ns_lists ? total - block->ns_lists : 0u;
}

/* ---- Flow accumulation over the kept cells ------------------------------------------------------------------------- */

static uint32_t weather_flow_find(const void *borrowed_context, int32_t cell_x, int32_t cell_y) {
    const climate_week_store_t *const store = borrowed_context;
    const int32_t tile_x = weather_floor_div(cell_x, CLIMATE_TILE_CELLS);
    const int32_t tile_y = weather_floor_div(cell_y, CLIMATE_TILE_CELLS);
    uint32_t index = 0u;
    if (climate_week_store_find(store, tile_x, tile_y, &index) == nullptr) {
        return CLIMATE_FLOW_NONE;
    }
    return index * CLIMATE_TILE_CELL_COUNT + (uint32_t)((cell_y - tile_y * CLIMATE_TILE_CELLS) * CLIMATE_TILE_CELLS +
                                                        (cell_x - tile_x * CLIMATE_TILE_CELLS));
}

void climate_week_flow_run(climate_week_store_t *store) {
    const uint32_t count = store->tile_count * CLIMATE_TILE_CELL_COUNT;
    climate_flow_cell_t *const flow = store->borrowed_flow;
    for (uint32_t t = 0u; t < store->tile_count; t++) {
        const climate_week_tile_t *const tile = &store->borrowed_tiles[t];
        const climate_week_side_t *const side = &store->borrowed_sides[t];
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            climate_flow_cell_t *const cell = &flow[t * CLIMATE_TILE_CELL_COUNT + c];
            memset(cell, 0, sizeof(*cell));
            cell->cell_x = weather_cell_x(tile, c);
            cell->cell_y = weather_cell_y(tile, c);
            cell->height = side->z_ground[c];
            cell->runoff = tile->cells[c].runoff_mm;
            cell->mean = side->mean_runoff[c];
            cell->sea = side->sea[c];
        }
    }
    climate_flow_receivers(flow, count, weather_flow_find, store);
    climate_flow_accumulate(flow, count, store->borrowed_flow_index, store->borrowed_flow_index + count);
    for (uint32_t t = 0u; t < store->tile_count; t++) {
        climate_week_tile_t *const tile = &store->borrowed_tiles[t];
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            const climate_flow_cell_t *const cell = &flow[t * CLIMATE_TILE_CELL_COUNT + c];
            tile->cells[c].flood = climate_flow_flood(cell->q_week, cell->q_mean);
        }
    }
}
