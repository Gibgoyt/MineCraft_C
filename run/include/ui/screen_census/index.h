/* See run/src/ui/screen_census/README.md. */
#ifndef MC_RUN_UI_SCREEN_CENSUS_INDEX_H
#define MC_RUN_UI_SCREEN_CENSUS_INDEX_H

#include "calendar/index.h"
#include "climate/outlook/index.h"
#include "climate/weather/week/index.h"
#include "ui/widget/index.h"

#include <stddef.h>
#include <stdint.h>

/* What a frame of the screen wants of whoever runs it. */
typedef enum ui_screen_census_action_t : uint8_t {
    UI_SCREEN_CENSUS_NONE = 0,
    UI_SCREEN_CENSUS_CONTINUE = 1, /* the button, or Esc, once the census is done */
} ui_screen_census_action_t;

/* Plan 004 phase 07: the census's report, what the screen draws. Defined here and not in game, which fills it, because ui
 * may not include game. Copied as a whole from the sim to main. */
enum : uint32_t {
    UI_CENSUS_NAMES_MAX = 4u, /* hurricanes named among the events */
    UI_CENSUS_NAME_BYTES = 24u,
};

typedef enum ui_census_event_t : uint32_t {
    UI_CENSUS_EVENT_FROST = 0, /* a cell's t_min fell below 0 */
    UI_CENSUS_EVENT_HEAT_WAVE = 1,
    UI_CENSUS_EVENT_THUNDERSTORM = 2,
    UI_CENSUS_EVENT_STORM = 3,
    UI_CENSUS_EVENT_HURRICANE = 4,
    UI_CENSUS_EVENT_FLOOD = 5,      /* flood > 1 */
    UI_CENSUS_EVENT_FIRST_SNOW = 6, /* snow went from 0 to over 0 in autumn or early winter */
    UI_CENSUS_EVENT_LAKE_ICE = 7,   /* ice crossed 5 cm going up */
    UI_CENSUS_EVENT_COUNT = 8,
} ui_census_event_t;

typedef struct ui_census_report_t {
    climate_week_t here;                         /* 0: the player's cell, last week */
    climate_outlook_t outlook;                   /* 64 */
    uint32_t event_cells[UI_CENSUS_EVENT_COUNT]; /* 752: cells within render distance with each event */
    char hurricane_names[UI_CENSUS_NAMES_MAX][UI_CENSUS_NAME_BYTES]; /* 784 */
    uint64_t week;                                                   /* 880: the census week shown */
    uint32_t weeks_total;                                            /* 888: K */
    uint32_t weeks_done;                                             /* 892 */
    uint32_t tiles_total;                                            /* 896: of the week being worked */
    uint32_t tiles_done;                                             /* 900 */
    uint32_t hurricane_count;                                        /* 904 */
    uint8_t here_known;                                              /* 908: the player's cell is kept */
    uint8_t outlook_known;                                           /* 909: the outlook could be worked out */
    uint8_t written;       /* 910: the file is on disk, or there is none to write */
    uint8_t failed;        /* 911: the file could not be written: the screen says so */
    uint8_t failed_status; /* 912: a file_status_t, why */
    uint8_t tiles_full;    /* 913: CLIMATE_WEEK_TILES_MAX was reached */
    uint8_t refused;       /* 914: the saved state was refused, and spin-up used */
    uint8_t south;         /* 915: the player is south of the equator */
    uint8_t _pad0[4];      /* 916 */
} ui_census_report_t;
static_assert(sizeof(ui_census_report_t) == 920);
static_assert(alignof(ui_census_report_t) == 8);
static_assert(offsetof(ui_census_report_t, here) == 0);
static_assert(offsetof(ui_census_report_t, outlook) == 64);
static_assert(offsetof(ui_census_report_t, event_cells) == 752);
static_assert(offsetof(ui_census_report_t, hurricane_names) == 784);
static_assert(offsetof(ui_census_report_t, week) == 880);
static_assert(offsetof(ui_census_report_t, weeks_total) == 888);
static_assert(offsetof(ui_census_report_t, weeks_done) == 892);
static_assert(offsetof(ui_census_report_t, tiles_total) == 896);
static_assert(offsetof(ui_census_report_t, tiles_done) == 900);
static_assert(offsetof(ui_census_report_t, hurricane_count) == 904);
static_assert(offsetof(ui_census_report_t, here_known) == 908);
static_assert(offsetof(ui_census_report_t, outlook_known) == 909);
static_assert(offsetof(ui_census_report_t, written) == 910);
static_assert(offsetof(ui_census_report_t, failed) == 911);
static_assert(offsetof(ui_census_report_t, failed_status) == 912);
static_assert(offsetof(ui_census_report_t, tiles_full) == 913);
static_assert(offsetof(ui_census_report_t, refused) == 914);
static_assert(offsetof(ui_census_report_t, south) == 915);
static_assert(offsetof(ui_census_report_t, _pad0) == 916);

/* The census screen: main's, like all of ui_t. Holds only what it was opened with. 16 bytes, as that is all ui_t has left: the
 * date is worked out from the clock when it is drawn, the widget and the report are passed to each frame. */
typedef struct ui_screen_census_t {
    calendar_clock_t clock; /* 0: the world clock when the census stopped the world */
    uint32_t weeks;         /* 8: how many weeks it ran, 1 unless some were missed */
    uint16_t day_zero;      /* 12: D0 of the world, to make a date of the clock */
    uint8_t south;          /* 14: which half of the world the season is named for */
    uint8_t _pad0[1];       /* 15 */
} ui_screen_census_t;
static_assert(sizeof(ui_screen_census_t) == 16);

/* Opens it for a census of weeks weeks that stopped the world at clock. day_zero below CALENDAR_DAYS_PER_YEAR. */
void ui_screen_census_open(ui_screen_census_t *screen, ui_widget_t *widget, uint32_t day_zero, calendar_clock_t clock,
                           uint32_t weeks, bool south);

/* One frame, between ui_frame_begin_over and ui_frame_end, over all of the canvas. report is the census's (nullptr for a
 * world with no climate census: then only the date, and Continue at once). With a report, Continue is greyed until the file
 * is written or has failed, and Esc continues only then, as the button does. Never logs, and changes nothing but the
 * widget's state. Allocates nothing. */
ui_screen_census_action_t ui_screen_census_frame(const ui_screen_census_t *screen, ui_widget_t *widget, bool escape,
                                                 const ui_census_report_t *report);

/* The compass point a wind blows from, "N" to "NW", from its u (east) and v (north). "calm" under 0.5 blocks a second. */
const char *ui_screen_census_wind_from(float32_t u, float32_t v);

#endif
