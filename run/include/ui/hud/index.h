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
    UI_HUD_ICON_SIZE = 9u,   /* a heart, a drumstick or a bubble */
    UI_HUD_ICONS = 10u,      /* of each, a row */
    UI_HUD_ROW_ABOVE = 10u,  /* from the top of the row to the top of the hotbar, and of the bubbles to the row's */
    UI_HUD_ITEM_SIZE = 16u,  /* an item, inside its slot */
    UI_HUD_ITEM_INSET = 3u,  /* from a slot's left and top to the item */
    UI_HUD_WEAR_WIDTH = 13u, /* the bar on what is worn */
    UI_HUD_WEAR_LEFT = 2u,   /* from the item's left */
    UI_HUD_WEAR_TOP = 13u,   /* from the item's top */
};

/* The armour row's three icons, by ui_hud_armor_icon. */
enum : uint32_t {
    UI_HUD_ARMOR_FULL = 0u,  /* two points */
    UI_HUD_ARMOR_HALF = 1u,  /* one, the left half full */
    UI_HUD_ARMOR_EMPTY = 2u, /* none */
    UI_HUD_ARMOR_KINDS = 3u,
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
    ui_image_t hotbar;       /* 0 */
    ui_image_t selected;     /* 16 */
    ui_image_t crosshair;    /* 32 */
    ui_image_t health;       /* 48 */
    ui_image_t health_empty; /* 64 */
    ui_image_t food;         /* 80 */
    ui_image_t food_empty;   /* 96 */
    ui_image_t air;          /* 112 */
    ui_image_t air_empty;    /* 128 */
    /* 144: ITEM_COUNT by item_id_t, ITEM_ID_NONE's with no pixels; then the armour row's UI_HUD_ARMOR_KINDS, as ui_t has no room to
     * grow: app_t is on main's stack, near its limit. Ask ui_hud_armor_icon for those. */
    const ui_image_t *borrowed_icons;
} ui_hud_t;
static_assert(sizeof(ui_hud_t) == 152);

/* All the game tells the HUD, for one frame. Plain values, so that this module knows no player and no thread. */
typedef struct ui_hud_state_t {
    item_stack_t hotbar[INVENTORY_HOTBAR_SLOTS]; /* 0 */
    uint8_t selected;                            /* 72: which of them is in hand; past the end marks none */
    uint8_t health;                              /* 73: half hearts, 0 to 20 */
    uint8_t food;                                /* 74: half drumsticks, 0 to 20 */
    uint8_t bubbles;                             /* 75: of air, 0 to UI_HUD_ICONS: see ui_hud_bubbles */
    uint8_t bubbles_shown;                       /* 76: whether that row is up at all: not on land with all its air */
    uint8_t armor; /* 77: points worn, 0 to 20: an icon a 2, half for an odd one; no row at 0 */
} ui_hud_state_t;
static_assert(sizeof(ui_hud_state_t) == 78);

/* All the debug overlay shows. */
typedef struct ui_hud_debug_t {
    float32_t fps;                          /* 0 */
    float32_t stage_ms[UI_HUD_STAGE_COUNT]; /* 4 */
    int32_t block[3];                       /* 40: x, y and z: see ui_hud_coordinates_draw */
    float32_t latitude_deg;                 /* 52: of the eye, positive north (plan 004 phase 02) */
    float32_t sun_elevation_deg;            /* 56: the sun above the eye's horizon; below 0 is night */
    uint32_t mobs;                          /* 60: in the world, as of the sim's last tick (plan 005 phase 7) */
    uint32_t drops;                         /* 64: stacks on the ground, the same */
    uint32_t uploads;                       /* 68: sections of the newest snapshot not yet on the card */
    uint32_t far_jobs;                      /* 72: far-field columns being built on workers */
    /* Plan 008 phase 01: the last second, picture by picture. shown is the time between new pictures on the screen. */
    float32_t shown_last_ms; /* 76: between the last two */
    float32_t shown_p99_ms;  /* 80 */
    float32_t shown_most_ms; /* 84 */
    float32_t card_mean_ms;  /* 88: the card's clock on a whole picture */
    float32_t card_p99_ms;   /* 92 */
    float32_t card_most_ms;  /* 96 */
    float32_t idle_ms;       /* 100: the gpu thread waiting for main to take the last picture, a picture's mean */
    float32_t x_put_ms;   /* 104: the X server's put, from its send to its completion, a picture's mean; 0 under KMS */
    float32_t turn_deg_s; /* 108: the camera's turn, the mean over a quarter of a second */
    float32_t card_mhz;   /* 112: the card's clock speed, at its last reading */
    uint32_t late;        /* 116: pictures ready after the take they were aimed at (plan 008 phase 02) */
    uint32_t recent;      /* 120: 0 if there is no last second to speak of, and the four lines say so */
    uint32_t skipped;     /* 124: X puts skipped, both segments being with the server; shown when not 0 */
} ui_hud_debug_t;
static_assert(sizeof(ui_hud_debug_t) == 128);

