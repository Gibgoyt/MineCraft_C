/* See README.md. Map layers to pixels: the phase 03 file's "Layer colours". The Atlas (phase 09) and the map tools draw with
 * this same code. */
#include "climate/layer/index.h"

#include "climate/systems/field/index.h"

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

/* ---- Phase 06: the "now" layers ------------------------------------------------------------------------------------ */

/* Where a mark may go: always, only over grey (a tile not built), or only off it. */
typedef enum layer_mode_t : uint8_t {
    LAYER_ALWAYS = 0,
    LAYER_ON_GREY = 1,
    LAYER_OFF_GREY = 2,
} layer_mode_t;

static void layer_put_mode(uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, int32_t x, int32_t y,
                           uint32_t colour, layer_mode_t mode) {
    if (x < 0 || y < 0 || (uint32_t)x >= width || (uint32_t)y >= height) {
        return;
    }
    uint32_t now = 0u;
    uint8_t *const at = pixels + (uint64_t)(uint32_t)y * pitch + (uint64_t)(uint32_t)x * 4u;
    memcpy(&now, at, sizeof(now));
    const bool grey = now == CLIMATE_LAYER_GREY;
    if ((mode == LAYER_ON_GREY && !grey) || (mode == LAYER_OFF_GREY && grey)) {
        return;
    }
    memcpy(at, &colour, sizeof(colour));
}

static void layer_line_mode(uint8_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, int32_t x0, int32_t y0,
                            int32_t x1, int32_t y1, uint32_t colour, layer_mode_t mode) {
    const int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        layer_put_mode(pixels, pitch, width, height, x0, y0, colour, mode);
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

/* A block position to its pixel, clamped far outside the picture so the integer math stays in range. */
static int32_t layer_pixel(float64_t offset, float64_t step) {
    float64_t p = floor(offset / step);
    p = p < -1.0e6f64 ? -1.0e6f64 : (p > 1.0e6f64 ? 1.0e6f64 : p);
    return (int32_t)p;
}
static int32_t layer_column_of(const layer_view_t *view, float64_t x) {
    return layer_pixel(x - view->west_x, view->step);
}
static int32_t layer_row_of(const layer_view_t *view, float64_t y) {
    return layer_pixel(view->north_y - y, view->step);
}

/* A circle by the midpoint rule, radius in pixels (at least 1). */
static void layer_circle(uint8_t *pixels, uint32_t pitch, const layer_view_t *view, int32_t cx, int32_t cy,
                         int32_t radius, uint32_t colour, layer_mode_t mode) {
    radius = radius < 1 ? 1 : radius;
    if (cx + radius < 0 || cy + radius < 0 || cx - radius >= (int32_t)view->width ||
        cy - radius >= (int32_t)view->height) {
        return;
    }
    int32_t x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        const int32_t points[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
        for (uint32_t k = 0u; k < 8u; k++) {
            layer_put_mode(pixels, pitch, view->width, view->height, cx + points[k][0], cy + points[k][1], colour,
                           mode);
        }
        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

/* A filled disc, radius in pixels (at least 1). */
static void layer_disc(uint8_t *pixels, uint32_t pitch, const layer_view_t *view, int32_t cx, int32_t cy,
                       int32_t radius, uint32_t colour, layer_mode_t mode) {
    radius = radius < 1 ? 1 : radius;
    const int32_t y0 = cy - radius < 0 ? 0 : cy - radius;
    const int32_t y1 = cy + radius >= (int32_t)view->height ? (int32_t)view->height - 1 : cy + radius;
    const int32_t x0 = cx - radius < 0 ? 0 : cx - radius;
    const int32_t x1 = cx + radius >= (int32_t)view->width ? (int32_t)view->width - 1 : cx + radius;
    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t x = x0; x <= x1; x++) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius) {
                layer_put_mode(pixels, pitch, view->width, view->height, x, y, colour, mode);
            }
        }
    }
}

