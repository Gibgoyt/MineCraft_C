/* See README.md. Currents, heat lows and winds of plan 004 master plan 6.3 to 6.5 and 6.9, by the phase 04 file's "What
 * climate_air_build_tile does". Nothing flows and nothing is integrated: tables by latitude and coast side, a turn of the
 * pressure gradient, and one yearly wave fitted from 12 days. */
#include "climate/air/index.h"

#include "climate/index.h"
#include "climate/sea/index.h"
#include "climate/sun/index.h"
#include "math/det/index.h"

#include <math.h>

#define AIR_TWO_PI CLIMATE_TWO_PI
#define AIR_DEGREES (CLIMATE_PI / 180.0f64)

/* ---- Pieces --------------------------------------------------------------------------------------------------------- */

float32_t climate_air_mirror_sign(float64_t y) {
    /* Into one period first, as latitude does, so a y far out keeps its digits. */
    const float64_t period = CLIMATE_LATITUDE_PERIOD;
    const float64_t near = y - period * floor((y + 0.5f64 * period) / period);
    return det_cos(CLIMATE_PI * near / (2.0f64 * CLIMATE_POLE_Y)) < 0.0f64 ? -1.0f32 : 1.0f32;
}

static float32_t air_sign(float64_t value) {
    return value > 0.0f64 ? 1.0f32 : (value < 0.0f64 ? -1.0f32 : 0.0f32);
}

void climate_air_poleward(float64_t y, float32_t out[2]) {
    out[0] = 0.0f32;
    out[1] = climate_air_mirror_sign(y) * air_sign(climate_latitude_deg(y));
}

float32_t climate_air_smoothstep(float32_t t) {
    t = t < 0.0f32 ? 0.0f32 : (t > 1.0f32 ? 1.0f32 : t);
    return t * t * (3.0f32 - 2.0f32 * t);
}

float32_t climate_air_doldrums(float32_t psi_deg) {
    const float32_t a = fabsf(psi_deg);
    return CLIMATE_DOLDRUMS_SPEED + (1.0f32 - CLIMATE_DOLDRUMS_SPEED) *
                                        climate_air_smoothstep((a - CLIMATE_DOLDRUMS_DEG) / CLIMATE_CALM_RAMP_DEG);
}

float32_t climate_air_horse(float32_t psi_deg) {
    const float32_t off = fabsf(fabsf(psi_deg) - CLIMATE_BELT_EDGES_DEG[0]);
    return CLIMATE_HORSE_SPEED +
           (1.0f32 - CLIMATE_HORSE_SPEED) * climate_air_smoothstep((off - CLIMATE_HORSE_DEG) / CLIMATE_CALM_RAMP_DEG);
}

float32_t climate_air_wind_latitude(float64_t phi_deg, float32_t day) {
    const float64_t delta = climate_sun_declination_deg((float64_t)day - (float64_t)CLIMATE_WIND_LAG_DAYS);
    return (float32_t)(phi_deg - (float64_t)CLIMATE_WIND_SHIFT * delta);
}

/* A belt's direction made unit, its north-south part times hemisphere and mirror. */
static void air_belt_unit(const float32_t direction[2], float32_t north_sign, float32_t out[2]) {
    const float32_t length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1]);
    out[0] = direction[0] / length;
    out[1] = direction[1] / length * north_sign;
}

