/* See README.md. The registry and every layer's drawing: pure functions of the view, the ground and the climate, on memory
 * the caller lends, so the same view gives the same bytes on any split into bands and any number of workers. */
#include "ui/atlas/layer/index.h"

#include "climate/layer/index.h"
#include "climate/outlook/index.h"
#include "climate/systems/field/index.h"
#include "climate/systems/name/index.h"

#include "assert/index.h"

#include <math.h>
#include <string.h>

/* Carves the memory a band borrows: 64-byte pieces, in order. */
typedef struct layer_cursor_t {
    uint8_t *at;  /* 0 */
    uint8_t *end; /* 8 */
} layer_cursor_t;
static_assert(sizeof(layer_cursor_t) == 16);

static void *layer_take(layer_cursor_t *cursor, uint64_t bytes) {
    uint8_t *const piece = cursor->at;
    const uint64_t rounded = (bytes + 63u) & ~63ull;
    ASSERT_ALWAYS((uint64_t)(cursor->end - piece) >=
                  rounded); /* ui_atlas_band_scratch_bytes is too small: a bug here */
    cursor->at = piece + rounded;
    return piece;
}

enum : uint64_t {
    LAYER_WEATHER_BYTES = 4ull << 20,     /* climate_weather_at's scratch: about 2 MiB, it says */
    LAYER_GROUND_READ_BYTES = 3ull << 20, /* what the ground's read borrows: a tile file's bytes and pixels */
};

/* ---- Scales of layers no earlier phase drew (the phase 09 file's table) ------------------------------------------- */

static const float32_t TEMPERATURE_C[9] = {-40.0f32, -20.0f32, -5.0f32, 0.0f32, 10.0f32,
                                           20.0f32,  30.0f32,  40.0f32, 50.0f32}; /* deg C */
static const uint32_t TEMPERATURE_RGB[9] = {0x3B0F70u, 0x2C7BB6u, 0xABD9E9u, 0xF7F7F7u, 0xA6D96Au,
                                            0xFEE08Bu, 0xF46D43u, 0xA50026u, 0x5A0014u};
static const ui_atlas_scale_t SCALE_TEMPERATURE = {TEMPERATURE_C, TEMPERATURE_RGB, 9u, {0}};
static const float32_t WEEK_RAIN_MM[6] = {0.0f32,
                                          250.0f32 / 52.0f32,
                                          500.0f32 / 52.0f32,
                                          1000.0f32 / 52.0f32,
                                          2000.0f32 / 52.0f32,
                                          3000.0f32 / 52.0f32}; /* mm a week */
static const uint32_t WEEK_RAIN_RGB[6] = {0x8C510Au, 0xD8B365u, 0xF6E8C3u, 0xC7EAE5u, 0x5AB4ACu, 0x01665Eu};
static const ui_atlas_scale_t SCALE_WEEK_RAIN = {WEEK_RAIN_MM, WEEK_RAIN_RGB, 6u, {0}};
static const float32_t SNOW_MM[4] = {0.0f32, 20.0f32, 100.0f32, 400.0f32}; /* mm of water */
static const uint32_t SNOW_RGB[4] = {0xFFFFFFu, 0xDCEBFFu, 0x96BEF0u, 0x3C5AC8u};
static const ui_atlas_scale_t SCALE_SNOW = {SNOW_MM, SNOW_RGB, 4u, {0}};
static const float32_t SOIL_MM[3] = {0.0f32, 75.0f32, 150.0f32}; /* mm */
static const uint32_t SOIL_RGB[3] = {0xA6611Au, 0xDFC27Du, 0x018571u};
static const ui_atlas_scale_t SCALE_SOIL = {SOIL_MM, SOIL_RGB, 3u, {0}};
static const float32_t FLOOD_INDEX[4] = {0.0f32, 0.5f32, 1.0f32, 2.0f32}; /* ratio: 1 is the banks full */
static const uint32_t FLOOD_RGB[4] = {0xFFFFFFu, 0xFEE08Bu, 0xF46D43u, 0xA50026u};
static const ui_atlas_scale_t SCALE_FLOOD = {FLOOD_INDEX, FLOOD_RGB, 4u, {0}};

/* Phase 05's class colours, in 6.10's order, for the list legend. */
static const ui_atlas_key_entry_t CLASS_ENTRIES[11] = {
    {"Ice", 0xFFFFFFu, {0}},         {"Tundra", 0xB0B8C0u, {0}},        {"Desert", 0xF0D080u, {0}},
    {"Steppe", 0xD0B060u, {0}},      {"Rainforest", 0x006020u, {0}},    {"Monsoon", 0x208040u, {0}},
    {"Savanna", 0xA0C040u, {0}},     {"Mediterranean", 0xE0A040u, {0}}, {"Temperate", 0x60B060u, {0}},
    {"Continental", 0x6080C0u, {0}}, {"Boreal", 0x305060u, {0}},
};
static const ui_atlas_key_entry_t CURRENT_ENTRIES[3] = {
    {"Warm", CLIMATE_LAYER_CURRENT_WARM, {0}},
    {"Cold", CLIMATE_LAYER_CURRENT_COLD, {0}},
    {"Neutral", CLIMATE_LAYER_CURRENT_NEUTRAL, {0}},
};
static const ui_atlas_key_entry_t SYSTEM_ENTRIES[4] = {
    {"Hurricane", CLIMATE_LAYER_HURRICANE_RING, {0}},
    {"Low and cold front", CLIMATE_LAYER_LOW_MARK, {0}},
    {"High", CLIMATE_LAYER_HIGH_MARK, {0}},
    {"Forecast cone", 0xFF6060u, {0}},
};
static const ui_atlas_key_entry_t THUNDER_ENTRIES[1] = {{"Thunderstorm cell", CLIMATE_LAYER_LIGHTNING, {0}}};
static const ui_atlas_key_entry_t LATITUDE_ENTRIES[3] = {
    {"Equator", CLIMATE_LAYER_EQUATOR, {0}},
    {"Tropics", CLIMATE_LAYER_TROPIC, {0}},
    {"Polar circles", CLIMATE_LAYER_POLAR, {0}},
};
static const ui_atlas_key_entry_t MONSOON_ENTRIES[2] = {
    {"Monsoon land", CLIMATE_LAYER_MONSOON_LAND, {0}},
    {"In its monsoon", CLIMATE_LAYER_MONSOON_TODAY, {0}},
};

/* ---- Geometry ---------------------------------------------------------------------------------------------------- */

