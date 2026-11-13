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
    /* The main page's rows, from the top: five sliders (plan 009 phase 04 took one out, and the rows under it moved up one), then VSync,
     * HUD... and Effects... on one row, left to right with SETTINGS_GAP between, each as wide as its box and its words want and Effects...
     * what is left; HUD... has the place the Coordinates box had before plan 004 phase 10 moved it to the HUD page. Then the sound volume's
     * slider, and Done. */
    SETTINGS_FOV_ROW = 1,
    SETTINGS_SENSITIVITY_ROW = 2,
    SETTINGS_RENDER_SCALE_ROW = 3,
    SETTINGS_WORKERS_ROW = 4,
    SETTINGS_BOXES_ROW = 5,
    SETTINGS_VSYNC_WIDTH = 46,
    SETTINGS_HUD_WIDTH = 82,
    /* The render scale's row: plan 008 phase 05's dynamic resolution beside it, a button that steps its floor and turns it off. */
    SETTINGS_DYNAMIC_WIDTH = 64,
    SETTINGS_VOLUME_ROW = 6,
    SETTINGS_MAIN_DONE_ROW = 7,
    /* The Effects page's Back, under its two columns of eight. */
    SETTINGS_DONE_ROW = 8,
    /* The HUD page's rows: plan 004 phase 10. */
    SETTINGS_HUD_COORDINATES_ROW = 0,
    SETTINGS_HUD_DATE_ROW = 1,
    SETTINGS_HUD_CLIMATE_ROW = 2,
    SETTINGS_HUD_WARNINGS_ROW = 3,
    /* Not the HUD, but the page with room: plan 005 phase 7's pause when the window loses the focus. */
    SETTINGS_HUD_PAUSE_AWAY_ROW = 4,
    /* Not the HUD either, but over the world as it is, beside what is: plan 005 phase 10's preview of placing. */
    SETTINGS_HUD_PLACE_PREVIEW_ROW = 5,
};

enum : uint32_t {
    SETTINGS_ID_RENDER_DISTANCE = 100u,
    SETTINGS_ID_FOV = 102u,
    SETTINGS_ID_SENSITIVITY = 103u,
    SETTINGS_ID_RENDER_SCALE = 104u,
    SETTINGS_ID_WORKERS = 105u,
    SETTINGS_ID_VSYNC = 106u,
    SETTINGS_ID_EFFECTS = 107u,
    SETTINGS_ID_DONE = 108u,
    SETTINGS_ID_COORDINATES = 109u,
    SETTINGS_ID_VOLUME = 110u,
    SETTINGS_ID_DATE = 111u,
    SETTINGS_ID_HUD = 112u,
    SETTINGS_ID_SHOW_CLIMATE = 113u,
    SETTINGS_ID_STORM_WARNINGS = 114u,
    SETTINGS_ID_PAUSE_AWAY = 115u,
    SETTINGS_ID_PLACE_PREVIEW = 116u,
    SETTINGS_ID_DYNAMIC = 117u,
    SETTINGS_ID_EFFECT_0 = 120u,
    SETTINGS_TEXT_COLOR = 0xFFFFFFFFu,
    SETTINGS_ERROR_COLOR = 0xFFFF6060u,
    SETTINGS_SHADE = 0x90000000u,      /* the world, still there behind it, where a person can see what a change does */
    SETTINGS_BACKGROUND = 0xFF202020u, /* no world to see */
    SETTINGS_LABEL_BYTES = 48u,
};