void climate_air_belt_wind(float32_t psi_deg, float32_t mirror, float32_t out[2]) {
    const float32_t a = fminf(fabsf(psi_deg), (float32_t)CLIMATE_POLE_DEG);
    const float32_t half_blend = 0.5f32 * CLIMATE_BELT_BLEND_DEG;
    const float32_t b1 =
        climate_air_smoothstep((a - (CLIMATE_BELT_EDGES_DEG[0] - half_blend)) / CLIMATE_BELT_BLEND_DEG);
    const float32_t b2 =
        climate_air_smoothstep((a - (CLIMATE_BELT_EDGES_DEG[1] - half_blend)) / CLIMATE_BELT_BLEND_DEG);
    /* sgn psi, smoothed over +-3 degrees so the equator has no jump, then mirrored. */
    const float32_t sigma = 2.0f32 * climate_air_smoothstep((psi_deg + CLIMATE_HEMISPHERE_BLEND_DEG) /
                                                            (2.0f32 * CLIMATE_HEMISPHERE_BLEND_DEG)) -
                            1.0f32;
    const float32_t north_sign = mirror * sigma;
    float32_t trade[2], westerly[2], polar[2];
    air_belt_unit(CLIMATE_TRADE_DIR, north_sign, trade);
    air_belt_unit(CLIMATE_WESTERLY_DIR, north_sign, westerly);
    air_belt_unit(CLIMATE_POLAR_DIR, north_sign, polar);
    const float32_t calm = climate_air_doldrums(psi_deg) * climate_air_horse(psi_deg);
    for (uint32_t axis = 0u; axis < 2u; axis++) {
        const float32_t low =
            (1.0f32 - b1) * CLIMATE_TRADE_SPEED * trade[axis] + b1 * CLIMATE_WESTERLY_SPEED * westerly[axis];
        out[axis] = calm * ((1.0f32 - b2) * low + b2 * CLIMATE_POLAR_SPEED * polar[axis]);
    }
}

float32_t climate_air_turn_deg(float64_t phi_deg, float32_t mirror) {
    return -CLIMATE_PRESSURE_TURN_DEG * air_sign(phi_deg) * mirror;
}

void climate_air_rotate(const float32_t in[2], float32_t degrees, float32_t out[2]) {
    const float64_t angle = (float64_t)degrees * AIR_DEGREES;
    const float64_t c = det_cos(angle), s = det_sin(angle);
    const float64_t u = (float64_t)in[0], v = (float64_t)in[1];
    out[0] = (float32_t)(u * c - v * s);
    out[1] = (float32_t)(u * s + v * c);
}

void climate_air_pressure_wind(const float32_t gradient[2], float32_t turn_deg, float32_t out[2]) {
    if (turn_deg == 0.0f32) {
        out[0] = 0.0f32; /* the equator: the Earth's spin turns nothing there */
        out[1] = 0.0f32;
        return;
    }
    const float32_t downhill[2] = {-gradient[0], -gradient[1]};
    float32_t turned[2];
    climate_air_rotate(downhill, turn_deg, turned);
    out[0] = CLIMATE_PRESSURE_WIND_K * turned[0];
    out[1] = CLIMATE_PRESSURE_WIND_K * turned[1];
}

void climate_air_steer(const float32_t wind[2], const float32_t slope[2], float32_t out[2]) {
    const float32_t length = sqrtf(slope[0] * slope[0] + slope[1] * slope[1]);
    if (length < CLIMATE_DIRECTION_EPSILON) {
        out[0] = wind[0];
        out[1] = wind[1];
        return;
    }
    const float32_t gx = slope[0] / length, gy = slope[1] / length;
    const float32_t s = fminf(1.0f32, length);
    const float32_t across = wind[0] * gx + wind[1] * gy;
    const float32_t take = CLIMATE_STEER * s * across;
    out[0] = wind[0] - take * gx;
    out[1] = wind[1] - take * gy;
}

float32_t climate_air_altitude_factor(float32_t z) {
    return fminf(CLIMATE_ALTITUDE_MAX, 1.0f32 + fmaxf(0.0f32, z) / CLIMATE_ALTITUDE_SCALE);
}

/* ---- Currents ------------------------------------------------------------------------------------------------------- */

