/* See run/src/ui/screen_census/README.md. */
#ifndef MC_RUN_UI_SCREEN_CENSUS_INDEX_H
#define MC_RUN_UI_SCREEN_CENSUS_INDEX_H

#include "calendar/index.h"
#include "ui/widget/index.h"

#include <stdint.h>

/* What a frame of the screen wants of whoever runs it. */
typedef enum ui_screen_census_action_t : uint8_t {
    UI_SCREEN_CENSUS_NONE = 0,
    UI_SCREEN_CENSUS_CONTINUE = 1, /* the button, or Esc */
} ui_screen_census_action_t;

/* The census screen: main's, like all of ui_t. Holds only what it was opened with. 16 bytes, as that is all ui_t has left: the
 * date is worked out from the clock when it is drawn, and the widget is passed to each frame. */
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

/* One frame, between ui_frame_begin_over and ui_frame_end, over all of the canvas. escape is this frame's Esc press, which
 * continues as the button does, so the world can never be left stuck. Never logs, and changes nothing but the widget's state. */
ui_screen_census_action_t ui_screen_census_frame(const ui_screen_census_t *screen, ui_widget_t *widget, bool escape);

#endif
