/* See run/src/climate/layer/README.md. */
#ifndef MC_RUN_CLIMATE_LAYER_INDEX_H
#define MC_RUN_CLIMATE_LAYER_INDEX_H

#include "climate/index.h"
#include "climate/systems/index.h"
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
    CLIMATE_LAYER_COUNT = 17,               /* the layers climate_layer_draw draws: everything above */
    /* Phase 06: "now" layers, drawn by climate_layer_draw_now at a moment from a day list. */
    CLIMATE_LAYER_PRESSURE_NOW = 17, /* belt + heat low + systems as colour, lines every 4 hPa, H and L at centres */
    CLIMATE_LAYER_SYSTEMS = 18,      /* the water layer, each big system's circle, eye, track behind it and front */
    CLIMATE_LAYER_RAIN_RADAR = 19,   /* background expected rain plus systems, times the high's cut, mm an hour */
    CLIMATE_LAYER_THUNDER = 20,      /* the water layer, each alive cell a disc with a lightning mark */
    CLIMATE_LAYER_WIND_NOW = 21,     /* phase 04's wind with each system's circling wind added */
    CLIMATE_LAYER_NOW_END = 22,
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
    /* Phase 06 */
    CLIMATE_LAYER_HIGH_MARK = 0x2040C0u,      /* the H at a high's centre */
    CLIMATE_LAYER_LOW_MARK = 0xC02020u,       /* the L at a low's or hurricane's centre */
    CLIMATE_LAYER_HURRICANE_RING = 0xFF3030u, /* a hurricane's circle at R, and its eye */
    CLIMATE_LAYER_LOW_RING = 0xFF9020u,       /* a low's circle */
    CLIMATE_LAYER_HIGH_RING = 0x3080FFu,      /* a high's circle */
    CLIMATE_LAYER_TRACK = 0xFFFFFFu,          /* the track behind a system */
    CLIMATE_LAYER_FRONT = 0x2050FFu,          /* a cold front, a tooth every 16 pixels */
    CLIMATE_LAYER_FRONT_TOOTH_SPACING = 16u,
    CLIMATE_LAYER_THUNDER_DISC = 0xC0C0FFu, /* an alive cell at R */
    CLIMATE_LAYER_LIGHTNING = 0xFFE020u,    /* the lightning mark at a cell's centre */
    CLIMATE_LAYER_RADAR_STOPS = 6u,
    CLIMATE_LAYER_MARK_WIDTH = 5u, /* the H and L bitmaps */
    CLIMATE_LAYER_MARK_HEIGHT = 7u,
};
/* The radar's steps, mm an hour: each colour from its rate up to the next; under the first, the water layer shows. */
static const float32_t CLIMATE_LAYER_RADAR_MM_HOUR[CLIMATE_LAYER_RADAR_STOPS] = {0.1f32,  1.0f32,  4.0f32,
                                                                                 10.0f32, 25.0f32, 50.0f32};
static const uint32_t CLIMATE_LAYER_RADAR_RGB[CLIMATE_LAYER_RADAR_STOPS] = {0x80C0FFu, 0x40A040u, 0xE0E040u,
                                                                            0xF08020u, 0xE02020u, 0xC020C0u};
/* Macros since phase 11, so climate_layer_scale's static tables are built from them: one home each. */
#define CLIMATE_LAYER_PRESSURE_LOW_HPA 990.0f32
#define CLIMATE_LAYER_PRESSURE_MID_HPA 1013.0f32
#define CLIMATE_LAYER_PRESSURE_HIGH_HPA 1030.0f32
static const float32_t CLIMATE_LAYER_PRESSURE_LINE_HPA = 4.0f32; /* master plan 9.3: lines every 4 hPa */
#define CLIMATE_LAYER_WIND_TOP 15.0f32                           /* blocks a second, drawn brightest */
static const float32_t CLIMATE_LAYER_WIND_ARROW_MAX = 30.0f32;   /* pixels: an arrow is 2 x speed, at most this */
#define CLIMATE_LAYER_LAND_TOP 454.0f32                          /* blocks: z_ground drawn lightest from here up */
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
#define CLIMATE_LAYER_SNOW_NEAR_BLOCKS 50.0f32
#define CLIMATE_LAYER_SNOW_FAR_BLOCKS 2000.0f32
/* Phase 11: the drawing's own numbers, one home each. */
enum : uint32_t {
    CLIMATE_LAYER_PIXEL_BYTES = 4u,        /* 0x00RRGGBB, little-endian: B, G, R, 0 */
    CLIMATE_LAYER_CHANNEL_BITS = 8u,       /* each of R, G and B */
    CLIMATE_LAYER_RGB_BITS = 24u,          /* the three channels */
    CLIMATE_LAYER_CHANNEL_MAX = 0xFFu,     /* a channel at full */
    CLIMATE_LAYER_BLACK = 0x000000u,       /* the sea distance layer's 0 */
    CLIMATE_LAYER_WHITE = 0xFFFFFFu,       /* and its 8192 */
    CLIMATE_LAYER_LATITUDE_LINES = 5u,     /* the equator, both tropics, both polar circles */
    CLIMATE_LAYER_PRESSURE_STOPS = 3u,     /* 990, 1013, 1030 hPa */
    CLIMATE_LAYER_SNOW_STOPS = 3u,         /* at, 50 below and 2000 below the snow balance */
    CLIMATE_LAYER_OCTANTS = 8u,            /* a midpoint circle draws 8 points per step */
    CLIMATE_LAYER_LIGHTNING_POINTS = 4u,   /* the lightning mark's zigzag: 4 points, 3 segments */
    CLIMATE_LAYER_BARB_PIXELS = 3u,        /* an arrow's two barbs */
    CLIMATE_LAYER_FRONT_TOOTH_PIXELS = 3u, /* a cold front's teeth */
};
static const float32_t CLIMATE_LAYER_BARB_COS = -0.8660254f32;     /* the barbs turned 150 degrees from the arrow */
static const float32_t CLIMATE_LAYER_CURRENT_ARROW_MIN = 4.0f32;   /* pixels: a current's arrow at no strength */
static const float32_t CLIMATE_LAYER_CURRENT_ARROW_SPAN = 12.0f32; /* pixels more at full strength */
static const float64_t CLIMATE_LAYER_PIXEL_CLAMP =
    1.0e6f64; /* pixels: positions far outside a picture are clamped here */