uint8_t climate_air_current_kind(float32_t lat_deg, float32_t n_x, float32_t *out_weight) {
    const float32_t a = fabsf(lat_deg);
    const bool west_coast = n_x > CLIMATE_CURRENT_SIDE;  /* land to the east */
    const bool east_coast = n_x < -CLIMATE_CURRENT_SIDE; /* land to the west */
    *out_weight = 1.0f32;
    if (a < CLIMATE_CURRENT_BANDS_DEG[0]) {
        *out_weight = (west_coast || east_coast) ? 1.0f32 : CLIMATE_CURRENT_WEAK;
        return CLIMATE_CURRENT_WARM;
    }
    if (a < CLIMATE_CURRENT_BANDS_DEG[1]) {
        return west_coast ? CLIMATE_CURRENT_COLD : (east_coast ? CLIMATE_CURRENT_WARM : CLIMATE_CURRENT_NONE);
    }
    if (a < CLIMATE_CURRENT_BANDS_DEG[2]) {
        return west_coast ? CLIMATE_CURRENT_WARM : (east_coast ? CLIMATE_CURRENT_COLD : CLIMATE_CURRENT_NONE);
    }
    return CLIMATE_CURRENT_COLD;
}

static uint8_t air_byte(float32_t share) {
    const float32_t value = roundf(CLIMATE_CURRENT_STRENGTH_MAX_F32 * share);
    return (uint8_t)(value < 0.0f32
                         ? 0.0f32
                         : (value > CLIMATE_CURRENT_STRENGTH_MAX_F32 ? CLIMATE_CURRENT_STRENGTH_MAX_F32 : value));
}

uint8_t climate_air_sea_strength(float32_t land_distance, float32_t weight) {
    float32_t fade = (CLIMATE_CURRENT_REACH - land_distance) / (CLIMATE_CURRENT_REACH - CLIMATE_CURRENT_FULL_WITHIN);
    fade = fade < 0.0f32 ? 0.0f32 : (fade > 1.0f32 ? 1.0f32 : fade);
    return air_byte(fade * weight);
}

uint8_t climate_air_land_strength(float32_t sea_distance, float32_t weight) {
    return air_byte((float32_t)det_exp((float64_t)(-sea_distance / CLIMATE_CURRENT_LAND_SCALE)) * weight);
}

float32_t climate_air_current_c(uint8_t kind) {
    return kind == CLIMATE_CURRENT_WARM ? CLIMATE_CURRENT_WARM_C
                                        : (kind == CLIMATE_CURRENT_COLD ? CLIMATE_CURRENT_COLD_C : 0.0f32);
}

void climate_air_flow(uint8_t kind, const float32_t n[2], float64_t y, float32_t out[2]) {
    out[0] = 0.0f32;
    out[1] = 0.0f32;
    if (kind == CLIMATE_CURRENT_NONE) {
        return;
    }
    float32_t poleward[2];
    climate_air_poleward(y, poleward);
    float32_t along[2] = {-n[1], n[0]};
    const float32_t toward_pole = along[0] * poleward[0] + along[1] * poleward[1];
    bool flip = false;
    if (toward_pole != 0.0f32) {
        flip = kind == CLIMATE_CURRENT_WARM ? toward_pole < 0.0f32 : toward_pole > 0.0f32;
    } else {
        flip = kind == CLIMATE_CURRENT_WARM ? along[0] < 0.0f32 : along[0] > 0.0f32; /* by rule: warm east, cold west */
    }
    if (flip) {
        along[0] = -along[0];
        along[1] = -along[1];
    }
    out[0] = along[0];
    out[1] = along[1];
}

/* ---- Swing, lag, heat lows, the sea ----------------------------------------------------------------------------------- */

static float32_t air_hemisphere(float64_t phi_deg) {
    return phi_deg < 0.0f64 ? -1.0f32 : 1.0f32; /* +1 at exactly 0, so the cell still gets a swing */
}

float32_t climate_air_swing(const climate_normals_t *normals, float64_t phi_deg, float32_t kappa) {
    const float64_t sea = climate_normals_a_sea(normals, phi_deg);
    const float64_t land = climate_normals_a_land(normals, phi_deg);
    return (float32_t)((sea + (float64_t)kappa * (land - sea)) * (float64_t)air_hemisphere(phi_deg));
}

float32_t climate_air_lag_days(float32_t kappa) {
    return CLIMATE_LAG_BASE_DAYS + CLIMATE_LAG_SEA_DAYS * (1.0f32 - kappa);
}

