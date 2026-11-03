/* See run/src/ui/screen_inventory/README.md. */
#ifndef MC_RUN_UI_SCREEN_INVENTORY_INDEX_H
#define MC_RUN_UI_SCREEN_INVENTORY_INDEX_H

#include "arena/index.h"
#include "item/index.h"
#include "render/frame/index.h"
#include "texture/index.h"
#include "ui/compose/index.h"
#include "ui/widget/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* A window's size and a slot's, in texture pixels. Every screen has the same window, with the player's rows in the same place. */
    UI_SCREEN_WINDOW_WIDTH = 176u,
    UI_SCREEN_WINDOW_HEIGHT = 166u,
    UI_SCREEN_SLOT = 18u,
    UI_SCREEN_ICON = 16u,
    UI_SCREEN_RING = 64u, /* clicks on their way to the sim; a power of two, and more than a person makes in a tick */
    UI_SCREEN_PLACES_MAX = 64u,
    UI_SCREEN_ROWS_X = 8u,
    UI_SCREEN_ROWS_Y = 84u,
    UI_SCREEN_HOTBAR_Y = 142u,
    UI_SCREEN_ARROW_WIDTH = 22u,
    UI_SCREEN_ARROW_HEIGHT = 15u,
};
static_assert((UI_SCREEN_RING & (UI_SCREEN_RING - 1u)) == 0u);

/* Which screen is up. */
typedef enum ui_screen_kind_t : uint8_t {
    UI_SCREEN_NONE = 0,
    UI_SCREEN_INVENTORY = 1,
    UI_SCREEN_CRAFTING = 2,
    UI_SCREEN_FURNACE = 3,
    UI_SCREEN_CHEST = 4,
} ui_screen_kind_t;

/* What part of a screen a point or a click is on. */
typedef enum ui_screen_zone_t : uint8_t {
    UI_SCREEN_ZONE_WINDOW = 0,  /* the window, on no slot: a click does nothing */
    UI_SCREEN_ZONE_OUTSIDE = 1, /* outside it: a click throws out what the pointer carries */
    UI_SCREEN_ZONE_PLAYER = 2,  /* an inventory slot */
    UI_SCREEN_ZONE_ARMOR = 3,   /* what is worn */
    UI_SCREEN_ZONE_GRID = 4,    /* a crafting cell */
    UI_SCREEN_ZONE_OUTPUT = 5,  /* what a grid makes */
    UI_SCREEN_ZONE_BLOCK = 6,   /* a slot of the chest or furnace that is open */
} ui_screen_zone_t;

typedef enum ui_screen_op_t : uint8_t {
    UI_SCREEN_OP_OPEN_INVENTORY = 0,
    UI_SCREEN_OP_CLOSE = 1,
    UI_SCREEN_OP_CLICK = 2,
} ui_screen_op_t;

enum : uint8_t {
    UI_SCREEN_FLAG_SECONDARY = 1u, /* the right button */
    UI_SCREEN_FLAG_SHIFT = 2u,
};

/* What main asks of the sim, through the ring. */
typedef struct ui_screen_action_t {
    uint8_t op;    /* 0: ui_screen_op_t */
    uint8_t zone;  /* 1: ui_screen_zone_t */
    uint8_t slot;  /* 2 */
    uint8_t flags; /* 3: UI_SCREEN_FLAG_* */
} ui_screen_action_t;
static_assert(sizeof(ui_screen_action_t) == 4);

/* One slot of a screen, in texture pixels from the window's top left. */
typedef struct ui_screen_place_t {
    int16_t x;        /* 0 */
    int16_t y;        /* 2 */
    uint8_t zone;     /* 4: ui_screen_zone_t */
    uint8_t slot;     /* 5 */
    uint8_t _pad0[2]; /* 6 */
} ui_screen_place_t;
static_assert(sizeof(ui_screen_place_t) == 8);

/* What main draws from: all of it the sim's, copied. So main never reads what the sim is writing, and draws a whole moment. */
typedef struct ui_screen_view_t {
    item_stack_t player[INVENTORY_SLOTS];      /* 0 */
    item_stack_t armor[EQUIPMENT_ARMOR_SLOTS]; /* 288 */
    item_stack_t grid[RECIPE_CELLS];           /* 320 */
    item_stack_t output;                       /* 392 */
    item_stack_t block[CONTAINER_CHEST_SLOTS]; /* 400: a chest's slots, or a furnace's first three */
    item_stack_t cursor;                       /* 616: what the pointer carries */
    float32_t cook;                            /* 624: a furnace's two gauges, from 0 to 1 */
    float32_t burn;                            /* 628 */
    uint8_t kind;                              /* 632: ui_screen_kind_t */
    uint8_t _pad0[7];                          /* 633 */
} ui_screen_view_t;
static_assert(sizeof(ui_screen_view_t) == 640);
static_assert(offsetof(ui_screen_view_t, block) == 400);
static_assert(offsetof(ui_screen_view_t, kind) == 632);

/* What the screens share: the road between the two threads, the sim's side of what is open, and the pictures. One, from the ui's table
 * arena, for as long as the ui_t. Each thread's fields are on lines of their own. */
