/* See README.md. */
#include "ui/screen_census/index.h"

#include "assert/index.h"
#include "climate/systems/index.h"
#include "file/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* In texture pixels, multiplied by the frame's scale: the same 320 by 240 as run/src/ui/screen_settings/, so it fits at 640
 * by 480 at scale 2 and at scale 1 in the smallest window. */
enum : int32_t {
    CENSUS_SCREEN_WIDTH = 320,
    CENSUS_SCREEN_HEIGHT = 240,
    CENSUS_ROW_WIDTH = 200,
    CENSUS_ROW_STEP = 24,
    CENSUS_FIRST_ROW_Y = 24,
    CENSUS_TITLE_Y = 8,
    CENSUS_TITLE_HEIGHT = 9,
    CENSUS_CONTINUE_ROW = 8,
    CENSUS_MARGIN = 6,        /* from the screen's left edge to the text */
    CENSUS_LINE = 9,          /* UI_FONT_LINE */
    CENSUS_COLUMN = 156,      /* the second column of events and systems */
    CENSUS_TABLE_LABEL = 52,  /* the outlook's row names: "Thunder", 7 letters, and a space */
    CENSUS_TABLE_COLUMN = 38, /* each day's column: "28/-12" and two units */
    CENSUS_BAR_HEIGHT = 6,
    CENSUS_BUTTON_Y = 212,
    /* The report's rows, from the screen's top. */
    CENSUS_Y_TITLE = 3,
    CENSUS_Y_HERE = 14,
    CENSUS_Y_EVENTS = 43,
    CENSUS_Y_OUTLOOK = 99,
    CENSUS_Y_SYSTEMS = 155,
    CENSUS_Y_STATUS = 194,
};

enum : uint32_t {
    CENSUS_ID_CONTINUE = 200u,
    CENSUS_TEXT_COLOR = 0xFFFFFFFFu,
    CENSUS_HEAD_COLOR = 0xFFFFE080u, /* section names */
    CENSUS_DIM_COLOR = 0xFFB0B0B0u,
    CENSUS_WARN_COLOR = 0xFFFF8060u,
    CENSUS_SHADE = 0x90000000u,    /* the world, stopped, behind it */
    CENSUS_BAR_BACK = 0xC0303030u, /* the progress bar's empty part */
    CENSUS_LABEL_BYTES = 96u,
    CENSUS_ROWS_EVENTS = 4u,
    CENSUS_ROWS_SYSTEMS = 4u,
};