float32_t climate_air_heat_low(float32_t kappa, float32_t t_swing, float32_t lag_days, float32_t day) {
    if (kappa == 0.0f32) {
        return 0.0f32;
    }
    const float64_t phase =
        AIR_TWO_PI * ((float64_t)day - (float64_t)CLIMATE_SUMMER_DAY - (float64_t)lag_days) / CLIMATE_YEAR_DAYS;
    return (float32_t)((float64_t)CLIMATE_HEAT_LOW_HPA_PER_C * (float64_t)kappa * (float64_t)t_swing * det_cos(phase));
}

float32_t climate_air_sea_temperature(const climate_normals_t *normals, float64_t phi_deg, float32_t current_c,
                                      float32_t day) {
    const float64_t phase = AIR_TWO_PI * ((float64_t)day - (float64_t)CLIMATE_SEA_PEAK_DAY) / CLIMATE_YEAR_DAYS;
    return (float32_t)(climate_normals_t_mean(normals, phi_deg) + (float64_t)current_c +
                       climate_normals_a_sea(normals, phi_deg) * (float64_t)air_hemisphere(phi_deg) * det_cos(phase));
}

/* ---- One tile ------------------------------------------------------------------------------------------------------- */

enum : uint32_t {
    AIR_RING = (uint32_t)CLIMATE_TILE_CELLS + 2u, /* 18: the tile and a ring of one cell, for the gradients */
    AIR_RING_COUNT = AIR_RING * AIR_RING,
    AIR_RING_FIRST = (uint32_t)CLIMATE_MARGIN_CELLS - 1u, /* window i, j of the ring's south-west cell: 127 */
};

/* The centre y of window row j. */
static float64_t air_window_y(const climate_window_t *window, uint32_t j) {
    return (float64_t)((int64_t)(window->tile_y - CLIMATE_WINDOW_TILES / 2) * CLIMATE_TILE_BLOCKS +
                       (int64_t)j * CLIMATE_CELL_BLOCKS + CLIMATE_CELL_BLOCKS / 2);
}

/* The current of window sea cell sea_at, from its nearest land: step 1. NONE with no land in reach. n is the unit vector
 * toward that land, worked out as phase 03 works out land_direction, so a tile's sea cell gets its own field's bits. */
static uint8_t air_sea_current(const climate_window_t *window, uint32_t sea_at, float32_t *out_weight, float32_t n[2]) {
    *out_weight = 0.0f32;
    n[0] = 0.0f32;
    n[1] = 0.0f32;
    const uint32_t distance2 = window->land_distance2[sea_at];
    if (distance2 == CLIMATE_SEA_FAR || distance2 == 0u) {
        return CLIMATE_CURRENT_NONE;
    }
    const uint32_t side = (uint32_t)CLIMATE_WINDOW_CELLS;
    const uint32_t nearest = window->land_nearest[sea_at];
    const float64_t cells = sqrt((float64_t)distance2);
    const float64_t dx = (float64_t)(int64_t)(nearest % side) - (float64_t)(int64_t)(sea_at % side);
    const float64_t dy = (float64_t)(int64_t)(nearest / side) - (float64_t)(int64_t)(sea_at / side);
    n[0] = (float32_t)(dx / cells);
    n[1] = (float32_t)(dy / cells);
    const float64_t lat = climate_latitude_deg(air_window_y(window, sea_at / side));
    return climate_air_current_kind((float32_t)lat, n[0], out_weight);
}

uint8_t climate_air_window_current(const climate_window_t *window, uint32_t sea_at, uint8_t *out_strength) {
    *out_strength = 0u;
    const float32_t land = climate_cell_distance_blocks(window->land_distance2[sea_at]);
    if (land > CLIMATE_CURRENT_REACH) {
        return CLIMATE_CURRENT_NONE;
    }
    float32_t weight = 0.0f32, n[2];
    const uint8_t current = air_sea_current(window, sea_at, &weight, n);
    if (current != CLIMATE_CURRENT_NONE) {
        *out_strength = climate_air_sea_strength(land, weight);
    }
    return current;
}