typedef struct ui_screens_t {
    render_exchange_t views;                            /* 0: sim to main, over view_slots */
    ui_screen_view_t view_slots[RENDER_EXCHANGE_SLOTS]; /* 192 */
    /* Main's. ring_head is released to the sim: it hands over what was written to the ring. */
    _Atomic uint32_t ring_head;             /* 2112 */
    uint8_t _pad0[4];                       /* 2116 */
    const ui_screen_view_t *borrowed_shown; /* 2120: from view_slots: what main draws and clicks on */
    uint64_t sent_count;                    /* 2128 */
    uint64_t lost_count;                    /* 2136: found the ring full, and were dropped */
    uint8_t _pad1[32];                      /* 2144 */
    /* The sim's. ring_tail is released to main: it hands back the ring's room. */
    _Atomic uint32_t ring_tail; /* 2176 */
    int32_t cell[3];            /* 2180: the block whose screen is up */
    item_stack_t cursor;        /* 2192: what the pointer carries, which no store does */
    uint64_t opened_count;      /* 2200 */
    uint64_t click_count;       /* 2208 */
    uint64_t published_count;   /* 2216 */
    uint8_t kind;               /* 2224: ui_screen_kind_t */
    uint8_t _pad2[15];          /* 2225 */
    /* Written by main and read by the sim, with the two numbers above saying whose each entry is. */
    ui_screen_action_t ring[UI_SCREEN_RING]; /* 2240 */
    /* Set once, before any thread. */
    const ui_image_t *borrowed_icons;              /* 2496: ITEM_COUNT, by item id; the first frame of each texture */
    ui_image_t armor_empty[EQUIPMENT_ARMOR_SLOTS]; /* 2504 */
    ui_image_t arrow;                              /* 2568 */
    ui_image_t arrow_dim;                          /* 2584: the same, faint: the part of a gauge still to fill */
    ui_image_t fire_empty;                         /* 2600 */
    ui_image_t fire_full;                          /* 2616 */
    uint8_t _pad3[56];                             /* 2632 */
} ui_screens_t;
static_assert(sizeof(ui_screens_t) == 2688);
static_assert(alignof(ui_screens_t) == 64);
static_assert(offsetof(ui_screens_t, ring_head) == 2112);
static_assert(offsetof(ui_screens_t, ring_tail) == 2176);
static_assert(offsetof(ui_screens_t, ring) == 2240);
static_assert(offsetof(ui_screens_t, borrowed_icons) == 2496);

/* Reads every item's picture and the screens' own, from arena. nullptr, having logged why, if one is missing. */
ui_screens_t *ui_screens_make(arena_t *arena, const texture_t *texture);

/* Nothing up, nothing carried, nothing on the way. Before the threads start. */
void ui_screens_begin(ui_screens_t *screens);

/* ---- Main's ---- */

/* Goes on to the newest view there is, and a screen that has just come up starts with the pointer in the middle. */
void ui_screens_view_take(ui_screens_t *screens, ui_widget_t *widget);

/* Asks the sim for something. Never waits: with the ring full it is lost, and counted. */
void ui_screens_send(ui_screens_t *screens, ui_screen_action_t action);

/* Where a screen of width by height pixels has its window. Every screen's is the same. */
ui_rect_t ui_screen_window(uint32_t width, uint32_t height);

/* What a point is on: its zone and slot, as a click with no flags. */
ui_screen_action_t ui_screen_hit_test(const ui_screen_place_t *places, uint32_t count, uint32_t width, uint32_t height,
                                      int32_t x, int32_t y);

/* The player's 36 slots, hotbar at the bottom, into out. Returns how many. Each screen's list starts with these. */
uint32_t ui_screen_player_places(ui_screen_place_t *out);

/* What every screen draws first: the world dimmed, the window, each place's slot and what is in it, and a tooltip for what the pointer is
 * over if it carries nothing. Between a frame's begin and end. */
void ui_screen_draw_base(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view,
                         const ui_screen_place_t *places, uint32_t count);

/* A stack in a slot's rect: its picture, how many, and a bar for how worn. */
void ui_screen_draw_stack(ui_widget_t *widget, const ui_screens_t *screens, ui_rect_t slot, const item_stack_t *stack);

/* What the pointer carries, around the pointer. Last, so that it is on top. */
void ui_screen_draw_cursor(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view);

/* A title on the window, at x and y in texture pixels from its top left. */
void ui_screen_draw_title(ui_widget_t *widget, ui_rect_t window, int32_t x, int32_t y, const char *text);

/* The arrow from a grid or an input to an output, fraction of it from the left at full strength and the rest faint. */
void ui_screen_draw_arrow(ui_widget_t *widget, const ui_screens_t *screens, ui_rect_t window, int32_t x, int32_t y,
                          float32_t fraction);

/* ---- The sim's ---- */

/* The next thing main asked for. False if there is nothing. */
bool ui_screens_next(ui_screens_t *screens, ui_screen_action_t *out);

/* Starts the next view with what every screen shows, and returns it for the screen's own part. */
ui_screen_view_t *ui_screens_view_next(ui_screens_t *screens, const item_t *item);

/* Sends main the view just filled. */
void ui_screens_view_publish(ui_screens_t *screens);

/* A click on an inventory slot, without shift: by item_stack_click_primary or _secondary, keeping the inventory's books. */
void ui_screen_player_click(inventory_t *inventory, uint32_t slot, uint32_t flags, item_stack_t *cursor);

/* A click on a grid's cell or its output, as both crafting screens have one. Shift on a cell sends what is in it to the inventory. */
void ui_screen_grid_click(crafting_grid_t *grid, inventory_t *inventory, ui_screen_action_t action,
                          item_stack_t *cursor);

/* ---- The inventory screen ---- */

/* Its places, into out, UI_SCREEN_PLACES_MAX of room. Returns how many. */
uint32_t ui_screen_inventory_places(ui_screen_place_t *out);

/* What is its own, on top of ui_screens_view_next: the grid of two. */
void ui_screen_inventory_view(const item_t *item, ui_screen_view_t *view);

/* What is its own, on top of ui_screen_draw_base. */
void ui_screen_inventory_draw(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view);

/* The sim applies a click: slots, armor, and the grid of two. */
void ui_screen_inventory_click(item_t *item, ui_screen_action_t action, item_stack_t *cursor);

#endif
