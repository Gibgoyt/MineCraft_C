/* See README.md. */
#include "ui/screen_census/index.h"

#include "assert/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* In texture pixels, multiplied by the frame's scale: the same 320 by 240 as run/src/ui/screen_settings/, so its rows line up. */
enum : int32_t {
    CENSUS_SCREEN_HEIGHT = 240,
    CENSUS_ROW_WIDTH = 200,
    CENSUS_ROW_STEP = 24,
    CENSUS_FIRST_ROW_Y = 24,
    CENSUS_TITLE_Y = 8,
    CENSUS_TITLE_HEIGHT = 9,
    CENSUS_CONTINUE_ROW = 8,
};

enum : uint32_t {
    CENSUS_ID_CONTINUE = 200u,
    CENSUS_TEXT_COLOR = 0xFFFFFFFFu,
    CENSUS_SHADE = 0x90000000u, /* the world, stopped, behind it */
    CENSUS_LABEL_BYTES = 80u,
};

void ui_screen_census_open(ui_screen_census_t *screen, ui_widget_t *widget, uint32_t day_zero, calendar_clock_t clock,
                           uint32_t weeks, bool south) {
    ASSERT(day_zero < CALENDAR_DAYS_PER_YEAR);
    memset(screen, 0, sizeof(*screen));
    screen->clock = clock;
    screen->weeks = weeks;
    screen->day_zero = (uint16_t)day_zero;
    screen->south = south ? 1u : 0u;
    widget->focus_id = 0u;
}

static ui_rect_t ui_screen_census_row(const ui_widget_t *widget, int32_t row, int32_t width) {
    const int32_t scale = (int32_t)widget->scale;
    const int32_t top = ((int32_t)widget->canvas_height - CENSUS_SCREEN_HEIGHT * scale) / 2;
    return (ui_rect_t){((int32_t)widget->canvas_width - width * scale) / 2,
                       top + (CENSUS_FIRST_ROW_Y + row * CENSUS_ROW_STEP) * scale, width * scale,
                       (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale};
}

ui_screen_census_action_t ui_screen_census_frame(const ui_screen_census_t *screen, ui_widget_t *widget, bool escape) {
    const int32_t scale = (int32_t)widget->scale;
    const ui_rect_t whole = {0, 0, (int32_t)widget->canvas_width, (int32_t)widget->canvas_height};
    ui_compose_fill(widget->borrowed_canvas, whole, CENSUS_SHADE);
    const int32_t top = ((int32_t)widget->canvas_height - CENSUS_SCREEN_HEIGHT * scale) / 2;
    const ui_rect_t title = {0, top + CENSUS_TITLE_Y * scale, whole.width, CENSUS_TITLE_HEIGHT * scale};
    ui_widget_label_centered(widget, title, "Census", CENSUS_TEXT_COLOR, true);

    const calendar_date_t date = calendar_date_of(screen->day_zero, screen->clock);
    char label[CENSUS_LABEL_BYTES];
    (void)snprintf(label, sizeof(label), "Week %" PRIu64, date.week);
    ui_widget_label_centered(widget, ui_screen_census_row(widget, 1, CENSUS_ROW_WIDTH), label, CENSUS_TEXT_COLOR, true);

    char line[CALENDAR_LINE_BYTES];
    const uint32_t length =
        calendar_date_line(&date, calendar_season_temperate(date.day_of_year, screen->south != 0u), line, sizeof(line));
    (void)snprintf(label, sizeof(label), "%.*s, %02u:%02u", (int32_t)length, line, (uint32_t)date.hour,
                   (uint32_t)date.minute);
    ui_widget_label_centered(widget, ui_screen_census_row(widget, 2, CENSUS_ROW_WIDTH), label, CENSUS_TEXT_COLOR, true);

    if (screen->weeks > 1u) {
        (void)snprintf(label, sizeof(label), "%" PRIu32 " weeks were worked out", screen->weeks);
        ui_widget_label_centered(widget, ui_screen_census_row(widget, 3, CENSUS_ROW_WIDTH), label, CENSUS_TEXT_COLOR,
                                 true);
    }
    const bool pressed =
        ui_widget_button(widget, CENSUS_ID_CONTINUE,
                         ui_screen_census_row(widget, CENSUS_CONTINUE_ROW, CENSUS_ROW_WIDTH), "Continue", true);
    return pressed || escape ? UI_SCREEN_CENSUS_CONTINUE : UI_SCREEN_CENSUS_NONE;
}