/* Plan 004 phase 09: where a picture is, for climate_layer_draw_scaled. Blocks a pixel may be any positive number, and the
 * grids are anchored to world pixels, not to the picture's corner, so a picture panned by any whole number of pixels samples
 * the same points where the two overlap. */
enum : uint32_t {
    CLIMATE_LAYER_PART_FIELD = 1u, /* every pixel's colour, with the pressure layer's lines */
    CLIMATE_LAYER_PART_MARKS = 2u, /* what crosses pixels: arrows, and the latitude layer's lines */
    CLIMATE_LAYER_PARTS_ALL = 3u,
};
typedef struct climate_layer_frame_t {
    float64_t west_x;     /* 0: block x of column 0's west edge */
    float64_t north_y;    /* 8: block y of row 0's north edge */
    float64_t step;       /* 16: blocks a pixel, above 0 */
    int64_t column_index; /* 24: the world pixel column 0 is: arrows sit where it is 16 more than a multiple of 32 */
    int64_t row_index;    /* 32: and row 0; sample points sit where both are multiples of sample */
    uint32_t *borrowed_scratch; /* 40: with sample above 1: 2 x (width / sample + 3) entries; else may be nullptr */
    uint32_t width;             /* 48 */
    uint32_t height;            /* 52 */
    uint32_t sample;            /* 56: 1 works out every pixel; n every n-th world pixel each way, blended between */
    uint32_t parts;             /* 60: CLIMATE_LAYER_PART_* */
} climate_layer_frame_t;
static_assert(sizeof(climate_layer_frame_t) == 64);

/* A layer of climate_layer_draw's (not the now layers) at any blocks a pixel. With sample 1, column_index and row_index 0
 * and whole numbers, its bytes are climate_layer_draw's: that function is this one. With sample n, each value is worked out
 * at the world pixels that are multiples of n, and pixels between blend their four (the pressure layer its hPa, before its
 * lines; the others their colours), or take the nearest where one of the four is grey. day may hold a fraction. */
void climate_layer_draw_scaled(const climate_t *climate, climate_layer_t layer, const climate_layer_frame_t *frame,
                               float32_t day, uint8_t *pixels, uint32_t pitch);

/* Each layer's colour stops, for a legend: values rising, colours 0x00RRGGBB. False for a layer with no scale. */
bool climate_layer_scale(climate_layer_t layer, const float32_t **out_values, const uint32_t **out_colours,
                         uint32_t *out_count);

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

/* Phase 06: a "now" layer at time t from a day list built for floor(t), centred where the picture is. Grey where a tile is
 * not built, with systems over grey drawn as outlines only (their circles, and in the systems layer their tracks), so storms
 * show before they reach explored land (master plan 2.4). Pixels as climate_layer_draw's. Any thread. */
void climate_layer_draw_now(const climate_t *climate, const climate_systems_day_t *day, climate_layer_t layer,
                            float64_t t, int32_t west_x, int32_t north_y, int32_t blocks_per_pixel, uint32_t width,
                            uint32_t height, uint8_t *pixels, uint32_t pitch);
/* The radar's colour for a rain rate: false under 0.1 mm an hour, where the water layer shows instead. */
bool climate_layer_radar_colour(float32_t mm_hour, uint32_t *out);
/* The rain the radar shows at a place and moment, mm an hour: the background's expected rate, the normal times 1 - f_sys,
 * plus the systems', times the high's cut. False where a tile is not built. */
bool climate_layer_rain_now(const climate_t *climate, const climate_systems_day_t *day, float64_t x, float64_t y,
                            float64_t t, float32_t *out);

#endif
