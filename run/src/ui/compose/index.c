/* See README.md. Every pixel the UI puts on a frame goes through ui_compose_span, and so through blend_over_batch. */
#include "ui/compose/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "simd/blend/index.h"

enum : uint32_t {
    UI_BYTES_PER_PIXEL = 4u,
    UI_NINE_PARTS = 3u,
};

bool ui_canvas_start(ui_canvas_t *canvas, uint8_t *pixels, uint64_t pitch, uint32_t width, uint32_t height,
                     uint32_t *row) {
    const bool fits = width != 0u && height != 0u && width <= UI_CANVAS_MAX_WIDTH && height <= (uint32_t)INT32_MAX &&
                      pitch >= (uint64_t)width * UI_BYTES_PER_PIXEL && pitch % UI_BYTES_PER_PIXEL == 0u &&
                      (uintptr_t)pixels % alignof(uint32_t) == 0u && (uintptr_t)row % alignof(uint32_t) == 0u;
    if (!fits) {
        return false;
    }
    *canvas = (ui_canvas_t){
        .borrowed_pixels = pixels,
        .borrowed_row = row,
        .pitch = pitch,
        .clip = {.x = 0, .y = 0, .width = (int32_t)width, .height = (int32_t)height},
        .width = width,
        .height = height,
        .blend_calls = 0u,
        .blended_pixels = 0u,
    };
    return true;
}

/* In 64 bits: a rect near the ends of int32_t has an edge past them. */
ui_rect_t ui_rect_intersect(ui_rect_t a, ui_rect_t b) {
    if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) {
        return (ui_rect_t){a.x, a.y, 0, 0};
    }
    const int64_t left = a.x > b.x ? a.x : b.x;
    const int64_t top = a.y > b.y ? a.y : b.y;
    const int64_t a_right = (int64_t)a.x + a.width, b_right = (int64_t)b.x + b.width;
    const int64_t a_bottom = (int64_t)a.y + a.height, b_bottom = (int64_t)b.y + b.height;
    const int64_t right = a_right < b_right ? a_right : b_right;
    const int64_t bottom = a_bottom < b_bottom ? a_bottom : b_bottom;
    if (right <= left || bottom <= top) {
        return (ui_rect_t){(int32_t)left, (int32_t)top, 0, 0};
    }
    return (ui_rect_t){(int32_t)left, (int32_t)top, (int32_t)(right - left), (int32_t)(bottom - top)};
}

ui_rect_t ui_canvas_clip_set(ui_canvas_t *canvas, ui_rect_t rect) {
    const ui_rect_t previous = canvas->clip;
    const ui_rect_t whole = {0, 0, (int32_t)canvas->width, (int32_t)canvas->height};
    canvas->clip = ui_rect_intersect(whole, rect);
    return previous;
}

