/* See run/src/ui/widget/README.md. */
#ifndef MC_RUN_UI_WIDGET_INDEX_H
#define MC_RUN_UI_WIDGET_INDEX_H

#include "arena/index.h"
#include "input/events/index.h"
#include "texture/index.h"
#include "ui/compose/index.h"
#include "ui/font/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    UI_WIDGET_TYPED_MAX = 32u,     /* characters kept between two frames; a person cannot type more in one */
    UI_WIDGET_TOOLTIP_MAX = 1024u, /* bytes of a tooltip that are kept, with its NUL */
    UI_WIDGET_BUTTON_HEIGHT = 20u, /* the textures' own, at scale 1 */
    UI_WIDGET_SCALE_BASE_W = 320u, /* the scale is the most that leaves at least this much room */
    UI_WIDGET_SCALE_BASE_H = 240u,
};

/* The textures widgets are drawn with. They live in the arena given to ui_widget_init. */
typedef struct ui_widget_skin_t {
    ui_image_t button;           /* 0 */
    ui_image_t button_hover;     /* 16 */
    ui_image_t button_disabled;  /* 32 */
    ui_image_t panel;            /* 48 */
    ui_image_t checkbox;         /* 64 */
    ui_image_t checkbox_checked; /* 80 */
    ui_image_t slider_track;     /* 96 */
    ui_image_t slider_knob;      /* 112 */
    ui_image_t text_field;       /* 128 */
    ui_image_t tooltip;          /* 144 */
    ui_image_t slot;             /* 160 */
    ui_image_t slot_highlight;   /* 176 */
    ui_image_t progress_empty;   /* 192 */
    ui_image_t progress_full;    /* 208 */
} ui_widget_skin_t;
static_assert(sizeof(ui_widget_skin_t) == 224);

/* What the player did since the last frame ended. ui_widget_input_feed adds to it, and ui_widget_frame_end clears the edges. */
typedef struct ui_widget_input_t {
    uint8_t typed[UI_WIDGET_TYPED_MAX]; /* 0: printable ASCII */
    int32_t pointer_x;                  /* 32: always on the canvas */
    int32_t pointer_y;                  /* 36 */
    int32_t wheel;                      /* 40: detents; away from the user is positive */
    uint32_t typed_count;               /* 44 */
    uint8_t left_down;                  /* 48 */
    uint8_t left_pressed;               /* 49 */
    uint8_t left_released;              /* 50 */
    uint8_t right_pressed;              /* 51 */
    uint8_t backspace;                  /* 52: pressed, or repeated by the kernel */
    uint8_t enter;                      /* 53 */
    uint8_t _pad0[2];                   /* 54 */
} ui_widget_input_t;
static_assert(sizeof(ui_widget_input_t) == 56);

/* What a frame of widgets needs. The caller picks each widget's id, not 0, and keeps it the same from frame to frame: that
 * is how a press in one frame and its release in a later one are known to be on the same thing. */
typedef struct ui_widget_t {
    ui_widget_skin_t skin;          /* 0 */
    ui_widget_input_t input;        /* 224 */
    const ui_font_t *borrowed_font; /* 280 */
    ui_canvas_t *borrowed_canvas;   /* 288: between ui_widget_frame_begin and ui_widget_frame_end */
    arena_t *borrowed_arena_frame;  /* 296: where the tooltip is kept; reset by the Owner, once a frame */
    const char *borrowed_tooltip;   /* 304: from that arena, or nullptr */
    uint32_t active_id;             /* 312: the widget a press landed on, until the release; 0 is none */
    uint32_t focus_id;              /* 316: the text field being typed in; 0 is none */
    uint32_t scale;                 /* 320: screen pixels per texture pixel, this frame */
    uint32_t canvas_width;          /* 324: of the last frame, to tell when the pointer must move to a new middle */
    uint32_t canvas_height;         /* 328 */
    uint8_t _pad0[4];               /* 332 */
} ui_widget_t;
static_assert(sizeof(ui_widget_t) == 336);