void ui_screen_settings_open(ui_screen_settings_t *screen, ui_widget_t *widget, settings_values_t *values,
                             uint32_t workers_running, bool over_world) {
    memset(screen, 0, sizeof(*screen));
    screen->borrowed_widget = widget;
    screen->borrowed_values = values;
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

void ui_screen_settings_distance_label(char *out, uint32_t capacity, const char *name, uint32_t chunks) {
    /* "32 chunks (512 blocks)" is wider than the track at most values; the tooltip says what the first number counts. */
    (void)snprintf(out, capacity, "%s: %" PRIu32 " (%" PRIu32 " blocks)", name, chunks,
                   chunks * (uint32_t)MC_WORLD_CHUNK_SIZE);
}

/* Plan 005 phase 8: one line on what the distance means, by the pointer while it is over its slider. */
static void ui_screen_settings_explain(ui_widget_t *widget) {
    const int32_t x = widget->input.pointer_x;
    const int32_t y = widget->input.pointer_y;
    if (ui_rect_contains(ui_screen_settings_row(widget, 0, SETTINGS_ROW_WIDTH), x, y)) {
        ui_widget_tooltip(widget, UI_SCREEN_SETTINGS_EXPLAIN_RENDER_DISTANCE);
    }
}

/* The next of dynamic resolution's choices: off, then each floor from the least up, then off. */
static void ui_screen_settings_dynamic_step(settings_values_t *values) {
    const uint32_t next =
        settings_persist_floor_next(values->dynamic_resolution != 0u ? values->dynamic_floor_percent : 0u);
    values->dynamic_resolution = next != 0u ? 1u : 0u;
    values->dynamic_floor_percent = next != 0u ? (uint8_t)next : values->dynamic_floor_percent;
}

/* The render scale's row: its slider, and beside it plan 008 phase 05's dynamic resolution: "Dyn 50%" is drawn down to half each side while
 * turning or late, "Dyn off" never. True if either changed. */
static bool ui_screen_settings_scale_row(ui_widget_t *widget, settings_values_t *values) {
    const int32_t scale = (int32_t)widget->scale;
    const ui_rect_t row = ui_screen_settings_row(widget, SETTINGS_RENDER_SCALE_ROW, SETTINGS_ROW_WIDTH);
    const int32_t dynamic_width = SETTINGS_DYNAMIC_WIDTH * scale;
    const int32_t slider_width = row.width - dynamic_width - SETTINGS_GAP * scale;
    char label[SETTINGS_LABEL_BYTES];
    (void)snprintf(label, sizeof(label), "Render scale: %" PRIu32 "%%", values->render_scale_percent);
    int32_t now = (int32_t)values->render_scale_percent;
    bool changed =
        ui_widget_slider(widget, SETTINGS_ID_RENDER_SCALE, (ui_rect_t){row.x, row.y, slider_width, row.height}, label,
                         &now, (int32_t)SETTINGS_RENDER_SCALE_MIN, (int32_t)SETTINGS_RENDER_SCALE_MAX);
    values->render_scale_percent = (uint32_t)now;
    if (values->dynamic_resolution != 0u) {
        (void)snprintf(label, sizeof(label), "Dyn %" PRIu32 "%%", (uint32_t)values->dynamic_floor_percent);
    } else {
        (void)snprintf(label, sizeof(label), "Dyn off");
    }
    const ui_rect_t button = {row.x + row.width - dynamic_width, row.y, dynamic_width, row.height};
    if (ui_widget_button(widget, SETTINGS_ID_DYNAMIC, button, label, true)) {
        ui_screen_settings_dynamic_step(values);
        changed = true;
    }
    return changed;
}

static bool ui_screen_settings_main_page(ui_screen_settings_t *screen, bool *out_done) {
    ui_widget_t *const widget = screen->borrowed_widget;
    settings_values_t *const values = screen->borrowed_values;
    const int32_t scale = (int32_t)widget->scale;
    char label[SETTINGS_LABEL_BYTES];
    bool changed = false;

    ui_screen_settings_distance_label(label, sizeof(label), "Render distance", values->render_distance);
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_RENDER_DISTANCE, 0, label, &values->render_distance,
                                         SETTINGS_RENDER_DISTANCE_MIN, SETTINGS_RENDER_DISTANCE_MAX);
    ui_screen_settings_explain(widget);
    (void)snprintf(label, sizeof(label), "Field of view: %" PRIu32, values->fov_degrees);
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_FOV, SETTINGS_FOV_ROW, label, &values->fov_degrees,
                                         SETTINGS_FOV_MIN, SETTINGS_FOV_MAX);
    (void)snprintf(label, sizeof(label), "Mouse sensitivity: %" PRIu32 "%%", values->sensitivity_percent);
    changed |=
        ui_screen_settings_slider(widget, SETTINGS_ID_SENSITIVITY, SETTINGS_SENSITIVITY_ROW, label,
                                  &values->sensitivity_percent, SETTINGS_SENSITIVITY_MIN, SETTINGS_SENSITIVITY_MAX);
    changed |= ui_screen_settings_scale_row(widget, values);
    const bool workers_later = values->worker_count != screen->workers_running;
    (void)snprintf(label, sizeof(label), "Workers: %" PRIu32 "%s", values->worker_count,
                   workers_later ? " (next start)" : "");
    changed |= ui_screen_settings_slider(widget, SETTINGS_ID_WORKERS, SETTINGS_WORKERS_ROW, label,
                                         &values->worker_count, SETTINGS_WORKERS_MIN, SETTINGS_WORKERS_MAX);

    int32_t volume = (int32_t)values->sound_volume_percent;
    (void)snprintf(label, sizeof(label), "Sound volume: %" PRId32 "%%", volume);
    changed |= ui_widget_slider(widget, SETTINGS_ID_VOLUME,
                                ui_screen_settings_row(widget, SETTINGS_VOLUME_ROW, SETTINGS_ROW_WIDTH), label, &volume,
                                (int32_t)SETTINGS_SOUND_VOLUME_MIN, (int32_t)SETTINGS_SOUND_VOLUME_MAX);
    values->sound_volume_percent = (uint8_t)volume;

    const ui_rect_t row = ui_screen_settings_row(widget, SETTINGS_BOXES_ROW, SETTINGS_ROW_WIDTH);
    const int32_t vsync_width = SETTINGS_VSYNC_WIDTH * scale;
    const int32_t hud_width = SETTINGS_HUD_WIDTH * scale;
    const int32_t hud_x = row.x + vsync_width + SETTINGS_GAP * scale;
    const int32_t effects_x = hud_x + hud_width + SETTINGS_GAP * scale;
    bool vsync = values->vsync != 0u;
    if (ui_widget_checkbox(widget, SETTINGS_ID_VSYNC, (ui_rect_t){row.x, row.y, vsync_width, row.height}, "VSync",
                           &vsync)) {
        values->vsync = vsync ? 1u : 0u;
        changed = true;
    }
    if (ui_widget_button(widget, SETTINGS_ID_HUD, (ui_rect_t){hud_x, row.y, hud_width, row.height}, "HUD...", true)) {
        screen->page = UI_SCREEN_SETTINGS_PAGE_HUD;
    }
    if (ui_widget_button(widget, SETTINGS_ID_EFFECTS,
                         (ui_rect_t){effects_x, row.y, row.x + row.width - effects_x, row.height}, "Effects...",
                         true)) {
        screen->page = UI_SCREEN_SETTINGS_PAGE_EFFECTS;
    }
    if (ui_widget_button(widget, SETTINGS_ID_DONE,
                         ui_screen_settings_row(widget, SETTINGS_MAIN_DONE_ROW, SETTINGS_ROW_WIDTH), "Done", true)) {
        *out_done = true;
    }
    return changed;
}