bool ui_rect_contains(ui_rect_t rect, int32_t x, int32_t y) {
    if (rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    return x >= rect.x && y >= rect.y && (int64_t)x < (int64_t)rect.x + rect.width &&
           (int64_t)y < (int64_t)rect.y + rect.height;
}

ui_rect_t ui_rect_center(ui_rect_t outer, int32_t width, int32_t height) {
    return (ui_rect_t){
        .x = (int32_t)((int64_t)outer.x + ((int64_t)outer.width - width) / 2),
        .y = (int32_t)((int64_t)outer.y + ((int64_t)outer.height - height) / 2),
        .width = width,
        .height = height,
    };
}

/* A texture is RGBA bytes and a frame is 0xAARRGGBB, so it is turned around once here, and never per frame. */
bool ui_image_load(ui_image_t *image, arena_t *arena, const texture_t *texture, const char *name) {
    uint32_t id = 0u;
    texture_mip_t mip;
    if (!texture_find(texture, name, &id) || !texture_mip_get(texture, id, 0u, 0u, &mip)) {
        LOG_ERROR("ui: no texture %s", name);
        return false;
    }
    const uint64_t count = (uint64_t)mip.width * mip.height;
    uint32_t *const pixels = arena_alloc_array(arena, count, sizeof(uint32_t), alignof(uint32_t));
    for (uint64_t i = 0u; i < count; i++) {
        const uint8_t *const rgba = &mip.borrowed_pixels[i * UI_BYTES_PER_PIXEL];
        pixels[i] = (uint32_t)rgba[3] << 24 | (uint32_t)rgba[0] << 16 | (uint32_t)rgba[1] << 8 | (uint32_t)rgba[2];
    }
    *image = (ui_image_t){.borrowed_pixels = pixels, .width = mip.width, .height = mip.height};
    return true;
}

void ui_compose_span(ui_canvas_t *canvas, int32_t x, int32_t y, const uint32_t *source, uint32_t count) {
    if (count == 0u) {
        return;
    }
    ASSERT(ui_rect_contains(canvas->clip, x, y) && ui_rect_contains(canvas->clip, x + (int32_t)count - 1, y));
    uint8_t *const row = canvas->borrowed_pixels + (uint64_t)y * canvas->pitch;
    blend_over_batch((uint32_t *)(void *)(row + (uint64_t)x * UI_BYTES_PER_PIXEL), source, count);
    canvas->blend_calls++;
    canvas->blended_pixels += count;
}

void ui_compose_fill(ui_canvas_t *canvas, ui_rect_t rect, uint32_t color) {
    const ui_rect_t visible = ui_rect_intersect(rect, canvas->clip);
    if (visible.width <= 0 || visible.height <= 0 || color >> 24 == 0u) {
        return;
    }
    const uint32_t count = (uint32_t)visible.width;
    for (uint32_t i = 0u; i < count; i++) {
        canvas->borrowed_row[i] = color;
    }
    for (int32_t y = visible.y; y < visible.y + visible.height; y++) {
        ui_compose_span(canvas, visible.x, y, canvas->borrowed_row, count);
    }
}

/* One row of the source stretched into the row scratch, for destination pixels first to first + count. Stepped, with no
 * division per pixel: position = source_x + (first + i) * source_width / rect_width. */
static void ui_compose_row_stretch(uint32_t *out, const uint32_t *source_row, int64_t source_x, int64_t source_width,
                                   int64_t rect_width, int64_t first, uint32_t count) {
    const int64_t start = first * source_width;
    int64_t position = source_x + start / rect_width;
    int64_t remainder = start % rect_width;
    const int64_t step = source_width / rect_width;
    const int64_t step_remainder = source_width % rect_width;
    for (uint32_t i = 0u; i < count; i++) {
        out[i] = source_row[position];
        position += step;
        remainder += step_remainder;
        if (remainder >= rect_width) {
            remainder -= rect_width;
            position++;
        }
    }
}

void ui_compose_image(ui_canvas_t *canvas, const ui_image_t *image, ui_rect_t source, ui_rect_t rect) {
    ASSERT_ALWAYS(source.x >= 0 && source.y >= 0 && source.width >= 0 && source.height >= 0 &&
                  (int64_t)source.x + source.width <= (int64_t)image->width &&
                  (int64_t)source.y + source.height <= (int64_t)image->height);
    const ui_rect_t visible = ui_rect_intersect(rect, canvas->clip);
    if (visible.width <= 0 || visible.height <= 0 || source.width == 0 || source.height == 0) {
        return;
    }
    const uint32_t count = (uint32_t)visible.width;
    const int64_t first_x = (int64_t)visible.x - rect.x;
    const bool same_width = source.width == rect.width;
    int64_t built_row = -1; /* the source row already in the scratch: a row drawn scale times is built once */
    for (int32_t y = visible.y; y < visible.y + visible.height; y++) {
        const int64_t source_y = source.y + ((int64_t)y - rect.y) * source.height / rect.height;
        const uint32_t *const source_row = image->borrowed_pixels + source_y * (int64_t)image->width;
        if (same_width) {
            ui_compose_span(canvas, visible.x, y, source_row + source.x + first_x, count);
            continue;
        }
        if (source_y != built_row) {
            ui_compose_row_stretch(canvas->borrowed_row, source_row, source.x, source.width, rect.width, first_x,
                                   count);
            built_row = source_y;
        }
        ui_compose_span(canvas, visible.x, y, canvas->borrowed_row, count);
    }
}

void ui_compose_image_whole(ui_canvas_t *canvas, const ui_image_t *image, ui_rect_t rect) {
    const ui_rect_t source = {0, 0, (int32_t)image->width, (int32_t)image->height};
    ui_compose_image(canvas, image, source, rect);
}

/* Lengths of the three parts along one side of the rect. A corner takes at most half; the second takes the odd pixel. */
static void ui_compose_nine_side(int32_t length, int64_t corner, int32_t out[static UI_NINE_PARTS]) {
    const int64_t half = (int64_t)length / 2;
    const int32_t first = (int32_t)(corner < half ? corner : half);
    out[0] = first;
    out[1] = length - 2 * first;
    out[2] = first;
}

void ui_compose_nine(ui_canvas_t *canvas, const ui_image_t *image, uint32_t border, uint32_t scale, ui_rect_t rect) {
    ASSERT_ALWAYS(border * 2u < image->width && border * 2u < image->height);
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const int32_t source_border = (int32_t)border;
    const int32_t source_xs[UI_NINE_PARTS] = {0, source_border, (int32_t)image->width - source_border};
    const int32_t source_ys[UI_NINE_PARTS] = {0, source_border, (int32_t)image->height - source_border};
    const int32_t source_widths[UI_NINE_PARTS] = {source_border, (int32_t)image->width - 2 * source_border,
                                                  source_border};
    const int32_t source_heights[UI_NINE_PARTS] = {source_border, (int32_t)image->height - 2 * source_border,
                                                   source_border};
    const int64_t corner = (int64_t)border * scale;
    int32_t widths[UI_NINE_PARTS], heights[UI_NINE_PARTS];
    ui_compose_nine_side(rect.width, corner, widths);
    ui_compose_nine_side(rect.height, corner, heights);
    int32_t y = rect.y;
    for (uint32_t row = 0u; row < UI_NINE_PARTS; row++) {
        int32_t x = rect.x;
        for (uint32_t column = 0u; column < UI_NINE_PARTS; column++) {
            const ui_rect_t source = {source_xs[column], source_ys[row], source_widths[column], source_heights[row]};
            ui_compose_image(canvas, image, source, (ui_rect_t){x, y, widths[column], heights[row]});
            x += widths[column];
        }
        y += heights[row];
    }
}