/* What a text field did this frame. */
typedef enum ui_field_event_t : uint8_t {
    UI_FIELD_NONE = 0,
    UI_FIELD_CHANGED = 1,   /* the text is different */
    UI_FIELD_SUBMITTED = 2, /* Enter, which also takes the focus away */
} ui_field_event_t;

/* What the pointer is doing to a slot this frame, as bits. */
enum : uint32_t {
    UI_SLOT_HOVER = 1u,
    UI_SLOT_LEFT_PRESSED = 2u,
    UI_SLOT_RIGHT_PRESSED = 4u,
};

/* Reads every texture a widget is drawn with into memory from arena. False, having logged why, if one is missing. */
bool ui_widget_init(ui_widget_t *widget, arena_t *arena, const texture_t *texture, const ui_font_t *font,
                    arena_t *arena_frame);

/* Empties the handle. The memory stays with the arenas. */
void ui_widget_deinit(ui_widget_t *widget);

/* The most screen pixels per texture pixel that leaves 320 by 240 of room. Never below 1. */
uint32_t ui_widget_scale_for(uint32_t canvas_width, uint32_t canvas_height);

/* One poll's input. Raw mouse counts move the pointer one pixel each. Keys go through a US layout, and only printable ASCII
 * is kept. Several polls may be fed to one frame. */
void ui_widget_input_feed(ui_widget_t *widget, const input_frame_t *frame);

/* Starts a frame on canvas. A canvas of a new size puts the pointer in its middle. */
void ui_widget_frame_begin(ui_widget_t *widget, ui_canvas_t *canvas);

/* Ends it: the tooltip goes on top of everything, then the pointer if asked for, and this frame's edges are forgotten. */
void ui_widget_frame_end(ui_widget_t *widget, bool show_pointer);

/* A backing for a group of widgets. */
void ui_widget_panel(ui_widget_t *widget, ui_rect_t rect);

/* Text with its top left at (x, y), at this frame's scale. shadowed is for text on anything dark. */
void ui_widget_label(ui_widget_t *widget, int32_t x, int32_t y, const char *text, uint32_t color, bool shadowed);

/* The same, in the middle of a rect. */
void ui_widget_label_centered(ui_widget_t *widget, ui_rect_t rect, const char *text, uint32_t color, bool shadowed);

/* True in the frame a press and then a release have both landed on it. A disabled button does nothing and looks it. */
bool ui_widget_button(ui_widget_t *widget, uint32_t id, ui_rect_t rect, const char *label, bool enabled);

/* A box at the left of rect and label after it. A click anywhere in rect flips *value, and then this returns true. */
bool ui_widget_checkbox(ui_widget_t *widget, uint32_t id, ui_rect_t rect, const char *label, bool *value);

/* Sets *value, from minimum to maximum, by dragging, or one step per detent of the wheel over it. label is drawn on the
 * track, so that the caller can say what the value means. True if *value is different now. */
bool ui_widget_slider(ui_widget_t *widget, uint32_t id, ui_rect_t rect, const char *label, int32_t *value,
                      int32_t minimum, int32_t maximum);

/* One line of text in buffer, capacity bytes with its NUL. A click focuses it, a press elsewhere takes the focus away, and
 * while focused it takes what is typed, at the end of the text, and Backspace. The caret is always at the end, and text too
 * wide shows its end. */
ui_field_event_t ui_widget_text_field(ui_widget_t *widget, uint32_t id, ui_rect_t rect, char *buffer,
                                      uint32_t capacity);

/* A slot, one texture pixel in from each side of rect, lit up under the pointer anywhere in rect, and the UI_SLOT_* bits.
 * Items are drawn on it by the caller. */
uint32_t ui_widget_slot(ui_widget_t *widget, ui_rect_t rect);

/* A bar filled value / maximum from the left. Past either end it is empty or full. */
void ui_widget_progress(ui_widget_t *widget, ui_rect_t rect, int32_t value, int32_t maximum);

/* Text to show by the pointer when this frame ends, over everything else. Copied, so it may be a local. The last call of a
 * frame is the one shown. */
void ui_widget_tooltip(ui_widget_t *widget, const char *text);

#endif
