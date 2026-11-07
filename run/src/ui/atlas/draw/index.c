/* See README.md. Pure drawing on buffers the caller lends: no allocation. */
#include "ui/atlas/draw/index.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t draw_channel(float32_t value) {
    const int64_t rounded = (int64_t)lroundf(value); /* half away from zero */
    return rounded < 0 ? 0u : (rounded > 255 ? 255u : (uint32_t)rounded);
}

uint32_t ui_atlas_scale_colour(const ui_atlas_scale_t *scale, float32_t value) {
    if (scale->count == 0u) {
        return 0u;
    }
    if (!(value > scale->values[0])) {
        return scale->colours[0] & 0xFFFFFFu; /* NaN too */
    }
    for (uint32_t k = 1u; k < scale->count; k++) {
        if (value < scale->values[k]) {
            const float32_t t = (value - scale->values[k - 1u]) / (scale->values[k] - scale->values[k - 1u]);
            uint32_t out = 0u;
            for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
                const float32_t a = (float32_t)((scale->colours[k - 1u] >> shift) & 0xFFu);
                const float32_t b = (float32_t)((scale->colours[k] >> shift) & 0xFFu);
                out |= draw_channel(a + t * (b - a)) << shift;
            }
            return out;
        }
    }
    return scale->colours[scale->count - 1u] & 0xFFFFFFu;
}

float32_t ui_atlas_shade(float32_t west, float32_t east, float32_t north, float32_t south, float32_t blocks_per_pixel,
                         float32_t exaggeration) {
    static const float32_t LIGHT[3] = {-0.5f32, 0.5f32, 0.70710678f32};
    const float32_t dzdx = (east - west) / (2.0f32 * blocks_per_pixel);
    const float32_t dzdy = (north - south) / (2.0f32 * blocks_per_pixel);
    const float32_t nx = -exaggeration * dzdx, ny = -exaggeration * dzdy;
    const float32_t length = sqrtf(nx * nx + ny * ny + 1.0f32);
    const float32_t dot = (nx * LIGHT[0] + ny * LIGHT[1] + LIGHT[2]) / length;
    const float32_t b = (dot > 0.0f32 ? dot : 0.0f32) / LIGHT[2];
    return b < UI_ATLAS_SHADE_MIN ? UI_ATLAS_SHADE_MIN : (b > UI_ATLAS_SHADE_MAX ? UI_ATLAS_SHADE_MAX : b);
}

uint32_t ui_atlas_shade_apply(uint32_t rgb, float32_t b) {
    uint32_t out = 0xFF000000u;
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        out |= draw_channel((float32_t)((rgb >> shift) & 0xFFu) * b) << shift;
    }
    return out;
}

/* The point where the level crosses the edge from corner a (at pa) to corner b (at pb). */
static void draw_cross(float32_t a, float32_t b, float32_t level, const float32_t pa[2], const float32_t pb[2],
                       float32_t out[2]) {
    const float32_t t = (level - a) / (b - a);
    out[0] = pa[0] + t * (pb[0] - pa[0]);
    out[1] = pa[1] + t * (pb[1] - pa[1]);
}

uint32_t ui_atlas_march(const float32_t corners[4], float32_t level, float32_t size, ui_atlas_segment_t out[2]) {
    for (uint32_t k = 0u; k < 4u; k++) {
        if (isnan(corners[k])) {
            return 0u;
        }
    }
    /* Corner places: top-left, top-right, bottom-right, bottom-left. Edges, in order: top, right, bottom, left. */
    const float32_t places[4][2] = {{0.0f32, 0.0f32}, {size, 0.0f32}, {size, size}, {0.0f32, size}};
    bool above[4];
    uint32_t mask = 0u;
    for (uint32_t k = 0u; k < 4u; k++) {
        above[k] = corners[k] > level;
        mask |= (above[k] ? 1u : 0u) << k;
    }
    if (mask == 0u || mask == 15u) {
        return 0u;
    }
    float32_t points[4][2];
    bool crossed[4];
    for (uint32_t edge = 0u; edge < 4u; edge++) {
        const uint32_t a = edge, b = (edge + 1u) % 4u;
        crossed[edge] = above[a] != above[b];
        if (crossed[edge]) {
            draw_cross(corners[a], corners[b], level, places[a], places[b], points[edge]);
        }
    }
    if (mask == 5u || mask == 10u) {
        /* A saddle. Pairing A, (top, right) and (bottom, left), cuts off the top-right and bottom-left corners; pairing B,
         * (left, top) and (right, bottom), the top-left and bottom-right. The mean says which corners are joined. */
        const float32_t mean = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f32;
        const bool mean_above = mean > level;
        const bool pairing_a = (mask == 5u) == mean_above; /* 5: top-left and bottom-right are above */
        static const uint32_t PAIRS[2][4] = {{0u, 1u, 2u, 3u}, {3u, 0u, 1u, 2u}};
        const uint32_t *const pair = PAIRS[pairing_a ? 0u : 1u];
        for (uint32_t s = 0u; s < 2u; s++) {
            out[s] = (ui_atlas_segment_t){points[pair[2u * s]][0], points[pair[2u * s]][1],
                                          points[pair[2u * s + 1u]][0], points[pair[2u * s + 1u]][1]};
        }
        return 2u;
    }
    uint32_t first = 4u, second = 4u;
    for (uint32_t edge = 0u; edge < 4u; edge++) {
        if (crossed[edge]) {
            if (first == 4u) {
                first = edge;
            } else {
                second = edge;
            }
        }
    }
    out[0] = (ui_atlas_segment_t){points[first][0], points[first][1], points[second][0], points[second][1]};
    return 1u;
}

