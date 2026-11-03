/* See run/src/ui/menu/README.md. */
#ifndef MC_RUN_UI_MENU_INDEX_H
#define MC_RUN_UI_MENU_INDEX_H

#include "arena/index.h"
#include "texture/index.h"
#include "ui/compose/index.h"
#include "ui/menu/catalog/index.h"
#include "ui/widget/index.h"

#include <stdint.h>

enum : uint32_t {
    UI_MENU_WORLDS_MAX = 256u,     /* listed at once */
    UI_MENU_SEED_TEXT_BYTES = 32u, /* with its NUL: any 64-bit number, and a word or two */
    UI_MENU_MESSAGE_BYTES = 64u,   /* what went wrong, on the screen, with its NUL */
};

typedef enum ui_menu_screen_t : uint8_t {
    UI_MENU_SCREEN_MAIN = 0,
    UI_MENU_SCREEN_SELECT = 1,
    UI_MENU_SCREEN_CREATE = 2,
    UI_MENU_SCREEN_PAUSE = 3,
    UI_MENU_SCREEN_LOADING = 4, /* says which world, and does nothing */
} ui_menu_screen_t;

/* What a frame of menus wants of whoever runs it. Moving between screens is this module's own business, and not in here. */
typedef enum ui_menu_action_t : uint8_t {
    UI_MENU_ACTION_NONE = 0,
    UI_MENU_ACTION_PLAY = 1,          /* open the world in chosen */
    UI_MENU_ACTION_RESUME = 2,        /* out of the pause screen, by its button or Esc */
    UI_MENU_ACTION_QUIT_TO_TITLE = 3, /* close the world; the next screen is the caller's to open */
    UI_MENU_ACTION_QUIT_GAME = 4,
    UI_MENU_ACTION_SETTINGS = 5, /* only if ui_menu_settings_enable has been called */
} ui_menu_action_t;

/* The main thread's, like all of ui_t. */
typedef struct ui_menu_t {
    ui_menu_world_t chosen;                  /* 0: what the last UI_MENU_ACTION_PLAY means */
    ui_image_t logo;                         /* 88 */
    ui_image_t background;                   /* 104 */
    ui_widget_t *borrowed_widget;            /* 120 */
    ui_menu_world_t *borrowed_worlds;        /* 128: UI_MENU_WORLDS_MAX, from the arena its Owner gave */
    const char *borrowed_saves;              /* 136: where worlds live */
    uint64_t blank_seed;                     /* 144: what a blank seed field means */
    char name_text[UI_MENU_NAME_BYTES];      /* 152 */
    char seed_text[UI_MENU_SEED_TEXT_BYTES]; /* 184 */
    char message[UI_MENU_MESSAGE_BYTES];     /* 216: empty if nothing is wrong */
    uint32_t world_count;                    /* 280 */
    uint32_t worlds_skipped;                 /* 284: what the last look at saves could not read */
    uint32_t first_row;                      /* 288: the world at the top of the list */
    int32_t selected;                        /* 292: in borrowed_worlds; -1 is none */
    uint8_t screen;                          /* 296: ui_menu_screen_t */
    uint8_t settings_enabled;                /* 297 */
    uint8_t _pad0[6];                        /* 298 */
} ui_menu_t;
static_assert(sizeof(ui_menu_t) == 304);

/* Reads its textures and takes room for the list, all from arena. False, having logged why, if a texture is missing. */
bool ui_menu_init(ui_menu_t *menu, arena_t *arena, const texture_t *texture, ui_widget_t *widget);

/* Empties the handle. The memory stays with the arena. */
void ui_menu_deinit(ui_menu_t *menu);

/* Where worlds live, which must outlive it, and what a seed left blank means. Before the first ui_menu_open of a list. */
void ui_menu_saves_set(ui_menu_t *menu, const char *saves, uint64_t blank_seed);

/* Gives the Settings button something to ask for. Until then it is there and looks disabled. */
void ui_menu_settings_enable(ui_menu_t *menu);

/* Goes to a screen with nothing left over from the last. The list looks at the disk, here and nowhere else. */
void ui_menu_open(ui_menu_t *menu, ui_menu_screen_t screen);

/* Says this on the screen now showing, in place of anything it said, until the next ui_menu_open. Cut short if it does not fit. */
void ui_menu_message_set(ui_menu_t *menu, const char *text);

/* One frame of the screen, between ui_frame_begin_* and ui_frame_end, over all of the canvas. escape is that Esc was pressed,
 * which goes back one screen. Never logs: under KMS nothing can read a log while a frame is drawn, so what went wrong is shown
 * in message. */
ui_menu_action_t ui_menu_frame(ui_menu_t *menu, bool escape);

#endif
