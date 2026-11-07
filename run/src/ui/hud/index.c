/* See README.md. Everything here is drawn fresh each frame from plain values, through ui/compose and so through the blend kernel. */
#include "ui/hud/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "tables/items/index.h"
#include "ui/widget/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    UI_HUD_TEXTURE_SLOTS = 8u,     /* what the hotbar texture draws */
    UI_HUD_TEXTURE_CLOSE_X = 160u, /* the two columns that close its last slot */
    UI_HUD_TEXTURE_CLOSE_W = 2u,
    UI_HUD_WEAR_HEIGHT = 2u,
    UI_HUD_COUNT_RIGHT = 17u,                                    /* from the item's left to where its count ends */
    UI_HUD_COUNT_TOP = 9u,                                       /* from the item's top to its count's */
    UI_HUD_TEXT_BUFFER = 64u,                                    /* past the longest a float32_t can print */
    UI_HUD_DEBUG_LINES = 1u + (uint32_t)UI_HUD_STAGE_COUNT + 2u, /* with the coordinates, and the latitude and sun */
    UI_HUD_DEBUG_MARGIN = 2u,  /* font pixels, from the canvas to the panel and from the panel to its text */
    UI_HUD_DEBUG_WIDTH = 176u, /* font pixels of text: coordinates of eight digits each way, which a test measures */
    UI_HUD_COLOR_TEXT = 0xFFFFFFFFu,
    UI_HUD_COLOR_SHADOW = 0xFF3F3F3Fu,
    UI_HUD_COLOR_FPS = 0xFFFFFF60u,
    UI_HUD_COLOR_PANEL = 0xA0000000u,
    UI_HUD_COLOR_NOTICE_BAND = 0xC0A00000u,
    UI_HUD_COLOR_WEAR_BACK = 0xFF000000u,
};

static const char *const ui_hud_stage_names[UI_HUD_STAGE_COUNT] = {
    [UI_HUD_STAGE_COARSE] = "coarse",     [UI_HUD_STAGE_PRIMARY] = "primary",   [UI_HUD_STAGE_SHADE] = "shade",
    [UI_HUD_STAGE_POST] = "post",         [UI_HUD_STAGE_READBACK] = "readback", [UI_HUD_STAGE_GPU_TOTAL] = "gpu total",
    [UI_HUD_STAGE_SIM_TICK] = "sim tick", [UI_HUD_STAGE_HUD] = "hud",           [UI_HUD_STAGE_PRESENT] = "present",
};

bool ui_hud_init(ui_hud_t *hud, arena_t *arena, const texture_t *texture) {
    *hud = (ui_hud_t){0};
    if (!ui_image_load(&hud->hotbar, arena, texture, "ui/hotbar") ||
        !ui_image_load(&hud->selected, arena, texture, "ui/hotbar_selected") ||
        !ui_image_load(&hud->crosshair, arena, texture, "ui/crosshair") ||
        !ui_image_load(&hud->health, arena, texture, "ui/icon_health") ||
        !ui_image_load(&hud->health_empty, arena, texture, "ui/icon_health_empty") ||
        !ui_image_load(&hud->food, arena, texture, "ui/icon_hunger") ||
        !ui_image_load(&hud->food_empty, arena, texture, "ui/icon_hunger_empty") ||
        !ui_image_load(&hud->air, arena, texture, "ui/icon_breath") ||
        !ui_image_load(&hud->air_empty, arena, texture, "ui/icon_breath_empty")) {
        return false; /* it has said why */
    }
    ASSERT_ALWAYS(hud->hotbar.width == UI_HUD_TEXTURE_CLOSE_X + UI_HUD_TEXTURE_CLOSE_W &&
                  hud->hotbar.height == UI_HUD_HOTBAR_HEIGHT);
    ui_image_t *const icons = arena_alloc_array(arena, ITEM_COUNT, sizeof(ui_image_t), alignof(ui_image_t));
    icons[ITEM_ID_NONE] = (ui_image_t){.borrowed_pixels = nullptr, .width = 0u, .height = 0u};
    for (uint32_t item = ITEM_ID_NONE + 1u; item < ITEM_COUNT; item++) {
        if (!ui_image_load(&icons[item], arena, texture, item_defs[item].texture)) {
            return false; /* it has said why */
        }
    }
    hud->borrowed_icons = icons;
    return true;
}