bool ui_atlas_draw_allowed(const ui_atlas_picture_t *picture, int32_t x, int32_t y, ui_atlas_mode_t mode) {
    if (x < 0 || y < 0 || (uint32_t)x >= picture->width || (uint32_t)y >= picture->height) {
        return false;
    }
    const bool grey = picture->borrowed_grey != nullptr &&
                      picture->borrowed_grey[(uint64_t)(uint32_t)y * picture->width + (uint32_t)x] != 0u;
    switch (mode) {
    case UI_ATLAS_MODE_OUTLINE:
        return true;
    case UI_ATLAS_MODE_FILL:
        return !grey;
    case UI_ATLAS_MODE_ON_GREY:
        return grey;
    }
    return false;
}

void ui_atlas_draw_point(const ui_atlas_picture_t *picture, int32_t x, int32_t y, uint32_t colour,
                         ui_atlas_mode_t mode) {
    if (ui_atlas_draw_allowed(picture, x, y, mode)) {
        picture->borrowed_pixels[(uint64_t)(uint32_t)y * picture->width + (uint32_t)x] = 0xFF000000u | colour;
    }
}

void ui_atlas_draw_point_blend(const ui_atlas_picture_t *picture, int32_t x, int32_t y, uint32_t colour, uint32_t alpha,
                               ui_atlas_mode_t mode) {
    if (!ui_atlas_draw_allowed(picture, x, y, mode)) {
        return;
    }
    uint32_t *const at = &picture->borrowed_pixels[(uint64_t)(uint32_t)y * picture->width + (uint32_t)x];
    uint32_t out = 0xFF000000u;
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        const uint32_t under = (*at >> shift) & 0xFFu, over = (colour >> shift) & 0xFFu;
        out |= ((over * alpha + under * (255u - alpha) + 127u) / 255u) << shift;
    }
    *at = out;
}

