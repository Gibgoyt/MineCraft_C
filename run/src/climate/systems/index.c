/* See README.md. Weather systems of plan 004 phase 06 (master plan 7.2 to 7.4): spawned by a hash per spawn square and day
 * at a rate set by place and season, thinned to the exact rate at their start, steered by rules that read only latitude,
 * the season and the coarse land mask, and listed per day near a place. Nothing about air or water is simulated. */
#include "climate/systems/index.h"

#include "assert/index.h"
#include "calendar/index.h"
#include "climate/air/index.h"
#include "climate/season/index.h"
#include "climate/systems/field/index.h"
#include "terrain/rng/index.h"

#include <math.h>
#include <string.h>

static const float64_t SYSTEMS_TWO_PI = CLIMATE_TWO_PI;
static const float64_t SYSTEMS_HOURS_A_DAY = CALENDAR_HOURS_PER_DAY_F64;
/* The open-sea test's five points: here, then CLIMATE_HURRICANE_OPEN_SEA_BLOCKS north, south, east and west. */
enum : uint32_t { SYSTEMS_OPEN_SEA_POINTS = 5u, SYSTEMS_INTERIOR_POINTS = 9u };
static const float64_t SYSTEMS_OPEN_SEA_STEPS[SYSTEMS_OPEN_SEA_POINTS][2] = {
    {0.0f64, 0.0f64}, {0.0f64, 1.0f64}, {0.0f64, -1.0f64}, {1.0f64, 0.0f64}, {-1.0f64, 0.0f64}};
/* cos 45 degrees: a diagonal's step along each axis. */
static const float64_t SYSTEMS_DIAGONAL = 0.70710678118654752440f64;
/* The land mask's pages a track keeps, direct-mapped by the low 3 bits of the tile's x and y. */
enum : uint32_t { SYSTEMS_MASK_LOW_BITS = 3u, SYSTEMS_MASK_LOW_MASK = (1u << SYSTEMS_MASK_LOW_BITS) - 1u };
/* The kept columns outside the keep square: direct-mapped by this many low bits of the cell on each axis. */
enum : uint32_t { SYSTEMS_POINT_BITS = 5u, SYSTEMS_POINT_MASK = (1u << SYSTEMS_POINT_BITS) - 1u };

/* ---- Pieces ---------------------------------------------------------------------------------------------------------- */

float64_t climate_systems_day_of_year(float64_t t) {
    const float64_t d = t - CLIMATE_SYSTEMS_YEAR_DAYS * floor(t / CLIMATE_SYSTEMS_YEAR_DAYS);
    return d >= CLIMATE_SYSTEMS_YEAR_DAYS ? 0.0f64 : d; /* t a hair under a whole year can round up to 365 */
}

float64_t climate_systems_smoothstep(float64_t x) {
    x = x < 0.0f64 ? 0.0f64 : (x > 1.0f64 ? 1.0f64 : x);
    return x * x * (3.0f64 - 2.0f64 * x);
}

float64_t climate_systems_life(float64_t tau) {
    if (tau < 0.0f64 || tau > 1.0f64) {
        return 0.0f64;
    }
    if (tau < CLIMATE_LIFE_GROW_SHARE) {
        return climate_systems_smoothstep(CLIMATE_LIFE_THIRDS * tau);
    }
    if (tau <= 1.0f64 - CLIMATE_LIFE_FADE_SHARE) {
        return 1.0f64;
    }
    return climate_systems_smoothstep(CLIMATE_LIFE_THIRDS * (1.0f64 - tau));
}

float64_t climate_systems_uniform(uint64_t hash) {
    return (float64_t)(hash >> CLIMATE_UNIFORM_SHIFT) * CLIMATE_UNIFORM_SCALE;
}

uint32_t climate_systems_poisson(float64_t u, float64_t lambda, float64_t exp_minus_lambda, uint32_t cap) {
    uint32_t k = 0u;
    float64_t p = exp_minus_lambda;
    float64_t cumulative = p;
    while (u >= cumulative && k < cap) {
        k++;
        p = p * lambda / (float64_t)k;
        cumulative += p;
    }
    return k;
}

float32_t climate_systems_hurricane_vmax(float32_t abs_hpa) {
    const float32_t at30 = CLIMATE_HURRICANE_VMAX_AT_30;
    if (abs_hpa < -CLIMATE_HURRICANE_HPA[0]) {
        return at30 * abs_hpa / -CLIMATE_HURRICANE_HPA[0];
    }
    return at30 + (CLIMATE_HURRICANE_VMAX_AT_90 - at30) * (abs_hpa + CLIMATE_HURRICANE_HPA[0]) /
                      (CLIMATE_HURRICANE_HPA[0] - CLIMATE_HURRICANE_HPA[1]);
}

/* lo + u (hi - lo), in 64 bits then rounded once. */
static float32_t systems_range(const float32_t range[2], float64_t u) {
    return (float32_t)((float64_t)range[0] + u * ((float64_t)range[1] - (float64_t)range[0]));
}

static float64_t systems_hemisphere(float64_t phi) {
    return phi >= 0.0f64 ? 1.0f64 : -1.0f64; /* s_h, +1 on the equator itself as phase 04 has it */
}

float64_t climate_systems_square(uint8_t kind) {
    switch (kind) {
    case CLIMATE_SYSTEM_HURRICANE:
        return CLIMATE_SQUARE_HURRICANE;
    case CLIMATE_SYSTEM_LOW:
        return CLIMATE_SQUARE_LOW;
    case CLIMATE_SYSTEM_HIGH:
        return CLIMATE_SQUARE_HIGH;
    default:
        return CLIMATE_SQUARE_THUNDER;
    }
}

/* The longest life of a kind, in days, and its fastest speed with drift, blocks a day. */
static float64_t systems_life_max(uint8_t kind) {
    switch (kind) {
    case CLIMATE_SYSTEM_HURRICANE:
        return (float64_t)CLIMATE_HURRICANE_LIFE_DAYS[1];
    case CLIMATE_SYSTEM_LOW:
        return (float64_t)CLIMATE_LOW_LIFE_DAYS[1];
    case CLIMATE_SYSTEM_HIGH:
        return (float64_t)CLIMATE_HIGH_LIFE_DAYS[1];
    default:
        return (float64_t)CLIMATE_THUNDER_LIFE_HOURS[1] / SYSTEMS_HOURS_A_DAY;
    }
}

float64_t climate_systems_reach(uint8_t kind) {
    const float64_t life = systems_life_max(kind);
    switch (kind) {
    case CLIMATE_SYSTEM_HURRICANE: {
        const float64_t drift = (float64_t)CLIMATE_HURRICANE_POLEWARD_EAST;
        const float64_t fastest =
            (float64_t)CLIMATE_HURRICANE_SPEED_EAST * (float64_t)CLIMATE_SPEED_SCALE[1] * sqrt(1.0f64 + drift * drift);
        return life * fastest + (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)CLIMATE_HURRICANE_RADIUS[1];
    }
    case CLIMATE_SYSTEM_LOW: {
        const float64_t drift = (float64_t)CLIMATE_LOW_POLEWARD;
        const float64_t fastest = (float64_t)CLIMATE_LOW_SPEED[1] * sqrt(1.0f64 + drift * drift);
        return life * fastest + (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)CLIMATE_LOW_RADIUS[1];
    }
    case CLIMATE_SYSTEM_HIGH: {
        const float64_t drift = (float64_t)CLIMATE_HIGH_EQUATORWARD;
        const float64_t fastest = (float64_t)CLIMATE_HIGH_SPEED[1] * sqrt(1.0f64 + drift * drift);
        return life * fastest + (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)CLIMATE_HIGH_RADIUS[1];
    }
    default:
        return life * (float64_t)CLIMATE_THUNDER_SPEED_MAX * (float64_t)CLIMATE_REAL_SECONDS_A_DAY +
               (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)CLIMATE_THUNDER_RADIUS[1];
    }
}