void ui_hud_deinit(ui_hud_t *hud) {
    *hud = (ui_hud_t){0};
}

uint32_t ui_hud_half_units(float32_t value, uint32_t maximum) {
    if (!(value > 0.0f32)) {
        return 0u;
    }
    if (value >= (float32_t)maximum) {
        return maximum;
    }
    const uint32_t whole = (uint32_t)value;
    return value > (float32_t)whole ? whole + 1u : whole;
}

uint32_t ui_hud_bubbles(uint32_t air, uint32_t maximum) {
    if (air >= maximum) {
        return UI_HUD_ICONS;
    }
    return (air * UI_HUD_ICONS + maximum - 1u) / maximum;
}

ui_hud_layout_t ui_hud_layout(uint32_t canvas_width, uint32_t canvas_height) {
    const uint32_t scale = ui_widget_scale_for(canvas_width, canvas_height);
    const int32_t size = (int32_t)scale;
    const int32_t bar_width = (int32_t)UI_HUD_HOTBAR_WIDTH * size;
    const int32_t bar_height = (int32_t)UI_HUD_HOTBAR_HEIGHT * size;
    const int32_t cross = (int32_t)UI_HUD_CROSSHAIR_SIZE * size;
    const ui_rect_t whole = {0, 0, (int32_t)canvas_width, (int32_t)canvas_height};
    ui_rect_t hotbar = ui_rect_center(whole, bar_width, bar_height);
    hotbar.y = (int32_t)canvas_height - bar_height;
    const int32_t row_y = hotbar.y - (int32_t)UI_HUD_ROW_ABOVE * size;
    return (ui_hud_layout_t){
        .hotbar = hotbar,
        .crosshair = ui_rect_center(whole, cross, cross),
        .row_y = row_y,
        .scale = scale,
        .air_y = row_y - (int32_t)UI_HUD_ROW_ABOVE * size,
    };
}

/* Nine slots out of eight: the last repeats the one before, then the columns that close it. Every slot keeps its own frame. */
static void ui_hud_hotbar_draw(const ui_hud_t *hud, ui_canvas_t *canvas, const ui_hud_layout_t *layout) {
    const int32_t size = (int32_t)layout->scale;
    const int32_t pitch = (int32_t)UI_HUD_SLOT_PITCH;
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        const uint32_t from = slot < UI_HUD_TEXTURE_SLOTS ? slot : UI_HUD_TEXTURE_SLOTS - 1u;
        const ui_rect_t source = {(int32_t)from * pitch, 0, pitch, (int32_t)UI_HUD_HOTBAR_HEIGHT};
        const ui_rect_t rect = {layout->hotbar.x + (int32_t)slot * pitch * size, layout->hotbar.y, pitch * size,
                                layout->hotbar.height};
        ui_compose_image(canvas, &hud->hotbar, source, rect);
    }
    const ui_rect_t close_source = {(int32_t)UI_HUD_TEXTURE_CLOSE_X, 0, (int32_t)UI_HUD_TEXTURE_CLOSE_W,
                                    (int32_t)UI_HUD_HOTBAR_HEIGHT};
    const ui_rect_t close_rect = {layout->hotbar.x + (int32_t)INVENTORY_HOTBAR_SLOTS * pitch * size, layout->hotbar.y,
                                  (int32_t)UI_HUD_TEXTURE_CLOSE_W * size, layout->hotbar.height};
    ui_compose_image(canvas, &hud->hotbar, close_source, close_rect);
}

