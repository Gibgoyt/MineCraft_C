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
    /* Phase 05: from climate_normals_at at the ground; the day ones drawn for the day given. */
    CLIMATE_LAYER_TEMPERATURE_DAY = 7,      /* the normal daily mean on the day */
    CLIMATE_LAYER_TEMPERATURE_MEAN = 8,     /* the yearly mean */
    CLIMATE_LAYER_TEMPERATURE_WARMEST = 9,  /* the warmest month's mean */
    CLIMATE_LAYER_TEMPERATURE_COLDEST = 10, /* the coldest month's mean */
    CLIMATE_LAYER_RAIN_YEAR = 11,           /* yearly rain */
    CLIMATE_LAYER_RAIN_DAY = 12,            /* the day's normal rain times 365, on the year's scale */
    CLIMATE_LAYER_HURRICANE_SEASON = 13,    /* the water layer, seas red by s_hur on the day, coasts in season orange */
    CLIMATE_LAYER_MONSOON = 14,             /* the water layer, monsoon land teal, brighter when monsoon today */
    CLIMATE_LAYER_CLIMATE_CLASS = 15,       /* each class its colour, the sea as the water layer */
    CLIMATE_LAYER_SNOW_BALANCE = 16,        /* land above its snow balance white, below it grey by how far */
    CLIMATE_LAYER_COUNT = 17,
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
    /* Phase 05 */
    CLIMATE_LAYER_HURRICANE_LOW = 0x603030u,   /* a sea with s_hur just above 0 */
    CLIMATE_LAYER_HURRICANE_HIGH = 0xFF2020u,  /* s_hur = 1 */
    CLIMATE_LAYER_HURRICANE_COAST = 0xFF9040u, /* a hurricane coast in season on the day */
    CLIMATE_LAYER_MONSOON_LAND = 0x208080u,    /* land with the monsoon flag */
    CLIMATE_LAYER_MONSOON_TODAY = 0x30E0E0u,   /* in its monsoon on the day */
    CLIMATE_LAYER_SNOW_ABOVE = 0xFFFFFFu,      /* land at or above its snow balance */
    CLIMATE_LAYER_SNOW_NEAR = 0xA0A0A0u,       /* within 50 blocks below it */
    CLIMATE_LAYER_SNOW_FAR = 0x404040u,        /* 2000 or more below it */
    CLIMATE_LAYER_TEMPERATURE_STOPS = 6u,
    CLIMATE_LAYER_RAIN_STOPS = 6u,
};
static const float32_t CLIMATE_LAYER_PRESSURE_LOW_HPA = 990.0f32;
static const float32_t CLIMATE_LAYER_PRESSURE_MID_HPA = 1013.0f32;
static const float32_t CLIMATE_LAYER_PRESSURE_HIGH_HPA = 1030.0f32;
static const float32_t CLIMATE_LAYER_PRESSURE_LINE_HPA = 4.0f32; /* master plan 9.3: lines every 4 hPa */
static const float32_t CLIMATE_LAYER_WIND_TOP = 15.0f32;         /* blocks a second, drawn brightest */
static const float32_t CLIMATE_LAYER_WIND_ARROW_MAX = 30.0f32;   /* pixels: an arrow is 2 x speed, at most this */
static const float32_t CLIMATE_LAYER_LAND_TOP = 454.0f32;        /* blocks: z_ground drawn lightest from here up */
/* Phase 05's colour stops, linear in each channel between stops and held past the ends. */
static const float32_t CLIMATE_LAYER_TEMPERATURE_C[CLIMATE_LAYER_TEMPERATURE_STOPS] = {-30.0f32, -10.0f32, 0.0f32,
                                                                                       15.0f32,  30.0f32,  45.0f32};
static const uint32_t CLIMATE_LAYER_TEMPERATURE_RGB[CLIMATE_LAYER_TEMPERATURE_STOPS] = {
    0x3040A0u, 0x60A0E0u, 0xF0F0F0u, 0xF0D060u, 0xE06020u, 0x801010u};
static const float32_t CLIMATE_LAYER_RAIN_MM[CLIMATE_LAYER_RAIN_STOPS] = {0.0f32,    250.0f32,  500.0f32,
                                                                          1000.0f32, 2000.0f32, 3000.0f32};
static const uint32_t CLIMATE_LAYER_RAIN_RGB[CLIMATE_LAYER_RAIN_STOPS] = {0xC8A060u, 0xE0D090u, 0xA0C060u,
                                                                          0x40A040u, 0x2070C0u, 0x203080u};
/* By CLIMATE_CLASS_*: NONE (the sea, drawn as the water layer instead), Ice, Tundra, Desert, Steppe, Rainforest, Monsoon,
 * Savanna, Mediterranean, Temperate, Continental, Boreal. */
static const uint32_t CLIMATE_LAYER_CLASS_RGB[CLIMATE_CLASS_COUNT] = {
    CLIMATE_LAYER_SEA, 0xFFFFFFu, 0xB0B8C0u, 0xF0D080u, 0xD0B060u, 0x006020u,
    0x208040u,         0xA0C040u, 0xE0A040u, 0x60B060u, 0x6080C0u, 0x305060u};
static const float32_t CLIMATE_LAYER_SNOW_NEAR_BLOCKS = 50.0f32;
static const float32_t CLIMATE_LAYER_SNOW_FAR_BLOCKS = 2000.0f32;

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

/* Phase 05: deg C to its colour by the temperature stops; mm a year by the rain stops. */
uint32_t climate_layer_temperature_colour(float32_t c);
uint32_t climate_layer_rain_colour(float32_t mm_year);
/* The colour of one place's normals in a phase 05 layer, grey for any other layer. Pure. */
uint32_t climate_layer_normals_colour(climate_layer_t layer, const climate_normals_at_t *normals);

#endif