/* ---- The mask, as a build reads it ------------------------------------------------------------------------------------ */

void climate_systems_mask_start(climate_systems_mask_t *mask, const climate_t *climate, arena_t *arena,
                                climate_systems_stats_t *stats) {
    memset(mask, 0, sizeof(*mask));
    mask->borrowed_climate = climate;
    mask->borrowed_stats = stats;
    mask->borrowed_pages = arena_alloc_array(arena, (uint64_t)CLIMATE_SYSTEMS_MASK_PAGES * CLIMATE_TILE_CELL_COUNT,
                                             sizeof(float32_t), alignof(float32_t));
    mask->borrowed_point_cells =
        arena_alloc_array(arena, 2ull * CLIMATE_SYSTEMS_MASK_POINTS, sizeof(int64_t), alignof(int64_t));
    mask->borrowed_point_heights =
        arena_alloc_array(arena, CLIMATE_SYSTEMS_MASK_POINTS, sizeof(float32_t), alignof(float32_t));
    if (mask->borrowed_point_cells != nullptr) {
        for (uint32_t i = 0u; i < 2u * CLIMATE_SYSTEMS_MASK_POINTS; i++) {
            mask->borrowed_point_cells[i] = INT64_MIN;
        }
    }
}

/* One column outside the keep square, kept by its cell: the ground is a pure function, so a kept height is the same bits. */
static float32_t systems_mask_point(climate_systems_mask_t *mask, int64_t cell_x, int64_t cell_y, float64_t x,
                                    float64_t y) {
    if (mask->borrowed_point_cells == nullptr || mask->borrowed_point_heights == nullptr) {
        return climate_mask_point(mask->borrowed_climate, x, y);
    }
    const uint32_t entry = (uint32_t)(((uint64_t)cell_x & SYSTEMS_POINT_MASK) |
                                      (((uint64_t)cell_y & SYSTEMS_POINT_MASK) << SYSTEMS_POINT_BITS));
    static_assert((SYSTEMS_POINT_MASK + 1u) * (SYSTEMS_POINT_MASK + 1u) == CLIMATE_SYSTEMS_MASK_POINTS);
    int64_t *const cell = mask->borrowed_point_cells + 2u * entry;
    if (cell[0] != cell_x || cell[1] != cell_y) {
        mask->borrowed_point_heights[entry] = climate_mask_point(mask->borrowed_climate, x, y);
        cell[0] = cell_x;
        cell[1] = cell_y;
    }
    return mask->borrowed_point_heights[entry];
}

float32_t climate_systems_mask_height(climate_systems_mask_t *mask, float64_t x, float64_t y) {
    const int64_t cell_x = (int64_t)floor(x / (float64_t)CLIMATE_CELL_BLOCKS);
    const int64_t cell_y = (int64_t)floor(y / (float64_t)CLIMATE_CELL_BLOCKS);
    const int32_t tile_x = (int32_t)(cell_x >> (CLIMATE_TILE_SHIFT - CLIMATE_CELL_SHIFT));
    const int32_t tile_y = (int32_t)(cell_y >> (CLIMATE_TILE_SHIFT - CLIMATE_CELL_SHIFT));
    const int64_t in_tile = (int64_t)CLIMATE_TILE_CELLS - 1;
    const uint64_t index = (uint64_t)((cell_y & in_tile) * CLIMATE_TILE_CELLS + (cell_x & in_tile));
    /* Direct-mapped by the tile's low bits: a track's neighbouring tiles fall in different entries. */
    const uint32_t entry = (((uint32_t)tile_x & SYSTEMS_MASK_LOW_MASK) |
                            (((uint32_t)tile_y & SYSTEMS_MASK_LOW_MASK) << SYSTEMS_MASK_LOW_BITS)) %
                           CLIMATE_SYSTEMS_MASK_PAGES;
    float32_t *const page = mask->borrowed_pages + (uint64_t)entry * CLIMATE_TILE_CELL_COUNT;
    if (mask->borrowed_stats != nullptr) {
        mask->borrowed_stats->mask_reads++;
    }
    if (mask->held[entry] == 0u || mask->tile_x[entry] != tile_x || mask->tile_y[entry] != tile_y) {
        if (!climate_mask_page_kept(mask->borrowed_climate, tile_x, tile_y)) {
            /* Outside the keep square no page is kept, so one column, the page's value there, costs 1/256 of one. */
            return systems_mask_point(mask, cell_x, cell_y, x, y);
        }
        (void)climate_mask_page_read(mask->borrowed_climate, tile_x, tile_y, page);
        mask->held[entry] = 1u;
        mask->tile_x[entry] = tile_x;
        mask->tile_y[entry] = tile_y;
        if (mask->borrowed_stats != nullptr) {
            mask->borrowed_stats->mask_pages++;
        }
    }
    return page[index];
}

/* A height through the build's mask when there is one, or by the Owner's point read with arena. */
static float32_t systems_height(const climate_t *climate, climate_systems_mask_t *mask, arena_t *arena, float64_t x,
                                float64_t y) {
    if (mask != nullptr) {
        return climate_systems_mask_height(mask, x, y);
    }
    return climate_mask_height_at(climate, arena, x, y);
}

/* ---- Rates ------------------------------------------------------------------------------------------------------------ */

float32_t climate_systems_sea_temperature(const climate_t *climate, float64_t y, float64_t t) {
    const climate_systems_hooks_t hooks = climate_systems_hooks(climate);
    return climate_air_sea_temperature(climate_normals(climate), climate_latitude_deg(y), hooks.hurricane_current_c,
                                       (float32_t)climate_systems_day_of_year(t));
}

float32_t climate_systems_hurricane_s(const climate_t *climate, float64_t y, float64_t t) {
    const climate_systems_hooks_t hooks = climate_systems_hooks(climate);
    if (hooks.hurricane_s > 0.0f32) {
        const uint32_t d = (uint32_t)climate_systems_day_of_year(t);
        const bool in = hooks.hurricane_first <= hooks.hurricane_last
                            ? (d >= hooks.hurricane_first && d <= hooks.hurricane_last)
                            : (d >= hooks.hurricane_first || d <= hooks.hurricane_last);
        return in ? hooks.hurricane_s : 0.0f32;
    }
    return climate_season_hurricane_s((float32_t)climate_latitude_deg(y),
                                      climate_systems_sea_temperature(climate, y, t));
}

/* Sea: the mask at or below 0 at the point, and CLIMATE_HURRICANE_OPEN_SEA_BLOCKS north, south, east and west of it when
 * that is over 0 (it is 0: the point alone). */
static bool systems_open_sea(const climate_t *climate, climate_systems_mask_t *mask, arena_t *arena, float64_t x,
                             float64_t y) {
    const float64_t d = (float64_t)CLIMATE_HURRICANE_OPEN_SEA_BLOCKS;
    const uint32_t points = d > 0.0f64 ? SYSTEMS_OPEN_SEA_POINTS : 1u;
    for (uint32_t k = 0u; k < points; k++) {
        if (systems_height(climate, mask, arena, x + d * SYSTEMS_OPEN_SEA_STEPS[k][0],
                           y + d * SYSTEMS_OPEN_SEA_STEPS[k][1]) > 0.0f32) {
            return false;
        }
    }
    return true;
}