/* A row of ten, each half or whole. Hearts go from the left, and drumsticks from the right, each half showing the side nearest the full ones. */
static void ui_hud_row_draw(ui_canvas_t *canvas, const ui_hud_layout_t *layout, const ui_image_t *full,
                            const ui_image_t *empty, int32_t y, uint32_t units, bool from_right) {
    const int32_t size = (int32_t)layout->scale;
    const int32_t icon = (int32_t)UI_HUD_ICON_SIZE * size;
    const int32_t half = icon / 2;
    for (uint32_t i = 0u; i < UI_HUD_ICONS; i++) {
        const int32_t x = from_right ? layout->hotbar.x + layout->hotbar.width - (int32_t)(i + 1u) * icon
                                     : layout->hotbar.x + (int32_t)i * icon;
        const ui_rect_t rect = {x, y, icon, icon};
        ui_compose_image_whole(canvas, empty, rect);
        if (units >= (i + 1u) * 2u) {
            ui_compose_image_whole(canvas, full, rect);
        } else if (units == i * 2u + 1u) {
            const int32_t source_half = (int32_t)full->width / 2;
            const ui_rect_t source = {from_right ? (int32_t)full->width - source_half : 0, 0, source_half,
                                      (int32_t)full->height};
            const ui_rect_t part = {from_right ? x + icon - half : x, y, half, icon};
            ui_compose_image(canvas, full, source, part);
        }
    }
}

void ui_hud_item_draw(const ui_hud_t *hud, ui_canvas_t *canvas, const ui_font_t *font, const item_stack_t *stack,
                      int32_t x, int32_t y, uint32_t scale) {
    if (item_stack_is_empty(stack) || stack->item >= ITEM_COUNT) {
        return;
    }
    const int32_t size = (int32_t)scale;
    const int32_t side = (int32_t)UI_HUD_ITEM_SIZE * size;
    ui_compose_image_whole(canvas, &hud->borrowed_icons[stack->item], (ui_rect_t){x, y, side, side});
    const uint32_t durability = item_defs[stack->item].durability;
    if (durability != 0u && stack->wear != 0u) {
        const uint32_t wear = stack->wear < durability ? stack->wear : durability;
        /* At least a pixel, until it breaks: what is still in hand still has a use in it. */
        const uint32_t left = (durability - wear) * UI_HUD_WEAR_WIDTH / durability;
        const uint32_t shown = left == 0u ? 1u : left;
        const uint32_t red = wear * 255u / durability;
        const uint32_t color = 0xFF000000u | red << 16 | (255u - red) << 8;
        const int32_t bar_x = x + (int32_t)UI_HUD_WEAR_LEFT * size;
        const int32_t bar_y = y + (int32_t)UI_HUD_WEAR_TOP * size;
        ui_compose_fill(
            canvas, (ui_rect_t){bar_x, bar_y, (int32_t)UI_HUD_WEAR_WIDTH * size, (int32_t)UI_HUD_WEAR_HEIGHT * size},
            UI_HUD_COLOR_WEAR_BACK);
        ui_compose_fill(canvas, (ui_rect_t){bar_x, bar_y, (int32_t)shown * size, size}, color);
    }
    if (stack->count > 1u) {
        char text[UI_HUD_TEXT_BUFFER];
        snprintf(text, sizeof(text), "%" PRIu32, (uint32_t)stack->count);
        const ui_text_size_t measured = ui_font_measure(font, text, scale);
        const ui_text_style_t style = {.color = UI_HUD_COLOR_TEXT, .shadow = UI_HUD_COLOR_SHADOW, .scale = scale};
        ui_font_draw(font, canvas, x + (int32_t)UI_HUD_COUNT_RIGHT * size - (int32_t)measured.width,
                     y + (int32_t)UI_HUD_COUNT_TOP * size, text, style);
    }
}

