/* bench_landform's pictures. See README.md. */
#include "landform/draw/index.h"

#include "terrain/climate_sample/index.h"
#include "climate/index.h"
#include "display/headless/png/index.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Pictures ------------------------------------------------------------------------------------------------------ */

typedef struct lf_ramp_stop_t {
    int32_t z;       /* 0 */
    uint32_t colour; /* 4 */
} lf_ramp_stop_t;
static_assert(sizeof(lf_ramp_stop_t) == 8);

/* bench_world_map's ramp, so the two programs' maps read alike. */
static const lf_ramp_stop_t LF_RAMP[] = {
    {-250, 0x305020u}, {0, 0x4C9A3Cu},   {60, 0x8CC468u},   {150, 0xE8DC70u},  {300, 0xD89038u},
    {500, 0x8C5A32u},  {750, 0x8A8A8Au}, {1000, 0xD8D8D8u}, {1250, 0xFFFFFFu},
};
enum : uint32_t { LF_RAMP_STOPS = sizeof(LF_RAMP) / sizeof(LF_RAMP[0]) };

static uint32_t lf_mix(uint32_t a, uint32_t b, uint32_t over, uint32_t of) {
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        const uint32_t ca = (a >> shift) & 0xFFu, cb = (b >> shift) & 0xFFu;
        out |= ((ca * (of - over) + cb * over) / of) << shift;
    }
    return out;
}

static uint32_t lf_shade(uint32_t colour, uint32_t percent) {
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        uint32_t channel = ((colour >> shift) & 0xFFu) * percent / 100u;
        out |= (channel < 255u ? channel : 255u) << shift;
    }
    return out;
}

static uint32_t lf_land_colour(float32_t height) {
    const int32_t z = (int32_t)floorf(height);
    if (z <= LF_RAMP[0].z) {
        return LF_RAMP[0].colour;
    }
    for (uint32_t i = 1u; i < LF_RAMP_STOPS; i++) {
        if (z <= LF_RAMP[i].z) {
            return lf_mix(LF_RAMP[i - 1u].colour, LF_RAMP[i].colour, (uint32_t)(z - LF_RAMP[i - 1u].z),
                          (uint32_t)(LF_RAMP[i].z - LF_RAMP[i - 1u].z));
        }
    }
    return LF_RAMP[LF_RAMP_STOPS - 1u].colour;
}

/* Shaded relief of a pixel of a grid of heights, lit from the low x and y, the sea by its depth. dry, if not nullptr, marks
 * dry basins: land, however low. */
static uint32_t lf_relief_colour(const float32_t *h, const uint8_t *dry, uint32_t wide, uint32_t x, uint32_t y,
                                 uint32_t step) {
    const uint64_t at = (uint64_t)y * wide + x;
    const float32_t z = h[at];
    if (z <= 0.0f32 && (dry == nullptr || dry[at] == 0u)) {
        const uint32_t depth = z < -100.0f32 ? 100u : (uint32_t)(-z);
        return lf_mix(LF_COLOUR_SEA, LF_COLOUR_DEEP, depth, 100u);
    }
    const float32_t west = x > 0u ? h[at - 1u] : z, north = y > 0u ? h[at - wide] : z;
    int32_t percent = 100 + (int32_t)(16.0f32 * (west + north - 2.0f32 * z) / (float32_t)step);
    percent = percent < 55 ? 55 : (percent > 140 ? 140 : percent);
    return lf_shade(lf_land_colour(z), (uint32_t)percent);
}

/* Contours on land, where the height's band changes between a pixel and its east or south neighbour: thin every 50, and
 * every 500 thick, two pixels. The same rule plan 007 phase 02 puts in the Atlas, with the same numbers. dry as for
 * lf_relief_colour: a dry basin is land, so its contours ring the hollow. */