static float32_t systems_hurricane_rate(const climate_t *climate, climate_systems_mask_t *mask, arena_t *arena,
                                        float64_t x, float64_t y, float64_t t) {
    const float32_t s = climate_systems_hurricane_s(climate, y, t);
    if (!(s > 0.0f32) || !systems_open_sea(climate, mask, arena, x, y)) {
        return 0.0f32;
    }
    return CLIMATE_HURRICANE_RATE * s;
}

float32_t climate_systems_hurricane_rate(const climate_t *climate, arena_t *arena, float64_t x, float64_t y,
                                         float64_t t) {
    return systems_hurricane_rate(climate, nullptr, arena, x, y, t);
}

float32_t climate_systems_hurricane_rate_mask(const climate_t *climate, climate_systems_mask_t *mask, float64_t x,
                                              float64_t y, float64_t t) {
    return systems_hurricane_rate(climate, mask, nullptr, x, y, t);
}

/* s_h cos(2 pi (d - 200) / 365): -1 in the depth of the local winter, +1 in its summer. */
static float64_t systems_season_wave(float64_t phi, float64_t d) {
    return systems_hemisphere(phi) *
           cos(SYSTEMS_TWO_PI * (d - (float64_t)CLIMATE_LOW_PEAK_DAY) / CLIMATE_SYSTEMS_YEAR_DAYS);
}

float32_t climate_systems_low_rate(const climate_t *climate, float64_t y, float64_t t) {
    (void)climate;
    const float64_t phi = climate_latitude_deg(y);
    const float64_t d = climate_systems_day_of_year(t);
    const float32_t psi = fabsf(climate_air_wind_latitude(phi, (float32_t)d));
    if (psi < CLIMATE_LOW_PSI_MIN_DEG || psi > CLIMATE_LOW_PSI_MAX_DEG) {
        return 0.0f32;
    }
    const float64_t mid = 0.5f64 * ((float64_t)CLIMATE_LOW_RATE_WINTER + (float64_t)CLIMATE_LOW_RATE_SUMMER);
    const float64_t swing = 0.5f64 * ((float64_t)CLIMATE_LOW_RATE_WINTER - (float64_t)CLIMATE_LOW_RATE_SUMMER);
    return (float32_t)(mid - swing * systems_season_wave(phi, d));
}

static float32_t systems_high_rate(const climate_t *climate, climate_systems_mask_t *mask, arena_t *arena, float64_t x,
                                   float64_t y, float64_t t) {
    const float64_t phi = climate_latitude_deg(y);
    const float64_t d = climate_systems_day_of_year(t);
    const float32_t psi = fabsf(climate_air_wind_latitude(phi, (float32_t)d));
    if (psi >= CLIMATE_HIGH_PSI_MIN_DEG && psi <= CLIMATE_HIGH_PSI_MAX_DEG) {
        return CLIMATE_HIGH_RATE;
    }
    /* Only from 20 degrees poleward: the equator has no winter for a cold continental high (Issues found 10). */
    if (!(systems_season_wave(phi, d) < 0.0f64) || psi < CLIMATE_HIGH_PSI_MIN_DEG ||
        psi > CLIMATE_HIGH_INTERIOR_PSI_MAX_DEG) {
        return 0.0f32;
    }
    /* A continental interior: land at the point and 3000 blocks away along the 4 axes and 4 diagonals. */
    const float64_t r = (float64_t)CLIMATE_HIGH_INTERIOR_BLOCKS;
    const float64_t diagonal = r * SYSTEMS_DIAGONAL;
    const float64_t points[SYSTEMS_INTERIOR_POINTS][2] = {
        {0.0f64, 0.0f64},     {r, 0.0f64},           {-r, 0.0f64},          {0.0f64, r},           {0.0f64, -r},
        {diagonal, diagonal}, {-diagonal, diagonal}, {diagonal, -diagonal}, {-diagonal, -diagonal}};
    for (uint32_t k = 0u; k < SYSTEMS_INTERIOR_POINTS; k++) {
        if (!(systems_height(climate, mask, arena, x + points[k][0], y + points[k][1]) > 0.0f32)) {
            return 0.0f32;
        }
    }
    return CLIMATE_HIGH_RATE;
}

float32_t climate_systems_high_rate(const climate_t *climate, arena_t *arena, float64_t x, float64_t y, float64_t t) {
    return systems_high_rate(climate, nullptr, arena, x, y, t);
}

float32_t climate_systems_thunder_base(float32_t t_day_c, float32_t rain_share, bool monsoon) {
    float32_t hot = (t_day_c - CLIMATE_THUNDER_HOT_C) / CLIMATE_THUNDER_HOT_RANGE_C;
    hot = hot < 0.0f32 ? 0.0f32 : (hot > 1.0f32 ? 1.0f32 : hot);
    const float32_t wet = rain_share < 0.0f32 ? 0.0f32 : rain_share;
    return CLIMATE_THUNDER_RATE_GAIN * CLIMATE_THUNDER_RATE * hot * wet *
           (1.0f32 + CLIMATE_THUNDER_MONSOON * (monsoon ? 1.0f32 : 0.0f32));
}

float32_t climate_systems_thunder_rain_scale(float32_t t_day_c, float32_t rain_share, bool monsoon) {
    float32_t hot = (t_day_c - CLIMATE_THUNDER_HOT_C) / CLIMATE_THUNDER_HOT_RANGE_C;
    hot = hot < CLIMATE_THUNDER_HOT_FLOOR ? CLIMATE_THUNDER_HOT_FLOOR : (hot > 1.0f32 ? 1.0f32 : hot);
    const float32_t often = hot * rain_share * (1.0f32 + CLIMATE_THUNDER_MONSOON * (monsoon ? 1.0f32 : 0.0f32));
    return often > 0.0f32 ? CLIMATE_THUNDER_RAIN_SCALE_BASE / often : 0.0f32;
}

float32_t climate_systems_thunder_rate_scale(const climate_t *climate, int32_t square_x, int32_t square_y, int64_t day,
                                             bool *ready, float32_t *rain_scale) {
    const float64_t q = CLIMATE_SQUARE_THUNDER;
    const float64_t x = ((float64_t)square_x + 0.5f64) * q, y = ((float64_t)square_y + 0.5f64) * q;
    climate_normals_at_t normals;
    const float32_t d = (float32_t)climate_systems_day_of_year((float64_t)day);
    *rain_scale = 0.0f32;
    if (!climate_normals_at(climate, x, y, nanf(""), d, &normals)) {
        *ready = false;
        return 0.0f32;
    }
    *ready = true;
    const float32_t share = normals.rain_max_mm_day > 0.0f32 ? normals.rain_mm_day / normals.rain_max_mm_day : 0.0f32;
    *rain_scale = climate_systems_thunder_rain_scale(normals.t_day_c, share, normals.monsoon_today != 0u);
    return climate_systems_thunder_base(normals.t_day_c, share, normals.monsoon_today != 0u);
}

float32_t climate_systems_thunder_rate(const climate_t *climate, int32_t square_x, int32_t square_y, int64_t day,
                                       bool *ready) {
    float32_t rain_scale = 0.0f32;
    return climate_systems_thunder_rate_scale(climate, square_x, square_y, day, ready, &rain_scale);
}

/* ---- Draws ------------------------------------------------------------------------------------------------------------ */