void ui_hud_draw(const ui_hud_t *hud, ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_state_t *state) {
    const ui_hud_layout_t layout = ui_hud_layout(canvas->width, canvas->height);
    const int32_t size = (int32_t)layout.scale;
    ui_compose_image_whole(canvas, &hud->crosshair, layout.crosshair);
    ui_hud_hotbar_draw(hud, canvas, &layout);
    const int32_t pitch = (int32_t)UI_HUD_SLOT_PITCH * size;
    if (state->selected < INVENTORY_HOTBAR_SLOTS) {
        const int32_t side = (int32_t)UI_HUD_SELECTED_SIZE * size;
        ui_compose_image_whole(
            canvas, &hud->selected,
            (ui_rect_t){layout.hotbar.x + (int32_t)state->selected * pitch - size, layout.hotbar.y - size, side, side});
    }
    const int32_t inset = (int32_t)UI_HUD_ITEM_INSET * size;
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        ui_hud_item_draw(hud, canvas, font, &state->hotbar[slot], layout.hotbar.x + (int32_t)slot * pitch + inset,
                         layout.hotbar.y + inset, layout.scale);
    }
    ui_hud_row_draw(canvas, &layout, &hud->health, &hud->health_empty, layout.row_y, state->health, false);
    ui_hud_row_draw(canvas, &layout, &hud->food, &hud->food_empty, layout.row_y, state->food, true);
    if (state->bubbles_shown != 0u) {
        /* The row counts in halves, and a bubble is only ever whole. */
        ui_hud_row_draw(canvas, &layout, &hud->air, &hud->air_empty, layout.air_y, (uint32_t)state->bubbles * 2u, true);
    }
}

/* Half the HUD's size, rounding up: a tenth of the screen's height is plenty for a thing only looked at to read numbers. */
static uint32_t ui_hud_debug_scale(uint32_t canvas_width, uint32_t canvas_height) {
    return (ui_widget_scale_for(canvas_width, canvas_height) + 1u) / 2u;
}

ui_rect_t ui_hud_debug_panel(uint32_t canvas_width, uint32_t canvas_height) {
    const int32_t size = (int32_t)ui_hud_debug_scale(canvas_width, canvas_height);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * size;
    return (ui_rect_t){margin, margin, (int32_t)UI_HUD_DEBUG_WIDTH * size + 2 * margin,
                       (int32_t)(UI_HUD_DEBUG_LINES * UI_FONT_LINE) * size + 2 * margin};
}

void ui_hud_notice_draw(ui_canvas_t *canvas, const ui_font_t *font, const char *text) {
    const uint32_t scale = ui_hud_debug_scale(canvas->width, canvas->height);
    const ui_text_size_t measured = ui_font_measure(font, text, scale);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * (int32_t)scale;
    const ui_rect_t band = {0, margin, (int32_t)canvas->width, (int32_t)measured.height + 2 * margin};
    ui_compose_fill(canvas, band, UI_HUD_COLOR_NOTICE_BAND);
    const ui_text_style_t style = {.color = UI_HUD_COLOR_TEXT, .shadow = UI_HUD_COLOR_SHADOW, .scale = scale};
    ui_font_draw(font, canvas, ((int32_t)canvas->width - (int32_t)measured.width) / 2, band.y + margin, text, style);
}

static void ui_hud_coordinates_text(char *text, uint64_t bytes, const int32_t block[3]) {
    snprintf(text, bytes, "x %" PRId32 " y %" PRId32 " z %" PRId32, block[0], block[1], block[2]);
}