static const char *const CENSUS_EVENT_NAMES[UI_CENSUS_EVENT_COUNT] = {
    "Frost", "Heat waves", "Thunder", "Storms", "Hurricanes", "Floods", "First snow", "Ice on lakes",
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

const char *ui_screen_census_wind_from(float32_t u, float32_t v) {
    static const char *const POINTS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    if (sqrtf(u * u + v * v) < 0.5f32) {
        return "calm";
    }
    /* From: the opposite of where it blows, measured clockwise from north. */
    const float64_t degrees = atan2(-(float64_t)u, -(float64_t)v) * (180.0f64 / 3.14159265358979323846f64);
    const float64_t turned = degrees < 0.0f64 ? degrees + 360.0f64 : degrees;
    return POINTS[(uint32_t)((turned + 22.5f64) / 45.0f64) % 8u];
}

static ui_rect_t ui_screen_census_row(const ui_widget_t *widget, int32_t row, int32_t width) {
    const int32_t scale = (int32_t)widget->scale;
    const int32_t top = ((int32_t)widget->canvas_height - CENSUS_SCREEN_HEIGHT * scale) / 2;
    return (ui_rect_t){((int32_t)widget->canvas_width - width * scale) / 2,
                       top + (CENSUS_FIRST_ROW_Y + row * CENSUS_ROW_STEP) * scale, width * scale,
                       (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale};
}

/* Text at (x, y) in the screen's own units. */
static void ui_screen_census_text(ui_widget_t *widget, int32_t x, int32_t y, const char *text, uint32_t color) {
    const int32_t scale = (int32_t)widget->scale;
    const int32_t left = ((int32_t)widget->canvas_width - CENSUS_SCREEN_WIDTH * scale) / 2;
    const int32_t top = ((int32_t)widget->canvas_height - CENSUS_SCREEN_HEIGHT * scale) / 2;
    ui_widget_label(widget, left + x * scale, top + y * scale, text, color, true);
}

/* Phase 01's frame, for a world with no climate census. */
static bool ui_screen_census_dated(const ui_screen_census_t *screen, ui_widget_t *widget) {
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
    return ui_widget_button(widget, CENSUS_ID_CONTINUE,
                            ui_screen_census_row(widget, CENSUS_CONTINUE_ROW, CENSUS_ROW_WIDTH), "Continue", true);
}

static uint32_t ui_screen_census_percent(float32_t chance) {
    const float32_t p = chance < 0.0f32 ? 0.0f32 : (chance > 1.0f32 ? 1.0f32 : chance);
    return (uint32_t)(p * 100.0f32 + 0.5f32);
}

static void ui_screen_census_here(ui_widget_t *widget, const ui_census_report_t *report) {
    char label[CENSUS_LABEL_BYTES];
    ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_HERE, "Where you are, last week", CENSUS_HEAD_COLOR);
    if (report->here_known == 0u) {
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_HERE + CENSUS_LINE, "Not worked out yet",
                              CENSUS_DIM_COLOR);
        return;
    }
    const climate_week_t *const here = &report->here;
    bool snowy = false;
    for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
        snowy = snowy || here->day_kind[i] == CLIMATE_DAY_SNOW || here->day_kind[i] == CLIMATE_DAY_SLEET;
    }
    (void)snprintf(label, sizeof(label), "High %.1f C  Low %.1f C  %s %.0f mm", (libc_double_t)here->t_max,
                   (libc_double_t)here->t_min, snowy ? "Rain or snow" : "Rain", (libc_double_t)here->rain_mm);
    ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_HERE + CENSUS_LINE, label, CENSUS_TEXT_COLOR);
    const float32_t speed = sqrtf(here->wind[0] * here->wind[0] + here->wind[1] * here->wind[1]);
    (void)snprintf(label, sizeof(label), "Snow on the ground %.0f mm  Wind %s %.1f blocks/s",
                   (libc_double_t)here->snow_mm, ui_screen_census_wind_from(here->wind[0], here->wind[1]),
                   (libc_double_t)speed);
    ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_HERE + 2 * CENSUS_LINE, label, CENSUS_TEXT_COLOR);
}

static void ui_screen_census_events(ui_widget_t *widget, const ui_census_report_t *report) {
    char label[CENSUS_LABEL_BYTES];
    ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_EVENTS, "Events within render distance", CENSUS_HEAD_COLOR);
    uint32_t shown = 0u;
    for (uint32_t e = 0u; e < UI_CENSUS_EVENT_COUNT; e++) {
        if (report->event_cells[e] == 0u) {
            continue;
        }
        const bool one_line = e == UI_CENSUS_EVENT_FIRST_SNOW || e == UI_CENSUS_EVENT_LAKE_ICE;
        if (one_line) {
            (void)snprintf(label, sizeof(label), "%s", CENSUS_EVENT_NAMES[e]);
        } else {
            (void)snprintf(label, sizeof(label), "%s in %" PRIu32 " places", CENSUS_EVENT_NAMES[e],
                           report->event_cells[e]);
        }
        const int32_t x = CENSUS_MARGIN + (int32_t)(shown / CENSUS_ROWS_EVENTS) * CENSUS_COLUMN;
        const int32_t y = CENSUS_Y_EVENTS + CENSUS_LINE * (int32_t)(1u + shown % CENSUS_ROWS_EVENTS);
        ui_screen_census_text(widget, x, y, label, CENSUS_TEXT_COLOR);
        shown++;
    }
    if (shown == 0u) {
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_EVENTS + CENSUS_LINE, "A quiet week", CENSUS_DIM_COLOR);
    }
    if (report->hurricane_count > 0u) {
        int32_t used =
            snprintf(label, sizeof(label), "%s", report->hurricane_count > 1u ? "Hurricanes:" : "Hurricane:");
        for (uint32_t i = 0u;
             i < report->hurricane_count && i < UI_CENSUS_NAMES_MAX && used > 0 && used < (int32_t)sizeof(label); i++) {
            const char *name = report->hurricane_names[i];
            const char *space = strrchr(name, ' ');
            used += snprintf(label + used, sizeof(label) - (uint64_t)used, "%s %s", i == 0u ? "" : ",",
                             space != nullptr ? space + 1 : name);
        }
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_EVENTS + 5 * CENSUS_LINE, label, CENSUS_TEXT_COLOR);
    }
}

