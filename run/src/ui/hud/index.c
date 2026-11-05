/* See README.md. Everything here is drawn fresh each frame from plain values, through ui/compose and so through the blend kernel. */
#include "ui/hud/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "tables/items/index.h"
#include "ui/widget/index.h"

#include <inttypes.h>
#include <stdio.h>

enum : uint32_t {
    UI_HUD_TEXTURE_SLOTS = 8u,     /* what the hotbar texture draws */
    UI_HUD_TEXTURE_CLOSE_X = 160u, /* the two columns that close its last slot */
    UI_HUD_TEXTURE_CLOSE_W = 2u,
    UI_HUD_WEAR_HEIGHT = 2u,
    UI_HUD_COUNT_RIGHT = 17u,                                    /* from the item's left to where its count ends */
    UI_HUD_COUNT_TOP = 9u,                                       /* from the item's top to its count's */
    UI_HUD_TEXT_BUFFER = 64u,                                    /* past the longest a float32_t can print */
    UI_HUD_DEBUG_LINES = 1u + (uint32_t)UI_HUD_STAGE_COUNT + 1u, /* with the coordinates */
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

void ui_hud_coordinates_draw(ui_canvas_t *canvas, const ui_font_t *font, const int32_t block[3], bool below_notice) {
    const uint32_t scale = ui_hud_debug_scale(canvas->width, canvas->height);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * (int32_t)scale;
    char text[UI_HUD_TEXT_BUFFER];
    ui_hud_coordinates_text(text, sizeof(text), block);
    const ui_text_size_t measured = ui_font_measure(font, text, scale);
    const int32_t height = (int32_t)measured.height + 2 * margin;
    /* A band starts a margin down and is as tall as this panel. */
    const int32_t top = below_notice ? margin + height : margin;
    ui_compose_fill(canvas, (ui_rect_t){margin, top, (int32_t)measured.width + 2 * margin, height}, UI_HUD_COLOR_PANEL);
    const ui_text_style_t style = {.color = UI_HUD_COLOR_TEXT, .shadow = UI_HUD_COLOR_SHADOW, .scale = scale};
    ui_font_draw(font, canvas, margin + margin, top + margin, text, style);
}

void ui_hud_date_draw(ui_canvas_t *canvas, const ui_font_t *font, const char *text, bool below_notice) {
    const uint32_t scale = ui_hud_debug_scale(canvas->width, canvas->height);
    const int32_t margin = (int32_t)UI_HUD_DEBUG_MARGIN * (int32_t)scale;
    const ui_text_size_t measured = ui_font_measure(font, text, scale);
    const int32_t width = (int32_t)measured.width + 2 * margin;
    const int32_t height = (int32_t)measured.height + 2 * margin;
    const int32_t top = below_notice ? margin + height : margin;
    const int32_t left = (int32_t)canvas->width - margin - width;
    ui_compose_fill(canvas, (ui_rect_t){left, top, width, height}, UI_HUD_COLOR_PANEL);
    const ui_text_style_t style = {.color = UI_HUD_COLOR_TEXT, .shadow = UI_HUD_COLOR_SHADOW, .scale = scale};
    ui_font_draw(font, canvas, left + margin, top + margin, text, style);
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
    (void)ui_canvas_clip_set(canvas, clip_before);
}