enum : uint32_t {
    DRAW_T = 0u,    /* start time in the day */
    DRAW_X = 1u,    /* start x in the square */
    DRAW_Y = 2u,    /* start y */
    DRAW_KEEP = 3u, /* thinning */
    DRAW_R = 4u,    /* radius */
    DRAW_C = 5u,    /* hurricanes: the eye */
    DRAW_A = 6u,    /* peak pressure */
    DRAW_L = 7u,    /* life */
    DRAW_V = 8u,    /* speed, a hurricane's speed scale, a cell's peak rain */
    DRAW_PSI = 9u,  /* hurricanes: the turn latitude; lows: the front's length */
    DRAW_S = 10u,   /* lows: the shield's rain */
};

/* The 12 draws of a candidate, always all of them, in this order (the phase 06 file's table), so nothing drawn later can
 * change anything drawn before, and a candidate thinned away moves no other candidate's numbers. */
typedef struct systems_draws_t {
    float64_t u[CLIMATE_SYSTEMS_DRAWS]; /* 0 */
    uint32_t name_seed;                 /* 88 */
    uint8_t _pad0[4];                   /* 92 */
} systems_draws_t;
static_assert(sizeof(systems_draws_t) == 96);

static void systems_draw(uint64_t id, systems_draws_t *out) {
    terrain_rng_t rng = terrain_rng_start(id);
    for (uint32_t k = 0u; k < CLIMATE_SYSTEMS_DRAWS; k++) {
        out->u[k] = terrain_rng_next_f64(&rng);
    }
    out->name_seed = terrain_rng_next_u32(&rng);
    memset(out->_pad0, 0, sizeof(out->_pad0));
}

static float64_t systems_rate_max(uint8_t kind) {
    switch (kind) {
    case CLIMATE_SYSTEM_HURRICANE:
        return (float64_t)CLIMATE_HURRICANE_RATE;
    case CLIMATE_SYSTEM_LOW:
        return (float64_t)fmaxf(CLIMATE_LOW_RATE_WINTER, CLIMATE_LOW_RATE_SUMMER);
    default:
        return (float64_t)CLIMATE_HIGH_RATE;
    }
}

/* ---- Tracks ----------------------------------------------------------------------------------------------------------- */

/* The velocity a hurricane, low or high has at a place and time, blocks a day. stage and turn_time belong to hurricanes. */
static void systems_velocity(const climate_system_t *system, float64_t x, float64_t y, float64_t t, uint8_t stage,
                             float64_t turn_time, float64_t out[2]) {
    (void)x;
    const float64_t phi = climate_latitude_deg(y);
    /* Poleward in world terms, (0, e s_h): -y north of the equator in a strip mirrored past a pole. */
    const float64_t poleward = (float64_t)climate_air_mirror_sign(y) * systems_hemisphere(phi);
    const float64_t pole_ok = fabs(phi) > (float64_t)CLIMATE_DRIFT_POLE_STOP_DEG ? 0.0f64 : 1.0f64;
    const float64_t equator_ok = fabs(phi) < (float64_t)CLIMATE_DRIFT_EQUATOR_STOP_DEG ? 0.0f64 : 1.0f64;
    const float64_t speed = (float64_t)system->speed;
    switch (system->kind) {
    case CLIMATE_SYSTEM_HURRICANE: {
        if (stage == 0u) {
            const float64_t v = speed * (float64_t)CLIMATE_HURRICANE_SPEED_WEST;
            out[0] = -v;
            out[1] = v * (float64_t)CLIMATE_HURRICANE_POLEWARD_WEST * poleward * pole_ok;
        } else if (stage == 1u) {
            const float64_t q = climate_systems_smoothstep((t - turn_time) / (float64_t)CLIMATE_HURRICANE_TURN_DAYS);
            const float64_t a_west = CLIMATE_PI - atan((float64_t)CLIMATE_HURRICANE_POLEWARD_WEST);
            const float64_t a_east = atan((float64_t)CLIMATE_HURRICANE_POLEWARD_EAST);
            const float64_t a = (1.0f64 - q) * a_west + q * a_east;
            const float64_t v =
                speed * ((float64_t)CLIMATE_HURRICANE_SPEED_WEST +
                         ((float64_t)CLIMATE_HURRICANE_SPEED_EAST - (float64_t)CLIMATE_HURRICANE_SPEED_WEST) * q);
            out[0] = v * cos(a);
            out[1] = v * sin(a) * poleward * pole_ok;
        } else {
            const float64_t v = speed * (float64_t)CLIMATE_HURRICANE_SPEED_EAST;
            out[0] = v;
            out[1] = v * (float64_t)CLIMATE_HURRICANE_POLEWARD_EAST * poleward * pole_ok;
        }
        return;
    }
    case CLIMATE_SYSTEM_LOW:
        out[0] = speed;
        out[1] = speed * (float64_t)CLIMATE_LOW_POLEWARD * poleward * pole_ok;
        return;
    default:
        out[0] = speed;
        out[1] = -speed * (float64_t)CLIMATE_HIGH_EQUATORWARD * poleward * equator_ok;
        return;
    }
}

void climate_systems_track(const climate_t *climate, climate_systems_mask_t *mask, climate_system_t *system) {
    const bool hurricane = system->kind == CLIMATE_SYSTEM_HURRICANE;
    const float64_t step = CLIMATE_TRACK_STEP_DAYS;
    const float64_t life = (float64_t)system->life_days;
    uint32_t count = (uint32_t)floor(life / step) + 1u;
    count = count > CLIMATE_TRACK_SAMPLES_MAX ? CLIMATE_TRACK_SAMPLES_MAX : count;
    /* Halving a day, a quarter of a day at a time. */
    const float32_t weaken_step = (float32_t)pow(0.5f64, step / (float64_t)CLIMATE_HURRICANE_HALF_LIFE_DAYS);
    float64_t x = system->start_x, y = system->start_y;
    float32_t weaken = 1.0f32;
    uint8_t stage = 0u;
    float64_t turn_time = 0.0f64;
    memset(system->samples, 0, sizeof(system->samples));
    uint32_t k = 0u;
    for (; k < count; k++) {
        const float64_t t = system->spawn_day + step * (float64_t)k;
        if (hurricane && stage == 0u) {
            const float64_t phi = climate_latitude_deg(y);
            const float32_t psi = climate_air_wind_latitude(phi, (float32_t)climate_systems_day_of_year(t));
            if (fabsf(psi) >= system->turn_psi_deg) {
                stage = 1u;
                turn_time = t;
            }
        }
        if (hurricane && stage == 1u && t - turn_time >= (float64_t)CLIMATE_HURRICANE_TURN_DAYS) {
            stage = 2u;
        }
        climate_track_sample_t *const sample = &system->samples[k];
        sample->x = x;
        sample->y = y;
        sample->weaken = hurricane ? weaken : 1.0f32;
        sample->over_land = climate_systems_mask_height(mask, x, y) > 0.0f32 ? 1u : 0u;
        sample->stage = stage;
        (void)climate;
        if (mask->borrowed_stats != nullptr) {
            mask->borrowed_stats->track_steps++;
        }
        if (hurricane && k > 0u) {
            const float64_t tau = step * (float64_t)k / life;
            const float64_t strength =
                fabs((float64_t)system->peak_hpa) * climate_systems_life(tau) * (float64_t)weaken;
            if (tau > CLIMATE_LIFE_GROW_SHARE && strength < (float64_t)CLIMATE_SYSTEM_DEAD_HPA) {
                k++;
                break; /* it has weakened to death: this sample is its last */
            }
        }
        if (k + 1u == count) {
            continue;
        }
        float64_t v[2];
        systems_velocity(system, x, y, t, stage, turn_time, v);
        if (hurricane && (sample->over_land != 0u || climate_systems_sea_temperature(mask->borrowed_climate, y, t) <
                                                         CLIMATE_HURRICANE_COOL_SEA_C)) {
            weaken *= weaken_step;
        }
        x += v[0] * step;
        y += v[1] * step;
    }
    system->sample_count = (uint8_t)k;
}