static void ui_screen_census_outlook(ui_widget_t *widget, const ui_census_report_t *report) {
    char label[CENSUS_LABEL_BYTES];
    const climate_outlook_t *const outlook = &report->outlook;
    if (report->outlook_known == 0u) {
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_OUTLOOK, "Outlook: not worked out yet",
                              CENSUS_HEAD_COLOR);
        return;
    }
    (void)snprintf(label, sizeof(label), "Outlook, chances in %% (highs and lows +-%.1f C)",
                   (libc_double_t)outlook->days[0].spread_c);
    ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_OUTLOOK, label, CENSUS_HEAD_COLOR);
    static const char *const ROWS[5] = {"Day", "Rain", "Thunder", "Storm", "Hi/lo C"};
    for (uint32_t row = 0u; row < 5u; row++) {
        const int32_t y = CENSUS_Y_OUTLOOK + CENSUS_LINE * (int32_t)(row + 1u);
        ui_screen_census_text(widget, CENSUS_MARGIN, y, ROWS[row], row == 0u ? CENSUS_DIM_COLOR : CENSUS_TEXT_COLOR);
        for (uint32_t day = 0u; day < CLIMATE_OUTLOOK_DAYS; day++) {
            const climate_outlook_day_t *const d = &outlook->days[day];
            switch (row) {
            case 0u:
                (void)snprintf(label, sizeof(label), "%" PRIu32, day + 1u);
                break;
            case 1u:
                (void)snprintf(label, sizeof(label), "%" PRIu32, ui_screen_census_percent(d->rain));
                break;
            case 2u:
                (void)snprintf(label, sizeof(label), "%" PRIu32, ui_screen_census_percent(d->thunder));
                break;
            case 3u:
                (void)snprintf(label, sizeof(label), "%" PRIu32, ui_screen_census_percent(d->storm));
                break;
            default:
                (void)snprintf(label, sizeof(label), "%.0f/%.0f", (libc_double_t)d->high_c, (libc_double_t)d->low_c);
                break;
            }
            ui_screen_census_text(widget, CENSUS_TABLE_LABEL + (int32_t)day * CENSUS_TABLE_COLUMN, y, label,
                                  row == 0u ? CENSUS_DIM_COLOR : CENSUS_TEXT_COLOR);
        }
    }
    uint32_t shown = 0u;
    for (uint32_t i = 0u; i < outlook->system_count && shown < CENSUS_ROWS_SYSTEMS * 2u; i++) {
        const climate_outlook_system_t *const system = &outlook->systems[i];
        uint32_t best = 0u;
        for (uint32_t day = 1u; day < CLIMATE_OUTLOOK_DAYS; day++) {
            best = system->chance[day] > system->chance[best] ? day : best;
        }
        if (ui_screen_census_percent(system->chance[best]) == 0u) {
            continue; /* named, but under half a percent on every day: nothing to prepare for */
        }
        /* Half the screen each: "Tropical storm" shortened so the longest name, chance and day fit. */
        const char *const name = strncmp(system->name, "Tropical storm ", 15u) == 0 ? system->name + 15 : system->name;
        (void)snprintf(label, sizeof(label), "%s%.23s %" PRIu32 "%% d%" PRIu32,
                       name != system->name ? "Trop. storm " : "", name, ui_screen_census_percent(system->chance[best]),
                       best + 1u);
        const int32_t x = CENSUS_MARGIN + (int32_t)(shown / CENSUS_ROWS_SYSTEMS) * CENSUS_COLUMN;
        const int32_t y = CENSUS_Y_SYSTEMS + CENSUS_LINE * (int32_t)(shown % CENSUS_ROWS_SYSTEMS);
        ui_screen_census_text(widget, x, y, label,
                              system->kind == CLIMATE_SYSTEM_HURRICANE ? CENSUS_WARN_COLOR : CENSUS_TEXT_COLOR);
        shown++;
    }
}

static const char *ui_screen_census_status_name(uint8_t status) {
    return file_status_name((file_status_t)status);
}