static int64_t layer_floor_div(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

float64_t ui_atlas_blocks_per_pixel(int32_t zoom) {
    const int32_t half = (int32_t)layer_floor_div(zoom, 2);
    const float64_t base = zoom - 2 * half == 0 ? 1.0f64 : 1.41421356237309504880f64;
    return ldexp(base, half);
}

uint32_t ui_atlas_level_of(int32_t zoom) {
    const int64_t level = layer_floor_div(zoom, 2);
    return level < 0 ? 0u : (level > (int64_t)UI_ATLAS_LEVELS - 1 ? UI_ATLAS_LEVELS - 1u : (uint32_t)level);
}

ui_atlas_geometry_t ui_atlas_geometry(const ui_atlas_view_t *view) {
    const float64_t step = ui_atlas_blocks_per_pixel(view->zoom);
    return (ui_atlas_geometry_t){
        .step = step,
        .column0 = (int64_t)floor(view->center[0] / step) - (int64_t)(view->width / 2u),
        .row0 = (int64_t)floor(-view->center[1] / step) - (int64_t)(view->height / 2u),
        .level = ui_atlas_level_of(view->zoom),
        ._pad0 = {0},
    };
}

float64_t ui_atlas_pixel_x(const ui_atlas_geometry_t *geometry, float64_t column) {
    return ((float64_t)geometry->column0 + column + 0.5f64) * geometry->step;
}

float64_t ui_atlas_pixel_y(const ui_atlas_geometry_t *geometry, float64_t row) {
    return -((float64_t)geometry->row0 + row + 0.5f64) * geometry->step;
}

/* A block to the picture's continuous pixel space, where pixel i spans [i, i + 1). */
static float32_t layer_to_column(const ui_atlas_geometry_t *g, float64_t x) {
    float64_t p = x / g->step - (float64_t)g->column0;
    p = p < -1.0e6f64 ? -1.0e6f64 : (p > 1.0e6f64 ? 1.0e6f64 : p);
    return (float32_t)p;
}
static float32_t layer_to_row(const ui_atlas_geometry_t *g, float64_t y) {
    float64_t p = -y / g->step - (float64_t)g->row0;
    p = p < -1.0e6f64 ? -1.0e6f64 : (p > 1.0e6f64 ? 1.0e6f64 : p);
    return (float32_t)p;
}

/* ---- The registry ---------------------------------------------------------------------------------------------------- */

static ui_atlas_band_draw_t layer_band_terrain, layer_band_climate, layer_band_value;
static ui_atlas_overlay_draw_t layer_overlay_marks, layer_overlay_wind_now, layer_overlay_pressure_now,
    layer_overlay_systems, layer_overlay_thunder, layer_overlay_outlines;

#define ROW(k, l, u, b, o, s, e, t, g, grey, n, c, v, a, land)                                                         \
    {.key = k,                                                                                                         \
     .label = l,                                                                                                       \
     .unit = u,                                                                                                        \
     .band = b,                                                                                                        \
     .overlay = o,                                                                                                     \
     .scale = s,                                                                                                       \
     .entries = e,                                                                                                     \
     .time = t,                                                                                                        \
     .group = g,                                                                                                       \
     .over_grey = grey,                                                                                                \
     .entry_count = n,                                                                                                 \
     .climate_layer = c,                                                                                               \
     .value = v,                                                                                                       \
     .arrows = a,                                                                                                      \
     .land_only = land}
#define NONE_C UI_ATLAS_NO_CLIMATE_LAYER
#define FIXED UI_ATLAS_TIME_FIXED
#define NORMALS UI_ATLAS_TIME_NORMALS
#define NOW UI_ATLAS_TIME_NOW
#define WEEK UI_ATLAS_TIME_WEEK

const ui_atlas_layer_t ui_atlas_layers[UI_ATLAS_LAYER_COUNT] = {
    ROW("terrain", "Terrain", "", layer_band_terrain, nullptr, nullptr, nullptr, FIXED, 1, 0, 0, NONE_C,
        UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("terrain_lines", "Terrain, a line every 100 blocks", "", layer_band_terrain, nullptr, nullptr, nullptr, FIXED,
        1, 0, 0, NONE_C, UI_ATLAS_VALUE_CONTOURS, 0, 0),
    ROW("temperature_day", "Temperature",
        "\xC2\xB0"
        "C",
        layer_band_climate, nullptr, nullptr, nullptr, NORMALS, 2, 0, 0, CLIMATE_LAYER_TEMPERATURE_DAY,
        UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("temperature_mean", "Temperature, yearly mean",
        "\xC2\xB0"
        "C",
        layer_band_climate, nullptr, nullptr, nullptr, FIXED, 2, 0, 0, CLIMATE_LAYER_TEMPERATURE_MEAN,
        UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("temperature_warmest", "Temperature, warmest month",
        "\xC2\xB0"
        "C",
        layer_band_climate, nullptr, nullptr, nullptr, FIXED, 2, 0, 0, CLIMATE_LAYER_TEMPERATURE_WARMEST,
        UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("temperature_coldest", "Temperature, coldest month",
        "\xC2\xB0"
        "C",
        layer_band_climate, nullptr, nullptr, nullptr, FIXED, 2, 0, 0, CLIMATE_LAYER_TEMPERATURE_COLDEST,
        UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("temperature_now", "Temperature now",
        "\xC2\xB0"
        "C",
        layer_band_value, nullptr, &SCALE_TEMPERATURE, nullptr, NOW, 2, 0, 0, NONE_C, UI_ATLAS_VALUE_TEMPERATURE_NOW, 0,
        0),
    ROW("rain_day", "Normal rain, on the year's scale", "mm", layer_band_climate, nullptr, nullptr, nullptr, NORMALS, 3,
        0, 0, CLIMATE_LAYER_RAIN_DAY, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("rain_year", "Yearly rain", "mm", layer_band_climate, nullptr, nullptr, nullptr, FIXED, 3, 0, 0,
        CLIMATE_LAYER_RAIN_YEAR, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("rain_radar", "Rain radar", "mm an hour", layer_band_value, layer_overlay_outlines, nullptr, nullptr, NOW, 3, 1,
        0, NONE_C, UI_ATLAS_VALUE_RAIN_RADAR, 0, 0),
    ROW("rain_last_week", "Rain and snow last week", "mm", layer_band_value, nullptr, &SCALE_WEEK_RAIN, nullptr, WEEK,
        3, 0, 0, NONE_C, UI_ATLAS_VALUE_RAIN_LAST_WEEK, 0, 1),
    ROW("wind_day", "Normal wind", "blocks a second", layer_band_climate, layer_overlay_marks, nullptr, nullptr,
        NORMALS, 4, 0, 0, CLIMATE_LAYER_WIND, UI_ATLAS_VALUE_NONE, 1, 0),
    ROW("wind_now", "Wind now", "blocks a second", layer_band_value, layer_overlay_wind_now, nullptr, nullptr, NOW, 4,
        0, 0, NONE_C, UI_ATLAS_VALUE_WIND_NOW, 1, 0),
    ROW("currents", "Ocean currents", "", layer_band_climate, layer_overlay_marks, nullptr, CURRENT_ENTRIES, FIXED, 5,
        0, 3, CLIMATE_LAYER_CURRENTS, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("pressure_day", "Pressure belts and heat lows", "hPa", layer_band_climate, nullptr, nullptr, nullptr, NORMALS,
        6, 0, 0, CLIMATE_LAYER_PRESSURE, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("pressure_now", "Pressure now", "hPa", layer_band_value, layer_overlay_pressure_now, nullptr, nullptr, NOW, 6,
        1, 0, NONE_C, UI_ATLAS_VALUE_PRESSURE_NOW, 0, 0),
    ROW("systems", "Weather systems", "", layer_band_terrain, layer_overlay_systems, nullptr, SYSTEM_ENTRIES, NOW, 7, 1,
        4, NONE_C, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("thunderstorm_cells", "Thunderstorm cells", "", layer_band_terrain, layer_overlay_thunder, nullptr,
        THUNDER_ENTRIES, NOW, 7, 1, 1, NONE_C, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("hurricane_season", "Hurricane season", "", layer_band_climate, nullptr, nullptr, nullptr, NORMALS, 7, 0, 0,
        CLIMATE_LAYER_HURRICANE_SEASON, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("monsoon", "Monsoon", "", layer_band_climate, nullptr, nullptr, MONSOON_ENTRIES, NORMALS, 7, 0, 2,
        CLIMATE_LAYER_MONSOON, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("climate_class", "Climate class", "", layer_band_climate, nullptr, nullptr, CLASS_ENTRIES, FIXED, 8, 0, 11,
        CLIMATE_LAYER_CLIMATE_CLASS, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("snow_balance", "Blocks below the snow balance", "blocks", layer_band_climate, nullptr, nullptr, nullptr, FIXED,
        8, 0, 0, CLIMATE_LAYER_SNOW_BALANCE, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("latitude_lines", "Latitude lines", "", layer_band_terrain, layer_overlay_marks, nullptr, LATITUDE_ENTRIES,
        FIXED, 8, 0, 3, CLIMATE_LAYER_LATITUDE, UI_ATLAS_VALUE_NONE, 0, 0),
    ROW("snow", "Snow on the ground", "mm of water", layer_band_value, nullptr, &SCALE_SNOW, nullptr, WEEK, 9, 0, 0,
        NONE_C, UI_ATLAS_VALUE_SNOW, 0, 1),
    ROW("soil_water", "Soil water", "mm", layer_band_value, nullptr, &SCALE_SOIL, nullptr, WEEK, 9, 0, 0, NONE_C,
        UI_ATLAS_VALUE_SOIL, 0, 1),
    ROW("flood", "Flood index, 1 is over the banks", "", layer_band_value, nullptr, &SCALE_FLOOD, nullptr, WEEK, 9, 0,
        0, NONE_C, UI_ATLAS_VALUE_FLOOD, 0, 1),
};

uint32_t ui_atlas_layer_find(const char *key) {
    for (uint32_t i = 0u; i < UI_ATLAS_LAYER_COUNT; i++) {
        if (strcmp(ui_atlas_layers[i].key, key) == 0) {
            return i;
        }
    }
    return UI_ATLAS_LAYER_COUNT;
}

uint32_t ui_atlas_layer_next_in_group(uint32_t current, uint32_t group) {
    uint32_t first = UI_ATLAS_LAYER_COUNT;
    for (uint32_t i = 0u; i < UI_ATLAS_LAYER_COUNT; i++) {
        if (ui_atlas_layers[i].group == group) {
            first = first == UI_ATLAS_LAYER_COUNT ? i : first;
            if (current < UI_ATLAS_LAYER_COUNT && ui_atlas_layers[current].group == group && i > current) {
                return i;
            }
        }
    }
    return first == UI_ATLAS_LAYER_COUNT ? current : first;
}

/* ---- The ground under a band ---------------------------------------------------------------------------------------- */

/* The ground of picture columns -1 to width and rows y - 1 to y + height, each the level pixel under its centre. */
typedef struct layer_ground_t {
    uint32_t *pixels; /* 0: (width + 2) x (height + 2) */
    uint32_t stride;  /* 8: width + 2 */
    uint32_t rows;    /* 12: height + 2 */
} layer_ground_t;
static_assert(sizeof(layer_ground_t) == 16);

static layer_ground_t layer_ground_read(const ui_atlas_draw_input_t *input, const ui_atlas_geometry_t *g, uint32_t y,
                                        uint32_t height, layer_cursor_t *cursor) {
    const uint32_t width = input->view.width;
    const uint32_t stride = width + 2u, rows = height + 2u;
    int64_t *const columns = layer_take(cursor, (uint64_t)stride * sizeof(int64_t));
    int64_t *const lines = layer_take(cursor, (uint64_t)rows * sizeof(int64_t));
    for (uint32_t i = 0u; i < stride; i++) {
        const float64_t x = floor(ui_atlas_pixel_x(g, (float64_t)i - 1.0f64));
        columns[i] = ui_atlas_pixel_column_of((int64_t)x, g->level);
    }
    for (uint32_t j = 0u; j < rows; j++) {
        const float64_t yy = floor(ui_atlas_pixel_y(g, (float64_t)y + (float64_t)j - 1.0f64));
        lines[j] = ui_atlas_pixel_row_of((int64_t)yy, g->level);
    }
    const int64_t c_min = columns[0], c_max = columns[stride - 1u];
    const int64_t r_min = lines[0], r_max = lines[rows - 1u];
    const uint32_t read_width = (uint32_t)(c_max - c_min + 1), read_height = (uint32_t)(r_max - r_min + 1);
    uint32_t *const read = layer_take(cursor, (uint64_t)read_width * read_height * 4u);
    arena_t borrowed = {0};
    arena_init(&borrowed, "atlas_ground_read", layer_take(cursor, LAYER_GROUND_READ_BYTES), LAYER_GROUND_READ_BYTES);
    input->ground.read(input->ground.borrowed_context, g->level, c_min, r_min, read_width, read_height, read,
                       &borrowed);
    arena_deinit(&borrowed);
    layer_ground_t out = {
        .pixels = layer_take(cursor, (uint64_t)stride * rows * 4u),
        .stride = stride,
        .rows = rows,
    };
    for (uint32_t j = 0u; j < rows; j++) {
        const uint32_t *const from = read + (uint64_t)(lines[j] - r_min) * read_width;
        for (uint32_t i = 0u; i < stride; i++) {
            out.pixels[(uint64_t)j * stride + i] = from[columns[i] - c_min];
        }
    }
    return out;
}

/* Picture pixel (i, j) of the band, j from 0 at its first row. */
static uint32_t layer_ground_at(const layer_ground_t *ground, int64_t i, int64_t j) {
    return ground->pixels[(uint64_t)(j + 1) * ground->stride + (uint64_t)(i + 1)];
}

/* The hill-shading factor at (i, j): an ungenerated neighbour counts as the pixel's own height. */
static float32_t layer_shade_at(const ui_atlas_draw_input_t *input, const ui_atlas_geometry_t *g,
                                const layer_ground_t *ground, int64_t i, int64_t j) {
    if (input->flat != 0u) {
        return 1.0f32;
    }
    const uint32_t here = layer_ground_at(ground, i, j);
    const float32_t z = (float32_t)ui_atlas_pixel_z(here);
    const uint32_t around[4] = {layer_ground_at(ground, i - 1, j), layer_ground_at(ground, i + 1, j),
                                layer_ground_at(ground, i, j - 1), layer_ground_at(ground, i, j + 1)};
    float32_t h[4];
    for (uint32_t k = 0u; k < 4u; k++) {
        h[k] = ui_atlas_pixel_generated(around[k]) ? (float32_t)ui_atlas_pixel_z(around[k]) : z;
    }
    return ui_atlas_shade(h[0], h[1], h[2], h[3], (float32_t)g->step, UI_ATLAS_SHADE_EXAGGERATION);
}

static void layer_put(const ui_atlas_target_t *target, uint32_t width, uint32_t x, uint32_t y, uint32_t colour,
                      bool grey, float32_t field) {
    const uint64_t at = (uint64_t)y * width + x;
    target->borrowed_pixels[at] = grey ? UI_ATLAS_COLOUR_UNGENERATED : colour;
    target->borrowed_grey[at] = grey ? 1u : 0u;
    target->borrowed_field[at] = field;
}

/* ---- Band: terrain ---------------------------------------------------------------------------------------------------- */

static void layer_band_terrain(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, uint32_t y,
                               uint32_t height, ui_atlas_scratch_t scratch) {
    layer_cursor_t cursor = {scratch.borrowed_bytes, scratch.borrowed_bytes + scratch.bytes};
    const ui_atlas_layer_t *const row = &ui_atlas_layers[input->view.layer];
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const layer_ground_t ground = layer_ground_read(input, &g, y, height, &cursor);
    const uint32_t width = input->view.width;
    const bool contours = row->value == UI_ATLAS_VALUE_CONTOURS;
    for (uint32_t j = 0u; j < height; j++) {
        for (uint32_t i = 0u; i < width; i++) {
            const uint32_t here = layer_ground_at(&ground, i, j);
            if (!ui_atlas_pixel_generated(here)) {
                layer_put(target, width, i, y + j, 0u, true, nanf(""));
                continue;
            }
            float32_t b = layer_shade_at(input, &g, &ground, i, j);
            if (contours) {
                /* A line where the height crosses a multiple of 100 between this pixel and the one east or south. */
                const int32_t band = (int32_t)layer_floor_div(ui_atlas_pixel_z(here), 100);
                const uint32_t east = layer_ground_at(&ground, (int64_t)i + 1, j);
                const uint32_t south = layer_ground_at(&ground, i, (int64_t)j + 1);
                const bool line =
                    (ui_atlas_pixel_generated(east) && layer_floor_div(ui_atlas_pixel_z(east), 100) != band) ||
                    (ui_atlas_pixel_generated(south) && layer_floor_div(ui_atlas_pixel_z(south), 100) != band);
                b = line ? b * 0.5f32 : b;
            }
            layer_put(target, width, i, y + j, ui_atlas_shade_apply(ui_atlas_rgb_of(ui_atlas_pixel_rgb565(here)), b),
                      false, nanf(""));
        }
    }
}

/* ---- Band: a field climate/layer draws ---------------------------------------------------------------------------- */

static void layer_band_climate(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, uint32_t y,
                               uint32_t height, ui_atlas_scratch_t scratch) {
    layer_cursor_t cursor = {scratch.borrowed_bytes, scratch.borrowed_bytes + scratch.bytes};
    const ui_atlas_layer_t *const row = &ui_atlas_layers[input->view.layer];
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const layer_ground_t ground = layer_ground_read(input, &g, y, height, &cursor);
    const uint32_t width = input->view.width;
    const uint32_t sample = input->sample == 0u ? 1u : input->sample;
    /* One row more above, so the pressure layer's lines at the band's top see the row north of them, as in one picture;
     * none above the picture's first row, which in one picture has nothing north of it either. */
    const uint32_t above = y > 0u ? 1u : 0u;
    uint32_t *const field = layer_take(&cursor, (uint64_t)width * (height + 1u) * 4u);
    uint32_t *const grid = layer_take(&cursor, 2u * ((uint64_t)width / sample + 3u) * 4u);
    const climate_layer_frame_t frame = {
        .west_x = (float64_t)g.column0 * g.step,
        .north_y = -((float64_t)g.row0 + (float64_t)y - (float64_t)above) * g.step,
        .step = g.step,
        .column_index = g.column0,
        .row_index = g.row0 + (int64_t)y - (int64_t)above,
        .borrowed_scratch = grid,
        .width = width,
        .height = height + above,
        .sample = sample,
        .parts = CLIMATE_LAYER_PART_FIELD,
    };
    const float32_t day = row->time == UI_ATLAS_TIME_NORMALS ? (float32_t)input->view.day : 0.0f32;
    climate_layer_draw_scaled(input->borrowed_climate, (climate_layer_t)row->climate_layer, &frame, day,
                              (uint8_t *)field, width * 4u);
    for (uint32_t j = 0u; j < height; j++) {
        for (uint32_t i = 0u; i < width; i++) {
            const uint32_t here = layer_ground_at(&ground, i, j);
            if (!ui_atlas_pixel_generated(here)) {
                layer_put(target, width, i, y + j, 0u, true, nanf(""));
                continue;
            }
            const float32_t b = layer_shade_at(input, &g, &ground, i, j);
            uint32_t colour = field[(uint64_t)(j + above) * width + i] & 0xFFFFFFu;
            if (row->climate_layer == CLIMATE_LAYER_LATITUDE || colour == CLIMATE_LAYER_GREY) {
                /* Lines only, drawn in the overlay; or a climate tile not built yet: the ground, until it is. */
                colour = ui_atlas_rgb_of(ui_atlas_pixel_rgb565(here));
            }
            layer_put(target, width, i, y + j, ui_atlas_shade_apply(colour, b), false, nanf(""));
        }
    }
}

/* ---- Band: the Atlas's own values -------------------------------------------------------------------------------- */

static const climate_systems_day_t LAYER_NO_SYSTEMS = {0};

static const climate_systems_day_t *layer_systems_of(const ui_atlas_draw_input_t *input) {
    return input->borrowed_systems != nullptr ? input->borrowed_systems : &LAYER_NO_SYSTEMS;
}

static const climate_week_t *layer_week_cell(const ui_atlas_draw_input_t *input, float64_t x, float64_t y) {
    if (input->borrowed_week == nullptr) {
        return nullptr;
    }
    const int64_t cell_x = layer_floor_div((int64_t)floor(x), CLIMATE_CELL_BLOCKS);
    const int64_t cell_y = layer_floor_div((int64_t)floor(y), CLIMATE_CELL_BLOCKS);
    return climate_week_store_cell(input->borrowed_week, (int32_t)cell_x, (int32_t)cell_y);
}

/* One value at a block, NaN where there is none. weather is a small arena of its own, reset for each costly read. */
static float32_t layer_value_at(const ui_atlas_draw_input_t *input, ui_atlas_value_t value, float64_t x, float64_t y,
                                arena_t *weather) {
    const climate_t *const climate = input->borrowed_climate;
    const climate_systems_day_t *const systems = layer_systems_of(input);
    const float64_t t = input->t;
    const float32_t day = (float32_t)climate_systems_day_of_year(t) + (float32_t)(t - floor(t));
    switch (value) {
    case UI_ATLAS_VALUE_TEMPERATURE_NOW: {
        climate_sample_t sample;
        climate_now_t now;
        if (!climate_sample_at(climate, x, y, &sample)) {
            return nanf("");
        }
        arena_reset(weather);
        const float64_t z = sample.z_ground > 0.0f32 ? (float64_t)sample.z_ground : 0.0f64;
        return climate_weather_at(climate, x, y, z, t, layer_week_cell(input, x, y), weather, &now) ? now.temperature_c
                                                                                                    : nanf("");
    }
    case UI_ATLAS_VALUE_RAIN_RADAR: {
        float32_t rain = 0.0f32;
        return climate_layer_rain_now(climate, systems, x, y, t, &rain) ? rain : nanf("");
    }
    case UI_ATLAS_VALUE_WIND_NOW:
    case UI_ATLAS_VALUE_PRESSURE_NOW: {
        climate_air_t air;
        if (!climate_air_at(climate, x, y, day < 365.0f32 ? day : 364.999f32, &air)) {
            return nanf("");
        }
        if (value == UI_ATLAS_VALUE_PRESSURE_NOW) {
            return air.pressure_hpa + climate_systems_pressure_at(systems, x, y, t);
        }
        float32_t wind[2];
        climate_systems_wind_at(systems, x, y, t, wind);
        const float32_t u = air.wind[0] + wind[0], v = air.wind[1] + wind[1];
        return sqrtf(u * u + v * v);
    }
    case UI_ATLAS_VALUE_RAIN_LAST_WEEK:
    case UI_ATLAS_VALUE_SNOW:
    case UI_ATLAS_VALUE_SOIL:
    case UI_ATLAS_VALUE_FLOOD: {
        climate_cell_t cell;
        const climate_week_t *const week = layer_week_cell(input, x, y);
        if (week == nullptr || !climate_cell_get(climate, (int32_t)floor(x), (int32_t)floor(y), &cell) ||
            (cell.flags & CLIMATE_CELL_SEA) != 0u) {
            return nanf("");
        }
        return value == UI_ATLAS_VALUE_RAIN_LAST_WEEK ? week->rain_mm
               : value == UI_ATLAS_VALUE_SNOW         ? week->snow_mm
               : value == UI_ATLAS_VALUE_SOIL         ? week->soil_mm
                                                      : week->flood;
    }
    default:
        return nanf("");
    }
}

/* The four grid values round a pixel blended; with a NaN among them, the nearest whole. */
static float32_t layer_blend(const float32_t corners[4], float32_t fx, float32_t fy) {
    if (isnan(corners[0]) || isnan(corners[1]) || isnan(corners[2]) || isnan(corners[3])) {
        return corners[(fy < 0.5f32 ? 0u : 2u) + (fx < 0.5f32 ? 0u : 1u)];
    }
    const float32_t top = corners[0] + fx * (corners[1] - corners[0]);
    const float32_t bottom = corners[2] + fx * (corners[3] - corners[2]);
    return top + fy * (bottom - top);
}

/* The value's colour; false where the ground shows instead. */
static bool layer_value_colour(const ui_atlas_layer_t *row, float32_t v, uint32_t *out) {
    if (isnan(v) || (row->land_only != 0u && !(v > 0.0f32))) {
        return false;
    }
    switch ((ui_atlas_value_t)row->value) {
    case UI_ATLAS_VALUE_RAIN_RADAR:
        return climate_layer_radar_colour(v, out);
    case UI_ATLAS_VALUE_WIND_NOW:
        *out = climate_layer_wind_colour(v);
        return true;
    case UI_ATLAS_VALUE_PRESSURE_NOW:
        *out = climate_layer_pressure_colour(v);
        return true;
    default:
        *out = ui_atlas_scale_colour(row->scale, v);
        return true;
    }
}

static void layer_band_value(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, uint32_t y,
                             uint32_t height, ui_atlas_scratch_t scratch) {
    layer_cursor_t cursor = {scratch.borrowed_bytes, scratch.borrowed_bytes + scratch.bytes};
    const ui_atlas_layer_t *const row = &ui_atlas_layers[input->view.layer];
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const layer_ground_t ground = layer_ground_read(input, &g, y, height, &cursor);
    const uint32_t width = input->view.width;
    const int64_t n = row->value == UI_ATLAS_VALUE_TEMPERATURE_NOW
                          ? (int64_t)UI_ATLAS_ARROW_PIXELS
                          : (int64_t)(input->sample == 0u ? 1u : input->sample);
    /* The grid, world pixels that are multiples of n, round rows y - 1 to y + height and every column. */
    const int64_t g0 = layer_floor_div(g.column0, n), g1 = layer_floor_div(g.column0 + (int64_t)width - 1, n) + 1;
    const int64_t k0 = layer_floor_div(g.row0 + (int64_t)y - 1, n);
    const int64_t k1 = layer_floor_div(g.row0 + (int64_t)y + (int64_t)height, n) + 1;
    const uint32_t gw = (uint32_t)(g1 - g0 + 1), gh = (uint32_t)(k1 - k0 + 1);
    float32_t *const grid = layer_take(&cursor, (uint64_t)gw * gh * 4u);
    arena_t weather = {0};
    if (row->value == UI_ATLAS_VALUE_TEMPERATURE_NOW) {
        arena_init(&weather, "atlas_weather", layer_take(&cursor, LAYER_WEATHER_BYTES), LAYER_WEATHER_BYTES);
    }
    for (uint32_t b = 0u; b < gh; b++) {
        const float64_t yy = -((float64_t)((k0 + (int64_t)b) * n) + 0.5f64) * g.step;
        for (uint32_t a = 0u; a < gw; a++) {
            const float64_t xx = ((float64_t)((g0 + (int64_t)a) * n) + 0.5f64) * g.step;
            grid[(uint64_t)b * gw + a] = layer_value_at(input, (ui_atlas_value_t)row->value, xx, yy, &weather);
        }
    }
    arena_deinit(&weather);
    /* Each pixel's value, for rows y - 1 to y + height: the radar's contour over grey looks at its neighbours. */
    float32_t *const values = layer_take(&cursor, (uint64_t)width * (height + 2u) * 4u);
    for (uint32_t j = 0u; j < height + 2u; j++) {
        const int64_t w_row = g.row0 + (int64_t)y + (int64_t)j - 1;
        const int64_t k = layer_floor_div(w_row, n);
        const float32_t fy = (float32_t)(w_row - k * n) / (float32_t)n;
        const float32_t *const top = grid + (uint64_t)(k - k0) * gw;
        for (uint32_t i = 0u; i < width; i++) {
            const int64_t w_col = g.column0 + (int64_t)i;
            const int64_t gc = layer_floor_div(w_col, n);
            const uint32_t a = (uint32_t)(gc - g0);
            const float32_t fx = (float32_t)(w_col - gc * n) / (float32_t)n;
            const float32_t corners[4] = {top[a], top[a + 1u], top[gw + a], top[gw + a + 1u]};
            values[(uint64_t)j * width + i] = layer_blend(corners, fx, fy);
        }
    }
    const bool radar = row->value == UI_ATLAS_VALUE_RAIN_RADAR;
    for (uint32_t j = 0u; j < height; j++) {
        const float32_t *const line = values + (uint64_t)(j + 1u) * width;
        for (uint32_t i = 0u; i < width; i++) {
            const float32_t v = line[i];
            const uint32_t here = layer_ground_at(&ground, i, j);
            if (!ui_atlas_pixel_generated(here)) {
                layer_put(target, width, i, y + j, 0u, true, v);
                if (radar && v >= 1.0f32) {
                    /* Over grey the radar is its 1 mm an hour edge only: a pixel at 1 or more beside one under it. */
                    const float32_t around[4] = {i > 0u ? line[i - 1u] : v, i + 1u < width ? line[i + 1u] : v,
                                                 line[(int64_t)i - (int64_t)width], line[i + width]};
                    bool edge = false;
                    for (uint32_t k = 0u; k < 4u; k++) {
                        edge = edge || !(around[k] >= 1.0f32);
                    }
                    if (edge) {
                        target->borrowed_pixels[(uint64_t)(y + j) * width + i] =
                            0xFF000000u | CLIMATE_LAYER_RADAR_RGB[1];
                    }
                }
                continue;
            }
            const float32_t b = layer_shade_at(input, &g, &ground, i, j);
            uint32_t colour = 0u;
            if (!layer_value_colour(row, v, &colour)) {
                colour = ui_atlas_rgb_of(ui_atlas_pixel_rgb565(here));
            }
            layer_put(target, width, i, y + j, ui_atlas_shade_apply(colour, b), false, v);
        }
    }
}

/* ---- Overlays ----------------------------------------------------------------------------------------------------- */

static ui_atlas_picture_t layer_picture(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    return (ui_atlas_picture_t){target->borrowed_pixels, target->borrowed_grey, input->view.width, input->view.height};
}

static uint32_t layer_text_scale(const ui_atlas_draw_input_t *input) {
    return input->view.width >= 1280u ? 2u : 1u;
}

/* Text on the picture, through a canvas over it, so text and clipping work as everywhere else. Over grey too: names are
 * what lets a storm be read before it arrives. */
static void layer_text(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, int32_t x, int32_t y,
                       const char *text, uint32_t colour, uint32_t scale) {
    if (input->borrowed_font == nullptr) {
        return;
    }
    ui_canvas_t canvas;
    if (!ui_canvas_start(&canvas, (uint8_t *)target->borrowed_pixels, (uint64_t)input->view.width * 4u,
                         input->view.width, input->view.height, (uint32_t *)(void *)target->borrowed_scratch)) {
        return;
    }
    const ui_text_style_t style = {.color = 0xFF000000u | colour, .shadow = 0xFF000000u, .scale = scale};
    ui_font_draw(input->borrowed_font, &canvas, x, y, text, style);
}

/* The climate's own marks over the whole picture: currents' and wind's arrows, latitude lines. */
static void layer_overlay_marks(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    const ui_atlas_layer_t *const row = &ui_atlas_layers[input->view.layer];
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const climate_layer_frame_t frame = {
        .west_x = (float64_t)g.column0 * g.step,
        .north_y = -(float64_t)g.row0 * g.step,
        .step = g.step,
        .column_index = g.column0,
        .row_index = g.row0,
        .borrowed_scratch = nullptr,
        .width = input->view.width,
        .height = input->view.height,
        .sample = 1u,
        .parts = CLIMATE_LAYER_PART_MARKS,
    };
    const float32_t day = row->time == UI_ATLAS_TIME_NORMALS ? (float32_t)input->view.day : 0.0f32;
    climate_layer_draw_scaled(input->borrowed_climate, (climate_layer_t)row->climate_layer, &frame, day,
                              (uint8_t *)target->borrowed_pixels, input->view.width * 4u);
}

/* The first picture pixel at or past 0 whose world pixel is offset more than a multiple of spacing. */
static uint32_t layer_grid_first(int64_t index, int64_t spacing, int64_t offset) {
    int64_t first = (offset - index) % spacing;
    first = first < 0 ? first + spacing : first;
    return (uint32_t)first;
}

static void layer_overlay_wind_now(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const ui_atlas_picture_t picture = layer_picture(input, target);
    const climate_systems_day_t *const systems = layer_systems_of(input);
    const float64_t t = input->t;
    const float32_t day = (float32_t)climate_systems_day_of_year(t);
    const int64_t spacing = UI_ATLAS_ARROW_PIXELS;
    for (uint32_t j = layer_grid_first(g.row0, spacing, spacing / 2); j < input->view.height;
         j += UI_ATLAS_ARROW_PIXELS) {
        for (uint32_t i = layer_grid_first(g.column0, spacing, spacing / 2); i < input->view.width;
             i += UI_ATLAS_ARROW_PIXELS) {
            const float64_t x = ui_atlas_pixel_x(&g, i), y = ui_atlas_pixel_y(&g, j);
            climate_air_t air;
            if (!climate_air_at(input->borrowed_climate, x, y, day, &air)) {
                continue;
            }
            float32_t wind[2];
            climate_systems_wind_at(systems, x, y, t, wind);
            const float32_t u = air.wind[0] + wind[0], v = air.wind[1] + wind[1];
            ui_atlas_draw_arrow(&picture, (int32_t)i, (int32_t)j, u, v, fminf(30.0f32, 2.0f32 * sqrtf(u * u + v * v)),
                                CLIMATE_LAYER_WIND_ARROW, UI_ATLAS_MODE_FILL);
        }
    }
}

/* A big system's ring colour. */
static uint32_t layer_ring_colour(uint8_t kind) {
    return kind == CLIMATE_SYSTEM_HURRICANE ? CLIMATE_LAYER_HURRICANE_RING
           : kind == CLIMATE_SYSTEM_LOW     ? CLIMATE_LAYER_LOW_RING
                                            : CLIMATE_LAYER_HIGH_RING;
}

/* A system's name beside its centre. */
static void layer_system_name(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target,
                              const climate_system_t *system, int32_t cx, int32_t cy, int32_t radius) {
    char title[24];
    climate_system_title(system, input->t, title);
    const uint32_t scale = layer_text_scale(input);
    const int32_t offset = radius < 12 ? 12 : (radius > 40 ? 40 : radius);
    layer_text(input, target, cx + offset / 2 + 4, cy - offset / 2 - (int32_t)(UI_FONT_GLYPH_HEIGHT * scale), title,
               0xFFFFFFu, scale);
}

/* Every alive big system's ring and name only over grey: what every other now layer shows of the systems there. */
static void layer_outlines_on_grey(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const ui_atlas_picture_t picture = layer_picture(input, target);
    const climate_systems_day_t *const systems = layer_systems_of(input);
    for (uint32_t k = 0u; k < systems->system_count; k++) {
        const climate_system_t *const system = &systems->borrowed_systems[k];
        climate_system_state_t state;
        climate_system_state_at(system, input->t, &state);
        if (state.alive == 0u) {
            continue;
        }
        const int32_t cx = (int32_t)floorf(layer_to_column(&g, state.x));
        const int32_t cy = (int32_t)floorf(layer_to_row(&g, state.y));
        const int32_t radius = (int32_t)lround((float64_t)system->radius / g.step);
        ui_atlas_draw_circle(&picture, cx, cy, radius, layer_ring_colour(system->kind), UI_ATLAS_MODE_ON_GREY);
        if (system->kind == CLIMATE_SYSTEM_HURRICANE &&
            ui_atlas_draw_allowed(&picture, cx, cy, UI_ATLAS_MODE_ON_GREY)) {
            layer_system_name(input, target, system, cx, cy, radius);
        }
    }
}

static void layer_overlay_outlines(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    layer_outlines_on_grey(input, target);
}

/* An H or an L, scale 2, at a centre over generated land. */
static void layer_mark(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, int32_t cx, int32_t cy,
                       bool high) {
    const ui_atlas_picture_t picture = layer_picture(input, target);
    if (!ui_atlas_draw_allowed(&picture, cx, cy, UI_ATLAS_MODE_FILL)) {
        return;
    }
    const uint32_t scale = 2u * layer_text_scale(input);
    layer_text(input, target, cx - (int32_t)(5u * scale) / 2, cy - (int32_t)(7u * scale) / 2, high ? "H" : "L",
               high ? CLIMATE_LAYER_HIGH_MARK : CLIMATE_LAYER_LOW_MARK, scale);
}

static void layer_overlay_pressure_now(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const ui_atlas_picture_t picture = layer_picture(input, target);
    const uint32_t width = input->view.width, height = input->view.height;
    const int64_t n = UI_ATLAS_ISOBAR_PIXELS;
    /* Isobars every 4 hPa by marching squares, on world pixels that are multiples of 4, from each pixel's own value. */
    const uint32_t i0 = layer_grid_first(g.column0, n, 0), j0 = layer_grid_first(g.row0, n, 0);
    for (uint32_t j = j0; j + (uint32_t)n < height; j += (uint32_t)n) {
        for (uint32_t i = i0; i + (uint32_t)n < width; i += (uint32_t)n) {
            const float32_t *const f = target->borrowed_field;
            const float32_t corners[4] = {f[(uint64_t)j * width + i], f[(uint64_t)j * width + i + (uint32_t)n],
                                          f[(uint64_t)(j + (uint32_t)n) * width + i + (uint32_t)n],
                                          f[(uint64_t)(j + (uint32_t)n) * width + i]};
            if (isnan(corners[0]) || isnan(corners[1]) || isnan(corners[2]) || isnan(corners[3])) {
                continue;
            }
            const float32_t low = fminf(fminf(corners[0], corners[1]), fminf(corners[2], corners[3]));
            const float32_t high = fmaxf(fmaxf(corners[0], corners[1]), fmaxf(corners[2], corners[3]));
            for (float32_t level = ceilf(low / 4.0f32) * 4.0f32; level <= high; level += 4.0f32) {
                ui_atlas_segment_t segments[2];
                const uint32_t count = ui_atlas_march(corners, level, (float32_t)n, segments);
                for (uint32_t s = 0u; s < count; s++) {
                    ui_atlas_draw_line(&picture, (int32_t)i + (int32_t)floorf(segments[s].x0),
                                       (int32_t)j + (int32_t)floorf(segments[s].y0),
                                       (int32_t)i + (int32_t)floorf(segments[s].x1),
                                       (int32_t)j + (int32_t)floorf(segments[s].y1), CLIMATE_LAYER_PRESSURE_LINE, 0u,
                                       UI_ATLAS_MODE_FILL);
                }
            }
        }
    }
    const climate_systems_day_t *const systems = layer_systems_of(input);
    for (uint32_t k = 0u; k < systems->system_count; k++) {
        climate_system_state_t state;
        climate_system_state_at(&systems->borrowed_systems[k], input->t, &state);
        if (state.alive != 0u) {
            layer_mark(input, target, (int32_t)floorf(layer_to_column(&g, state.x)),
                       (int32_t)floorf(layer_to_row(&g, state.y)),
                       systems->borrowed_systems[k].kind == CLIMATE_SYSTEM_HIGH);
        }
    }
    layer_outlines_on_grey(input, target);
}

/* A hurricane's forecast cone: circles a quarter day apart from now to the end of its life or 7 days, each at the steered
 * track plus phase 07's forecast error, radius 1.177 sqrt(sigma(tau)^2 + R^2). */
static void layer_cone(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target,
                       const ui_atlas_geometry_t *g, const climate_system_t *system) {
    float32_t cx[UI_ATLAS_CONE_CIRCLES], cy[UI_ATLAS_CONE_CIRCLES], r[UI_ATLAS_CONE_CIRCLES];
    uint32_t count = 0u;
    const uint64_t seed = climate_seed(input->borrowed_climate);
    for (uint32_t k = 0u; k < UI_ATLAS_CONE_CIRCLES; k++) {
        const float64_t tau = 0.25f64 * (float64_t)k;
        climate_system_state_t state;
        climate_system_state_at(system, input->t + tau, &state);
        if (state.alive == 0u) {
            break;
        }
        /* The error is drawn for whole days ahead; between them it moves in a straight line from none now. */
        const uint32_t whole = (uint32_t)floor(tau);
        float64_t before[2] = {0.0f64, 0.0f64}, after[2] = {0.0f64, 0.0f64};
        if (whole > 0u) {
            climate_outlook_error(seed, system->id, input->census_week, whole, before);
        }
        climate_outlook_error(seed, system->id, input->census_week, whole + 1u, after);
        const float64_t share = tau - (float64_t)whole;
        const float64_t sigma = climate_outlook_sigma(tau);
        const float64_t ex = (before[0] + share * (after[0] - before[0])) * sigma;
        const float64_t ey = (before[1] + share * (after[1] - before[1])) * sigma;
        cx[count] = layer_to_column(g, state.x + ex);
        cy[count] = layer_to_row(g, state.y + ey);
        r[count] = (float32_t)(UI_ATLAS_CONE_REACH * sqrt(sigma * sigma + (float64_t)system->radius * system->radius) /
                               g->step);
        count++;
    }
    const ui_atlas_picture_t picture = layer_picture(input, target);
    ui_atlas_draw_cone(&picture, cx, cy, r, count, 0xFF6060u, 0xFF2020u, 64u,
                       target->borrowed_scratch + UI_CANVAS_ROW_BYTES);
}

/* The two arcs of a hurricane's spiral, round its eye. */
static void layer_spiral(const ui_atlas_picture_t *picture, int32_t cx, int32_t cy, int32_t eye) {
    const float32_t inner = (float32_t)(eye + 2), outer = (float32_t)(eye + 9);
    for (uint32_t arm = 0u; arm < 2u; arm++) {
        for (uint32_t k = 0u; k <= 24u; k++) {
            const float32_t share = (float32_t)k / 24.0f32;
            const float32_t angle = 3.14159265f32 * ((float32_t)arm + share);
            const float32_t radius = inner + (outer - inner) * share;
            ui_atlas_draw_point(picture, cx + (int32_t)lroundf(radius * cosf(angle)),
                                cy - (int32_t)lroundf(radius * sinf(angle)), 0xFFFFFFu, UI_ATLAS_MODE_FILL);
        }
    }
}

/* A low's cold front: a 2-pixel blue line, and a small triangle every 24 pixels on the side it moves toward. */
static void layer_front(const ui_atlas_picture_t *picture, const ui_atlas_geometry_t *g, const climate_system_t *low,
                        const climate_system_state_t *state, float64_t t) {
    float64_t from[2], to[2];
    if (!climate_systems_front(low, t, from, to)) {
        return;
    }
    const float32_t x0 = layer_to_column(g, from[0]), y0 = layer_to_row(g, from[1]);
    const float32_t x1 = layer_to_column(g, to[0]), y1 = layer_to_row(g, to[1]);
    const float32_t dx = x1 - x0, dy = y1 - y0, length = sqrtf(dx * dx + dy * dy);
    if (length < 1.0f32) {
        return;
    }
    float32_t nx = -dy / length, ny = dx / length;
    /* Toward where it moves: heading's v is north, up the screen. */
    if (nx * state->heading[0] - ny * state->heading[1] < 0.0f32) {
        nx = -nx;
        ny = -ny;
    }
    for (int32_t w = 0; w < 2; w++) {
        ui_atlas_draw_line(picture, (int32_t)floorf(x0 + nx * (float32_t)w), (int32_t)floorf(y0 + ny * (float32_t)w),
                           (int32_t)floorf(x1 + nx * (float32_t)w), (int32_t)floorf(y1 + ny * (float32_t)w),
                           CLIMATE_LAYER_FRONT, 0u, UI_ATLAS_MODE_FILL);
    }
    for (float32_t s = 12.0f32; s + 4.0f32 < length; s += 24.0f32) {
        const float32_t bx = x0 + dx * s / length, by = y0 + dy * s / length;
        const float32_t ux = dx / length, uy = dy / length;
        const int32_t ax = (int32_t)floorf(bx - 4.0f32 * ux), ay = (int32_t)floorf(by - 4.0f32 * uy);
        const int32_t cx = (int32_t)floorf(bx + 4.0f32 * ux), cy = (int32_t)floorf(by + 4.0f32 * uy);
        const int32_t tx = (int32_t)floorf(bx + 5.0f32 * nx), ty = (int32_t)floorf(by + 5.0f32 * ny);
        ui_atlas_draw_line(picture, ax, ay, tx, ty, CLIMATE_LAYER_FRONT, 0u, UI_ATLAS_MODE_FILL);
        ui_atlas_draw_line(picture, tx, ty, cx, cy, CLIMATE_LAYER_FRONT, 0u, UI_ATLAS_MODE_FILL);
    }
}

static void layer_overlay_systems(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const ui_atlas_picture_t picture = layer_picture(input, target);
    const climate_systems_day_t *const systems = layer_systems_of(input);
    const float64_t t = input->t;
    /* Cones first, so rings, tracks and names lie over them; highs, then lows, then hurricanes on top. */
    for (uint32_t k = 0u; k < systems->system_count; k++) {
        const climate_system_t *const system = &systems->borrowed_systems[k];
        if (system->kind == CLIMATE_SYSTEM_HURRICANE) {
            layer_cone(input, target, &g, system);
        }
    }
    static const uint8_t ORDER[3] = {CLIMATE_SYSTEM_HIGH, CLIMATE_SYSTEM_LOW, CLIMATE_SYSTEM_HURRICANE};
    for (uint32_t o = 0u; o < 3u; o++) {
        for (uint32_t k = 0u; k < systems->system_count; k++) {
            const climate_system_t *const system = &systems->borrowed_systems[k];
            if (system->kind != ORDER[o]) {
                continue;
            }
            climate_system_state_t state;
            climate_system_state_at(system, t, &state);
            if (state.alive == 0u) {
                continue;
            }
            const int32_t cx = (int32_t)floorf(layer_to_column(&g, state.x));
            const int32_t cy = (int32_t)floorf(layer_to_row(&g, state.y));
            const int32_t radius = (int32_t)lround((float64_t)system->radius / g.step);
            if (system->kind == CLIMATE_SYSTEM_HIGH) {
                ui_atlas_draw_circle(&picture, cx, cy, radius, 0x80A0E0u, UI_ATLAS_MODE_OUTLINE);
                layer_mark(input, target, cx, cy, true);
                continue;
            }
            /* The track behind it, a dotted line through its quarter-day samples since it formed. */
            int32_t px = (int32_t)floorf(layer_to_column(&g, system->samples[0].x));
            int32_t py = (int32_t)floorf(layer_to_row(&g, system->samples[0].y));
            for (uint32_t s = 1u; s < system->sample_count; s++) {
                if (system->spawn_day + CLIMATE_TRACK_STEP_DAYS * (float64_t)s > t) {
                    break;
                }
                const int32_t nx = (int32_t)floorf(layer_to_column(&g, system->samples[s].x));
                const int32_t ny = (int32_t)floorf(layer_to_row(&g, system->samples[s].y));
                ui_atlas_draw_line(&picture, px, py, nx, ny, CLIMATE_LAYER_TRACK, 2u, UI_ATLAS_MODE_OUTLINE);
                px = nx;
                py = ny;
            }
            ui_atlas_draw_line(&picture, px, py, cx, cy, CLIMATE_LAYER_TRACK, 2u, UI_ATLAS_MODE_OUTLINE);
            if (system->kind == CLIMATE_SYSTEM_LOW) {
                ui_atlas_draw_circle(&picture, cx, cy, radius, CLIMATE_LAYER_LOW_RING, UI_ATLAS_MODE_OUTLINE);
                layer_front(&picture, &g, system, &state, t);
                layer_mark(input, target, cx, cy, false);
                continue;
            }
            ui_atlas_draw_circle(&picture, cx, cy, radius, CLIMATE_LAYER_HURRICANE_RING, UI_ATLAS_MODE_OUTLINE);
            const int32_t eye = (int32_t)lround((float64_t)system->core_radius / g.step);
            ui_atlas_draw_disc(&picture, cx, cy, eye < 2 ? 2 : eye, 0x202020u, 255u, UI_ATLAS_MODE_FILL);
            layer_spiral(&picture, cx, cy, eye < 2 ? 2 : eye);
            layer_system_name(input, target, system, cx, cy, radius);
        }
    }
}

static void layer_overlay_thunder(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    const ui_atlas_geometry_t g = ui_atlas_geometry(&input->view);
    const ui_atlas_picture_t picture = layer_picture(input, target);
    const climate_systems_day_t *const systems = layer_systems_of(input);
    if (g.step <= (float64_t)UI_ATLAS_CELLS_MAX_BPP) {
        static const int32_t ZIGZAG[4][2] = {{-1, -3}, {1, -1}, {-1, 1}, {1, 3}};
        for (uint32_t k = 0u; k < systems->thunder_count; k++) {
            const climate_thunder_t *const cell = &systems->borrowed_thunder[k];
            float64_t centre[2], share = 0.0f64;
            if (!climate_systems_thunder_at(cell, input->t, centre, &share)) {
                continue;
            }
            const int32_t cx = (int32_t)floorf(layer_to_column(&g, centre[0]));
            const int32_t cy = (int32_t)floorf(layer_to_row(&g, centre[1]));
            const int32_t radius = (int32_t)lround((float64_t)cell->radius / g.step);
            uint32_t colour = CLIMATE_LAYER_THUNDER_DISC;
            (void)climate_layer_radar_colour(cell->peak_mm_hour * (float32_t)share, &colour);
            ui_atlas_draw_disc(&picture, cx, cy, radius, colour, 255u, UI_ATLAS_MODE_FILL);
            ui_atlas_draw_circle(&picture, cx, cy, radius, CLIMATE_LAYER_THUNDER_DISC, UI_ATLAS_MODE_OUTLINE);
            for (uint32_t z = 0u; z < 3u; z++) {
                ui_atlas_draw_line(&picture, cx + ZIGZAG[z][0], cy + ZIGZAG[z][1], cx + ZIGZAG[z + 1u][0],
                                   cy + ZIGZAG[z + 1u][1], CLIMATE_LAYER_LIGHTNING, 0u, UI_ATLAS_MODE_FILL);
            }
        }
    }
    layer_outlines_on_grey(input, target);
}

/* ---- One picture -------------------------------------------------------------------------------------------------- */

uint64_t ui_atlas_band_scratch_bytes(uint32_t width, uint32_t height) {
    const uint64_t w = width, h = height;
    const uint64_t pieces = (w + 2u) * 8u + (h + 2u) * 8u                        /* the ground's columns and rows */
                            + (2u * w + 6u) * (2u * h + 6u) * 4u                 /* what it reads: s / 2^L is under 2 */
                            + LAYER_GROUND_READ_BYTES + (w + 2u) * (h + 2u) * 4u /* its read's loan, the ground */
                            + w * (h + 1u) * 4u + 2u * (w + 3u) * 4u             /* a climate field and its grid rows */
                            + (w + 3u) * (h + 4u) * 4u + w * (h + 2u) * 4u       /* an own value's grid and values */
                            + LAYER_WEATHER_BYTES;
    return pieces + 16u * 64u; /* each piece rounded to 64 bytes */
}

/* A layer that reads the climate, in a world with none (a test world): only its ground is drawn. */
static bool layer_without_climate(const ui_atlas_draw_input_t *input) {
    const ui_atlas_layer_t *const row = &ui_atlas_layers[input->view.layer];
    return input->borrowed_climate == nullptr && (row->band != layer_band_terrain || row->overlay != nullptr);
}

void ui_atlas_draw_band(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target, uint32_t band,
                        uint32_t bands, ui_atlas_scratch_t scratch) {
    const uint32_t height = input->view.height;
    const uint32_t y0 = (uint32_t)((uint64_t)height * band / bands);
    const uint32_t y1 = (uint32_t)((uint64_t)height * (band + 1u) / bands);
    if (y1 > y0) {
        ui_atlas_band_draw_t *const draw =
            layer_without_climate(input) ? layer_band_terrain : ui_atlas_layers[input->view.layer].band;
        draw(input, target, y0, y1 - y0, scratch);
    }
}

void ui_atlas_draw_finish(const ui_atlas_draw_input_t *input, const ui_atlas_target_t *target) {
    const ui_atlas_layer_t *const row = &ui_atlas_layers[input->view.layer];
    if (row->overlay != nullptr && !layer_without_climate(input)) {
        row->overlay(input, target);
    }
    const uint64_t count = (uint64_t)input->view.width * input->view.height;
    for (uint64_t k = 0u; k < count; k++) {
        if (row->over_grey == 0u && target->borrowed_grey[k] != 0u) {
            target->borrowed_pixels[k] = UI_ATLAS_COLOUR_UNGENERATED; /* no climate drawn over ungenerated land: 2.4 */
        } else {
            target->borrowed_pixels[k] |= 0xFF000000u;
        }
    }
}

bool ui_atlas_systems_build(const climate_t *climate, arena_t *arena, const ui_atlas_view_t *view, float64_t t,
                            climate_systems_day_t *out) {
    const float64_t step = ui_atlas_blocks_per_pixel(view->zoom);
    const float64_t half_w = 0.5f64 * (float64_t)view->width * step, half_h = 0.5f64 * (float64_t)view->height * step;
    const float32_t radius = (float32_t)sqrt(half_w * half_w + half_h * half_h);
    const bool cells = step <= (float64_t)UI_ATLAS_CELLS_MAX_BPP;
    const float32_t thunder = cells ? fminf(radius, CLIMATE_THUNDER_QUERY_MAX) : 0.0f32;
    return climate_systems_day_build(climate, arena, view->center[0], view->center[1], radius, thunder,
                                     (int64_t)floor(t), cells ? CLIMATE_SYSTEMS_ALL : CLIMATE_SYSTEMS_BIG, out);
}

/* ---- A ground from the terrain's estimate ------------------------------------------------------------------------- */

uint32_t ui_atlas_estimate_colour(float32_t z) {
    return z < 0.0f32     ? 0x2050A0u
           : z < 3.0f32   ? 0xE0D090u
           : z < 300.0f32 ? 0x50A040u
           : z < 700.0f32 ? 0x808070u
                          : 0xF0F0F0u;
}

void ui_atlas_estimate_read(void *borrowed_context, uint32_t level, int64_t px, int64_t py, uint32_t width,
                            uint32_t height, uint32_t *out, arena_t *scratch) {
    (void)scratch;
    const ui_atlas_estimate_t *const estimate = borrowed_context;
    const int64_t size = (int64_t)1 << level;
    for (uint32_t j = 0u; j < height; j++) {
        /* Row py + j covers y from -(row + 1) 2^L to -row 2^L - 1: its middle column. */
        const int64_t y = -1 - ((py + (int64_t)j) * size + size / 2);
        for (uint32_t i = 0u; i < width; i++) {
            const int64_t x = (px + (int64_t)i) * size + size / 2;
            if (x >= estimate->generated_east_x || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
                out[(uint64_t)j * width + i] = UI_ATLAS_PIXEL_NONE;
                continue;
            }
            const float32_t h = terrain_height_estimate(estimate->borrowed_terrain, (int32_t)x, (int32_t)y);
            const float32_t z = h < -32000.0f32 ? -32000.0f32 : (h > 32000.0f32 ? 32000.0f32 : h);
            out[(uint64_t)j * width + i] =
                ui_atlas_pixel_pack(ui_atlas_rgb565_of(ui_atlas_estimate_colour(z)), (int16_t)lroundf(z));
        }
    }
}
