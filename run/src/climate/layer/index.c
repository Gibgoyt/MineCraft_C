/* See README.md. Map layers to pixels: the phase 03 file's "Layer colours". The Atlas (phase 09) and the map tools draw with
 * this same code. */
#include "climate/layer/index.h"

#include <math.h>
#include <string.h>

/* A channel at a share t (0 to 1) of the way from one colour to another, rounded. */
static uint32_t layer_mix(uint32_t from, uint32_t to, float32_t t) {
    t = t < 0.0f32 ? 0.0f32 : (t > 1.0f32 ? 1.0f32 : t);
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        const float32_t a = (float32_t)((from >> shift) & 0xFFu);
        const float32_t b = (float32_t)((to >> shift) & 0xFFu);
        const uint32_t c = (uint32_t)lroundf(a + t * (b - a));
        out |= (c > 255u ? 255u : c) << shift;
    }
    return out;
}

uint32_t climate_layer_colour(climate_layer_t layer, const climate_sample_t *sample) {
    switch (layer) {
    case CLIMATE_LAYER_SEA_DISTANCE:
        return layer_mix(0x000000u, 0xFFFFFFu, sample->sea_distance / CLIMATE_SEA_DISTANCE_MAX);
    case CLIMATE_LAYER_CONTINENTALITY:
        return layer_mix(CLIMATE_LAYER_KAPPA_LOW, CLIMATE_LAYER_KAPPA_HIGH, sample->kappa);
    case CLIMATE_LAYER_WATER:
        if ((sample->flags & CLIMATE_CELL_SEA) != 0u) {
            return CLIMATE_LAYER_SEA;
        }
        if ((sample->flags & CLIMATE_CELL_LAKE) != 0u) {
            return CLIMATE_LAYER_LAKE_LARGE;
        }
        if ((sample->flags & CLIMATE_CELL_LAKE_SMALL) != 0u) {
            return CLIMATE_LAYER_LAKE_SMALL;
        }
        return layer_mix(CLIMATE_LAYER_LAND_LOW, CLIMATE_LAYER_LAND_HIGH, sample->z_ground / CLIMATE_LAYER_LAND_TOP);
    default:
        break;
    }
    return CLIMATE_LAYER_GREY;
}

/* ---- Phase 05: temperature, rain, seasons, classes, snow ----------------------------------------------------------- */

/* A value along stops: linear between the two round it, held past the ends. */
static uint32_t layer_stops(const float32_t *at, const uint32_t *colours, uint32_t count, float32_t value) {
    if (!(value > at[0])) {
        return colours[0]; /* NaN too */
    }
    for (uint32_t k = 1u; k < count; k++) {
        if (value < at[k]) {
            return layer_mix(colours[k - 1u], colours[k], (value - at[k - 1u]) / (at[k] - at[k - 1u]));
        }
    }
    return colours[count - 1u];
}

uint32_t climate_layer_temperature_colour(float32_t c) {
    return layer_stops(CLIMATE_LAYER_TEMPERATURE_C, CLIMATE_LAYER_TEMPERATURE_RGB, CLIMATE_LAYER_TEMPERATURE_STOPS, c);
}

uint32_t climate_layer_rain_colour(float32_t mm_year) {
    return layer_stops(CLIMATE_LAYER_RAIN_MM, CLIMATE_LAYER_RAIN_RGB, CLIMATE_LAYER_RAIN_STOPS, mm_year);
}

/* Phase 03's water layer under a place's normals. */
static uint32_t layer_water_under(const climate_normals_at_t *normals) {
    climate_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.z_ground = normals->z;
    sample.flags = normals->flags;
    return climate_layer_colour(CLIMATE_LAYER_WATER, &sample);
}

