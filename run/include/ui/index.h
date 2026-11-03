/* See run/src/ui/README.md. */
#ifndef MC_RUN_UI_INDEX_H
#define MC_RUN_UI_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "texture/index.h"
#include "ui/compose/index.h"
#include "ui/font/index.h"
#include "ui/hud/index.h"
#include "ui/widget/index.h"

#include <stdint.h>

/* The main thread's. Between ui_frame_begin and ui_frame_end, canvas is where to draw, and widget, font and hud what with. */
typedef struct ui_t {
    arena_t arena_tables;          /* 0: lives as long as this does; what is read from textures */
    arena_t arena_frame;           /* 64: reset at each frame's start */
    heap_region_t owned_region;    /* 128: under both arenas */
    heap_region_t owned_composite; /* 144: a frame's size, and only once a picture has needed it */
    uint32_t *borrowed_row;        /* 160: the canvas's scratch, from arena_tables */
    ui_font_t font;                /* 168 */
    ui_canvas_t canvas;            /* 176 */
    ui_widget_t widget;            /* 240 */
    ui_hud_t hud;                  /* 576 */
    uint8_t _pad0[8];              /* 696 */
} ui_t;
static_assert(sizeof(ui_t) == 704);
static_assert(alignof(ui_t) == 64);

/* Reserves its memory and reads the font and the widgets' textures, which texture must keep for as long as this lives. On
 * failure logs why, leaves nothing behind and returns false. */
bool ui_init(ui_t *ui, const texture_t *texture);

/* Releases what init reserved. Safe on a zeroed ui_t. */
void ui_deinit(ui_t *ui);

/* Starts a frame drawing straight onto pixels, in memory that can be read quickly. False for a size ui_canvas_start refuses,
 * with no frame started. */
bool ui_frame_begin(ui_t *ui, uint8_t *pixels, uint64_t pitch, uint32_t width, uint32_t height);

/* Starts a frame over a finished picture of width * 4 bytes a row, which is not changed: the same picture may be shown
 * again, with no UI of the last frame baked into it, and blending twice would darken it. So this draws on a copy. The copy's
 * memory is reserved the first time it is needed at a size, not in every frame. False if that fails or the size is refused,
 * with no frame started. */
bool ui_frame_begin_over(ui_t *ui, const uint8_t *picture, uint32_t width, uint32_t height);

/* Ends the frame, as ui_widget_frame_end does, and returns what was drawn on, for render_frame_present. */
const uint8_t *ui_frame_end(ui_t *ui, bool show_pointer);

#endif
