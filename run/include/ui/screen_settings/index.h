/* See run/src/ui/screen_settings/README.md. */
#ifndef MC_RUN_UI_SCREEN_SETTINGS_INDEX_H
#define MC_RUN_UI_SCREEN_SETTINGS_INDEX_H

#include "settings/persist/index.h"
#include "ui/widget/index.h"

#include <stdint.h>

enum : uint32_t {
    UI_SCREEN_SETTINGS_MESSAGE_BYTES = 64u, /* what went wrong, with its NUL */
};

typedef enum ui_screen_settings_page_t : uint8_t {
    UI_SCREEN_SETTINGS_PAGE_MAIN = 0,
    UI_SCREEN_SETTINGS_PAGE_EFFECTS = 1,
} ui_screen_settings_page_t;

/* What a frame of the screen wants of whoever runs it. */
typedef enum ui_screen_settings_action_t : uint8_t {
    UI_SCREEN_SETTINGS_NONE = 0,
    UI_SCREEN_SETTINGS_CHANGED = 1, /* a value is different now */
    UI_SCREEN_SETTINGS_DONE = 2,    /* the person has finished, by Done or Esc */
} ui_screen_settings_action_t;

/* The main thread's, like all of ui_t. */
typedef struct ui_screen_settings_t {
    ui_widget_t *borrowed_widget;                   /* 0 */
    settings_values_t *borrowed_values;             /* 8 */
    char message[UI_SCREEN_SETTINGS_MESSAGE_BYTES]; /* 16: empty if nothing is wrong */
    uint32_t full_detail_live_max; /* 80: past this full detail takes a new world; 0 for no world open */
    uint32_t workers_running;      /* 84 */
    uint8_t page;                  /* 88: ui_screen_settings_page_t */
    uint8_t over_world;            /* 89: there is a picture under this: shade it and do not cover it */
    uint8_t _pad0[6];              /* 90 */
} ui_screen_settings_t;
static_assert(sizeof(ui_screen_settings_t) == 96);

/* Goes to the main page, with values to change, which must outlive it. over_world and full_detail_live_max as in the struct. */
void ui_screen_settings_open(ui_screen_settings_t *screen, ui_widget_t *widget, settings_values_t *values,
                             uint32_t full_detail_live_max, uint32_t workers_running, bool over_world);

/* Says on the screen what went wrong with keeping them. */
void ui_screen_settings_message_set(ui_screen_settings_t *screen, const char *message);

/* One frame, between ui_frame_begin_* and ui_frame_end, over all of the canvas. escape is that Esc was pressed, which goes back a page, and is
 * done from the main page. Never logs. Changes the values as it goes, and nothing else. */
ui_screen_settings_action_t ui_screen_settings_frame(ui_screen_settings_t *screen, bool escape);

#endif