uint32_t climate_layer_normals_colour(climate_layer_t layer, const climate_normals_at_t *normals) {
    const bool sea = (normals->flags & CLIMATE_CELL_SEA) != 0u;
    const bool water = (normals->flags & (CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE)) != 0u;
    switch (layer) {
    case CLIMATE_LAYER_TEMPERATURE_DAY:
        return climate_layer_temperature_colour(normals->t_day_c);
    case CLIMATE_LAYER_TEMPERATURE_MEAN:
        return climate_layer_temperature_colour(normals->t_mean_c);
    case CLIMATE_LAYER_TEMPERATURE_WARMEST:
        return climate_layer_temperature_colour(normals->t_max_month_c);
    case CLIMATE_LAYER_TEMPERATURE_COLDEST:
        return climate_layer_temperature_colour(normals->t_min_month_c);
    case CLIMATE_LAYER_RAIN_YEAR:
        return climate_layer_rain_colour(normals->rain_mm_year);
    case CLIMATE_LAYER_RAIN_DAY:
        return climate_layer_rain_colour(normals->rain_mm_day * CLIMATE_DAYS_A_YEAR);
    case CLIMATE_LAYER_HURRICANE_SEASON:
        if (sea && normals->hurricane_s > 0.0f32) {
            return layer_mix(CLIMATE_LAYER_HURRICANE_LOW, CLIMATE_LAYER_HURRICANE_HIGH, normals->hurricane_s);
        }
        if ((normals->flags & CLIMATE_CELL_HURRICANE_COAST) != 0u && normals->hurricane_season != 0u) {
            return CLIMATE_LAYER_HURRICANE_COAST;
        }
        return layer_water_under(normals);
    case CLIMATE_LAYER_MONSOON:
        if (normals->monsoon_today != 0u) {
            return CLIMATE_LAYER_MONSOON_TODAY;
        }
        return (normals->flags & CLIMATE_CELL_MONSOON) != 0u ? CLIMATE_LAYER_MONSOON_LAND : layer_water_under(normals);
    case CLIMATE_LAYER_CLIMATE_CLASS:
        if (normals->climate_class == CLIMATE_CLASS_NONE || normals->climate_class >= CLIMATE_CLASS_COUNT) {
            return layer_water_under(normals);
        }
        return CLIMATE_LAYER_CLASS_RGB[normals->climate_class];
    case CLIMATE_LAYER_SNOW_BALANCE: {
        if (water) {
            return layer_water_under(normals);
        }
        const float32_t below = normals->snow_balance_z - normals->z;
        if (below <= 0.0f32) {
            return CLIMATE_LAYER_SNOW_ABOVE;
        }
        return layer_mix(CLIMATE_LAYER_SNOW_NEAR, CLIMATE_LAYER_SNOW_FAR,
                         (below - CLIMATE_LAYER_SNOW_NEAR_BLOCKS) /
                             (CLIMATE_LAYER_SNOW_FAR_BLOCKS - CLIMATE_LAYER_SNOW_NEAR_BLOCKS));
    }
    default:
        break;
    }
    return CLIMATE_LAYER_GREY;
}

static void layer_put(uint8_t *pixels, uint32_t pitch, uint32_t x, uint32_t y, uint32_t colour) {
    memcpy(pixels + (uint64_t)y * pitch + (uint64_t)x * 4u, &colour, sizeof(colour));
}

/* The latitude lines: a row is coloured where its y range, [north - (row + 1) b, north - row b), holds a y whose phi is
 * the line's value. phi is a triangle wave, phi = 90 y / Y for |y| <= Y, so phi = L at y = L Y / 90 + 4 Y k and at
 * y = 2 Y - L Y / 90 + 4 Y k: every repeat in view. */
static void layer_latitude_lines(int32_t north_y, int32_t blocks_per_pixel, uint32_t width, uint32_t height,
                                 uint8_t *pixels, uint32_t pitch) {
    static const float64_t LINES[5] = {0.0f64, 23.5f64, -23.5f64, 66.5f64, -66.5f64};
    static const uint32_t COLOURS[5] = {CLIMATE_LAYER_EQUATOR, CLIMATE_LAYER_TROPIC, CLIMATE_LAYER_TROPIC,
                                        CLIMATE_LAYER_POLAR, CLIMATE_LAYER_POLAR};
    const float64_t pole = CLIMATE_POLE_Y;
    const float64_t period = CLIMATE_LATITUDE_PERIOD;
    const float64_t step = (float64_t)blocks_per_pixel;
    const float64_t top = (float64_t)north_y;
    const float64_t bottom = top - step * (float64_t)height;
    for (uint32_t line = 0u; line < 5u; line++) {
        const float64_t base = LINES[line] * pole / 90.0f64;
        const float64_t family[2] = {base, 2.0f64 * pole - base};
        for (uint32_t f = 0u; f < 2u; f++) {
            const float64_t k_first = ceil((bottom - family[f]) / period);
            for (float64_t k = k_first;; k += 1.0f64) {
                const float64_t y = family[f] + k * period;
                if (y >= top) {
                    break;
                }
                if (y < bottom) {
                    continue;
                }
                const float64_t row = ceil((top - y) / step) - 1.0f64;
                if (row < 0.0f64 || row >= (float64_t)height) {
                    continue;
                }
                for (uint32_t x = 0u; x < width; x++) {
                    layer_put(pixels, pitch, x, (uint32_t)row, COLOURS[line]);
                }
            }
        }
    }
}