/* Steps 1 to 3 for one tile cell. */
static void air_cell_current(const climate_window_t *window, uint32_t at, climate_cell_t *cell) {
    float32_t weight = 0.0f32, n[2];
    const uint8_t kind = window->kind[at];
    if (kind == CLIMATE_SEA_KIND_SEA) {
        cell->current = climate_air_window_current(window, at, &cell->current_strength);
        return;
    }
    if (kind == CLIMATE_SEA_KIND_LAKE_LARGE || window->sea_distance2[at] == CLIMATE_SEA_FAR) {
        return; /* lakes count not for currents (6.2); and no sea, no current */
    }
    const uint8_t current = air_sea_current(window, window->sea_nearest[at], &weight, n);
    if (current != CLIMATE_CURRENT_NONE) {
        cell->current = current;
        cell->current_strength =
            climate_air_land_strength(climate_cell_distance_blocks(window->sea_distance2[at]), weight);
    }
}

void climate_air_build_tile(const climate_normals_t *normals, arena_t *arena, const climate_window_t *window,
                            climate_cell_t *cells, climate_air_trace_t *trace) {
    const uint32_t side = (uint32_t)CLIMATE_WINDOW_CELLS;
    const uint32_t tile = (uint32_t)CLIMATE_TILE_CELLS;
    const bool steer = trace == nullptr || trace->steer_off == 0u;
    float32_t *const ring_kappa = arena_alloc_array(arena, AIR_RING_COUNT, sizeof(float32_t), CLIMATE_ALIGN_BYTES);
    float32_t *const ring_swing = arena_alloc_array(arena, AIR_RING_COUNT, sizeof(float32_t), CLIMATE_ALIGN_BYTES);
    float32_t *const ring_lag = arena_alloc_array(arena, AIR_RING_COUNT, sizeof(float32_t), CLIMATE_ALIGN_BYTES);
    float32_t *const heat = arena_alloc_array(arena, AIR_RING_COUNT, sizeof(float32_t), CLIMATE_ALIGN_BYTES);
    /* Per tile cell and day: u, v, dp. */
    float32_t *const days =
        arena_alloc_array(arena, (uint64_t)CLIMATE_TILE_CELL_COUNT * CLIMATE_FIT_DAYS * CLIMATE_AIR_DAY_VALUES,
                          sizeof(float32_t), CLIMATE_ALIGN_BYTES);
    float64_t row_lat[AIR_RING];
    float32_t row_mirror[AIR_RING];

    /* 4. Swing and lag for the ring, kappa as phase 03 works it out. */
    for (uint32_t r = 0u; r < AIR_RING; r++) {
        const float64_t y = air_window_y(window, AIR_RING_FIRST + r);
        row_lat[r] = climate_latitude_deg(y);
        row_mirror[r] = climate_air_mirror_sign(y);
        for (uint32_t c = 0u; c < AIR_RING; c++) {
            const uint32_t at = (AIR_RING_FIRST + r) * side + AIR_RING_FIRST + c;
            const float32_t kappa = window->kind[at] == CLIMATE_SEA_KIND_SEA
                                        ? 0.0f32
                                        : climate_cell_kappa(climate_cell_distance_blocks(window->sea_distance2[at]));
            ring_kappa[r * AIR_RING + c] = kappa;
            ring_swing[r * AIR_RING + c] = climate_air_swing(normals, row_lat[r], kappa);
            ring_lag[r * AIR_RING + c] = climate_air_lag_days(kappa);
        }
    }
    /* 1 to 3, and 4 into the cells. */
    for (uint32_t iy = 0u; iy < tile; iy++) {
        for (uint32_t ix = 0u; ix < tile; ix++) {
            const uint32_t ring_at = (iy + 1u) * AIR_RING + ix + 1u;
            climate_cell_t *const cell = &cells[iy * tile + ix];
            air_cell_current(window, ((uint32_t)CLIMATE_MARGIN_CELLS + iy) * side + (uint32_t)CLIMATE_MARGIN_CELLS + ix,
                             cell);
            cell->t_swing = ring_swing[ring_at];
            cell->t_lag_days = ring_lag[ring_at];
        }
    }

    /* 5. The 12 days. */
    const float32_t gradient_step = 2.0f32 * (float32_t)CLIMATE_CELL_BLOCKS; /* 128 blocks between the two neighbours */
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        const float32_t day = climate_harmonic_day(k);
        for (uint32_t i = 0u; i < AIR_RING_COUNT; i++) {
            heat[i] = climate_air_heat_low(ring_kappa[i], ring_swing[i], ring_lag[i], day);
        }
        for (uint32_t iy = 0u; iy < tile; iy++) {
            const uint32_t r = iy + 1u;
            const float32_t psi = climate_air_wind_latitude(row_lat[r], day);
            float32_t belt[2];
            climate_air_belt_wind(psi, row_mirror[r], belt);
            const float32_t turn = climate_air_turn_deg(row_lat[r], row_mirror[r]);
            for (uint32_t ix = 0u; ix < tile; ix++) {
                const uint32_t c = ix + 1u;
                const uint32_t cell_index = iy * tile + ix;
                const climate_cell_t *const cell = &cells[cell_index];
                const float32_t gradient[2] = {
                    (heat[r * AIR_RING + c + 1u] - heat[r * AIR_RING + c - 1u]) / gradient_step,
                    (heat[(r + 1u) * AIR_RING + c] - heat[(r - 1u) * AIR_RING + c]) / gradient_step,
                };
                float32_t pressure_wind[2];
                climate_air_pressure_wind(gradient, turn, pressure_wind);
                float32_t wind[2] = {belt[0] + pressure_wind[0], belt[1] + pressure_wind[1]};
                if (steer) {
                    /* f. The slope from the window's heights, the sea flat at 0. */
                    const uint32_t at = (AIR_RING_FIRST + r) * side + AIR_RING_FIRST + c;
                    const float32_t slope[2] = {
                        (fmaxf(0.0f32, window->heights[at + 1u]) - fmaxf(0.0f32, window->heights[at - 1u])) /
                            gradient_step,
                        (fmaxf(0.0f32, window->heights[at + side]) - fmaxf(0.0f32, window->heights[at - side])) /
                            gradient_step,
                    };
                    float32_t steered[2];
                    climate_air_steer(wind, slope, steered);
                    wind[0] = steered[0];
                    wind[1] = steered[1];
                }
                /* g. Altitude: 1 over the sea. */
                const float32_t lift =
                    (cell->flags & CLIMATE_CELL_SEA) != 0u ? 1.0f32 : climate_air_altitude_factor(cell->z_ground);
                wind[0] *= lift;
                wind[1] *= lift;
                float32_t *const keep = &days[(cell_index * CLIMATE_FIT_DAYS + k) * CLIMATE_AIR_DAY_VALUES];
                keep[0] = wind[0];
                keep[1] = wind[1];
                keep[2] = heat[r * AIR_RING + c];
                if (trace != nullptr) {
                    trace->belt[cell_index][k][0] = belt[0];
                    trace->belt[cell_index][k][1] = belt[1];
                    trace->pressure_wind[cell_index][k][0] = pressure_wind[0];
                    trace->pressure_wind[cell_index][k][1] = pressure_wind[1];
                    trace->wind[cell_index][k][0] = wind[0];
                    trace->wind[cell_index][k][1] = wind[1];
                    trace->heat_low[cell_index][k] = keep[2];
                }
            }
        }
    }

    /* 6. The fit. */
    for (uint32_t cell_index = 0u; cell_index < (uint32_t)CLIMATE_TILE_CELL_COUNT; cell_index++) {
        float32_t u[CLIMATE_FIT_DAYS], v[CLIMATE_FIT_DAYS], p[CLIMATE_FIT_DAYS];
        for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
            const float32_t *const keep = &days[(cell_index * CLIMATE_FIT_DAYS + k) * CLIMATE_AIR_DAY_VALUES];
            u[k] = keep[0];
            v[k] = keep[1];
            p[k] = keep[2];
        }
        climate_cell_t *const cell = &cells[cell_index];
        climate_harmonic_fit1(u, &cell->wind[0]);
        climate_harmonic_fit1(v, &cell->wind[3]);
        climate_harmonic_fit1(p, cell->pressure);
    }
}