ui_rect_t ui_hud_coordinates_panel(const ui_canvas_t *canvas, const ui_font_t *font, const int32_t block[3],
                                   bool below_notice) {
    const uint32_t scale = ui_hud_debug_scale(canvas->width, canvas->height);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * (int32_t)scale;
    char text[UI_HUD_TEXT_BUFFER];
    ui_hud_coordinates_text(text, sizeof(text), block);
    const ui_text_size_t measured = ui_font_measure(font, text, scale);
    const int32_t height = (int32_t)measured.height + 2 * margin;
    /* A band starts a margin down and is as tall as this panel. */
    const int32_t top = below_notice ? margin + height : margin;
    return (ui_rect_t){margin, top, (int32_t)measured.width + 2 * margin, height};
}

void ui_hud_coordinates_draw(ui_canvas_t *canvas, const ui_font_t *font, const int32_t block[3], bool below_notice) {
    const uint32_t scale = ui_hud_debug_scale(canvas->width, canvas->height);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * (int32_t)scale;
    char text[UI_HUD_TEXT_BUFFER];
    ui_hud_coordinates_text(text, sizeof(text), block);
    const ui_rect_t panel = ui_hud_coordinates_panel(canvas, font, block, below_notice);
    ui_compose_fill(canvas, panel, UI_HUD_COLOR_PANEL);
    const ui_text_style_t style = {.color = UI_HUD_COLOR_TEXT, .shadow = UI_HUD_COLOR_SHADOW, .scale = scale};
    ui_font_draw(font, canvas, panel.x + margin, panel.y + margin, text, style);
}

void ui_hud_date_draw(ui_canvas_t *canvas, const ui_font_t *font, const char *text, bool below_notice) {
    ui_hud_corner_t corner;
    ui_hud_corner_clear(&corner);
    (void)snprintf(corner.lines[UI_HUD_CORNER_DATE], sizeof(corner.lines[UI_HUD_CORNER_DATE]), "%s", text);
    (void)ui_hud_corner_draw(canvas, font, &corner, (ui_rect_t){0, 0, 0, 0}, below_notice);
}

/* ---- The top right corner ------------------------------------------------------------------------------------------ */

void ui_hud_corner_clear(ui_hud_corner_t *corner) {
    memset(corner, 0, sizeof(*corner));
    corner->arrow_line = (uint8_t)UI_HUD_CORNER_NONE;
    corner->warning_line = (uint8_t)UI_HUD_CORNER_NONE;
}

/* What the arrow adds to its line's panel: a space, then a square a line high. */
static int32_t ui_hud_corner_arrow_extra(uint32_t scale) {
    return (int32_t)((UI_FONT_SPACE_WIDTH + UI_FONT_SPACING + UI_FONT_LINE) * scale);
}

void ui_hud_corner_arrow_ends(ui_rect_t square, const float32_t arrow[2], uint32_t scale, int32_t out_tail[2],
                              int32_t out_tip[2]) {
    /* Half its length: the square's half less a font pixel, so the shadow too stays inside. */
    const float32_t half = (float32_t)square.width * 0.5f32 - (float32_t)scale;
    const float32_t cx = (float32_t)square.x + (float32_t)square.width * 0.5f32;
    const float32_t cy = (float32_t)square.y + (float32_t)square.height * 0.5f32;
    out_tail[0] = (int32_t)floorf(cx - arrow[0] * half);
    out_tail[1] = (int32_t)floorf(cy - arrow[1] * half);
    out_tip[0] = (int32_t)floorf(cx + arrow[0] * half);
    out_tip[1] = (int32_t)floorf(cy + arrow[1] * half);
}

/* Bresenham's line, both ends included, each point a square of size pixels with the point at its top left. */
static void ui_hud_corner_line(ui_canvas_t *canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t size,
                               uint32_t color) {
    const int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int32_t step_x = x0 < x1 ? 1 : -1;
    const int32_t step_y = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    int32_t x = x0, y = y0;
    bool more = true;
    while (more) {
        ui_compose_fill(canvas, (ui_rect_t){x, y, size, size}, color);
        more = x != x1 || y != y1;
        const int32_t twice = 2 * error;
        if (more && twice >= dy) {
            error += dy;
            x += step_x;
        }
        if (more && twice <= dx) {
            error += dx;
            y += step_y;
        }
    }
}