/* ---- Phase 04: currents, pressure, wind ------------------------------------------------------------------------- */

uint32_t climate_layer_pressure_colour(float32_t hpa) {
    if (hpa < CLIMATE_LAYER_PRESSURE_MID_HPA) {
        return layer_mix(CLIMATE_LAYER_PRESSURE_LOW, CLIMATE_LAYER_PRESSURE_MID,
                         (hpa - CLIMATE_LAYER_PRESSURE_LOW_HPA) /
                             (CLIMATE_LAYER_PRESSURE_MID_HPA - CLIMATE_LAYER_PRESSURE_LOW_HPA));
    }
    return layer_mix(CLIMATE_LAYER_PRESSURE_MID, CLIMATE_LAYER_PRESSURE_HIGH,
                     (hpa - CLIMATE_LAYER_PRESSURE_MID_HPA) /
                         (CLIMATE_LAYER_PRESSURE_HIGH_HPA - CLIMATE_LAYER_PRESSURE_MID_HPA));
}

uint32_t climate_layer_wind_colour(float32_t speed) {
    return layer_mix(CLIMATE_LAYER_WIND_CALM, CLIMATE_LAYER_WIND_FAST, speed / CLIMATE_LAYER_WIND_TOP);
}

static float32_t layer_get_float(const uint8_t *pixels, uint32_t pitch, uint32_t x, uint32_t y) {
    float32_t value = 0.0f32;
    memcpy(&value, pixels + (uint64_t)y * pitch + (uint64_t)x * 4u, sizeof(value));
    return value;
}

void climate_layer_pressure_finish(uint32_t width, uint32_t height, uint8_t *pixels, uint32_t pitch) {
    /* From the south-east corner back, so the pixels north and west of each are still numbers when it is coloured. */
    for (uint32_t r = height; r-- > 0u;) {
        for (uint32_t c = width; c-- > 0u;) {
            const float32_t p = layer_get_float(pixels, pitch, c, r);
            uint32_t colour = CLIMATE_LAYER_GREY;
            if (!isnan(p)) {
                const float32_t band = floorf(p / CLIMATE_LAYER_PRESSURE_LINE_HPA);
                const float32_t north = r > 0u ? layer_get_float(pixels, pitch, c, r - 1u) : p;
                const float32_t west = c > 0u ? layer_get_float(pixels, pitch, c - 1u, r) : p;
                const bool line = (!isnan(north) && floorf(north / CLIMATE_LAYER_PRESSURE_LINE_HPA) != band) ||
                                  (!isnan(west) && floorf(west / CLIMATE_LAYER_PRESSURE_LINE_HPA) != band);
                colour = line ? CLIMATE_LAYER_PRESSURE_LINE : climate_layer_pressure_colour(p);
            }
            layer_put(pixels, pitch, c, r, colour);
        }
    }
}

/* Bresenham's line from (x0, y0) to (x1, y1), both ends included, pixels outside the picture skipped. */
static void layer_line(uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint32_t colour) {
    const int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0; /* minus the height */
    const int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && (uint32_t)x0 < width && (uint32_t)y0 < height) {
            layer_put(pixels, pitch, (uint32_t)x0, (uint32_t)y0, colour);
        }
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int32_t twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