void ui_atlas_draw_line(const ui_atlas_picture_t *picture, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        uint32_t colour, uint32_t dash, ui_atlas_mode_t mode) {
    const int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0; /* minus the height */
    const int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    /* A line wholly off the picture costs nothing; one that only crosses it is walked, which the clamp keeps short. */
    const int32_t far = 1 << 20;
    if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) || (x0 >= (int32_t)picture->width && x1 >= (int32_t)picture->width) ||
        (y0 >= (int32_t)picture->height && y1 >= (int32_t)picture->height) || dx > far || -dy > far) {
        return;
    }
    int32_t error = dx + dy;
    uint32_t step = 0u;
    for (;;) {
        if (dash == 0u || (step / dash) % 2u == 0u) {
            ui_atlas_draw_point(picture, x0, y0, colour, mode);
        }
        step++;
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

void ui_atlas_draw_circle(const ui_atlas_picture_t *picture, int32_t cx, int32_t cy, int32_t radius, uint32_t colour,
                          ui_atlas_mode_t mode) {
    radius = radius < 1 ? 1 : radius;
    if (radius > (1 << 20) || cx + radius < 0 || cy + radius < 0 || cx - radius >= (int32_t)picture->width ||
        cy - radius >= (int32_t)picture->height) {
        return;
    }
    int32_t x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        const int32_t points[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
        for (uint32_t k = 0u; k < 8u; k++) {
            ui_atlas_draw_point(picture, cx + points[k][0], cy + points[k][1], colour, mode);
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

void ui_atlas_draw_disc(const ui_atlas_picture_t *picture, int32_t cx, int32_t cy, int32_t radius, uint32_t colour,
                        uint32_t alpha, ui_atlas_mode_t mode) {
    radius = radius < 1 ? 1 : radius;
    const int64_t r = radius;
    const int64_t y0 = cy - r < 0 ? 0 : cy - r;
    const int64_t y1 = cy + r >= (int64_t)picture->height ? (int64_t)picture->height - 1 : cy + r;
    const int64_t x0 = cx - r < 0 ? 0 : cx - r;
    const int64_t x1 = cx + r >= (int64_t)picture->width ? (int64_t)picture->width - 1 : cx + r;
    for (int64_t y = y0; y <= y1; y++) {
        for (int64_t x = x0; x <= x1; x++) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                if (alpha >= 255u) {
                    ui_atlas_draw_point(picture, (int32_t)x, (int32_t)y, colour, mode);
                } else {
                    ui_atlas_draw_point_blend(picture, (int32_t)x, (int32_t)y, colour, alpha, mode);
                }
            }
        }
    }
}

void ui_atlas_draw_arrow(const ui_atlas_picture_t *picture, int32_t x, int32_t y, float32_t u, float32_t v,
                         float32_t length, uint32_t colour, ui_atlas_mode_t mode) {
    const float32_t size = sqrtf(u * u + v * v);
    if (size < 1.0e-6f32 || length < 1.0f32) {
        return;
    }
    const float32_t dx = u / size, dy = -v / size; /* rows go south */
    const float32_t head_x = (float32_t)x + length * dx, head_y = (float32_t)y + length * dy;
    const int32_t x1 = (int32_t)lroundf(head_x), y1 = (int32_t)lroundf(head_y);
    ui_atlas_draw_line(picture, x, y, x1, y1, colour, 0u, mode);
    const float32_t barb_cos = -0.8660254f32, barb_sin = 0.5f32, barb = 3.0f32;
    for (int32_t side = -1; side <= 1; side += 2) {
        const float32_t s = barb_sin * (float32_t)side;
        const float32_t bx = dx * barb_cos - dy * s, by = dx * s + dy * barb_cos;
        ui_atlas_draw_line(picture, x1, y1, (int32_t)lroundf(head_x + barb * bx), (int32_t)lroundf(head_y + barb * by),
                           colour, 0u, mode);
    }
}

/* Row y of the cone's inside into row: 1 where a pixel's centre is within some circle, for columns 0 to width - 1. */
static void draw_cone_row(const float32_t *cx, const float32_t *cy, const float32_t *r, uint32_t count, int32_t y,
                          uint32_t width, uint8_t *row) {
    memset(row, 0, width);
    const float32_t py = (float32_t)y + 0.5f32;
    for (uint32_t k = 0u; k < count; k++) {
        const float32_t d = py - cy[k];
        if (d * d > r[k] * r[k]) {
            continue;
        }
        const float32_t half = sqrtf(r[k] * r[k] - d * d);
        float32_t from = ceilf(cx[k] - half - 0.5f32), to = floorf(cx[k] + half - 0.5f32);
        from = from < 0.0f32 ? 0.0f32 : from;
        to = to > (float32_t)width - 1.0f32 ? (float32_t)width - 1.0f32 : to;
        for (float32_t x = from; x <= to; x += 1.0f32) {
            row[(uint32_t)x] = 1u;
        }
    }
}

void ui_atlas_draw_cone(const ui_atlas_picture_t *picture, const float32_t *centres_x, const float32_t *centres_y,
                        const float32_t *radii, uint32_t count, uint32_t edge, uint32_t fill, uint32_t fill_alpha,
                        uint8_t *scratch) {
    if (count == 0u) {
        return;
    }
    float32_t top = centres_y[0] - radii[0], bottom = centres_y[0] + radii[0];
    for (uint32_t k = 1u; k < count; k++) {
        top = fminf(top, centres_y[k] - radii[k]);
        bottom = fmaxf(bottom, centres_y[k] + radii[k]);
    }
    const int32_t y0 = top < 0.0f32 ? 0 : (int32_t)top;
    const int32_t y1 = bottom >= (float32_t)picture->height ? (int32_t)picture->height - 1 : (int32_t)bottom;
    if (y0 > y1) {
        return;
    }
    const uint32_t width = picture->width;
    uint8_t *rows[3] = {scratch, scratch + width, scratch + 2u * width};
    draw_cone_row(centres_x, centres_y, radii, count, y0 - 1, width, rows[0]);
    draw_cone_row(centres_x, centres_y, radii, count, y0, width, rows[1]);
    for (int32_t y = y0; y <= y1; y++) {
        draw_cone_row(centres_x, centres_y, radii, count, y + 1, width, rows[2]);
        for (uint32_t x = 0u; x < width; x++) {
            if (rows[1][x] == 0u) {
                continue;
            }
            const bool west = x == 0u || rows[1][x - 1u] != 0u;
            const bool east = x + 1u == width || rows[1][x + 1u] != 0u;
            const bool inner = west && east && rows[0][x] != 0u && rows[2][x] != 0u;
            if (inner) {
                ui_atlas_draw_point_blend(picture, (int32_t)x, y, fill, fill_alpha, UI_ATLAS_MODE_FILL);
            } else {
                ui_atlas_draw_point(picture, (int32_t)x, y, edge, UI_ATLAS_MODE_OUTLINE);
            }
        }
        uint8_t *const done = rows[0];
        rows[0] = rows[1];
        rows[1] = rows[2];
        rows[2] = done;
    }
}

/* A number short enough for a legend: no decimals from 100 up, one under. */
static void draw_value(float32_t value, char *out, uint32_t bytes) {
    const float32_t size = fabsf(value);
    if (size >= 100.0f32 || value == floorf(value)) {
        (void)snprintf(out, bytes, "%.0f", (libc_double_t)value);
    } else {
        (void)snprintf(out, bytes, "%.1f", (libc_double_t)value);
    }
}

int32_t ui_atlas_draw_legend(ui_canvas_t *canvas, const ui_font_t *font, ui_rect_t rect, uint32_t text_scale,
                             const char *title, const char *unit, const ui_atlas_scale_t *scale,
                             const ui_atlas_key_entry_t *entries, uint32_t entry_count, float32_t arrow_speed) {
    const int32_t s = (int32_t)text_scale;
    const int32_t line = (int32_t)UI_FONT_LINE * s;
    const int32_t pad = 3 * s;
    const ui_text_style_t white = {.color = 0xFFFFFFFFu, .shadow = 0u, .scale = text_scale};
    int32_t height = pad + line;
    if (scale != nullptr && scale->count > 0u) {
        height += 6 * s + line + line;
    }
    height += (int32_t)entry_count * line;
    height += arrow_speed > 0.0f32 ? line + 2 * s : 0;
    height += pad;
    const ui_rect_t panel = {rect.x, rect.y + rect.height - height, rect.width, height};
    ui_compose_fill(canvas, panel, 0xC0101010u);
    int32_t y = panel.y + pad;
    char text[96];
    (void)snprintf(text, sizeof(text), "%s%s%s%s", title, unit[0] != '\0' ? " (" : "", unit,
                   unit[0] != '\0' ? ")" : "");
    ui_font_draw(font, canvas, panel.x + pad, y, text, white);
    y += line;
    if (scale != nullptr && scale->count > 0u) {
        const int32_t bar_width = panel.width - 2 * pad;
        const float32_t low = scale->values[0], high = scale->values[scale->count - 1u];
        for (int32_t i = 0; i < bar_width; i++) {
            const float32_t value = low + (high - low) * ((float32_t)i + 0.5f32) / (float32_t)bar_width;
            const ui_rect_t column = {panel.x + pad + i, y, 1, 6 * s};
            ui_compose_fill(canvas, column, 0xFF000000u | ui_atlas_scale_colour(scale, value));
        }
        y += 6 * s + s;
        for (uint32_t k = 0u; k < 5u; k++) {
            const float32_t value = low + (high - low) * (float32_t)k / 4.0f32;
            char number[24];
            draw_value(value, number, sizeof(number));
            const ui_text_size_t size = ui_font_measure(font, number, text_scale);
            int32_t x =
                panel.x + pad + (int32_t)((float32_t)(bar_width - 1) * (float32_t)k / 4.0f32) - (int32_t)size.width / 2;
            x = x < panel.x + pad ? panel.x + pad : x;
            x = x + (int32_t)size.width > panel.x + panel.width - pad
                    ? panel.x + panel.width - pad - (int32_t)size.width
                    : x;
            ui_font_draw(font, canvas, x, y, number, white);
        }
        y += line + line;
    }
    for (uint32_t k = 0u; k < entry_count; k++) {
        const ui_rect_t swatch = {panel.x + pad, y, 6 * s, 6 * s};
        ui_compose_fill(canvas, swatch, 0xFF000000u | entries[k].colour);
        ui_font_draw(font, canvas, panel.x + pad + 9 * s, y, entries[k].label, white);
        y += line;
    }
    if (arrow_speed > 0.0f32) {
        /* An arrow of that speed: phase 04's length, 2 pixels a block a second, at most 30. */
        const int32_t length = (int32_t)fminf(30.0f32, 2.0f32 * arrow_speed);
        const int32_t mid = y + line / 2;
        ui_compose_fill(canvas, (ui_rect_t){panel.x + pad, mid, length, 1}, 0xFFFFFFFFu);
        ui_compose_fill(canvas, (ui_rect_t){panel.x + pad + length - 3, mid - 2, 1, 5}, 0xFFFFFFFFu);
        (void)snprintf(text, sizeof(text), "%.0f blocks a second", (libc_double_t)arrow_speed);
        ui_font_draw(font, canvas, panel.x + pad + length + 4 * s, y, text, white);
    }
    return height;
}