/* Where things go on a canvas of some size, in its pixels. */
typedef struct ui_hud_layout_t {
    ui_rect_t hotbar;    /* 0 */
    ui_rect_t crosshair; /* 16 */
    int32_t row_y;       /* 32: the top of the hearts and the drumsticks */
    uint32_t scale;      /* 36: screen pixels per texture pixel */
    int32_t air_y;       /* 40: the top of the bubbles, over the drumsticks, and of the armour, over the hearts */
    int32_t air_high_y;  /* 44: the top of the bubbles while the armour is up as well: a row higher */
} ui_hud_layout_t;
static_assert(sizeof(ui_hud_layout_t) == 48);

/* Reads every texture the HUD is drawn with, and every item's, into memory from arena. False, having logged why, if one is missing. */
bool ui_hud_init(ui_hud_t *hud, arena_t *arena, const texture_t *texture);

/* Empties the handle. The memory stays with the arena. */
void ui_hud_deinit(ui_hud_t *hud);

/* One of the armour row's icons, UI_HUD_ARMOR_*, 9 square. */
const ui_image_t *ui_hud_armor_icon(const ui_hud_t *hud, uint32_t kind);

/* A value out of maximum, as the halves of a row: anything above a half counts as the next half, so that a body with any health left
 * shows some. Nothing for 0 or below, and for what is no number. */
uint32_t ui_hud_half_units(float32_t value, uint32_t maximum);

/* Air out of maximum, as the bubbles of a row of UI_HUD_ICONS: any part of a bubble's share shows as a whole one, so that the last goes just as the air
 * runs out, and not before. */
uint32_t ui_hud_bubbles(uint32_t air, uint32_t maximum);

/* The same scale as the widgets: ui_widget_scale_for. A canvas too small to hold it all still gets this, and the clip does the rest. */
ui_hud_layout_t ui_hud_layout(uint32_t canvas_width, uint32_t canvas_height);

/* The item's icon with its top left at (x, y), scale screen pixels to a texture pixel, with a bar if it is worn and its count if it is
 * more than one. Nothing for an empty stack. Stage 14.3's slots can use it as it is. */
void ui_hud_item_draw(const ui_hud_t *hud, ui_canvas_t *canvas, const ui_font_t *font, const item_stack_t *stack,
                      int32_t x, int32_t y, uint32_t scale);

/* The crosshair, the hotbar with what is on it, the hearts, the drumsticks, the armour if any is worn, and the bubbles if they are up.
 * No allocation. */
void ui_hud_draw(const ui_hud_t *hud, ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_state_t *state);

/* A stage's name as the overlay writes it, for the log too; "?" for one past the end. */
const char *ui_hud_stage_name(ui_hud_stage_t stage);

/* The frames per second, each stage's time, where the body is, and what a world left alone piles up, on a dark panel at the top left.
 * No allocation. */
void ui_hud_debug_draw(ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_debug_t *debug);

enum : uint32_t {
    UI_HUD_NOTICE_BYTES = 64u, /* with its NUL */
};

