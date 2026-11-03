/* See run/src/ui/hud/README.md. */
#ifndef MC_RUN_UI_HUD_INDEX_H
#define MC_RUN_UI_HUD_INDEX_H

#include "arena/index.h"
#include "item/inventory/index.h"
#include "item/stack/index.h"
#include "texture/index.h"
#include "types/index.h"
#include "ui/compose/index.h"
#include "ui/font/index.h"

#include <stdint.h>

/* In texture pixels. The hotbar texture draws eight slots, and the game has nine: see README.md. */
enum : uint32_t {
    UI_HUD_SLOT_PITCH = 20u,    /* from one slot of the hotbar to the next */
    UI_HUD_HOTBAR_WIDTH = 182u, /* nine slots, and the two columns that close the last */
    UI_HUD_HOTBAR_HEIGHT = 22u,
    UI_HUD_SELECTED_SIZE = 24u,
    UI_HUD_CROSSHAIR_SIZE = 6u,
    UI_HUD_ICON_SIZE = 9u,   /* a heart or a drumstick */
    UI_HUD_ICONS = 10u,      /* of each, a row */
    UI_HUD_ROW_ABOVE = 10u,  /* from the top of the row to the top of the hotbar */
    UI_HUD_ITEM_SIZE = 16u,  /* an item, inside its slot */
    UI_HUD_ITEM_INSET = 3u,  /* from a slot's left and top to the item */
    UI_HUD_WEAR_WIDTH = 13u, /* the bar on what is worn */
    UI_HUD_WEAR_LEFT = 2u,   /* from the item's left */
    UI_HUD_WEAR_TOP = 13u,   /* from the item's top */
};

/* What is timed, in the order the overlay lists it. The first five are the frame passes of the card, in their order. */
typedef enum ui_hud_stage_t : uint8_t {
    UI_HUD_STAGE_COARSE = 0,
    UI_HUD_STAGE_PRIMARY = 1,
    UI_HUD_STAGE_SHADE = 2,
    UI_HUD_STAGE_POST = 3,
    UI_HUD_STAGE_READBACK = 4,
    UI_HUD_STAGE_GPU_TOTAL = 5, /* on the card, with what lies between the passes */
    UI_HUD_STAGE_SIM_TICK = 6,  /* the sim thread's last tick */
    UI_HUD_STAGE_HUD = 7,       /* main: the copy of the picture and what is drawn on it */
    UI_HUD_STAGE_PRESENT = 8,   /* main: copying it to the display's buffer */
    UI_HUD_STAGE_COUNT = 9,
} ui_hud_stage_t;

/* The textures the HUD is drawn with, and an icon for every item. All live in the arena given to ui_hud_init. */
typedef struct ui_hud_t {
    ui_image_t hotbar;                /* 0 */
    ui_image_t selected;              /* 16 */
    ui_image_t crosshair;             /* 32 */
    ui_image_t health;                /* 48 */
    ui_image_t health_empty;          /* 64 */
    ui_image_t food;                  /* 80 */
    ui_image_t food_empty;            /* 96 */
    const ui_image_t *borrowed_icons; /* 112: ITEM_COUNT by item_id_t; ITEM_ID_NONE's has no pixels */
} ui_hud_t;
static_assert(sizeof(ui_hud_t) == 120);

/* All the game tells the HUD, for one frame. Plain values, so that this module knows no player and no thread. */
typedef struct ui_hud_state_t {
    item_stack_t hotbar[INVENTORY_HOTBAR_SLOTS]; /* 0 */
    uint8_t selected;                            /* 72: which of them is in hand; past the end marks none */
    uint8_t health;                              /* 73: half hearts, 0 to 20 */
    uint8_t food;                                /* 74: half drumsticks, 0 to 20 */
    uint8_t _pad0[1];                            /* 75 */
} ui_hud_state_t;
static_assert(sizeof(ui_hud_state_t) == 76);

/* All the debug overlay shows. */
typedef struct ui_hud_debug_t {
    float32_t fps;                          /* 0 */
    float32_t stage_ms[UI_HUD_STAGE_COUNT]; /* 4 */
} ui_hud_debug_t;
static_assert(sizeof(ui_hud_debug_t) == 40);

/* Where things go on a canvas of some size, in its pixels. */
typedef struct ui_hud_layout_t {
    ui_rect_t hotbar;    /* 0 */
    ui_rect_t crosshair; /* 16 */
    int32_t row_y;       /* 32: the top of the hearts and the drumsticks */
    uint32_t scale;      /* 36: screen pixels per texture pixel */
} ui_hud_layout_t;
static_assert(sizeof(ui_hud_layout_t) == 40);

/* Reads every texture the HUD is drawn with, and every item's, into memory from arena. False, having logged why, if one is missing. */
bool ui_hud_init(ui_hud_t *hud, arena_t *arena, const texture_t *texture);

/* Empties the handle. The memory stays with the arena. */
void ui_hud_deinit(ui_hud_t *hud);

/* A value out of maximum, as the halves of a row: anything above a half counts as the next half, so that a body with any health left
 * shows some. Nothing for 0 or below, and for what is no number. */
uint32_t ui_hud_half_units(float32_t value, uint32_t maximum);

/* The same scale as the widgets: ui_widget_scale_for. A canvas too small to hold it all still gets this, and the clip does the rest. */
ui_hud_layout_t ui_hud_layout(uint32_t canvas_width, uint32_t canvas_height);

/* The item's icon with its top left at (x, y), scale screen pixels to a texture pixel, with a bar if it is worn and its count if it is
 * more than one. Nothing for an empty stack. Stage 14.3's slots can use it as it is. */
void ui_hud_item_draw(const ui_hud_t *hud, ui_canvas_t *canvas, const ui_font_t *font, const item_stack_t *stack,
                      int32_t x, int32_t y, uint32_t scale);

/* The crosshair, the hotbar with what is on it, the hearts and the drumsticks. No allocation. */
void ui_hud_draw(const ui_hud_t *hud, ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_state_t *state);

/* The frames per second and each stage's time, on a dark panel at the top left. No allocation. */
void ui_hud_debug_draw(ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_debug_t *debug);

enum : uint32_t {
    UI_HUD_NOTICE_BYTES = 64u, /* with its NUL */
};

/* One line in red across the top of the canvas, of something a person playing needs to know and has no other way to learn. */
void ui_hud_notice_draw(ui_canvas_t *canvas, const ui_font_t *font, const char *text);

/* Where that panel lies on a canvas of this size, which nothing else in the overlay goes outside. */
ui_rect_t ui_hud_debug_panel(uint32_t canvas_width, uint32_t canvas_height);

#endif