/* The 5 by 7 H and L, a row a byte, bit 4 the left column: the climate cannot use ui/font, which ranks above it. */
static const uint8_t LAYER_MARK_H[CLIMATE_LAYER_MARK_HEIGHT] = {0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
static const uint8_t LAYER_MARK_L[CLIMATE_LAYER_MARK_HEIGHT] = {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu};

/* A mark centred on (cx, cy): its middle pixel, column 2 and row 3, is there. */
static void layer_mark(uint8_t *pixels, uint32_t pitch, const layer_view_t *view, int32_t cx, int32_t cy, bool high) {
    const uint8_t *const rows = high ? LAYER_MARK_H : LAYER_MARK_L;
    const uint32_t colour = high ? CLIMATE_LAYER_HIGH_MARK : CLIMATE_LAYER_LOW_MARK;
    for (int32_t r = 0; r < (int32_t)CLIMATE_LAYER_MARK_HEIGHT; r++) {
        for (int32_t c = 0; c < (int32_t)CLIMATE_LAYER_MARK_WIDTH; c++) {
            if (((rows[r] >> (4 - c)) & 1u) != 0u) {
                layer_put_mode(pixels, pitch, view->width, view->height, cx - 2 + c, cy - 3 + r, colour,
                               LAYER_OFF_GREY);
            }
        }
    }
}

static uint32_t layer_ring_colour(uint8_t kind) {
    return kind == CLIMATE_SYSTEM_HURRICANE
               ? CLIMATE_LAYER_HURRICANE_RING
               : (kind == CLIMATE_SYSTEM_LOW ? CLIMATE_LAYER_LOW_RING : CLIMATE_LAYER_HIGH_RING);
}

/* The track behind a system: its samples from its spawn up to t, then where it is at t. */
static void layer_track(uint8_t *pixels, uint32_t pitch, const layer_view_t *view, const climate_system_t *system,
                        const climate_system_state_t *state, float64_t t) {
    int32_t px = layer_column_of(view, system->samples[0].x), py = layer_row_of(view, system->samples[0].y);
    for (uint32_t k = 1u; k < system->sample_count; k++) {
        if (system->spawn_day + CLIMATE_TRACK_STEP_DAYS * (float64_t)k > t) {
            break;
        }
        const int32_t nx = layer_column_of(view, system->samples[k].x), ny = layer_row_of(view, system->samples[k].y);
        layer_line_mode(pixels, pitch, view->width, view->height, px, py, nx, ny, CLIMATE_LAYER_TRACK, LAYER_ALWAYS);
        px = nx;
        py = ny;
    }
    layer_line_mode(pixels, pitch, view->width, view->height, px, py, layer_column_of(view, state->x),
                    layer_row_of(view, state->y), CLIMATE_LAYER_TRACK, LAYER_ALWAYS);
}

/* A low's cold front: the line, and a 3-pixel tooth every 16 pixels along it on its eastern side. */
static void layer_front(uint8_t *pixels, uint32_t pitch, const layer_view_t *view, const climate_system_t *low,
                        float64_t t) {
    float64_t from[2], to[2];
    if (!climate_systems_front(low, t, from, to)) {
        return;
    }
    const int32_t x0 = layer_column_of(view, from[0]), y0 = layer_row_of(view, from[1]);
    const int32_t x1 = layer_column_of(view, to[0]), y1 = layer_row_of(view, to[1]);
    layer_line_mode(pixels, pitch, view->width, view->height, x0, y0, x1, y1, CLIMATE_LAYER_FRONT, LAYER_OFF_GREY);
    const float32_t dx = (float32_t)(x1 - x0), dy = (float32_t)(y1 - y0);
    const float32_t length = sqrtf(dx * dx + dy * dy);
    if (length < 1.0f32) {
        return;
    }
    /* The normal pointing east (+column): a tooth sticks out toward the warm air ahead of the front. */
    float32_t nx = -dy / length, ny = dx / length;
    if (nx < 0.0f32) {
        nx = -nx;
        ny = -ny;
    }
    for (float32_t s = (float32_t)CLIMATE_LAYER_FRONT_TOOTH_SPACING; s < length;
         s += (float32_t)CLIMATE_LAYER_FRONT_TOOTH_SPACING) {
        const float32_t bx = (float32_t)x0 + dx * s / length, by = (float32_t)y0 + dy * s / length;
        layer_line_mode(pixels, pitch, view->width, view->height, (int32_t)lroundf(bx), (int32_t)lroundf(by),
                        (int32_t)lroundf(bx + 3.0f32 * nx), (int32_t)lroundf(by + 3.0f32 * ny), CLIMATE_LAYER_FRONT,
                        LAYER_OFF_GREY);
    }
}

/* Each alive big system's circle at R; with full, the systems layer's eye, track and front too. */
static void layer_systems(const climate_systems_day_t *day, const layer_view_t *view, float64_t t, bool full,
                          uint8_t *pixels, uint32_t pitch) {
    for (uint32_t i = 0u; i < day->system_count; i++) {
        const climate_system_t *const system = &day->borrowed_systems[i];
        climate_system_state_t state;
        climate_system_state_at(system, t, &state);
        if (state.alive == 0u) {
            continue;
        }
        const int32_t cx = layer_column_of(view, state.x), cy = layer_row_of(view, state.y);
        const int32_t radius = (int32_t)lround((float64_t)system->radius / view->step);
        const uint32_t ring = layer_ring_colour(system->kind);
        if (!full) {
            layer_circle(pixels, pitch, view, cx, cy, radius, ring, LAYER_ON_GREY);
            continue;
        }
        layer_circle(pixels, pitch, view, cx, cy, radius, ring, LAYER_ALWAYS);
        if (system->kind == CLIMATE_SYSTEM_HURRICANE) {
            const int32_t eye = (int32_t)lround((float64_t)system->core_radius / view->step);
            layer_disc(pixels, pitch, view, cx, cy, eye, ring, LAYER_OFF_GREY);
        }
        if (system->kind == CLIMATE_SYSTEM_LOW) {
            layer_front(pixels, pitch, view, system, t);
        }
        layer_track(pixels, pitch, view, system, &state, t);
    }
}

bool climate_layer_radar_colour(float32_t mm_hour, uint32_t *out) {
    if (!(mm_hour >= CLIMATE_LAYER_RADAR_MM_HOUR[0])) {
        return false;
    }
    uint32_t k = 0u;
    while (k + 1u < CLIMATE_LAYER_RADAR_STOPS && mm_hour >= CLIMATE_LAYER_RADAR_MM_HOUR[k + 1u]) {
        k++;
    }
    *out = CLIMATE_LAYER_RADAR_RGB[k];
    return true;
}

bool climate_layer_rain_now(const climate_t *climate, const climate_systems_day_t *day, float64_t x, float64_t y,
                            float64_t t, float32_t *out) {
    climate_normals_at_t normals;
    if (!climate_normals_at(climate, x, y, nanf(""), (float32_t)climate_systems_day_of_year(t), &normals)) {
        return false;
    }
    const float32_t share = climate_systems_rain_share_today(
        (float32_t)climate_latitude_deg(y), (float32_t)climate_systems_day_of_year(t), normals.t_day_c);
    const float32_t background = normals.rain_mm_day / 24.0f32 * (1.0f32 - share);
    const float32_t systems = climate_systems_rain_at(day, x, y, t) * climate_systems_wetness(normals.rain_mm_day);
    *out = (background + systems) * climate_systems_rain_factor(day, x, y, t);
    return true;
}

/* The wind now: phase 04's plus each system's. */
static bool layer_wind_now(const climate_t *climate, const climate_systems_day_t *day, float64_t x, float64_t y,
                           float64_t t, float32_t out[2]) {
    climate_air_t air;
    if (!climate_air_at(climate, x, y, (float32_t)climate_systems_day_of_year(t), &air)) {
        return false;
    }
    float32_t systems[2];
    climate_systems_wind_at(day, x, y, t, systems);
    out[0] = air.wind[0] + systems[0];
    out[1] = air.wind[1] + systems[1];
    return true;
}

static uint32_t layer_water_at(const climate_t *climate, float64_t x, float64_t y) {
    climate_sample_t sample;
    return climate_sample_at(climate, x, y, &sample) ? climate_layer_colour(CLIMATE_LAYER_WATER, &sample)
                                                     : CLIMATE_LAYER_GREY;
}

/* One pixel of a now layer's first pass. The pressure layer writes its hPa as a float, NaN where unbuilt. */
static void layer_now_pixel(const climate_t *climate, const climate_systems_day_t *day, climate_layer_t layer,
                            float64_t t, float64_t x, float64_t y, uint8_t *at) {
    uint32_t colour = CLIMATE_LAYER_GREY;
    switch (layer) {
    case CLIMATE_LAYER_PRESSURE_NOW: {
        climate_air_t air;
        float32_t hpa = nanf("");
        if (climate_air_at(climate, x, y, (float32_t)climate_systems_day_of_year(t), &air)) {
            hpa = air.pressure_hpa + climate_systems_pressure_at(day, x, y, t);
        }
        memcpy(at, &hpa, sizeof(hpa));
        return;
    }
    case CLIMATE_LAYER_RAIN_RADAR: {
        float32_t rain = 0.0f32;
        if (climate_layer_rain_now(climate, day, x, y, t, &rain) && !climate_layer_radar_colour(rain, &colour)) {
            colour = layer_water_at(climate, x, y);
        }
        break;
    }
    case CLIMATE_LAYER_WIND_NOW: {
        float32_t wind[2];
        if (layer_wind_now(climate, day, x, y, t, wind)) {
            colour = climate_layer_wind_colour(sqrtf(wind[0] * wind[0] + wind[1] * wind[1]));
        }
        break;
    }
    default: /* the systems and thunder layers: the water layer underneath */
        colour = layer_water_at(climate, x, y);
        break;
    }
    memcpy(at, &colour, sizeof(colour));
}

/* The thunder layer's cells: a disc at R and the lightning mark, a 3-segment zigzag, at the centre. */
static void layer_thunder(const climate_systems_day_t *day, const layer_view_t *view, float64_t t, uint8_t *pixels,
                          uint32_t pitch) {
    static const int32_t ZIGZAG[4][2] = {{-1, -3}, {1, -1}, {-1, 1}, {1, 3}};
    for (uint32_t i = 0u; i < day->thunder_count; i++) {
        const climate_thunder_t *const cell = &day->borrowed_thunder[i];
        float64_t centre[2], share = 0.0f64;
        if (!climate_systems_thunder_at(cell, t, centre, &share)) {
            continue;
        }
        const int32_t cx = layer_column_of(view, centre[0]), cy = layer_row_of(view, centre[1]);
        layer_disc(pixels, pitch, view, cx, cy, (int32_t)lround((float64_t)cell->radius / view->step),
                   CLIMATE_LAYER_THUNDER_DISC, LAYER_OFF_GREY);
        for (uint32_t k = 0u; k < 3u; k++) {
            layer_line_mode(pixels, pitch, view->width, view->height, cx + ZIGZAG[k][0], cy + ZIGZAG[k][1],
                            cx + ZIGZAG[k + 1u][0], cy + ZIGZAG[k + 1u][1], CLIMATE_LAYER_LIGHTNING, LAYER_OFF_GREY);
        }
    }
}

void climate_layer_draw_now(const climate_t *climate, const climate_systems_day_t *day, climate_layer_t layer,
                            float64_t t, int32_t west_x, int32_t north_y, int32_t blocks_per_pixel, uint32_t width,
                            uint32_t height, uint8_t *pixels, uint32_t pitch) {
    const layer_view_t view = {
        .west_x = (float64_t)west_x,
        .north_y = (float64_t)north_y,
        .step = (float64_t)blocks_per_pixel,
        .width = width,
        .height = height,
    };
    for (uint32_t row = 0u; row < height; row++) {
        const float64_t y = layer_view_y(&view, row);
        for (uint32_t column = 0u; column < width; column++) {
            layer_now_pixel(climate, day, layer, t, layer_view_x(&view, column), y,
                            pixels + (uint64_t)row * pitch + (uint64_t)column * 4u);
        }
    }
    if (layer == CLIMATE_LAYER_PRESSURE_NOW) {
        climate_layer_pressure_finish(width, height, pixels, pitch);
    }
    if (layer == CLIMATE_LAYER_WIND_NOW) {
        const uint32_t spacing = CLIMATE_LAYER_ARROW_SPACING;
        for (uint32_t row = spacing / 2u; row < height; row += spacing) {
            for (uint32_t column = spacing / 2u; column < width; column += spacing) {
                float32_t wind[2];
                if (!layer_wind_now(climate, day, layer_view_x(&view, column), layer_view_y(&view, row), t, wind)) {
                    continue;
                }
                const float32_t speed = sqrtf(wind[0] * wind[0] + wind[1] * wind[1]);
                layer_arrow(pixels, pitch, width, height, column, row, wind[0], wind[1],
                            fminf(CLIMATE_LAYER_WIND_ARROW_MAX, 2.0f32 * speed), CLIMATE_LAYER_WIND_ARROW);
            }
        }
    }
    if (layer == CLIMATE_LAYER_THUNDER) {
        layer_thunder(day, &view, t, pixels, pitch);
    }
    /* Systems over grey as outlines, in every now layer; the systems layer draws them whole. */
    layer_systems(day, &view, t, layer == CLIMATE_LAYER_SYSTEMS, pixels, pitch);
    if (layer == CLIMATE_LAYER_PRESSURE_NOW) {
        for (uint32_t i = 0u; i < day->system_count; i++) {
            const climate_system_t *const system = &day->borrowed_systems[i];
            climate_system_state_t state;
            climate_system_state_at(system, t, &state);
            if (state.alive != 0u) {
                layer_mark(pixels, pitch, &view, layer_column_of(&view, state.x), layer_row_of(&view, state.y),
                           system->kind == CLIMATE_SYSTEM_HIGH);
            }
        }
    }
}