void climate_system_state_at(const climate_system_t *system, float64_t t, climate_system_state_t *out) {
    memset(out, 0, sizeof(*out));
    out->heading[0] = 1.0f32;
    const uint32_t count = system->sample_count;
    if (count == 0u) {
        return;
    }
    const float64_t step = CLIMATE_TRACK_STEP_DAYS;
    const float64_t age = t - system->spawn_day;
    const float64_t end = step * (float64_t)(count - 1u);
    if (age < 0.0f64 || age > end) {
        const climate_track_sample_t *const at = age < 0.0f64 ? &system->samples[0] : &system->samples[count - 1u];
        out->x = at->x;
        out->y = at->y;
        out->over_land = at->over_land;
        out->life_share = (float32_t)(age / (float64_t)system->life_days);
        return;
    }
    uint32_t k = (uint32_t)floor(age / step);
    if (count == 1u) {
        k = 0u;
    } else if (k > count - 2u) {
        k = count - 2u;
    }
    const climate_track_sample_t *const a = &system->samples[k];
    const climate_track_sample_t *const b = count == 1u ? a : &system->samples[k + 1u];
    const float64_t f = count == 1u ? 0.0f64 : (age - step * (float64_t)k) / step;
    out->x = a->x + f * (b->x - a->x);
    out->y = a->y + f * (b->y - a->y);
    const float64_t weaken = (float64_t)a->weaken + f * ((float64_t)b->weaken - (float64_t)a->weaken);
    const float64_t tau = age / (float64_t)system->life_days;
    out->life_share = (float32_t)tau;
    out->pressure_hpa = (float32_t)((float64_t)system->peak_hpa * climate_systems_life(tau) * weaken);
    const float32_t strength = fabsf(out->pressure_hpa);
    out->wind_max = system->kind == CLIMATE_SYSTEM_HURRICANE ? climate_systems_hurricane_vmax(strength)
                                                             : CLIMATE_SYSTEM_VMAX_PER_HPA * strength;
    const float64_t dx = b->x - a->x, dy = b->y - a->y;
    const float64_t length = sqrt(dx * dx + dy * dy);
    if (length > 0.0f64) {
        out->heading[0] = (float32_t)(dx / length);
        out->heading[1] = (float32_t)(dy / length);
    }
    out->over_land = f < 0.5f64 ? a->over_land : b->over_land;
    out->alive = 1u;
}

/* ---- Spawning --------------------------------------------------------------------------------------------------------- */

uint32_t climate_systems_square_spawns(const climate_t *climate, climate_systems_mask_t *mask, uint8_t kind,
                                       int32_t square_x, int32_t square_y, int64_t day, climate_system_t *out,
                                       uint32_t capacity, bool *overflowed) {
    ASSERT(kind < CLIMATE_SYSTEM_THUNDER);
    const climate_systems_hooks_t hooks = climate_systems_hooks(climate);
    if (((hooks.big_off >> kind) & 1u) != 0u) {
        return 0u;
    }
    const float64_t q = climate_systems_square(kind);
    const uint64_t h = terrain_rng_hash3(climate_systems_seed(climate, kind), square_x, square_y, (int32_t)day);
    const float64_t lambda_max = systems_rate_max(kind);
    const uint32_t n =
        climate_systems_poisson(climate_systems_uniform(h), lambda_max, exp(-lambda_max), CLIMATE_SPAWNS_MAX);
    climate_systems_stats_t *const stats = mask->borrowed_stats;
    if (stats != nullptr) {
        stats->hashed++;
        stats->candidates += n;
    }
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        const uint64_t id = terrain_rng_derive(h, (uint64_t)i + 1u);
        systems_draws_t draws;
        systems_draw(id, &draws);
        const float64_t t = (float64_t)day + draws.u[DRAW_T];
        const float64_t x = ((float64_t)square_x + draws.u[DRAW_X]) * q;
        const float64_t y = ((float64_t)square_y + draws.u[DRAW_Y]) * q;
        float32_t lambda = 0.0f32;
        if (kind == CLIMATE_SYSTEM_HURRICANE) {
            lambda = systems_hurricane_rate(climate, mask, nullptr, x, y, t);
        } else if (kind == CLIMATE_SYSTEM_LOW) {
            lambda = climate_systems_low_rate(climate, y, t);
        } else {
            lambda = systems_high_rate(climate, mask, nullptr, x, y, t);
        }
        if (!(draws.u[DRAW_KEEP] < (float64_t)lambda / lambda_max)) {
            continue;
        }
        if (stats != nullptr) {
            stats->kept++;
        }
        if (count == capacity) {
            *overflowed = true;
            continue;
        }
        climate_system_t *const system = &out[count++];
        memset(system, 0, offsetof(climate_system_t, samples));
        system->spawn_day = t;
        system->start_x = x;
        system->start_y = y;
        system->id = id;
        system->name_seed = draws.name_seed;
        system->square_x = square_x;
        system->square_y = square_y;
        system->kind = kind;
        system->spawn_index = (uint8_t)i;
        if (kind == CLIMATE_SYSTEM_HURRICANE) {
            system->radius = systems_range(CLIMATE_HURRICANE_RADIUS, draws.u[DRAW_R]);
            system->core_radius = systems_range(CLIMATE_HURRICANE_EYE, draws.u[DRAW_C]);
            system->peak_hpa = systems_range(CLIMATE_HURRICANE_HPA, draws.u[DRAW_A]);
            system->life_days = systems_range(CLIMATE_HURRICANE_LIFE_DAYS, draws.u[DRAW_L]);
            system->speed = systems_range(CLIMATE_SPEED_SCALE, draws.u[DRAW_V]);
            system->turn_psi_deg = systems_range(CLIMATE_HURRICANE_TURN_PSI_DEG, draws.u[DRAW_PSI]);
        } else if (kind == CLIMATE_SYSTEM_LOW) {
            system->radius = systems_range(CLIMATE_LOW_RADIUS, draws.u[DRAW_R]);
            system->core_radius = CLIMATE_SYSTEM_CORE_SHARE * system->radius;
            system->peak_hpa = systems_range(CLIMATE_LOW_HPA, draws.u[DRAW_A]);
            system->life_days = systems_range(CLIMATE_LOW_LIFE_DAYS, draws.u[DRAW_L]);
            system->speed = systems_range(CLIMATE_LOW_SPEED, draws.u[DRAW_V]);
            system->front_length = systems_range(CLIMATE_LOW_FRONT_BLOCKS, draws.u[DRAW_PSI]);
            /* Twice as many lows in winter, each that much lighter: the rain they bring through the year follows the
             * normal's own season, not theirs (Issues found 12). lambda is at least the summer rate, as it was kept. */
            const float64_t mid = 0.5f64 * ((float64_t)CLIMATE_LOW_RATE_WINTER + (float64_t)CLIMATE_LOW_RATE_SUMMER);
            system->shield_mm_hour = (float32_t)((float64_t)systems_range(CLIMATE_LOW_SHIELD_MM_HOUR, draws.u[DRAW_S]) *
                                                 mid / (float64_t)lambda);
        } else {
            system->radius = systems_range(CLIMATE_HIGH_RADIUS, draws.u[DRAW_R]);
            system->core_radius = CLIMATE_SYSTEM_CORE_SHARE * system->radius;
            system->peak_hpa = systems_range(CLIMATE_HIGH_HPA, draws.u[DRAW_A]);
            system->life_days = systems_range(CLIMATE_HIGH_LIFE_DAYS, draws.u[DRAW_L]);
            system->speed = systems_range(CLIMATE_HIGH_SPEED, draws.u[DRAW_V]);
        }
        climate_systems_track(climate, mask, system);
    }
    return count;
}

