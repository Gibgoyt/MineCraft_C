/* See README.md. */
#include "ui/screen_settings/index.h"

#include "assert/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Every number here is in texture pixels, and multiplied by the frame's scale. The screen is made to fit 320 by 240. */
enum : int32_t {
    SETTINGS_SCREEN_HEIGHT = 240,
    SETTINGS_ROW_WIDTH = 200,
    SETTINGS_ROW_STEP = 24, /* a row's 20, and a gap */
    SETTINGS_FIRST_ROW_Y = 24,
    SETTINGS_TITLE_Y = 8,
    SETTINGS_TITLE_HEIGHT = 9,
    SETTINGS_GAP = 4,
    SETTINGS_EFFECT_WIDTH = 120,
    SETTINGS_EFFECT_ROWS = 8,
    /* Row 6, left to right, with SETTINGS_GAP between: each as wide as its box and its words want, and Effects... has what is left. */
    SETTINGS_VSYNC_WIDTH = 46,
    SETTINGS_COORDINATES_WIDTH = 82,
    SETTINGS_DONE_ROW = 8,
    SETTINGS_MESSAGE_ROW = 7,
};

enum : uint32_t {
    SETTINGS_ID_RENDER_DISTANCE = 100u,
    SETTINGS_ID_FULL_DETAIL = 101u,
    SETTINGS_ID_FOV = 102u,
    SETTINGS_ID_SENSITIVITY = 103u,
    SETTINGS_ID_RENDER_SCALE = 104u,
    SETTINGS_ID_WORKERS = 105u,
    SETTINGS_ID_VSYNC = 106u,
    SETTINGS_ID_EFFECTS = 107u,
    SETTINGS_ID_DONE = 108u,
    SETTINGS_ID_COORDINATES = 109u,
    SETTINGS_ID_EFFECT_0 = 120u,
    SETTINGS_TEXT_COLOR = 0xFFFFFFFFu,
    SETTINGS_ERROR_COLOR = 0xFFFF6060u,
    SETTINGS_SHADE = 0x90000000u,      /* the world, still there behind it, where a person can see what a change does */
    SETTINGS_BACKGROUND = 0xFF202020u, /* no world to see */
    SETTINGS_LABEL_BYTES = 48u,
};

void ui_screen_settings_open(ui_screen_settings_t *screen, ui_widget_t *widget, settings_values_t *values,
                             uint32_t full_detail_live_max, uint32_t workers_running, bool over_world) {
    memset(screen, 0, sizeof(*screen));
    screen->borrowed_widget = widget;
    screen->borrowed_values = values;
    screen->full_detail_live_max = full_detail_live_max;
    screen->workers_running = workers_running;
    screen->page = UI_SCREEN_SETTINGS_PAGE_MAIN;
    screen->over_world = over_world ? 1u : 0u;
    widget->focus_id = 0u;
}

void ui_screen_settings_message_set(ui_screen_settings_t *screen, const char *message) {
    (void)snprintf(screen->message, sizeof(screen->message), "%s", message);
}

/* Where the screen's own 240 starts, in the middle of what there is. */
static int32_t ui_screen_settings_top(const ui_widget_t *widget) {
    return ((int32_t)widget->canvas_height - SETTINGS_SCREEN_HEIGHT * (int32_t)widget->scale) / 2;
}