/* An arrow from a pixel, length pixels along (u, v) with v north, so drawn upward; two 3-pixel barbs at its head. */
static void layer_arrow(uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, uint32_t column, uint32_t row,
                        float32_t u, float32_t v, float32_t length, uint32_t colour) {
    const float32_t size = sqrtf(u * u + v * v);
    if (size < 1.0e-6f32 || length < 1.0f32) {
        return;
    }
    const float32_t dx = u / size, dy = -v / size; /* rows go south */
    const int32_t x0 = (int32_t)column, y0 = (int32_t)row;
    const float32_t head_x = (float32_t)x0 + length * dx, head_y = (float32_t)y0 + length * dy;
    const int32_t x1 = (int32_t)lroundf(head_x), y1 = (int32_t)lroundf(head_y);
    layer_line(pixels, pitch, width, height, x0, y0, x1, y1, colour);
    /* The barbs: the direction turned 150 degrees each way. */
    const float32_t barb_cos = -0.8660254f32, barb_sin = 0.5f32, barb = 3.0f32;
    for (int32_t side = -1; side <= 1; side += 2) {
        const float32_t s = barb_sin * (float32_t)side;
        const float32_t bx = dx * barb_cos - dy * s, by = dx * s + dy * barb_cos;
        layer_line(pixels, pitch, width, height, x1, y1, (int32_t)lroundf(head_x + barb * bx),
                   (int32_t)lroundf(head_y + barb * by), colour);
    }
}

/* Where a picture is: its north-west corner, blocks a pixel and size. */
typedef struct layer_view_t {
    float64_t west_x;  /* 0 */
    float64_t north_y; /* 8 */
    float64_t step;    /* 16: blocks a pixel */
    uint32_t width;    /* 24 */
    uint32_t height;   /* 28 */
} layer_view_t;
static_assert(sizeof(layer_view_t) == 32);

static float64_t layer_view_x(const layer_view_t *view, uint32_t column) {
    return view->west_x + ((float64_t)column + 0.5f64) * view->step;
}
static float64_t layer_view_y(const layer_view_t *view, uint32_t row) {
    return view->north_y - ((float64_t)row + 0.5f64) * view->step;
}

/* An arrow every 32 pixels, from pixel 16, on each sea cell with a current: along its flow, 4 + 12 x strength / 255 long. */
static void layer_current_arrows(const climate_t *climate, const layer_view_t *view, uint8_t *pixels, uint32_t pitch) {
    const uint32_t spacing = CLIMATE_LAYER_ARROW_SPACING;
    for (uint32_t row = spacing / 2u; row < view->height; row += spacing) {
        for (uint32_t column = spacing / 2u; column < view->width; column += spacing) {
            const float64_t x = floor(layer_view_x(view, column)), y = floor(layer_view_y(view, row));
            if (x < (float64_t)INT32_MIN || x > (float64_t)INT32_MAX || y < (float64_t)INT32_MIN ||
                y > (float64_t)INT32_MAX) {
                continue;
            }
            climate_cell_t cell;
            if (!climate_cell_get(climate, (int32_t)x, (int32_t)y, &cell) || (cell.flags & CLIMATE_CELL_SEA) == 0u) {
                continue;
            }
            if (cell.current == CLIMATE_CURRENT_NONE) {
                if (cell.land_distance <= CLIMATE_CURRENT_REACH) {
                    /* Neutral, the third kind: no flow to draw, so a small grey cross. */
                    layer_line(pixels, pitch, view->width, view->height, (int32_t)column - 1, (int32_t)row,
                               (int32_t)column + 1, (int32_t)row, CLIMATE_LAYER_CURRENT_NEUTRAL);
                    layer_line(pixels, pitch, view->width, view->height, (int32_t)column, (int32_t)row - 1,
                               (int32_t)column, (int32_t)row + 1, CLIMATE_LAYER_CURRENT_NEUTRAL);
                }
                continue;
            }
            const float64_t centre_y = floor(y / (float64_t)CLIMATE_CELL_BLOCKS) * (float64_t)CLIMATE_CELL_BLOCKS +
                                       0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS;
            float32_t flow[2];
            climate_current_flow(&cell, (float32_t)centre_y, flow);
            const float32_t length = 4.0f32 + 12.0f32 * (float32_t)cell.current_strength / 255.0f32;
            const uint32_t colour =
                cell.current == CLIMATE_CURRENT_WARM ? CLIMATE_LAYER_CURRENT_WARM : CLIMATE_LAYER_CURRENT_COLD;
            layer_arrow(pixels, pitch, view->width, view->height, column, row, flow[0], flow[1], length, colour);
        }
    }
}

