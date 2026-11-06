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
    case CLIMATE_LAYER_LATITUDE:
    case CLIMATE_LAYER_COUNT_PHASE_03:
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

void climate_layer_draw(const climate_t *climate, climate_layer_t layer, int32_t west_x, int32_t north_y,
                        int32_t blocks_per_pixel, uint32_t width, uint32_t height, uint32_t day, uint8_t *pixels,
                        uint32_t pitch) {
    (void)day; /* phase 03's layers do not change through the year */
    if (layer == CLIMATE_LAYER_LATITUDE) {
        layer_latitude_lines(north_y, blocks_per_pixel, width, height, pixels, pitch);
        return;
    }
    const float64_t step = (float64_t)blocks_per_pixel;
    for (uint32_t row = 0u; row < height; row++) {
        const float64_t y = (float64_t)north_y - ((float64_t)row + 0.5f64) * step;
        for (uint32_t column = 0u; column < width; column++) {
            const float64_t x = (float64_t)west_x + ((float64_t)column + 0.5f64) * step;
            climate_sample_t sample;
            const uint32_t colour =
                climate_sample_at(climate, x, y, &sample) ? climate_layer_colour(layer, &sample) : CLIMATE_LAYER_GREY;
            layer_put(pixels, pitch, column, row, colour);
        }
    }
}