static ui_rect_t ui_screen_settings_row(const ui_widget_t *widget, int32_t row, int32_t width) {
    const int32_t scale = (int32_t)widget->scale;
    return (ui_rect_t){((int32_t)widget->canvas_width - width * scale) / 2,
                       ui_screen_settings_top(widget) + (SETTINGS_FIRST_ROW_Y + row * SETTINGS_ROW_STEP) * scale,
                       width * scale, (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale};
}

/* A setting's slider. True if its value is different now. */
static bool ui_screen_settings_slider(ui_widget_t *widget, uint32_t id, int32_t row, const char *label, uint32_t *value,
                                      uint32_t minimum, uint32_t maximum) {
    int32_t now = (int32_t)*value;
    const bool changed = ui_widget_slider(widget, id, ui_screen_settings_row(widget, row, SETTINGS_ROW_WIDTH), label,
                                          &now, (int32_t)minimum, (int32_t)maximum);
    *value = (uint32_t)now;
    return changed;
}

static bool ui_screen_settings_main_page(ui_screen_settings_t *screen, bool *out_done) {
    ui_widget_t *const widget = screen->borrowed_widget;
    settings_values_t *const values = screen->borrowed_values;
    const int32_t scale = (int32_t)widget->scale;
    char label[SETTINGS_LABEL_BYTES];
    bool changed = false;

    (void)snprintf(label, sizeof(label), "Render distance: %" PRIu32 " chunks", values->render_distance);
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_RENDER_DISTANCE, 0, label, &values->render_distance,
                                         SETTINGS_RENDER_DISTANCE_MIN, SETTINGS_RENDER_DISTANCE_MAX);
    const bool past_live = screen->full_detail_live_max != 0u && values->full_detail > screen->full_detail_live_max;
    (void)snprintf(label, sizeof(label), "Full detail: %" PRIu32 "%s", values->full_detail,
                   past_live ? " (new world)" : " chunks");
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_FULL_DETAIL, 1, label, &values->full_detail,
                                         SETTINGS_FULL_DETAIL_MIN, SETTINGS_FULL_DETAIL_MAX);
    (void)snprintf(label, sizeof(label), "Field of view: %" PRIu32, values->fov_degrees);
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_FOV, 2, label, &values->fov_degrees, SETTINGS_FOV_MIN,
                                         SETTINGS_FOV_MAX);
    (void)snprintf(label, sizeof(label), "Mouse sensitivity: %" PRIu32 "%%", values->sensitivity_percent);
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_SENSITIVITY, 3, label, &values->sensitivity_percent,
                                         SETTINGS_SENSITIVITY_MIN, SETTINGS_SENSITIVITY_MAX);
    (void)snprintf(label, sizeof(label), "Render scale: %" PRIu32 "%%", values->render_scale_percent);
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_RENDER_SCALE, 4, label, &values->render_scale_percent,
                                         SETTINGS_RENDER_SCALE_MIN, SETTINGS_RENDER_SCALE_MAX);
    const bool workers_later = values->worker_count != screen->workers_running;
    (void)snprintf(label, sizeof(label), "Workers: %" PRIu32 "%s", values->worker_count,
                   workers_later ? " (next start)" : "");
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_WORKERS, 5, label, &values->worker_count,
                                         SETTINGS_WORKERS_MIN, SETTINGS_WORKERS_MAX);

    const ui_rect_t row = ui_screen_settings_row(widget, 6, SETTINGS_ROW_WIDTH);
    const int32_t vsync_width = SETTINGS_VSYNC_WIDTH * scale;
    const int32_t coordinates_width = SETTINGS_COORDINATES_WIDTH * scale;
    const int32_t coordinates_x = row.x + vsync_width + SETTINGS_GAP * scale;
    const int32_t effects_x = coordinates_x + coordinates_width + SETTINGS_GAP * scale;
    bool vsync = values->vsync != 0u;
    if (ui_widget_checkbox(widget, SETTINGS_ID_VSYNC, (ui_rect_t){row.x, row.y, vsync_width, row.height}, "VSync",
                           &vsync)) {
        values->vsync = vsync ? 1u : 0u;
        changed = true;
    }
    bool coordinates = values->show_coordinates != 0u;
    if (ui_widget_checkbox(widget, SETTINGS_ID_COORDINATES,
                           (ui_rect_t){coordinates_x, row.y, coordinates_width, row.height}, "Coordinates",
                           &coordinates)) {
        values->show_coordinates = coordinates ? 1u : 0u;
        changed = true;
    }
    if (ui_widget_button(widget, SETTINGS_ID_EFFECTS,
                         (ui_rect_t){effects_x, row.y, row.x + row.width - effects_x, row.height}, "Effects...",
                         true)) {
        screen->page = UI_SCREEN_SETTINGS_PAGE_EFFECTS;
    }
    if (ui_widget_button(widget, SETTINGS_ID_DONE,
                         ui_screen_settings_row(widget, SETTINGS_DONE_ROW, SETTINGS_ROW_WIDTH), "Done", true)) {
        *out_done = true;
    }
    return changed;
}

