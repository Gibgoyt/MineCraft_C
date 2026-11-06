/* See run/src/climate/layer/README.md. */
#ifndef MC_RUN_CLIMATE_LAYER_INDEX_H
#define MC_RUN_CLIMATE_LAYER_INDEX_H

#include "climate/index.h"
#include "types/index.h"

#include <stdint.h>

typedef enum climate_layer_t : uint32_t {
    CLIMATE_LAYER_SEA_DISTANCE = 0,
    CLIMATE_LAYER_CONTINENTALITY = 1,
    CLIMATE_LAYER_WATER = 2,    /* sea, large lakes, small lakes, land shaded by z_ground */
    CLIMATE_LAYER_LATITUDE = 3, /* drawn over another layer: only its lines' pixels change */
    CLIMATE_LAYER_COUNT_PHASE_03 = 4,
} climate_layer_t;

/* The colours the README gives, 0x00RRGGBB. */
enum : uint32_t {
    CLIMATE_LAYER_GREY = 0x808080u, /* a tile not built */
    CLIMATE_LAYER_KAPPA_LOW = 0x2060C0u,
    CLIMATE_LAYER_KAPPA_HIGH = 0xA06030u,
    CLIMATE_LAYER_SEA = 0x103070u,
    CLIMATE_LAYER_LAKE_LARGE = 0x3080E0u,
    CLIMATE_LAYER_LAKE_SMALL = 0x70B0F0u,
    CLIMATE_LAYER_LAND_LOW = 0x406040u,
    CLIMATE_LAYER_LAND_HIGH = 0xF0F0F0u,
    CLIMATE_LAYER_EQUATOR = 0xFF2020u,
    CLIMATE_LAYER_TROPIC = 0xFF9020u,
    CLIMATE_LAYER_POLAR = 0x20E0FFu,
};
static const float32_t CLIMATE_LAYER_LAND_TOP = 454.0f32; /* blocks: z_ground drawn lightest from here up */

/* width by height XRGB8888 pixels, rows pitch bytes apart, pixel (0, 0) at the north-west corner, blocks_per_pixel apart,
 * each pixel the sample at its centre (x east, y north, so rows go south). Grey where a tile is not built. day is for later
 * layers. Any thread. */
void climate_layer_draw(const climate_t *climate, climate_layer_t layer, int32_t west_x, int32_t north_y,
                        int32_t blocks_per_pixel, uint32_t width, uint32_t height, uint32_t day, uint8_t *pixels,
                        uint32_t pitch);

/* The colour one sample gives in a layer other than latitude. */
uint32_t climate_layer_colour(climate_layer_t layer, const climate_sample_t *sample);

#endif