/* The shaft and the two strokes of the head, each a font pixel thick, moved by off pixels right and down. */
static void ui_hud_corner_arrow_strokes(ui_canvas_t *canvas, const int32_t tail[2], const int32_t tip[2],
                                        const float32_t arrow[2], uint32_t scale, int32_t off, uint32_t color) {
    const int32_t size = (int32_t)scale;
    /* A point's square grows right and down from it: drawing from half a square up and left keeps the line on its ends. */
    const int32_t back = size / 2 - off;
    ui_hud_corner_line(canvas, tail[0] - back, tail[1] - back, tip[0] - back, tip[1] - back, size, color);
    const float32_t gx = (float32_t)(tip[0] - tail[0]), gy = (float32_t)(tip[1] - tail[1]);
    const float32_t head = sqrtf(gx * gx + gy * gy) * 0.4f32;
    /* The head's strokes go back from the tip at 35 degrees either side of the shaft. */
    const float32_t c = 0.81915204f32, s = 0.57357644f32;
    for (int32_t side = -1; side <= 1; side += 2) {
        const float32_t bx = -arrow[0] * c + (float32_t)side * arrow[1] * s;
        const float32_t by = -arrow[1] * c - (float32_t)side * arrow[0] * s;
        const int32_t ex = (int32_t)floorf((float32_t)tip[0] + bx * head + 0.5f32);
        const int32_t ey = (int32_t)floorf((float32_t)tip[1] + by * head + 0.5f32);
        ui_hud_corner_line(canvas, tip[0] - back, tip[1] - back, ex - back, ey - back, size, color);
    }
}

ui_rect_t ui_hud_corner_draw(ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_corner_t *corner, ui_rect_t avoid,
                             bool below_notice) {
    const uint32_t scale = ui_hud_debug_scale(canvas->width, canvas->height);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * (int32_t)scale;
    int32_t widths[UI_HUD_CORNER_LINES] = {0};
    int32_t heights[UI_HUD_CORNER_LINES] = {0};
    int32_t text_widths[UI_HUD_CORNER_LINES] = {0};
    int32_t widest = 0, total = 0;
    /* Slot 0 is the settlement's, which plan 004 never draws. */
    for (uint32_t i = UI_HUD_CORNER_DATE; i < UI_HUD_CORNER_LINES; i++) {
        if (corner->lines[i][0] == '\0') {
            continue;
        }
        const ui_text_size_t measured = ui_font_measure(font, corner->lines[i], scale);
        text_widths[i] = (int32_t)measured.width;
        widths[i] =
            (int32_t)measured.width + 2 * margin + (corner->arrow_line == i ? ui_hud_corner_arrow_extra(scale) : 0);
        heights[i] = (int32_t)measured.height + 2 * margin;
        widest = widths[i] > widest ? widths[i] : widest;
        total += heights[i];
    }
    if (total == 0) {
        return (ui_rect_t){0, 0, 0, 0};
    }
    /* Below a notice's band, as the coordinates go: it starts a margin down and is as tall as a line's panel. */
    const int32_t band = (int32_t)ui_font_measure(font, "x", scale).height + 2 * margin;
    const int32_t right = (int32_t)canvas->width - margin;
    ui_rect_t whole = {right - widest, below_notice ? margin + band : margin, widest, total};
    if (avoid.width > 0 && avoid.height > 0) {
        const ui_rect_t meet = ui_rect_intersect(whole, avoid);
        if (meet.width > 0 && meet.height > 0) {
            whole.y = avoid.y + avoid.height + margin;
        }
    }
    const ui_text_style_t style = {.color = UI_HUD_COLOR_TEXT, .shadow = UI_HUD_COLOR_SHADOW, .scale = scale};
    int32_t y = whole.y;
    for (uint32_t i = UI_HUD_CORNER_DATE; i < UI_HUD_CORNER_LINES; i++) {
        if (heights[i] == 0) {
            continue;
        }
        const ui_rect_t panel = {right - widths[i], y, widths[i], heights[i]};
        ui_compose_fill(canvas, panel, corner->warning_line == i ? UI_HUD_COLOR_NOTICE_BAND : UI_HUD_COLOR_PANEL);
        ui_font_draw(font, canvas, panel.x + margin, panel.y + margin, corner->lines[i], style);
        if (corner->arrow_line == i) {
            const int32_t side = (int32_t)(UI_FONT_LINE * scale);
            const ui_rect_t square = {panel.x + margin + text_widths[i] +
                                          (int32_t)((UI_FONT_SPACE_WIDTH + UI_FONT_SPACING) * scale),
                                      panel.y + (panel.height - side) / 2, side, side};
            int32_t tail[2], tip[2];
            ui_hud_corner_arrow_ends(square, corner->arrow, scale, tail, tip);
            ui_hud_corner_arrow_strokes(canvas, tail, tip, corner->arrow, scale, (int32_t)scale, UI_HUD_COLOR_SHADOW);
            ui_hud_corner_arrow_strokes(canvas, tail, tip, corner->arrow, scale, 0, UI_HUD_COLOR_TEXT);
        }
        y += heights[i];
    }
    return whole;
}

