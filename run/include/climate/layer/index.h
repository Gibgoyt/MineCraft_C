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
    /* Phase 04: each drawn for the day it is given, the Atlas's day slider. */
    CLIMATE_LAYER_CURRENTS = 4, /* the water layer, with an arrow along each current */
    CLIMATE_LAYER_PRESSURE = 5, /* p_belt + dp_heat as colour, a line every 4 hPa */
    CLIMATE_LAYER_WIND = 6,     /* speed as colour, an arrow downwind */
    CLIMATE_LAYER_COUNT = 7,
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
    /* Phase 04 */
    CLIMATE_LAYER_CURRENT_WARM = 0xE03030u,
    CLIMATE_LAYER_CURRENT_COLD = 0x3060E0u,
    CLIMATE_LAYER_CURRENT_NEUTRAL = 0x909090u, /* a mark on sea within 600 blocks of land whose current is neutral */
    CLIMATE_LAYER_PRESSURE_LOW = 0x3050C0u,    /* 990 hPa and below */
    CLIMATE_LAYER_PRESSURE_MID = 0xF0F0F0u,    /* 1013 */
    CLIMATE_LAYER_PRESSURE_HIGH = 0xC04030u,   /* 1030 and above */
    CLIMATE_LAYER_PRESSURE_LINE = 0x202020u,   /* where pressure crosses a multiple of 4 hPa */
    CLIMATE_LAYER_WIND_CALM = 0x202040u,       /* 0 blocks a second */
    CLIMATE_LAYER_WIND_FAST = 0xF0E040u,       /* 15 and over */
    CLIMATE_LAYER_WIND_ARROW = 0xFFFFFFu,
    CLIMATE_LAYER_ARROW_SPACING = 32u, /* pixels between arrows, the first at 16 */
};
static const float32_t CLIMATE_LAYER_PRESSURE_LOW_HPA = 990.0f32;
static const float32_t CLIMATE_LAYER_PRESSURE_MID_HPA = 1013.0f32;
static const float32_t CLIMATE_LAYER_PRESSURE_HIGH_HPA = 1030.0f32;
static const float32_t CLIMATE_LAYER_PRESSURE_LINE_HPA = 4.0f32; /* master plan 9.3: lines every 4 hPa */
static const float32_t CLIMATE_LAYER_WIND_TOP = 15.0f32;         /* blocks a second, drawn brightest */
static const float32_t CLIMATE_LAYER_WIND_ARROW_MAX = 30.0f32;   /* pixels: an arrow is 2 x speed, at most this */
static const float32_t CLIMATE_LAYER_LAND_TOP = 454.0f32;        /* blocks: z_ground drawn lightest from here up */

/* width by height XRGB8888 pixels, rows pitch bytes apart, pixel (0, 0) at the north-west corner, blocks_per_pixel apart,
 * each pixel the sample at its centre (x east, y north, so rows go south). Grey where a tile is not built. day (0 to 364) is
 * the day the currents, pressure and wind layers are drawn for. Any thread. */
void climate_layer_draw(const climate_t *climate, climate_layer_t layer, int32_t west_x, int32_t north_y,
                        int32_t blocks_per_pixel, uint32_t width, uint32_t height, uint32_t day, uint8_t *pixels,
                        uint32_t pitch);

/* The colour one sample gives in a layer of phase 03's other than latitude; grey for phase 04's, which need the air. */
uint32_t climate_layer_colour(climate_layer_t layer, const climate_sample_t *sample);

/* Pressure in hPa to its colour: linear from 990 to 1013 and from 1013 to 1030, held past each end. */
uint32_t climate_layer_pressure_colour(float32_t hpa);
/* Wind speed in blocks a second to its colour: linear from 0 to 15, held past it. */
uint32_t climate_layer_wind_colour(float32_t speed);
/* The pressure layer's second pass: every pixel holds a float32_t in hPa (NaN where no tile is built) on the way in, and its
 * colour on the way out, a line (CLIMATE_LAYER_PRESSURE_LINE) where floor(p / 4) differs from the pixel north or west of it.
 * Pure: no allocation, as the picture itself holds the numbers. */
void climate_layer_pressure_finish(uint32_t width, uint32_t height, uint8_t *pixels, uint32_t pitch);

#endif