/* One line in red across the top of the canvas, of something a person playing needs to know and has no other way to learn. */
void ui_hud_notice_draw(ui_canvas_t *canvas, const ui_font_t *font, const char *text);

/* x, y and z in whole blocks, on a dark panel at the top left. z is up, and 0 is sea level. below_notice is for when ui_hud_notice_draw has drawn on
 * this canvas, as that band crosses the top left too. No allocation. */
void ui_hud_coordinates_draw(ui_canvas_t *canvas, const ui_font_t *font, const int32_t block[3], bool below_notice);

/* The date line, plan 004 phase 01, on a dark panel at the top right, as the coordinates are at the top left. below_notice as for those. No
 * allocation. */
void ui_hud_date_draw(ui_canvas_t *canvas, const ui_font_t *font, const char *text, bool below_notice);

/* Where that panel lies on a canvas of this size, which nothing else in the overlay goes outside. */
ui_rect_t ui_hud_debug_panel(uint32_t canvas_width, uint32_t canvas_height);

/* ---- The top right corner: plan 004 phase 10 ------------------------------------------------------------------------ */

enum : uint32_t {
    UI_HUD_CORNER_LINES = 4u,  /* 0 settlement (always empty in plan 004), 1 date, 2 climate, 3 warning */
    UI_HUD_CORNER_BYTES = 96u, /* one line, with its NUL: the longest date line is under 70 */
    UI_HUD_CORNER_NONE = 255u, /* for arrow_line and warning_line: no such line */
    UI_HUD_CORNER_SETTLEMENT = 0u,
    UI_HUD_CORNER_DATE = 1u,
    UI_HUD_CORNER_CLIMATE = 2u,
    UI_HUD_CORNER_WARNING = 3u,
};

/* What the top right corner shows, as text main has already made. Plain values, so this module knows no climate and no thread. */
typedef struct ui_hud_corner_t {
    char lines[UI_HUD_CORNER_LINES][UI_HUD_CORNER_BYTES]; /* 0: drawn top to bottom; an empty one takes no room */
    float32_t arrow[2];                                   /* 384: unit vector on the screen, x right, y down */
    uint8_t arrow_line;                                   /* 392: the line the arrow ends, or UI_HUD_CORNER_NONE */
    uint8_t warning_line; /* 393: the line drawn on the red panel, or UI_HUD_CORNER_NONE */
    uint8_t _pad0[6];     /* 394 */
} ui_hud_corner_t;
static_assert(sizeof(ui_hud_corner_t) == 400);

/* Nothing to show: every line empty, no arrow and no warning. */
void ui_hud_corner_clear(ui_hud_corner_t *corner);

/* The panel ui_hud_coordinates_draw draws, without drawing it, so that others can keep clear of it. */
ui_rect_t ui_hud_coordinates_panel(const ui_canvas_t *canvas, const ui_font_t *font, const int32_t block[3],
                                   bool below_notice);

/* The corner's panels, right-aligned against the top right, one a line, each as wide as its text, touching, in line order.
 * Slot 0 is reserved for a settlement's name, which a later plan fills: in plan 004 it is never drawn, whatever it holds.
 * below_notice as for the coordinates. avoid is the top-left panel that was drawn, or an empty rect: if the corner would
 * overlap it, the whole corner goes a margin below it. The warning line is on the notice band's red, and the arrow line ends
 * in a square the height of a line, after a space, with the arrow in it. Returns the rect of all of it, empty if nothing was
 * drawn. No allocation. */
ui_rect_t ui_hud_corner_draw(ui_canvas_t *canvas, const ui_font_t *font, const ui_hud_corner_t *corner, ui_rect_t avoid,
                             bool below_notice);

/* Where the arrow's tail and tip go in its square, in screen pixels, for an arrow of unit length on the screen: what
 * ui_hud_corner_draw draws between. */
void ui_hud_corner_arrow_ends(ui_rect_t square, const float32_t arrow[2], uint32_t scale, int32_t out_tail[2],
                              int32_t out_tip[2]);

#endif