void climate_systems_thunder_velocity(float32_t wind_x, float32_t wind_y, float32_t out[2]) {
    const float64_t speed_max = (float64_t)CLIMATE_THUNDER_SPEED_MAX;
    float64_t u = (float64_t)wind_x, v = (float64_t)wind_y;
    const float64_t speed = sqrt(u * u + v * v);
    if (speed > speed_max) {
        u *= speed_max / speed;
        v *= speed_max / speed;
    }
    out[0] = (float32_t)(u * (float64_t)CLIMATE_REAL_SECONDS_A_DAY);
    out[1] = (float32_t)(v * (float64_t)CLIMATE_REAL_SECONDS_A_DAY);
}

uint32_t climate_systems_thunder_square(const climate_t *climate, const climate_systems_day_t *big, int32_t square_x,
                                        int32_t square_y, int64_t day, float32_t lambda_base, float32_t rain_scale,
                                        climate_thunder_t *out, uint32_t capacity, bool *air_ready, bool *overflowed) {
    *air_ready = true;
    if (!(lambda_base > 0.0f32)) {
        return 0u;
    }
    const float64_t q = CLIMATE_SQUARE_THUNDER;
    const uint64_t seed = climate_systems_seed(climate, CLIMATE_SYSTEM_THUNDER);
    const float64_t start_hour = (float64_t)CLIMATE_THUNDER_START_HOURS[0] - (float64_t)CLIMATE_DAY_START_HOUR;
    const float64_t start_span = (float64_t)CLIMATE_THUNDER_START_HOURS[1] - (float64_t)CLIMATE_THUNDER_START_HOURS[0];
    /* Noon of the start day: 6 hours after the day's 06:00 start. */
    const float32_t noon =
        (float32_t)(climate_systems_day_of_year((float64_t)day) +
                    ((float64_t)CALENDAR_NOON_HOUR - (float64_t)CLIMATE_DAY_START_HOUR) / SYSTEMS_HOURS_A_DAY);
    uint32_t count = 0u;
    for (uint32_t process = 0u; process < 2u; process++) {
        /* Afternoon cells at lambda_base; front cells at 4 lambda_base, kept only on a front: (1 + 4 f_front). */
        const uint64_t process_seed = process == 0u ? seed : terrain_rng_derive(seed, 1u);
        const float64_t lambda_max =
            (float64_t)lambda_base * (process == 0u ? 1.0f64 : (float64_t)CLIMATE_THUNDER_FRONT);
        const uint64_t h = terrain_rng_hash3(process_seed, square_x, square_y, (int32_t)day);
        const uint32_t n = climate_systems_poisson(climate_systems_uniform(h), lambda_max, exp(-lambda_max),
                                                   CLIMATE_THUNDER_SPAWNS_MAX);
        for (uint32_t i = 0u; i < n; i++) {
            const uint64_t id = terrain_rng_derive(h, (uint64_t)i + 1u);
            systems_draws_t draws;
            systems_draw(id, &draws);
            const float64_t f =
                process == 0u ? (start_hour + start_span * draws.u[DRAW_T]) / SYSTEMS_HOURS_A_DAY : draws.u[DRAW_T];
            const float64_t t = (float64_t)day + f;
            const float64_t x = ((float64_t)square_x + draws.u[DRAW_X]) * q;
            const float64_t y = ((float64_t)square_y + draws.u[DRAW_Y]) * q;
            float64_t keep = 1.0f64;
            if (process == 1u && !climate_systems_on_front(big, x, y, t)) {
                keep = 0.0f64;
            }
            if (climate_systems_under_high(big, x, y, t)) {
                keep *= (float64_t)CLIMATE_THUNDER_HIGH;
            }
            if (!(draws.u[DRAW_KEEP] < keep)) {
                continue;
            }
            climate_air_t air;
            if (!climate_air_at(climate, x, y, noon, &air)) {
                *air_ready = false;
                return 0u;
            }
            if (count == capacity) {
                *overflowed = true;
                continue;
            }
            climate_thunder_t *const cell = &out[count++];
            memset(cell, 0, sizeof(*cell));
            cell->spawn_day = t;
            cell->start_x = x;
            cell->start_y = y;
            cell->id = id;
            climate_systems_thunder_velocity(air.wind[0], air.wind[1], cell->velocity);
            cell->radius = systems_range(CLIMATE_THUNDER_RADIUS, draws.u[DRAW_R]);
            cell->peak_hpa = systems_range(CLIMATE_THUNDER_HPA, draws.u[DRAW_A]);
            cell->life_days = (float32_t)((float64_t)systems_range(CLIMATE_THUNDER_LIFE_HOURS, draws.u[DRAW_L]) /
                                          SYSTEMS_HOURS_A_DAY);
            /* Raw rain before the wetness of where it falls, times its square's rain scale (Issues found 12). */
            cell->peak_mm_hour = systems_range(CLIMATE_THUNDER_MM_HOUR, draws.u[DRAW_V]) * rain_scale;
            cell->on_front = (uint8_t)process;
        }
    }
    return count;
}

/* ---- The list --------------------------------------------------------------------------------------------------------- */

/* The squared distance from (x, y) to the segment a-b. */
static float64_t systems_segment_distance2(float64_t x, float64_t y, float64_t ax, float64_t ay, float64_t bx,
                                           float64_t by) {
    const float64_t dx = bx - ax, dy = by - ay;
    const float64_t length2 = dx * dx + dy * dy;
    float64_t s = 0.0f64;
    if (length2 > 0.0f64) {
        s = ((x - ax) * dx + (y - ay) * dy) / length2;
        s = s < 0.0f64 ? 0.0f64 : (s > 1.0f64 ? 1.0f64 : s);
    }
    const float64_t ex = ax + s * dx - x, ey = ay + s * dy - y;
    return ex * ex + ey * ey;
}

/* Whether a big system's centre comes within limit of (x, y) at some moment of [day, day + 1). */
static bool systems_reaches(const climate_system_t *system, float64_t x, float64_t y, float64_t limit, int64_t day) {
    const float64_t start = (float64_t)day, stop = start + 1.0f64;
    const float64_t step = CLIMATE_TRACK_STEP_DAYS;
    const float64_t limit2 = limit * limit;
    if (system->sample_count == 1u) {
        const float64_t dx = system->samples[0].x - x, dy = system->samples[0].y - y;
        return system->spawn_day >= start && system->spawn_day < stop && dx * dx + dy * dy <= limit2;
    }
    for (uint32_t k = 0u; k + 1u < system->sample_count; k++) {
        const float64_t t0 = system->spawn_day + step * (float64_t)k, t1 = t0 + step;
        const float64_t a = t0 > start ? t0 : start;
        const float64_t b = t1 < stop ? t1 : stop;
        if (b < a || a >= stop) {
            continue;
        }
        const climate_track_sample_t *const p = &system->samples[k];
        const climate_track_sample_t *const r = &system->samples[k + 1u];
        const float64_t fa = (a - t0) / step, fb = (b - t0) / step;
        const float64_t ax = p->x + fa * (r->x - p->x), ay = p->y + fa * (r->y - p->y);
        const float64_t bx = p->x + fb * (r->x - p->x), by = p->y + fb * (r->y - p->y);
        if (systems_segment_distance2(x, y, ax, ay, bx, by) <= limit2) {
            return true;
        }
    }
    return false;
}