ui_screen_census_action_t ui_screen_census_frame(const ui_screen_census_t *screen, ui_widget_t *widget, bool escape,
                                                 const ui_census_report_t *report) {
    const int32_t scale = (int32_t)widget->scale;
    const ui_rect_t whole = {0, 0, (int32_t)widget->canvas_width, (int32_t)widget->canvas_height};
    ui_compose_fill(widget->borrowed_canvas, whole, CENSUS_SHADE);
    const int32_t top = ((int32_t)widget->canvas_height - CENSUS_SCREEN_HEIGHT * scale) / 2;
    if (report == nullptr) {
        const ui_rect_t title = {0, top + CENSUS_TITLE_Y * scale, whole.width, CENSUS_TITLE_HEIGHT * scale};
        ui_widget_label_centered(widget, title, "Census", CENSUS_TEXT_COLOR, true);
        const bool pressed = ui_screen_census_dated(screen, widget);
        return pressed || escape ? UI_SCREEN_CENSUS_CONTINUE : UI_SCREEN_CENSUS_NONE;
    }
    char label[CENSUS_LABEL_BYTES];
    const calendar_date_t date = calendar_date_of(screen->day_zero, screen->clock);
    (void)snprintf(label, sizeof(label), "Census - Week %" PRIu64 " - Year %" PRIu32 " - Day %u", date.week, date.year,
                   (uint32_t)date.day_of_year + 1u);
    const ui_rect_t title = {0, top + CENSUS_Y_TITLE * scale, whole.width, CENSUS_TITLE_HEIGHT * scale};
    ui_widget_label_centered(widget, title, label, CENSUS_TEXT_COLOR, true);
    const bool done = report->written != 0u || report->failed != 0u;
    if (done) {
        ui_screen_census_here(widget, report);
        ui_screen_census_events(widget, report);
        ui_screen_census_outlook(widget, report);
    }
    if (!done) {
        (void)snprintf(label, sizeof(label), "Week %" PRIu32 " of %" PRIu32 " - %" PRIu32 " of %" PRIu32 " tiles",
                       report->weeks_done + 1u > report->weeks_total ? report->weeks_total : report->weeks_done + 1u,
                       report->weeks_total, report->tiles_done, report->tiles_total);
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_STATUS - CENSUS_LINE - 4, "Working out the weather...",
                              CENSUS_HEAD_COLOR);
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_STATUS, label, CENSUS_TEXT_COLOR);
        const int32_t left = ((int32_t)widget->canvas_width - CENSUS_SCREEN_WIDTH * scale) / 2;
        const ui_rect_t bar = {left + CENSUS_MARGIN * scale, top + (CENSUS_Y_STATUS + CENSUS_LINE + 1) * scale,
                               (CENSUS_SCREEN_WIDTH - 2 * CENSUS_MARGIN) * scale, CENSUS_BAR_HEIGHT * scale};
        const uint32_t tiles = report->tiles_total > 0u ? report->tiles_total : 1u;
        const int32_t maximum = (int32_t)(report->weeks_total > 0u ? report->weeks_total : 1u) * (int32_t)tiles;
        const int32_t value = (int32_t)(report->weeks_done * tiles + report->tiles_done);
        /* A bar of its own, the whole width: the widget's progress arrow is a furnace's, and too small to read here. */
        ui_compose_fill(widget->borrowed_canvas, bar, CENSUS_BAR_BACK);
        const int32_t filled = (int32_t)((int64_t)bar.width * (value > maximum ? maximum : value) / maximum);
        ui_compose_fill(widget->borrowed_canvas, (ui_rect_t){bar.x, bar.y, filled, bar.height}, CENSUS_HEAD_COLOR);
    } else if (report->failed != 0u) {
        (void)snprintf(label, sizeof(label), "The weekly state could not be saved: %s",
                       ui_screen_census_status_name(report->failed_status));
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_STATUS, label, CENSUS_WARN_COLOR);
    } else if (report->tiles_full != 0u) {
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_STATUS,
                              "The world is too big: new land has no weather kept", CENSUS_WARN_COLOR);
    } else if (report->refused != 0u) {
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_STATUS, "The saved weather was refused, and started anew",
                              CENSUS_WARN_COLOR);
    } else if (report->weeks_total > 1u) {
        (void)snprintf(label, sizeof(label), "%" PRIu32 " weeks were worked out", report->weeks_total);
        ui_screen_census_text(widget, CENSUS_MARGIN, CENSUS_Y_STATUS, label, CENSUS_DIM_COLOR);
    }
    const int32_t left = ((int32_t)widget->canvas_width - CENSUS_ROW_WIDTH * scale) / 2;
    const ui_rect_t button = {left, top + CENSUS_BUTTON_Y * scale, CENSUS_ROW_WIDTH * scale,
                              (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale};
    const bool pressed = ui_widget_button(widget, CENSUS_ID_CONTINUE, button, "Continue", done);
    return done && (pressed || escape) ? UI_SCREEN_CENSUS_CONTINUE : UI_SCREEN_CENSUS_NONE;
}