static bool ui_screen_settings_effects_page(ui_screen_settings_t *screen) {
    ui_widget_t *const widget = screen->borrowed_widget;
    settings_values_t *const values = screen->borrowed_values;
    const int32_t scale = (int32_t)widget->scale;
    bool changed = false;
    for (uint32_t i = 0u; i < SETTINGS_EFFECT_COUNT; i++) {
        const settings_effect_t *const effect = settings_persist_effect(i);
        /* Two columns of eight, and what is past them on the row of Back, whose place is its right. */
        const bool past = (int32_t)i >= 2 * SETTINGS_EFFECT_ROWS;
        const int32_t row = past ? SETTINGS_DONE_ROW : (int32_t)i % SETTINGS_EFFECT_ROWS;
        const ui_rect_t middle = ui_screen_settings_row(widget, row, 0);
        const int32_t x = (int32_t)i < SETTINGS_EFFECT_ROWS || past
                              ? middle.x - (SETTINGS_EFFECT_WIDTH + SETTINGS_GAP / 2) * scale
                              : middle.x + SETTINGS_GAP / 2 * scale;
        const ui_rect_t rect = {x, middle.y, SETTINGS_EFFECT_WIDTH * scale, middle.height};
        bool on = (values->effects & effect->bit) != 0u;
        if (ui_widget_checkbox(widget, SETTINGS_ID_EFFECT_0 + i, rect, effect->label, &on)) {
            values->effects = on ? values->effects | effect->bit : values->effects & ~effect->bit;
            changed = true;
        }
    }
    const ui_rect_t back_row = ui_screen_settings_row(widget, SETTINGS_DONE_ROW, 0);
    const ui_rect_t back = {back_row.x + SETTINGS_GAP / 2 * scale, back_row.y, SETTINGS_EFFECT_WIDTH * scale,
                            back_row.height};
    if (ui_widget_button(widget, SETTINGS_ID_DONE, back, "Back", true)) {
        screen->page = UI_SCREEN_SETTINGS_PAGE_MAIN;
    }
    return changed;
}

ui_screen_settings_action_t ui_screen_settings_frame(ui_screen_settings_t *screen, bool escape) {
    ui_widget_t *const widget = screen->borrowed_widget;
    const int32_t scale = (int32_t)widget->scale;
    const ui_rect_t whole = {0, 0, (int32_t)widget->canvas_width, (int32_t)widget->canvas_height};
    ui_compose_fill(widget->borrowed_canvas, whole, screen->over_world != 0u ? SETTINGS_SHADE : SETTINGS_BACKGROUND);
    const bool effects_page = screen->page == UI_SCREEN_SETTINGS_PAGE_EFFECTS;
    const ui_rect_t title = {0, ui_screen_settings_top(widget) + SETTINGS_TITLE_Y * scale, whole.width,
                             SETTINGS_TITLE_HEIGHT * scale};
    ui_widget_label_centered(widget, title, effects_page ? "Effects" : "Settings", SETTINGS_TEXT_COLOR, true);
    bool done = false;
    const bool changed =
        effects_page ? ui_screen_settings_effects_page(screen) : ui_screen_settings_main_page(screen, &done);
    if (!effects_page && screen->message[0] != '\0') {
        const ui_rect_t line = ui_screen_settings_row(widget, SETTINGS_MESSAGE_ROW, SETTINGS_ROW_WIDTH);
        ui_widget_label_centered(widget, line, screen->message, SETTINGS_ERROR_COLOR, true);
    }
    if (escape) {
        if (effects_page) {
            screen->page = UI_SCREEN_SETTINGS_PAGE_MAIN;
        } else {
            done = true;
        }
    }
    if (done) {
        return UI_SCREEN_SETTINGS_DONE;
    }
    return changed ? UI_SCREEN_SETTINGS_CHANGED : UI_SCREEN_SETTINGS_NONE;
}