static bool ui_screen_settings_effects_page(ui_screen_settings_t *screen) {
    ui_widget_t *const widget = screen->borrowed_widget;
    settings_values_t *const values = screen->borrowed_values;
    const int32_t scale = (int32_t)widget->scale;
    bool changed = false;
    for (uint32_t i = 0u; i < SETTINGS_EFFECT_ON_SCREEN; i++) {
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

/* One of the HUD page's boxes, over a byte that is 0 or 1. True if it changed. */
static bool ui_screen_settings_flag(ui_widget_t *widget, uint32_t id, int32_t row, const char *label, uint8_t *flag) {
    bool on = *flag != 0u;
    if (!ui_widget_checkbox(widget, id, ui_screen_settings_row(widget, row, SETTINGS_ROW_WIDTH), label, &on)) {
        return false;
    }
    *flag = on ? 1u : 0u;
    return true;
}

/* The same over one bit of climate_hud. */
static bool ui_screen_settings_bit(ui_widget_t *widget, uint32_t id, int32_t row, const char *label, uint8_t *bits,
                                   uint8_t bit) {
    uint8_t flag = (*bits & bit) != 0u ? 1u : 0u;
    if (!ui_screen_settings_flag(widget, id, row, label, &flag)) {
        return false;
    }
    *bits = flag != 0u ? (uint8_t)(*bits | bit) : (uint8_t)(*bits & ~bit);
    return true;
}

/* Plan 004 phase 10: what the corners of the HUD show, a box a row, and Back where Done is. Plan 005 phase 7's pause when the window
 * loses the focus is the next box, as this page had the room, and plan 005 phase 10's preview of placing the last. */
static bool ui_screen_settings_hud_page(ui_screen_settings_t *screen) {
    ui_widget_t *const widget = screen->borrowed_widget;
    settings_values_t *const values = screen->borrowed_values;
    bool changed = false;
    changed |= ui_screen_settings_flag(widget, SETTINGS_ID_COORDINATES, SETTINGS_HUD_COORDINATES_ROW, "Coordinates",
                                       &values->show_coordinates);
    changed |= ui_screen_settings_flag(widget, SETTINGS_ID_DATE, SETTINGS_HUD_DATE_ROW, "Date", &values->show_date);
    changed |= ui_screen_settings_bit(widget, SETTINGS_ID_SHOW_CLIMATE, SETTINGS_HUD_CLIMATE_ROW,
                                      "Temperature and wind", &values->climate_hud, SETTINGS_CLIMATE_HUD_LINE);
    changed |= ui_screen_settings_bit(widget, SETTINGS_ID_STORM_WARNINGS, SETTINGS_HUD_WARNINGS_ROW, "Storm warnings",
                                      &values->climate_hud, SETTINGS_CLIMATE_HUD_WARNINGS);
    changed |= ui_screen_settings_flag(widget, SETTINGS_ID_PAUSE_AWAY, SETTINGS_HUD_PAUSE_AWAY_ROW, "Pause when away",
                                       &values->pause_on_focus_loss);
    changed |= ui_screen_settings_flag(widget, SETTINGS_ID_PLACE_PREVIEW, SETTINGS_HUD_PLACE_PREVIEW_ROW,
                                       "Placing preview", &values->place_preview);
    if (ui_widget_button(widget, SETTINGS_ID_DONE,
                         ui_screen_settings_row(widget, SETTINGS_DONE_ROW, SETTINGS_ROW_WIDTH), "Back", true)) {
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
    const bool hud_page = screen->page == UI_SCREEN_SETTINGS_PAGE_HUD;
    const ui_rect_t title = {0, ui_screen_settings_top(widget) + SETTINGS_TITLE_Y * scale, whole.width,
                             SETTINGS_TITLE_HEIGHT * scale};
    /* What went wrong with keeping them takes the title's place: every row below has its use. */
    const bool has_message = !effects_page && !hud_page && screen->message[0] != '\0';
    ui_widget_label_centered(
        widget, title, has_message ? screen->message : (effects_page ? "Effects" : (hud_page ? "HUD" : "Settings")),
        has_message ? SETTINGS_ERROR_COLOR : SETTINGS_TEXT_COLOR, true);
    bool done = false;
    const bool changed =
        effects_page ? ui_screen_settings_effects_page(screen)
                     : (hud_page ? ui_screen_settings_hud_page(screen) : ui_screen_settings_main_page(screen, &done));
    if (escape) {
        if (effects_page || hud_page) {
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
