/* See run/src/ui/atlas/layer/README.md. */
#ifndef MC_RUN_UI_ATLAS_LAYER_INDEX_H
#define MC_RUN_UI_ATLAS_LAYER_INDEX_H

#include "arena/index.h"
#include "climate/index.h"
#include "climate/systems/index.h"
#include "climate/weather/index.h"
#include "job/index.h"
#include "terrain/index.h"
#include "types/index.h"
#include "ui/atlas/draw/index.h"
#include "ui/atlas/tile/index.h"
#include "ui/font/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 09: the layer registry, every layer's drawing, and one picture's whole draw. */

enum : int32_t {
    UI_ATLAS_ZOOM_MIN = -4,    /* 2^(-4/2) = 0.25 blocks a pixel: a block is 4 pixels */
    UI_ATLAS_ZOOM_MAX = 16,    /* 2^(16/2) = 256 blocks a pixel */
    UI_ATLAS_ZOOM_DEFAULT = 2, /* 2 blocks a pixel */
};
enum : uint32_t {
    UI_ATLAS_LAYER_COUNT = 26u,
    UI_ATLAS_GROUPS = 9u,         /* the number keys 1 to 9 */
    UI_ATLAS_BANDS = 16u,         /* a picture's rows in this many jobs */
    UI_ATLAS_SAMPLE_PIXELS = 4u,  /* the climate sample grid */
    UI_ATLAS_ARROW_PIXELS = 32u,  /* the arrow grid, and the coarse grid of the costly temperature now */
    UI_ATLAS_ISOBAR_PIXELS = 4u,  /* the pressure-now isobars' marching squares */
    UI_ATLAS_CELLS_MAX_BPP = 16u, /* thunderstorm cells only at 16 blocks a pixel or closer */
    UI_ATLAS_CONE_CIRCLES = 29u,  /* a quarter day apart for 7 days, and now */
    UI_ATLAS_NO_CLIMATE_LAYER = 255u,
};
/* What the overlay borrows: the font's row, the cone's three rows, at the widest picture. */
enum : uint64_t {
    UI_ATLAS_OVERLAY_SCRATCH_BYTES = (uint64_t)UI_CANVAS_ROW_BYTES + 3u * 3840u + 4096u,
};

typedef enum ui_atlas_time_t : uint8_t {
    UI_ATLAS_TIME_FIXED = 0,   /* changes only with the ground: terrain, yearly means, classes */
    UI_ATLAS_TIME_NORMALS = 1, /* a day of the year, from the slider and [ ] */
    UI_ATLAS_TIME_NOW = 2,     /* the world's time: drawn again twice a second */
    UI_ATLAS_TIME_WEEK = 3,    /* the last census's week: drawn again when a census runs */
} ui_atlas_time_t;

/* The Atlas's own values, worked out on the sample grid by layers no earlier phase drew. */
typedef enum ui_atlas_value_t : uint8_t {
    UI_ATLAS_VALUE_NONE = 0,
    UI_ATLAS_VALUE_CONTOURS = 1,        /* terrain with a line every 100 blocks of height */
    UI_ATLAS_VALUE_TEMPERATURE_NOW = 2, /* climate_weather_at, deg C */
    UI_ATLAS_VALUE_RAIN_RADAR = 3,      /* climate_layer_rain_now, mm an hour */
    UI_ATLAS_VALUE_RAIN_LAST_WEEK = 4,  /* the weekly state's rain_mm */
    UI_ATLAS_VALUE_WIND_NOW = 5,        /* climate_air_at plus the systems' wind, blocks a second */
    UI_ATLAS_VALUE_PRESSURE_NOW = 6,    /* climate_air_at plus the systems' pressure, hPa */
    UI_ATLAS_VALUE_SNOW = 7,            /* the weekly state's snow_mm */
    UI_ATLAS_VALUE_SOIL = 8,            /* the weekly state's soil_mm */
    UI_ATLAS_VALUE_FLOOD = 9,           /* the weekly state's flood index */
} ui_atlas_value_t;

/* What one picture is drawn from. Two views that are the same bytes give the same picture. */
typedef struct ui_atlas_view_t {
    float64_t center[2];      /* 0: block x and y at the picture's middle */
    int32_t zoom;             /* 16: blocks a pixel is 2^(zoom/2) */
    uint32_t width;           /* 20 */
    uint32_t height;          /* 24 */
    uint16_t day;             /* 28: 0 to 364; only normals layers read it, others keep 0 */
    uint8_t layer;            /* 30: a row of ui_atlas_layers */
    uint8_t _pad0[1];         /* 31 */
    uint64_t now_bucket;      /* 32: now layers: which half second of world time; others 0 */
    uint64_t week;            /* 40: this week's layers: the census week; others 0 */
    uint64_t ground_revision; /* 48: the store's revision, sampled at most twice a second */
} ui_atlas_view_t;
static_assert(sizeof(ui_atlas_view_t) == 56);