/* An arrow every 32 pixels, from pixel 16, pointing downwind, 2 x speed long, at most 30. */
static void layer_wind_arrows(const climate_t *climate, const layer_view_t *view, float32_t day, uint8_t *pixels,
                              uint32_t pitch) {
    const uint32_t spacing = CLIMATE_LAYER_ARROW_SPACING;
    for (uint32_t row = spacing / 2u; row < view->height; row += spacing) {
        for (uint32_t column = spacing / 2u; column < view->width; column += spacing) {
            climate_air_t air;
            if (!climate_air_at(climate, layer_view_x(view, column), layer_view_y(view, row), day, &air)) {
                continue;
            }
            const float32_t speed = sqrtf(air.wind[0] * air.wind[0] + air.wind[1] * air.wind[1]);
            layer_arrow(pixels, pitch, view->width, view->height, column, row, air.wind[0], air.wind[1],
                        fminf(CLIMATE_LAYER_WIND_ARROW_MAX, 2.0f32 * speed), CLIMATE_LAYER_WIND_ARROW);
        }
    }
}

void climate_layer_draw(const climate_t *climate, climate_layer_t layer, int32_t west_x, int32_t north_y,
                        int32_t blocks_per_pixel, uint32_t width, uint32_t height, uint32_t day, uint8_t *pixels,
                        uint32_t pitch) {
    if (layer == CLIMATE_LAYER_LATITUDE) {
        layer_latitude_lines(north_y, blocks_per_pixel, width, height, pixels, pitch);
        return;
    }
    const layer_view_t view = {
        .west_x = (float64_t)west_x,
        .north_y = (float64_t)north_y,
        .step = (float64_t)blocks_per_pixel,
        .width = width,
        .height = height,
    };
    const float32_t on_day = (float32_t)day; /* phase 03's layers do not change through the year */
    for (uint32_t row = 0u; row < height; row++) {
        const float64_t y = layer_view_y(&view, row);
        for (uint32_t column = 0u; column < width; column++) {
            const float64_t x = layer_view_x(&view, column);
            if (layer >= CLIMATE_LAYER_TEMPERATURE_DAY && layer < CLIMATE_LAYER_COUNT) {
                climate_normals_at_t normals;
                const bool built = climate_normals_at(climate, x, y, nanf(""), on_day, &normals);
                layer_put(pixels, pitch, column, row,
                          built ? climate_layer_normals_colour(layer, &normals) : CLIMATE_LAYER_GREY);
                continue;
            }
            if (layer == CLIMATE_LAYER_PRESSURE || layer == CLIMATE_LAYER_WIND) {
                climate_air_t air;
                const bool built = climate_air_at(climate, x, y, on_day, &air);
                if (layer == CLIMATE_LAYER_PRESSURE) {
                    /* The number itself for now; the second pass colours it and draws the lines. */
                    const float32_t hpa = built ? air.pressure_hpa : nanf("");
                    memcpy(pixels + (uint64_t)row * pitch + (uint64_t)column * 4u, &hpa, sizeof(hpa));
                } else {
                    const float32_t speed =
                        built ? sqrtf(air.wind[0] * air.wind[0] + air.wind[1] * air.wind[1]) : 0.0f32;
                    layer_put(pixels, pitch, column, row,
                              built ? climate_layer_wind_colour(speed) : CLIMATE_LAYER_GREY);
                }
                continue;
            }
            climate_sample_t sample;
            const climate_layer_t base = layer == CLIMATE_LAYER_CURRENTS ? CLIMATE_LAYER_WATER : layer;
            const uint32_t colour =
                climate_sample_at(climate, x, y, &sample) ? climate_layer_colour(base, &sample) : CLIMATE_LAYER_GREY;
            layer_put(pixels, pitch, column, row, colour);
        }
    }
    if (layer == CLIMATE_LAYER_PRESSURE) {
        climate_layer_pressure_finish(width, height, pixels, pitch);
    } else if (layer == CLIMATE_LAYER_CURRENTS) {
        layer_current_arrows(climate, &view, pixels, pitch);
    } else if (layer == CLIMATE_LAYER_WIND) {
        layer_wind_arrows(climate, &view, on_day, pixels, pitch);
    }
}
