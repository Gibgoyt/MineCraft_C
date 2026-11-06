/* See README.md. Rain, master plan 6.7, by the phase 05 file's "Rain, in climate/rain": the belts at the wind latitude, the
 * upwind walk for moisture, the orographic factor, on 12 days, fitted to two yearly waves. Nothing flows. */
#include "climate/rain/index.h"

#include "assert/index.h"
#include "climate/air/index.h"
#include "climate/harmonic/index.h"
#include "climate/sea/index.h"

#include <math.h>
#include <string.h>

/* The window cell holding a point given in blocks east and north of the window's south-west corner. */
static uint32_t rain_window_index(float64_t east, float64_t north) {
    const float64_t i = floor(east / (float64_t)CLIMATE_CELL_BLOCKS);
    const float64_t j = floor(north / (float64_t)CLIMATE_CELL_BLOCKS);
    ASSERT_ALWAYS(i >= 0.0f64 && j >= 0.0f64 && i < (float64_t)CLIMATE_WINDOW_CELLS &&
                  j < (float64_t)CLIMATE_WINDOW_CELLS);
    return (uint32_t)j * (uint32_t)CLIMATE_WINDOW_CELLS + (uint32_t)i;
}

/* The window's heights read bilinearly between cell centres, the sea as 0, at a point in blocks from its corner. */
static float32_t rain_height_bilinear(const climate_window_t *window, float64_t east, float64_t north) {
    const float64_t u = east / (float64_t)CLIMATE_CELL_BLOCKS - 0.5f64;
    const float64_t v = north / (float64_t)CLIMATE_CELL_BLOCKS - 0.5f64;
    const float64_t u0 = floor(u), v0 = floor(v);
    ASSERT_ALWAYS(u0 >= 0.0f64 && v0 >= 0.0f64 && u0 + 1.0f64 < (float64_t)CLIMATE_WINDOW_CELLS &&
                  v0 + 1.0f64 < (float64_t)CLIMATE_WINDOW_CELLS);
    const uint32_t side = (uint32_t)CLIMATE_WINDOW_CELLS;
    const uint32_t at = (uint32_t)v0 * side + (uint32_t)u0;
    const float32_t tx = (float32_t)(u - u0), ty = (float32_t)(v - v0);
    const float32_t sw = fmaxf(0.0f32, window->heights[at]), se = fmaxf(0.0f32, window->heights[at + 1u]);
    const float32_t nw = fmaxf(0.0f32, window->heights[at + side]);
    const float32_t ne = fmaxf(0.0f32, window->heights[at + side + 1u]);
    const float32_t south = sw + tx * (se - sw), north_row = nw + tx * (ne - nw);
    return south + ty * (north_row - south);
}

void climate_rain_walk(const climate_window_t *window, uint32_t i, uint32_t j, float32_t z_ground, float32_t wx,
                       float32_t wy, climate_rain_walk_t *out) {
    memset(out, 0, sizeof(*out));
    const float64_t east = (float64_t)i * (float64_t)CLIMATE_CELL_BLOCKS + 0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS;
    const float64_t north = (float64_t)j * (float64_t)CLIMATE_CELL_BLOCKS + 0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS;
    const float64_t step = (float64_t)CLIMATE_WALK_STEP_BLOCKS;
    float32_t h_up = z_ground;
    out->d_up_blocks = CLIMATE_WALK_NONE_BLOCKS;
    for (uint32_t k = 1u; k <= CLIMATE_WALK_STEPS_MAX; k++) {
        const uint32_t at =
            rain_window_index(east - step * (float64_t)k * (float64_t)wx, north - step * (float64_t)k * (float64_t)wy);
        const float32_t height = window->heights[at];
        bool water = false;
        if (k <= CLIMATE_WALK_STEPS_WINDOW) {
            /* The window knows what its water is: sea or a large lake stops the walk, a small lake is land. */
            water = window->kind[at] == CLIMATE_SEA_KIND_SEA || window->kind[at] == CLIMATE_SEA_KIND_LAKE_LARGE;
        } else {
            water = height <= 0.0f32; /* the mask's heights alone: any water, no current */
        }
        out->steps = (uint8_t)k;
        if (water) {
            out->met_water = 1u;
            out->d_up_blocks = CLIMATE_WALK_STEP_BLOCKS * (float32_t)(k - 1u);
            if (k <= CLIMATE_WALK_STEPS_WINDOW && window->kind[at] == CLIMATE_SEA_KIND_SEA) {
                uint8_t strength = 0u;
                out->water_current = climate_air_window_current(window, at, &strength);
            }
            break;
        }
        h_up = fmaxf(h_up, fmaxf(0.0f32, height));
    }
    out->h_up = h_up;
    const float32_t behind =
        rain_height_bilinear(window, east - (float64_t)CLIMATE_OROGRAPHIC_RISE_BLOCKS * (float64_t)wx,
                             north - (float64_t)CLIMATE_OROGRAPHIC_RISE_BLOCKS * (float64_t)wy);
    out->s_w = fmaxf(0.0f32, (z_ground - behind) / CLIMATE_OROGRAPHIC_RISE_BLOCKS);
}