void ui_hud_debug_draw(ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_debug_t *debug) {
    const uint32_t scale = ui_hud_debug_scale(canvas->width, canvas->height);
    const int32_t size = (int32_t)scale;
    const ui_rect_t panel = ui_hud_debug_panel(canvas->width, canvas->height);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * size;
    const int32_t line = (int32_t)UI_FONT_LINE * size;
    const int32_t left = panel.x + margin;
    const int32_t right = panel.x + panel.width - margin;
    /* A time too long for its line must stay on the panel. */
    const ui_rect_t clip_before = ui_canvas_clip_set(canvas, ui_rect_intersect(panel, canvas->clip));
    ui_compose_fill(canvas, panel, UI_HUD_COLOR_PANEL);
    char text[UI_HUD_TEXT_BUFFER];
    snprintf(text, sizeof(text), "FPS %.1f", (libc_double_t)debug->fps);
    ui_text_style_t style = {.color = UI_HUD_COLOR_FPS, .shadow = UI_HUD_COLOR_SHADOW, .scale = scale};
    int32_t y = panel.y + margin;
    ui_font_draw(font, canvas, left, y, text, style);
    style.color = UI_HUD_COLOR_TEXT;
    for (uint32_t stage = 0u; stage < UI_HUD_STAGE_COUNT; stage++) {
        y += line;
        ui_font_draw(font, canvas, left, y, ui_hud_stage_names[stage], style);
        snprintf(text, sizeof(text), "%.2f ms", (libc_double_t)debug->stage_ms[stage]);
        const ui_text_size_t measured = ui_font_measure(font, text, scale);
        ui_font_draw(font, canvas, right - (int32_t)measured.width, y, text, style);
    }
    y += line;
    ui_hud_coordinates_text(text, sizeof(text), debug->block);
    ui_font_draw(font, canvas, left, y, text, style);
    /* Plan 004 phase 02: "lat 45.00 N  sun 30.0", N at exactly 0. The longest, "lat 90.00 N  sun -66.5", fits the panel. */
    y += line;
    const float32_t latitude = debug->latitude_deg;
    snprintf(text, sizeof(text), "lat %.2f %c  sun %.1f", (libc_double_t)(latitude < 0.0f32 ? -latitude : latitude),
             latitude < 0.0f32 ? 'S' : 'N', (libc_double_t)debug->sun_elevation_deg);
    ui_font_draw(font, canvas, left, y, text, style);
    (void)ui_canvas_clip_set(canvas, clip_before);
}