/* The same for a cell's straight line. */
static bool systems_thunder_reaches(const climate_thunder_t *cell, float64_t x, float64_t y, float64_t limit,
                                    int64_t day) {
    const float64_t start = (float64_t)day, stop = start + 1.0f64;
    const float64_t t0 = cell->spawn_day, t1 = cell->spawn_day + (float64_t)cell->life_days;
    const float64_t a = t0 > start ? t0 : start;
    const float64_t b = t1 < stop ? t1 : stop;
    if (b < a || a >= stop) {
        return false;
    }
    const float64_t ax = cell->start_x + (float64_t)cell->velocity[0] * (a - t0);
    const float64_t ay = cell->start_y + (float64_t)cell->velocity[1] * (a - t0);
    const float64_t bx = cell->start_x + (float64_t)cell->velocity[0] * (b - t0);
    const float64_t by = cell->start_y + (float64_t)cell->velocity[1] * (b - t0);
    return systems_segment_distance2(x, y, ax, ay, bx, by) <= limit * limit;
}

/* The list order: big systems by kind, spawn_day, square_y, square_x, spawn_index; cells by spawn_day, then id. Both total
 * on keys that do not depend on the scan. Negative before, positive after. */
static int32_t systems_compare_big(const void *context, uint32_t i, uint32_t j) {
    const climate_system_t *const list = context;
    const climate_system_t *const a = &list[i], *const b = &list[j];
    if (a->kind != b->kind) {
        return a->kind < b->kind ? -1 : 1;
    }
    if (a->spawn_day != b->spawn_day) {
        return a->spawn_day < b->spawn_day ? -1 : 1;
    }
    if (a->square_y != b->square_y) {
        return a->square_y < b->square_y ? -1 : 1;
    }
    if (a->square_x != b->square_x) {
        return a->square_x < b->square_x ? -1 : 1;
    }
    return a->spawn_index < b->spawn_index ? -1 : (a->spawn_index > b->spawn_index ? 1 : 0);
}

static int32_t systems_compare_thunder(const void *context, uint32_t i, uint32_t j) {
    const climate_thunder_t *const list = context;
    const climate_thunder_t *const a = &list[i], *const b = &list[j];
    if (a->spawn_day != b->spawn_day) {
        return a->spawn_day < b->spawn_day ? -1 : 1;
    }
    return a->id < b->id ? -1 : (a->id > b->id ? 1 : 0);
}

