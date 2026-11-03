/* See run/src/ui/README.md. */
#ifndef MC_RUN_UI_INDEX_H
#define MC_RUN_UI_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "texture/index.h"
#include "ui/compose/index.h"
#include "ui/font/index.h"
#include "ui/screen_chest/index.h"
#include "ui/screen_crafting/index.h"
#include "ui/screen_furnace/index.h"
#include "ui/screen_inventory/index.h"
#include "ui/widget/index.h"

#include <stdint.h>

/* The main thread's. Between ui_frame_begin and ui_frame_end, canvas is where to draw, and widget and font what with. */
typedef struct ui_t {
    arena_t arena_tables;           /* 0: lives as long as this does; what is read from textures */
    arena_t arena_frame;            /* 64: reset at each frame's start */
    heap_region_t owned_region;     /* 128: under both arenas */
    heap_region_t owned_composite;  /* 144: a frame's size, and only once a picture has needed it */
    uint32_t *borrowed_row;         /* 160: the canvas's scratch, from arena_tables */
    ui_font_t font;                 /* 168 */
    ui_canvas_t canvas;             /* 176 */
    ui_widget_t widget;             /* 240 */
    ui_screens_t *borrowed_screens; /* 576: from arena_tables: what the game screens share */
    uint8_t _pad0[56];              /* 584 */
} ui_t;
static_assert(sizeof(ui_t) == 640);
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

/* ---- The game screens: see README.md for who calls what, on which thread ---- */

/* What the sim tells the screens about the moment: where things go, and whether a screen may come up. */
typedef struct ui_screen_sim_t {
    item_t *borrowed_item;  /* 0 */
    item_spill_t *spill;    /* 8: what falls out of the pointer or a full inventory; given a stack and cell */
    void *borrowed_context; /* 16: passed to spill */
    int32_t cell[3];        /* 24: where the player stands */
    uint8_t may_open;       /* 36: no body, no screen */
    uint8_t _pad0[3];       /* 37 */
} ui_screen_sim_t;
static_assert(sizeof(ui_screen_sim_t) == 40);

/* Nothing up, for a world opening. Before the threads start. */
void ui_screen_world_begin(ui_t *ui);

/* Main: one poll's input. E opens the inventory and E or Esc closes what is up; a click on a screen goes to the sim. True if a screen is up,
 * which then has this input, and nothing else should act on it. width and height are the picture's. */
bool ui_screen_input(ui_t *ui, const input_frame_t *frame, uint32_t width, uint32_t height);

/* Main: picture, with what is up drawn on a copy and returned, or picture itself if nothing is. */
const uint8_t *ui_screen_over(ui_t *ui, const uint8_t *picture, uint32_t width, uint32_t height);

/* Which screen a block opens. UI_SCREEN_NONE for almost all of them. */
ui_screen_kind_t ui_screen_kind_of_block(uint16_t block);

/* The sim: what is up now, as the sim knows. */
ui_screen_kind_t ui_screen_sim_kind(const ui_t *ui);

/* The sim: the cell of the block whose screen is up. */
const int32_t *ui_screen_sim_cell(const ui_t *ui);

/* The sim: a block's screen comes up. False, with nothing changed, if another one is up or the world has no room for this one's slots. */
bool ui_screen_sim_open(ui_t *ui, item_t *item, ui_screen_kind_t kind, const int32_t cell[3]);

/* The sim: what is up goes. What was in a grid and on the pointer goes back to the inventory, and what has no room falls out. */
void ui_screen_sim_close(ui_t *ui, const ui_screen_sim_t *sim);

/* The sim: does what main has asked for, and tells main what it now looks like. With tick, tells it anyway if a screen is up, since a
 * furnace burns and items are picked up with no one asking. Never waits, and does nothing much if there is nothing to do. */
void ui_screen_sim_step(ui_t *ui, const ui_screen_sim_t *sim, bool tick);

#endif
