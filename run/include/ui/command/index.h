/* See run/src/ui/command/README.md. */
#ifndef MC_RUN_UI_COMMAND_INDEX_H
#define MC_RUN_UI_COMMAND_INDEX_H

#include "ui/compose/index.h"
#include "ui/font/index.h"
#include "ui/widget/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 006 phase 01: the command box's look, and the F3 tuning lines' panel. Holds no game state: what to show comes in a view. */

enum : uint32_t {
    UI_COMMAND_FIELD_ID = 0x00434D44u, /* the box's widget id, "CMD" */
    UI_COMMAND_LINES_MAX = 16u,        /* lines over the box, or in a panel */
};

/* What the box shows, from the bottom up: the text being typed, a red line if the last command was bad, a line of what the text
 * means or what the last command said, and lines over all of that (/help's). Empty strings and nullptrs are not drawn. */
typedef struct ui_command_view_t {
    char *borrowed_text;               /* 0: the widget's field draws it, and leaves it as it is */
    const char *borrowed_error;        /* 8: in red */
    const char *borrowed_line;         /* 16: the preview, or what a command said */
    const char *const *borrowed_lines; /* 24: line_count of them, the first at the top */
    uint32_t line_count;               /* 32 */
    uint8_t _pad0[4];                  /* 36 */
} ui_command_view_t;
static_assert(sizeof(ui_command_view_t) == 40);

/* What was typed since the last call, into buffer (capacity bytes with its NUL) at its end: printable ASCII, Backspace off the end,
 * and Enter. Takes those from the widget's input, so nothing else sees them. Main, after ui_widget_input_feed. */
ui_field_event_t ui_command_keys(ui_widget_t *widget, char *buffer, uint32_t capacity);

/* The box across the bottom of the canvas, focused, with the view's lines over it. Between ui_frame_begin* and ui_frame_end. */
void ui_command_draw(ui_widget_t *widget, ui_canvas_t *canvas, const ui_font_t *font, const ui_command_view_t *view);

/* count lines of text on a dark panel with its top left at (x, y), at the F3 overlay's scale, as wide as the longest. Returns the
 * panel. No allocation. */
ui_rect_t ui_command_lines_draw(ui_canvas_t *canvas, const ui_font_t *font, int32_t x, int32_t y,
                                const char *const *lines, uint32_t count);

#endif