static void lf_contours(const float32_t *h, const uint8_t *dry, uint32_t wide, uint32_t high, uint32_t *pixels,
                        uint32_t pitch, uint32_t left, uint32_t top, bool flip) {
    for (uint32_t y = 0u; y + 1u < high; y++) {
        for (uint32_t x = 0u; x + 1u < wide; x++) {
            const uint64_t at = (uint64_t)y * wide + x;
            const float32_t z = h[at], e = h[at + 1u], s = h[at + wide];
            const bool sea = (z <= 0.0f32 && (dry == nullptr || dry[at] == 0u)) ||
                             (e <= 0.0f32 && (dry == nullptr || dry[at + 1u] == 0u)) ||
                             (s <= 0.0f32 && (dry == nullptr || dry[at + wide] == 0u));
            if (sea) {
                continue;
            }
            const int32_t band = (int32_t)floorf(z / 50.0f32);
            const int32_t index = (int32_t)floorf(z / 500.0f32);
            const uint32_t py = flip ? top + (high - 1u - y) : top + y;
            uint32_t *const p = &pixels[(uint64_t)py * pitch + left + x];
            if (index != (int32_t)floorf(e / 500.0f32) || index != (int32_t)floorf(s / 500.0f32)) {
                p[0] = lf_shade(p[0], 30u);
                p[flip ? 1u : pitch + 1u] = lf_shade(p[flip ? 1u : pitch + 1u], 30u);
            } else if (band != (int32_t)floorf(e / 50.0f32) || band != (int32_t)floorf(s / 50.0f32)) {
                p[0] = lf_shade(p[0], 65u);
            }
        }
    }
}

static void lf_fill(uint32_t *pixels, uint32_t pitch, uint32_t high, int32_t x, int32_t y, uint32_t w, uint32_t h,
                    uint32_t colour) {
    for (int32_t j = y; j < y + (int32_t)h; j++) {
        for (int32_t i = x; i < x + (int32_t)w; i++) {
            if (i >= 0 && j >= 0 && i < (int32_t)pitch && j < (int32_t)high) {
                pixels[(uint64_t)j * pitch + (uint32_t)i] = colour;
            }
        }
    }
}

/* Text in the game's font, its top left at x, y, on a box of back unless back is UINT32_MAX. */
static void lf_text(const lf_memory_t *m, uint32_t pitch, uint32_t high, int32_t x, int32_t y, const char *text,
                    uint32_t colour, uint32_t back, uint32_t scale) {
    const ui_font_t *const font = m->borrowed_font;
    if (font == nullptr) {
        return;
    }
    uint32_t wide = 0u;
    for (const char *p = text; *p != '\0'; p++) {
        const bool degree = (uint8_t)p[0] == 0xC2u && (uint8_t)p[1] == 0xB0u;
        wide += (degree ? ui_font_glyph_extra(font, 0u) : ui_font_glyph(font, (uint8_t)*p))->advance;
        p += degree ? 1 : 0;
    }
    if (back != UINT32_MAX) {
        lf_fill(m->pixels, pitch, high, x - 2, y - 2, wide * scale + 4u, UI_FONT_GLYPH_HEIGHT * scale + 4u, back);
    }
    for (const char *p = text; *p != '\0'; p++) {
        const bool degree = (uint8_t)p[0] == 0xC2u && (uint8_t)p[1] == 0xB0u;
        const ui_font_glyph_t *const glyph = degree ? ui_font_glyph_extra(font, 0u) : ui_font_glyph(font, (uint8_t)*p);
        p += degree ? 1 : 0;
        for (uint32_t row = 0u; row < UI_FONT_GLYPH_HEIGHT; row++) {
            for (uint32_t column = 0u; column < UI_FONT_GLYPH_WIDTH; column++) {
                if (((glyph->rows[row] >> column) & 1u) != 0u) {
                    lf_fill(m->pixels, pitch, high, x + (int32_t)(column * scale), y + (int32_t)(row * scale), scale,
                            scale, colour);
                }
            }
        }
        x += (int32_t)(glyph->advance * scale);
    }
}

static void lf_mark(uint32_t *pixels, uint32_t side, const lf_grid_t *grid, const lf_place_t *place, uint32_t colour) {
    if (place->found == 0u) {
        return;
    }
    const int32_t px = (place->x - grid->origin_x) / (int32_t)grid->step;
    const int32_t py = (place->y - grid->origin_y) / (int32_t)grid->step;
    lf_fill(pixels, side, side, px - 24, py - 2, 49u, 5u, 0x000000u);
    lf_fill(pixels, side, side, px - 2, py - 24, 5u, 49u, 0x000000u);
    lf_fill(pixels, side, side, px - 22, py - 1, 45u, 3u, colour);
    lf_fill(pixels, side, side, px - 1, py - 22, 3u, 45u, colour);
}