/* A stable bottom-up merge sort of index[0 .. count - 1] by compare, with scratch of count. No recursion. */
static void systems_sort(uint32_t *index, uint32_t *scratch, uint32_t count,
                         int32_t (*compare)(const void *, uint32_t, uint32_t), const void *context) {
    uint32_t *from = index, *to = scratch;
    for (uint32_t width = 1u; width < count; width *= 2u) {
        for (uint32_t left = 0u; left < count; left += 2u * width) {
            const uint32_t middle = left + width < count ? left + width : count;
            const uint32_t right = left + 2u * width < count ? left + 2u * width : count;
            uint32_t a = left, b = middle, o = left;
            while (a < middle && b < right) {
                to[o++] = compare(context, from[b], from[a]) < 0 ? from[b++] : from[a++];
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
    if (from != index) {
        memcpy(index, from, (uint64_t)count * sizeof(uint32_t));
    }
}

/* The squares of side q whose area comes within reach of (x, y), both ends included. */
typedef struct systems_squares_t {
    int32_t x0; /* 0 */
    int32_t x1; /* 4 */
    int32_t y0; /* 8 */
    int32_t y1; /* 12 */
} systems_squares_t;
static_assert(sizeof(systems_squares_t) == 16);

static systems_squares_t systems_squares(float64_t x, float64_t y, float64_t reach, float64_t q) {
    return (systems_squares_t){
        .x0 = (int32_t)floor((x - reach) / q),
        .x1 = (int32_t)floor((x + reach) / q),
        .y0 = (int32_t)floor((y - reach) / q),
        .y1 = (int32_t)floor((y + reach) / q),
    };
}

/* Whether square (sx, sy) of side q has a point within reach of (x, y). */
static bool systems_square_near(int32_t sx, int32_t sy, float64_t q, float64_t x, float64_t y, float64_t reach) {
    const float64_t west = (float64_t)sx * q, south = (float64_t)sy * q;
    const float64_t dx = x < west ? west - x : (x > west + q ? x - west - q : 0.0f64);
    const float64_t dy = y < south ? south - y : (y > south + q ? y - south - q : 0.0f64);
    return dx * dx + dy * dy <= reach * reach;
}

bool climate_systems_day_build(const climate_t *climate, arena_t *arena, float64_t x, float64_t y, float32_t radius,
                               float32_t thunder_radius, int64_t day, uint32_t kinds, climate_systems_day_t *out) {
    return climate_systems_day_build_stats(climate, arena, x, y, radius, thunder_radius, day, kinds, out, nullptr);
}

/* Big systems into out, sorted. scratch holds CLIMATE_SYSTEMS_LIST_MAX systems; square holds CLIMATE_SPAWNS_MAX. */
static void systems_build_big(const climate_t *climate, climate_systems_mask_t *mask, climate_system_t *scratch,
                              climate_system_t *square, uint32_t *index, uint32_t *index_scratch, uint32_t kinds,
                              climate_systems_day_t *out) {
    uint32_t count = 0u;
    bool overflowed = false;
    for (uint8_t kind = 0u; kind < CLIMATE_SYSTEM_THUNDER; kind++) {
        if (((kinds >> kind) & 1u) == 0u) {
            continue;
        }
        const float64_t q = climate_systems_square(kind);
        const float64_t reach = climate_systems_reach(kind) + (float64_t)out->radius;
        const int64_t days_back = (int64_t)ceil(systems_life_max(kind));
        const systems_squares_t squares = systems_squares(out->centre_x, out->centre_y, reach, q);
        for (int32_t sy = squares.y0; sy <= squares.y1; sy++) {
            for (int32_t sx = squares.x0; sx <= squares.x1; sx++) {
                if (!systems_square_near(sx, sy, q, out->centre_x, out->centre_y, reach)) {
                    continue;
                }
                for (int64_t d = out->day - days_back; d <= out->day; d++) {
                    bool full = false;
                    const uint32_t n = climate_systems_square_spawns(climate, mask, kind, sx, sy, d, square,
                                                                     CLIMATE_SPAWNS_MAX, &full);
                    overflowed = overflowed || full;
                    for (uint32_t i = 0u; i < n; i++) {
                        const float64_t limit = (float64_t)out->radius +
                                                (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)square[i].radius;
                        if (!systems_reaches(&square[i], out->centre_x, out->centre_y, limit, out->day)) {
                            continue;
                        }
                        if (count == out->system_capacity) {
                            overflowed = true;
                            continue;
                        }
                        memcpy(&scratch[count++], &square[i], sizeof(climate_system_t));
                    }
                }
            }
        }
    }
    for (uint32_t i = 0u; i < count; i++) {
        index[i] = i;
    }
    systems_sort(index, index_scratch, count, systems_compare_big, scratch);
    for (uint32_t i = 0u; i < count; i++) {
        memcpy(&out->borrowed_systems[i], &scratch[index[i]], sizeof(climate_system_t));
    }
    out->system_count = count;
    out->overflowed = (uint8_t)(out->overflowed | (overflowed ? 1u : 0u));
}

/* Cells into out, sorted, after its big systems. scratch holds the list's capacity; square holds two processes' worth. */
static void systems_build_thunder(const climate_t *climate, climate_thunder_t *scratch, climate_thunder_t *square,
                                  uint32_t *index, uint32_t *index_scratch, climate_systems_day_t *out,
                                  climate_systems_stats_t *stats) {
    const float64_t q = CLIMATE_SQUARE_THUNDER;
    const float64_t reach = climate_systems_reach(CLIMATE_SYSTEM_THUNDER) + (float64_t)out->thunder_radius;
    const systems_squares_t squares = systems_squares(out->centre_x, out->centre_y, reach, q);
    uint32_t count = 0u;
    bool overflowed = false, complete = true;
    for (int64_t d = out->day - 1; d <= out->day; d++) {
        for (int32_t sy = squares.y0; sy <= squares.y1; sy++) {
            for (int32_t sx = squares.x0; sx <= squares.x1; sx++) {
                if (!systems_square_near(sx, sy, q, out->centre_x, out->centre_y, reach)) {
                    continue;
                }
                bool ready = false;
                float32_t rain_scale = 0.0f32;
                const float32_t lambda = climate_systems_thunder_rate_scale(climate, sx, sy, d, &ready, &rain_scale);
                if (stats != nullptr) {
                    stats->hashed += 2u;
                }
                if (!ready) {
                    complete = false;
                    continue;
                }
                bool air_ready = true, full = false;
                const uint32_t n = climate_systems_thunder_square(climate, out, sx, sy, d, lambda, rain_scale, square,
                                                                  2u * CLIMATE_THUNDER_SPAWNS_MAX, &air_ready, &full);
                overflowed = overflowed || full;
                if (!air_ready) {
                    complete = false;
                    continue;
                }
                if (stats != nullptr) {
                    stats->kept += n;
                }
                for (uint32_t i = 0u; i < n; i++) {
                    const float64_t limit = (float64_t)out->thunder_radius +
                                            (float64_t)CLIMATE_SYSTEM_REACH_RADII * (float64_t)square[i].radius;
                    if (!systems_thunder_reaches(&square[i], out->centre_x, out->centre_y, limit, out->day)) {
                        continue;
                    }
                    if (count == out->thunder_capacity) {
                        overflowed = true;
                        continue;
                    }
                    scratch[count++] = square[i];
                }
            }
        }
    }
    for (uint32_t i = 0u; i < count; i++) {
        index[i] = i;
    }
    systems_sort(index, index_scratch, count, systems_compare_thunder, scratch);
    for (uint32_t i = 0u; i < count; i++) {
        out->borrowed_thunder[i] = scratch[index[i]];
    }
    out->thunder_count = count;
    out->thunder_complete = complete ? 1u : 0u;
    out->overflowed = (uint8_t)(out->overflowed | (overflowed ? 1u : 0u));
}

bool climate_systems_day_build_stats(const climate_t *climate, arena_t *arena, float64_t x, float64_t y,
                                     float32_t radius, float32_t thunder_radius, int64_t day, uint32_t kinds,
                                     climate_systems_day_t *out, climate_systems_stats_t *stats) {
    memset(out, 0, sizeof(*out));
    out->centre_x = x;
    out->centre_y = y;
    out->day = day;
    const bool cells = (kinds & (1u << CLIMATE_SYSTEM_THUNDER)) != 0u && thunder_radius > 0.0f32;
    float32_t big_radius = radius < 0.0f32 ? 0.0f32 : radius;
    if (cells) {
        thunder_radius = thunder_radius > CLIMATE_THUNDER_QUERY_MAX ? CLIMATE_THUNDER_QUERY_MAX : thunder_radius;
        /* Cells are thinned by the fronts and highs they start under: those must all be in the list. */
        const float32_t needed =
            thunder_radius + (float32_t)climate_systems_reach(CLIMATE_SYSTEM_THUNDER) + CLIMATE_THUNDER_BIG_MARGIN;
        big_radius = big_radius > needed ? big_radius : needed;
        kinds |= CLIMATE_SYSTEMS_BIG;
        out->thunder_radius = thunder_radius;
    }
    out->radius = big_radius;
    const uint64_t list_max = CLIMATE_SYSTEMS_LIST_MAX, square_max = CLIMATE_SPAWNS_MAX;
    void *systems = nullptr, *scratch = nullptr, *square = nullptr, *index = nullptr, *index_scratch = nullptr;
    void *pages = nullptr;
    const uint64_t index_count = cells ? CLIMATE_THUNDER_LIST_MAX : CLIMATE_SYSTEMS_LIST_MAX;
    if (!arena_alloc_try(arena, list_max * sizeof(climate_system_t), alignof(climate_system_t), &systems) ||
        !arena_alloc_try(arena, list_max * sizeof(climate_system_t), alignof(climate_system_t), &scratch) ||
        !arena_alloc_try(arena, square_max * sizeof(climate_system_t), alignof(climate_system_t), &square) ||
        !arena_alloc_try(arena, index_count * sizeof(uint32_t), alignof(uint32_t), &index) ||
        !arena_alloc_try(arena, index_count * sizeof(uint32_t), alignof(uint32_t), &index_scratch) ||
        !arena_alloc_try(arena, (uint64_t)CLIMATE_SYSTEMS_MASK_PAGES * CLIMATE_TILE_CELL_COUNT * sizeof(float32_t),
                         alignof(float32_t), &pages)) {
        return false;
    }
    out->borrowed_systems = systems;
    out->system_capacity = CLIMATE_SYSTEMS_LIST_MAX;
    climate_systems_mask_t mask;
    memset(&mask, 0, sizeof(mask));
    mask.borrowed_climate = climate;
    mask.borrowed_stats = stats;
    mask.borrowed_pages = pages;
    systems_build_big(climate, &mask, scratch, square, index, index_scratch, kinds & CLIMATE_SYSTEMS_BIG, out);
    if (!cells) {
        return true;
    }
    void *thunder = nullptr, *thunder_scratch = nullptr, *thunder_square = nullptr;
    if (!arena_alloc_try(arena, (uint64_t)CLIMATE_THUNDER_LIST_MAX * sizeof(climate_thunder_t),
                         alignof(climate_thunder_t), &thunder) ||
        !arena_alloc_try(arena, (uint64_t)CLIMATE_THUNDER_LIST_MAX * sizeof(climate_thunder_t),
                         alignof(climate_thunder_t), &thunder_scratch) ||
        !arena_alloc_try(arena, 2u * (uint64_t)CLIMATE_THUNDER_SPAWNS_MAX * sizeof(climate_thunder_t),
                         alignof(climate_thunder_t), &thunder_square)) {
        return false;
    }
    out->borrowed_thunder = thunder;
    out->thunder_capacity = CLIMATE_THUNDER_LIST_MAX;
    systems_build_thunder(climate, thunder_scratch, thunder_square, index, index_scratch, out, stats);
    return true;
}