float32_t climate_rain_moisture(const climate_rain_walk_t *walk, const climate_cell_t *cell) {
    const float32_t dry = fmaxf(CLIMATE_MOISTURE_MIN, expf(-walk->d_up_blocks / CLIMATE_MOISTURE_SCALE_BLOCKS));
    if (cell->current == CLIMATE_CURRENT_COLD && cell->sea_distance <= CLIMATE_MOISTURE_COLD_BLOCKS) {
        return dry * CLIMATE_MOISTURE_COLD; /* cold wins over warm: the phase 05 file's order */
    }
    if (walk->water_current == CLIMATE_CURRENT_WARM) {
        return dry * CLIMATE_MOISTURE_WARM;
    }
    return dry;
}

float32_t climate_rain_orographic(const climate_rain_walk_t *walk, float32_t z_ground) {
    const float32_t o =
        1.0f32 + CLIMATE_OROGRAPHIC_GAIN * walk->s_w - fmaxf(0.0f32, walk->h_up - z_ground) / CLIMATE_SHADOW_BLOCKS;
    return o < CLIMATE_OROGRAPHIC_MIN ? CLIMATE_OROGRAPHIC_MIN
                                      : (o > CLIMATE_OROGRAPHIC_MAX ? CLIMATE_OROGRAPHIC_MAX : o);
}

float32_t climate_rain_belt_mm_year(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                                    float32_t psi_deg) {
    if (hooks != nullptr && hooks->belt_mm_year > 0.0f32) {
        return hooks->belt_mm_year;
    }
    return (float32_t)climate_normals_rain_belt(normals, (float64_t)psi_deg);
}

void climate_rain_wind(const climate_cell_t *cell, const climate_test_hooks_t *hooks, float32_t day, float32_t out[2]) {
    if (hooks != nullptr && (hooks->wind[0] != 0.0f32 || hooks->wind[1] != 0.0f32)) {
        out[0] = hooks->wind[0];
        out[1] = hooks->wind[1];
        return;
    }
    out[0] = climate_harmonic_at1(&cell->wind[0], day);
    out[1] = climate_harmonic_at1(&cell->wind[3], day);
}

float32_t climate_rain_psi(const climate_test_hooks_t *hooks, float32_t lat_deg, float32_t day) {
    if (hooks != nullptr && hooks->freeze_seasons != 0u) {
        return lat_deg;
    }
    return climate_air_wind_latitude((float64_t)lat_deg, day);
}

float32_t climate_rain_day_mm_week(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                                   const climate_window_t *window, uint32_t i, uint32_t j, const climate_cell_t *cell,
                                   float32_t day, climate_rain_walk_t *out_walk) {
    climate_rain_walk_t walk;
    memset(&walk, 0, sizeof(walk));
    const bool water = (cell->flags & (CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE)) != 0u;
    float32_t orographic = 1.0f32;
    if (!water) {
        float32_t wind[2];
        climate_rain_wind(cell, hooks, day, wind);
        const float32_t speed = sqrtf(wind[0] * wind[0] + wind[1] * wind[1]);
        if (speed < CLIMATE_WALK_CALM) {
            /* No direction to walk: the air is taken to come from the nearest sea. */
            walk.d_up_blocks = fminf(cell->sea_distance, CLIMATE_WALK_NONE_BLOCKS);
            walk.h_up = cell->z_ground;
            walk.water_current = cell->current;
            walk.met_water = cell->sea_distance < CLIMATE_SEA_DISTANCE_MAX ? 1u : 0u;
        } else {
            climate_rain_walk(window, i, j, cell->z_ground, wind[0] / speed, wind[1] / speed, &walk);
        }
        orographic = climate_rain_orographic(&walk, cell->z_ground);
    }
    if (hooks != nullptr && hooks->orographic_one != 0u) {
        orographic = 1.0f32;
    }
    const float32_t moisture = climate_rain_moisture(&walk, cell);
    const float32_t belt = climate_rain_belt_mm_year(normals, hooks, climate_rain_psi(hooks, cell->lat_deg, day));
    if (out_walk != nullptr) {
        *out_walk = walk;
    }
    return belt * moisture * orographic * CLIMATE_DAYS_A_WEEK / CLIMATE_DAYS_A_YEAR;
}

void climate_rain_build_tile(const climate_normals_t *normals, const climate_test_hooks_t *hooks,
                             const climate_window_t *window, climate_cell_t *cells) {
    const uint32_t tile = (uint32_t)CLIMATE_TILE_CELLS;
    const uint32_t margin = (uint32_t)CLIMATE_MARGIN_CELLS;
    /* South row first, x fastest, and the 12 days in order: the same sums in the same order on any worker. */
    for (uint32_t iy = 0u; iy < tile; iy++) {
        for (uint32_t ix = 0u; ix < tile; ix++) {
            climate_cell_t *const cell = &cells[iy * tile + ix];
            float32_t week[CLIMATE_FIT_DAYS];
            for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
                week[k] = climate_rain_day_mm_week(normals, hooks, window, margin + ix, margin + iy, cell,
                                                   climate_harmonic_day(k), nullptr);
            }
            climate_harmonic_fit2(week, cell->rain_mm_week);
        }
    }
}