static bool lf_png(const lf_memory_t *m, uint64_t seed, const char *what, uint32_t wide, uint32_t high) {
    char path[LF_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%" PRIu64 "_%s.png", m->directory, seed, what);
    uint64_t bytes = 0u;
    if (!display_png_encode((const uint8_t *)m->pixels, wide * 4u, wide, high, m->stream, m->stream_bytes, &bytes)) {
        fprintf(stderr, "bench_landform: cannot encode %s\n", path);
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_landform: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    const bool wrote = fwrite(m->stream, 1u, bytes, file) == bytes;
    if (fclose(file) != 0 || !wrote) {
        fprintf(stderr, "bench_landform: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    printf("  picture: %s, %u by %u\n", path, wide, high);
    return true;
}

bool lf_draw_maps(const lf_grid_t *grid, const lf_memory_t *m, const lf_result_t *r) {
    const uint32_t side = grid->side;
    uint32_t *const px = m->pixels;
    const lf_place_t *const places = r->place;
    bool ok = true;
    /* Relief with contours, and the places of the /tp lines. */
    for (uint32_t y = 0u; y < side; y++) {
        for (uint32_t x = 0u; x < side; x++) {
            px[(uint64_t)y * side + x] = lf_relief_colour(grid->h, grid->dry, side, x, y, grid->step);
        }
    }
    lf_contours(grid->h, grid->dry, side, side, px, side, 0u, 0u, false);
    lf_mark(px, side, grid, &places[LF_PLACE_BELOW_SEA], 0xFFFFFFu);
    lf_mark(px, side, grid, &places[LF_PLACE_PEAK], 0xFF2020u);
    lf_mark(px, side, grid, &places[LF_PLACE_RANGE], 0xA05020u);
    lf_mark(px, side, grid, &places[LF_PLACE_PLATEAU], 0xFFFF40u);
    lf_mark(px, side, grid, &places[LF_PLACE_KNOB_HIGH], 0xFF40FFu);
    lf_mark(px, side, grid, &places[LF_PLACE_PLAINS], 0x40FFFFu);
    ok = lf_png(m, r->seed, "relief_64km", side, side) && ok;
    /* Slope classes. */
    for (uint64_t i = 0u; i < (uint64_t)side * side; i++) {
        const bool sea = grid->h[i] <= 0.0f32 && (grid->dry == nullptr || grid->dry[i] == 0u);
        px[i] = sea ? LF_COLOUR_SEA : LF_CLASS_COLOUR[lf_class((float64_t)grid->g[i])];
    }
    ok = lf_png(m, r->seed, "slope_64km", side, side) && ok;
    /* Plateaus, their escarpment edges, and the ranges, over grey relief. */
    for (uint32_t y = 0u; y < side; y++) {
        for (uint32_t x = 0u; x < side; x++) {
            const uint64_t at = (uint64_t)y * side + x;
            const float32_t z = grid->h[at];
            uint32_t colour = 0xB0C8E0u;
            if (z > 0.0f32) {
                const uint32_t grey = 150u + (uint32_t)(fminf(z, 1250.0f32) * 90.0f32 / 1250.0f32);
                colour = (grey << 16u) | (grey << 8u) | grey;
                const float32_t west = x > 0u ? grid->h[at - 1u] : z, north = y > 0u ? grid->h[at - side] : z;
                int32_t percent = 100 + (int32_t)(16.0f32 * (west + north - 2.0f32 * z) / (float32_t)grid->step);
                percent = percent < 55 ? 55 : (percent > 140 ? 140 : percent);
                colour = lf_shade(colour, (uint32_t)percent);
            }
            const bool range_rim = m->range[at] != 0u &&
                                   (x == 0u || y == 0u || x + 1u == side || y + 1u == side || m->range[at - 1u] == 0u ||
                                    m->range[at + 1u] == 0u || m->range[at - side] == 0u || m->range[at + side] == 0u);
            const bool plateau_rim =
                m->plateau[at] != 0u &&
                (x == 0u || y == 0u || x + 1u == side || y + 1u == side || m->plateau[at - 1u] == 0u ||
                 m->plateau[at + 1u] == 0u || m->plateau[at - side] == 0u || m->plateau[at + side] == 0u);
            colour = m->range[at] != 0u ? lf_mix(colour, 0xC8A070u, 1u, 2u) : colour;
            colour = range_rim ? 0x6B3A1Eu : colour;
            colour = m->plateau[at] != 0u ? lf_mix(colour, 0xF0E070u, 2u, 3u) : colour;
            colour = plateau_rim ? 0x303030u : colour;
            colour = m->plateau[at] == 2u ? 0xE00000u : colour;
            px[at] = colour;
        }
    }
    ok = lf_png(m, r->seed, "plateaus_64km", side, side) && ok;
    return ok;
}

/* One true-scale cross-section, a block a pixel both ways, LF_PANEL_WIDE long, centred on x, y along ax, ay. Returns the
 * rows it drew from row first. */
static uint32_t lf_panel(const terrain_t *terrain, const lf_memory_t *m, uint32_t first, int32_t cx, int32_t cy,
                         float64_t ax, float64_t ay, const char *title) {
    float32_t *const line = m->c;
    float32_t low = LF_HUGE, top = -LF_HUGE;
    for (uint32_t i = 0u; i < LF_PANEL_WIDE; i++) {
        const float64_t t = (float64_t)i - (float64_t)(LF_PANEL_WIDE / 2u);
        line[i] = terrain_height_estimate(terrain, cx + (int32_t)floor(t * ax + 0.5f64),
                                          cy + (int32_t)floor(t * ay + 0.5f64));
        low = fminf(low, line[i]);
        top = fmaxf(top, line[i]);
    }
    const int32_t z_low = (int32_t)floorf((fminf(low, 0.0f32) - 40.0f32) / 100.0f32) * 100;
    int32_t z_high = (int32_t)ceilf((top + 120.0f32) / 100.0f32) * 100;
    z_high = z_high - z_low > (int32_t)LF_PANEL_MOST ? z_low + (int32_t)LF_PANEL_MOST : z_high;
    const uint32_t rows = (uint32_t)(z_high - z_low);
    for (uint32_t row = 0u; row < rows; row++) {
        const int32_t z = z_high - 1 - (int32_t)row;
        uint32_t *const out = m->pixels + (uint64_t)(first + row) * LF_PANEL_WIDE;
        for (uint32_t i = 0u; i < LF_PANEL_WIDE; i++) {
            const float32_t ground = line[i];
            uint32_t colour = 0xF4F8FFu;
            if ((float32_t)z <= ground) {
                colour = (float32_t)z > ground - 4.0f32 ? (ground > 0.0f32 ? 0x4C9A3Cu : 0xC8B070u) : 0x8C6A4Au;
            } else if (z <= 0) {
                colour = LF_COLOUR_SEA;
            } else if (z % 100 == 0 || ((int32_t)i - (int32_t)(LF_PANEL_WIDE / 2u)) % 100 == 0) {
                colour = z == 0 ? 0x2040A0u : 0xC8CCD8u;
            }
            out[i] = colour;
        }
    }
    char label[32];
    for (int32_t z = z_low + 100; z < z_high; z += 100) {
        snprintf(label, sizeof(label), "%d", z);
        lf_text(m, LF_PANEL_WIDE, first + rows, 4, (int32_t)first + (z_high - 1 - z) - 3, label, 0x202020u, UINT32_MAX,
                1u);
    }
    lf_text(m, LF_PANEL_WIDE, first + rows, 60, (int32_t)first + 8, title, 0x000000u, 0xFFFFFFu, 2u);
    for (uint32_t i = 0u; i < LF_PANEL_WIDE; i++) {
        m->pixels[(uint64_t)(first + rows - 1u) * LF_PANEL_WIDE + i] = 0x000000u;
    }
    return rows;
}

bool lf_draw_profiles(const terrain_t *terrain, const lf_grid_t *grid, const lf_memory_t *m, const lf_result_t *r) {
    static const float64_t DIRECTION[4][2] = {
        {1.0f64, 0.0f64}, {0.0f64, 1.0f64}, {0.70710678f64, 0.70710678f64}, {0.70710678f64, -0.70710678f64}};
    static const char *const DIRECTION_NAME[4] = {"x", "y", "x+y", "x-y"};
    char title[256];
    uint32_t rows = 0u;
    const lf_place_t *const peak = &r->place[LF_PLACE_PEAK];
    for (uint32_t d = 0u; d < 2u; d++) {
        snprintf(title, sizeof(title), "tallest peak %d, %d (z %.0f), along %s; 1 block a pixel, true scale", peak->x,
                 peak->y, (libc_double_t)peak->z, DIRECTION_NAME[d]);
        rows += lf_panel(terrain, m, rows, peak->x, peak->y, DIRECTION[d][0], DIRECTION[d][1], title);
    }
    if (r->plateau_edge != UINT32_MAX) {
        const lf_place_t edge = lf_place_at(grid, r->plateau_edge);
        snprintf(title, sizeof(title), "the largest plateau's edge nearest its middle, %d, %d, along x", edge.x,
                 edge.y);
        rows += lf_panel(terrain, m, rows, edge.x, edge.y, 1.0f64, 0.0f64, title);
    } else {
        const lf_place_t *const plains = &r->place[LF_PLACE_PLAINS];
        snprintf(title, sizeof(title), "no plateau piece with an escarpment: instead a plains place %d, %d, along x",
                 plains->x, plains->y);
        rows += lf_panel(terrain, m, rows, plains->x, plains->y, 1.0f64, 0.0f64, title);
    }
    const lf_place_t *const range = r->place[LF_PLACE_RANGE].found != 0u ? &r->place[LF_PLACE_RANGE] : peak;
    snprintf(title, sizeof(title), "the longest range piece at >= 500, through %d, %d along %s (%.1f km long)",
             range->x, range->y, DIRECTION_NAME[r->range_direction], (libc_double_t)r->v[LF_V_RANGE_KM]);
    rows += lf_panel(terrain, m, rows, range->x, range->y, DIRECTION[r->range_direction][0],
                     DIRECTION[r->range_direction][1], title);
    return lf_png(m, r->seed, "profiles", LF_PANEL_WIDE, rows);
}

/* The snowline from plan 006's fallback climate (master 2.2): where the warmest month's mean reaches 0 deg C, plan 004's
 * formula at kappa 0.5 and no current, by latitude alone. Never: above the world. */
static float64_t lf_snowline(int64_t y) {
    terrain_climate_cell_t cell;
    terrain_climate_fallback_cell(y, &cell);
    return cell.snow_line_z == TERRAIN_CLIMATE_NO_SNOW_LINE ? 1.0e9f64 : (float64_t)cell.snow_line_z;
}

bool lf_draw_strip(const lf_memory_t *m, uint64_t seed) {
    const uint32_t wide = LF_STRIP_LEFT + LF_STRIP_WIDE, high = LF_STRIP_TOP + LF_STRIP_HIGH;
    uint32_t *const px = m->pixels;
    for (uint64_t i = 0u; i < (uint64_t)wide * high; i++) {
        px[i] = 0xFFFFFFu;
    }
    for (uint32_t j = 0u; j < LF_STRIP_HIGH; j++) {
        const uint32_t py = LF_STRIP_TOP + (LF_STRIP_HIGH - 1u - j);
        for (uint32_t i = 0u; i < LF_STRIP_WIDE; i++) {
            px[(uint64_t)py * wide + LF_STRIP_LEFT + i] =
                lf_relief_colour(m->strip, nullptr, LF_STRIP_WIDE, i, j, LF_STRIP_STEP);
        }
    }
    lf_contours(m->strip, nullptr, LF_STRIP_WIDE, LF_STRIP_HIGH, px, wide, LF_STRIP_LEFT, LF_STRIP_TOP, true);
    /* The snowline, dashed, where the ground crosses it. */
    float64_t below = lf_snowline(0);
    for (uint32_t j = 1u; j < LF_STRIP_HIGH; j++) {
        const float64_t line = lf_snowline((int64_t)(j * LF_STRIP_STEP));
        const uint32_t py = LF_STRIP_TOP + (LF_STRIP_HIGH - 1u - j);
        for (uint32_t i = 0u; i + 1u < LF_STRIP_WIDE; i++) {
            const float32_t z = m->strip[(uint64_t)j * LF_STRIP_WIDE + i];
            const float32_t e = m->strip[(uint64_t)j * LF_STRIP_WIDE + i + 1u];
            const float32_t s = m->strip[(uint64_t)(j - 1u) * LF_STRIP_WIDE + i];
            const bool above = (float64_t)z >= line;
            if (z > 0.0f32 && (above != ((float64_t)e >= line) || above != ((float64_t)s >= below)) &&
                ((i + j) / 4u) % 2u == 0u) {
                px[(uint64_t)py * wide + LF_STRIP_LEFT + i] = 0xFF00FFu;
            }
        }
        below = line;
    }
    char label[64];
    for (uint32_t deg = 0u; deg <= 90u; deg += 10u) {
        uint32_t j = (uint32_t)((float64_t)deg / 90.0f64 * (float64_t)CLIMATE_POLE_Y / (float64_t)LF_STRIP_STEP);
        j = j >= LF_STRIP_HIGH ? LF_STRIP_HIGH - 1u : j;
        const uint32_t py = LF_STRIP_TOP + (LF_STRIP_HIGH - 1u - j);
        lf_fill(px, wide, high, (int32_t)LF_STRIP_LEFT - 12, (int32_t)py, 12u + LF_STRIP_WIDE / 16u, 1u, 0x000000u);
        snprintf(label, sizeof(label), "%u\xC2\xB0", deg);
        lf_text(m, wide, high, 4, (int32_t)py - 3, label, 0x000000u, UINT32_MAX, 1u);
    }
    lf_text(m, wide, high, 4, 4, "equator (bottom) to pole (top): x -2048 to 2040, 8 blocks a pixel", 0x000000u,
            UINT32_MAX, 1u);
    lf_text(m, wide, high, 4, 15, "magenta: snowline from plan 006's terrain_climate_fallback_cell: the", 0x000000u,
            UINT32_MAX, 1u);
    lf_text(m, wide, high, 4, 26, "warmest month's mean 0 C, plan 004's formula at kappa 0.5, no current", 0x000000u,
            UINT32_MAX, 1u);
    return lf_png(m, seed, "strip", wide, high);
}

bool lf_draw_windows(const lf_memory_t *m, uint64_t seed, uint32_t windows_side) {
    const uint32_t wide = windows_side * LF_WINDOW_SIDE + (windows_side + 1u) * LF_WINDOW_GAP;
    for (uint64_t i = 0u; i < (uint64_t)wide * wide; i++) {
        m->pixels[i] = 0x303030u;
    }
    for (uint32_t w = 0u; w < windows_side * windows_side; w++) {
        const uint32_t left = LF_WINDOW_GAP + (w % windows_side) * (LF_WINDOW_SIDE + LF_WINDOW_GAP);
        const uint32_t top = LF_WINDOW_GAP + (w / windows_side) * (LF_WINDOW_SIDE + LF_WINDOW_GAP);
        const float32_t *const h = m->windows_real + (uint64_t)w * LF_WINDOW_COLUMNS;
        const uint8_t *const land = m->windows_land + (uint64_t)w * LF_WINDOW_COLUMNS;
        for (uint32_t row = 1u; row <= LF_WINDOW_SIDE; row++) {
            for (uint32_t column = 1u; column <= LF_WINDOW_SIDE; column++) {
                const uint32_t at = row * LF_WINDOW_GRID + column;
                const float64_t g =
                    lf_gradient((float64_t)h[at - 1u], (float64_t)h[at + 1u], (float64_t)h[at - LF_WINDOW_GRID],
                                (float64_t)h[at + LF_WINDOW_GRID], 8.0f64);
                m->pixels[(uint64_t)(top + row - 1u) * wide + left + column - 1u] =
                    land[at] != 0u ? LF_CLASS_COLOUR[lf_class(g)] : LF_COLOUR_SEA;
            }
        }
    }
    return lf_png(m, seed, "windows", wide, wide);
}