/* Where a view's pixels are: blocks a pixel, the world pixel of column 0 and row 0 (world pixel column w covers x from w s
 * to (w + 1) s; row q covers y from -(q + 1) s to -q s), and the tile level the zoom reads. */
typedef struct ui_atlas_geometry_t {
    float64_t step;   /* 0 */
    int64_t column0;  /* 8 */
    int64_t row0;     /* 16 */
    uint32_t level;   /* 24 */
    uint8_t _pad0[4]; /* 28 */
} ui_atlas_geometry_t;
static_assert(sizeof(ui_atlas_geometry_t) == 32);

/* Where the ground comes from: the store in the game, the terrain's estimate in the map tool and tests. */
typedef struct ui_atlas_ground_t {
    void *borrowed_context; /* 0 */
    /* Copies level's pixels for the rectangle of its pixel columns from px and rows from py (rows grow south), width by
     * height, into out, UI_ATLAS_PIXEL_NONE where nothing is known. scratch may lend memory. Any thread. */
    void (*read)(void *borrowed_context, uint32_t level, int64_t px, int64_t py, uint32_t width, uint32_t height,
                 uint32_t *out, arena_t *scratch); /* 8 */
} ui_atlas_ground_t;
static_assert(sizeof(ui_atlas_ground_t) == 16);

/* Everything one picture reads, as plain values and borrowed handles. */
typedef struct ui_atlas_draw_input_t {
    ui_atlas_view_t view;                          /* 0 */
    ui_atlas_ground_t ground;                      /* 56 */
    const climate_t *borrowed_climate;             /* 72: nullptr in a world with no climate: ground only */
    const climate_week_store_t *borrowed_week;     /* 80: the weekly state, or nullptr */
    const ui_font_t *borrowed_font;                /* 88 */
    const climate_systems_day_t *borrowed_systems; /* 96: the weather systems round the view at t, or nullptr */
    float64_t t;                                   /* 104: the systems' time, D + f */
    uint64_t census_week;                          /* 112: the forecast cones' week */
    uint32_t sample;  /* 120: the climate sample grid, pixels: 1 or UI_ATLAS_SAMPLE_PIXELS */
    uint8_t flat;     /* 124: 1: no hill shading */
    uint8_t _pad0[3]; /* 125 */
} ui_atlas_draw_input_t;
static_assert(sizeof(ui_atlas_draw_input_t) == 128);

/* Where one picture goes: width by height of each, rows of width. */
typedef struct ui_atlas_target_t {
    uint32_t *borrowed_pixels; /* 0: 0xFFRRGGBB */
    uint8_t *borrowed_grey;    /* 8: 1 over ungenerated land */
    float32_t *borrowed_field; /* 16: each pixel's own value, NaN where none: the overlay's isolines read it */
    uint8_t *borrowed_scratch; /* 24: UI_ATLAS_OVERLAY_SCRATCH_BYTES, the overlay's */
} ui_atlas_target_t;
static_assert(sizeof(ui_atlas_target_t) == 32);

/* Memory a band borrows: at least ui_atlas_band_scratch_bytes for its size, carved up inside it. */
typedef struct ui_atlas_scratch_t {
    uint8_t *borrowed_bytes; /* 0: 64-byte aligned */
    uint64_t bytes;          /* 8 */
} ui_atlas_scratch_t;
static_assert(sizeof(ui_atlas_scratch_t) == 16);

/* Draws rows [y, y + height) of the target, every column. The same pixels whatever the band. */
typedef void ui_atlas_band_draw_t(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, uint32_t y,
                                  uint32_t height, ui_atlas_scratch_t scratch);
/* Draws lines, arrows, marks and names over the whole picture, after every band. One job, a fixed order. */
typedef void ui_atlas_overlay_draw_t(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target);

/* One layer. A later plan adds a layer by adding a row and its functions, and changes nothing else. */
typedef struct ui_atlas_layer_t {
    const char *key;                     /* 0: "temperature_day": stable; tests, golden names and the map tool use it */
    const char *label;                   /* 8: the legend's title */
    const char *unit;                    /* 16: "°C", or "" */
    ui_atlas_band_draw_t *band;          /* 24: ground and field colours */
    ui_atlas_overlay_draw_t *overlay;    /* 32: or nullptr */
    const ui_atlas_scale_t *scale;       /* 40: or nullptr for a list legend or none */
    const ui_atlas_key_entry_t *entries; /* 48: or nullptr */
    uint8_t time;                        /* 56: ui_atlas_time_t */
    uint8_t group;                       /* 57: its number key, 1 to 9 */
    uint8_t over_grey;                   /* 58: 1: drawn as outlines over ungenerated land; 0: not drawn there */
    uint8_t entry_count;                 /* 59 */
    uint8_t climate_layer;               /* 60: the climate_layer_t it draws through, or UI_ATLAS_NO_CLIMATE_LAYER */
    uint8_t value;                       /* 61: ui_atlas_value_t, for the Atlas's own */
    uint8_t arrows;                      /* 62: 1: the legend shows an arrow of 10 blocks a second */
    uint8_t land_only;                   /* 63: plain terrain over sea and where the value is 0 or less */
} ui_atlas_layer_t;
static_assert(sizeof(ui_atlas_layer_t) == 64);

extern const ui_atlas_layer_t ui_atlas_layers[UI_ATLAS_LAYER_COUNT];

/* The row with this key, or UI_ATLAS_LAYER_COUNT. */
uint32_t ui_atlas_layer_find(const char *key);
/* The first layer of a group, or the next in it after current (round), for a number key. */
uint32_t ui_atlas_layer_next_in_group(uint32_t current, uint32_t group);

/* 2^(zoom / 2), exactly for even zooms. */
float64_t ui_atlas_blocks_per_pixel(int32_t zoom);
/* clamp(floor(log2 s), 0, 7). */
uint32_t ui_atlas_level_of(int32_t zoom);
/* The view's pixels' place: column0 = floor(cx / s) - width / 2, row0 = floor(-cy / s) - height / 2, so the centre snaps to
 * the world pixel grid and every pan samples the same world pixels. */
ui_atlas_geometry_t ui_atlas_geometry(const ui_atlas_view_t *view);
/* A picture pixel's centre, in blocks. */
float64_t ui_atlas_pixel_x(const ui_atlas_geometry_t *geometry, float64_t column);
float64_t ui_atlas_pixel_y(const ui_atlas_geometry_t *geometry, float64_t row);

/* Phase 06's day list round the view at t: big systems whose footprint reaches the view, and at 16 blocks a pixel or closer
 * thunderstorm cells within 8192 blocks of its middle. From arena. False if the arena is too small. */
bool ui_atlas_systems_build(const climate_t *climate, arena_t *arena, const ui_atlas_view_t *view, float64_t t,
                            climate_systems_day_t *out);

/* What a band of this width and height borrows at most: its ground and the ground it reads, its field and grid, the
 * costly temperature's weather scratch, and what the ground's read borrows for a tile file. */
uint64_t ui_atlas_band_scratch_bytes(uint32_t width, uint32_t height);
/* Band `band` of `bands` (rows [height x band / bands, height x (band + 1) / bands)), with memory it borrows. The bytes are
 * the same for any split. ui_atlas_draw_picture (run/src/ui/atlas/) runs the bands and then the finish. */
void ui_atlas_draw_band(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, uint32_t band,
                        uint32_t bands, ui_atlas_scratch_t scratch);
/* After every band: the overlay, then grey put back over ungenerated land on every layer that draws nothing there, and
 * every pixel's alpha set. */
void ui_atlas_draw_finish(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target);

/* A ground from the terrain's height estimate: the level pixel's centre column, coloured by height from a fixed palette (sea
 * below 0, sand to 3, green to 300, rock to 700, snow above), and nothing east of generated_east_x. For the map tool, which
 * shows everything, and the golden pictures, which test the Atlas's drawing and not the terrain. */
typedef struct ui_atlas_estimate_t {
    const terrain_t *borrowed_terrain; /* 0 */
    int64_t generated_east_x;          /* 8: columns at or east of this are not generated; INT64_MAX for none */
} ui_atlas_estimate_t;
static_assert(sizeof(ui_atlas_estimate_t) == 16);
void ui_atlas_estimate_read(void *borrowed_context, uint32_t level, int64_t px, int64_t py, uint32_t width,
                            uint32_t height, uint32_t *out, arena_t *scratch);
/* The palette's colour for a height. */
uint32_t ui_atlas_estimate_colour(float32_t z);

#endif
