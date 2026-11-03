/* See README.md. */
#include "ui/menu/index.h"

#include "assert/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Every number here is in texture pixels, and multiplied by the frame's scale. The screens are made to fit 320 by 240. */
enum : int32_t {
    UI_MENU_BUTTON_WIDTH = 200,
    UI_MENU_ROW_STEP = 24, /* a button's 20, and a gap */
    UI_MENU_GAP = 4,
    UI_MENU_LOGO_SCALE = 2, /* of the frame's: it is the one thing meant to be big */
    UI_MENU_TITLE_Y = 8,
    UI_MENU_TITLE_HEIGHT = 9,
    UI_MENU_LIST_WIDTH = 260,
    UI_MENU_LIST_Y = 24,
    UI_MENU_LIST_INSET = 4,
    UI_MENU_LIST_BOTTOM = 56,     /* room under the list: two rows of buttons, and the edge */
    UI_MENU_LIST_ROW_HEIGHT = 22, /* two lines of text */
    UI_MENU_SCROLLBAR_WIDTH = 6,
    UI_MENU_FIELD_NAME_LABEL_Y = 40,
    UI_MENU_FIELD_NAME_Y = 51,
    UI_MENU_FIELD_SEED_LABEL_Y = 80,
    UI_MENU_FIELD_SEED_Y = 91,
    UI_MENU_CREATE_BUTTONS_Y = 130,
    UI_MENU_CREATE_MESSAGE_Y = 160,
    UI_MENU_SCREEN_HEIGHT = 240, /* what the create screen is laid out in, in the middle of what there is */
};

/* The widgets' ids. A row's is its place in the list, after UI_MENU_ID_ROW. */
enum : uint32_t {
    UI_MENU_ID_SINGLEPLAYER = 1u,
    UI_MENU_ID_SETTINGS = 2u,
    UI_MENU_ID_QUIT = 3u,
    UI_MENU_ID_PLAY = 4u,
    UI_MENU_ID_CREATE_NEW = 5u,
    UI_MENU_ID_BACK = 6u,
    UI_MENU_ID_NAME = 7u,
    UI_MENU_ID_SEED = 8u,
    UI_MENU_ID_CREATE = 9u,
    UI_MENU_ID_CANCEL = 10u,
    UI_MENU_ID_RESUME = 11u,
    UI_MENU_ID_TITLE = 12u,
    UI_MENU_ID_ROW = 16u,
};

enum : uint32_t {
    UI_MENU_TEXT_COLOR = 0xFFFFFFFFu,
    UI_MENU_DIM_COLOR = 0xFFA0A0A0u,
    UI_MENU_SEED_COLOR = 0xFFD8D8D8u, /* on a button's grey, where the dim colour cannot be read */
    UI_MENU_ERROR_COLOR = 0xFFFF6060u,
    UI_MENU_PAUSE_SHADE = 0x90000000u,    /* the world, still there behind the pause screen */
    UI_MENU_SELECTED_COLOR = 0x50FFFFFFu, /* over a chosen row */
    UI_MENU_SCROLL_TRACK = 0x60000000u,
    UI_MENU_SCROLL_KNOB = 0xFFC0C0C0u,
};

bool ui_menu_init(ui_menu_t *menu, arena_t *arena, const texture_t *texture, ui_widget_t *widget) {
    memset(menu, 0, sizeof(*menu));
    if (!ui_image_load(&menu->logo, arena, texture, "ui/logo") ||
        !ui_image_load(&menu->background, arena, texture, "ui/menu_background")) {
        return false; /* it has said why */
    }
    menu->borrowed_worlds =
        arena_alloc_array(arena, UI_MENU_WORLDS_MAX, sizeof(ui_menu_world_t), alignof(ui_menu_world_t));
    menu->borrowed_widget = widget;
    menu->selected = -1;
    return true;
}

void ui_menu_deinit(ui_menu_t *menu) {
    memset(menu, 0, sizeof(*menu));
}

void ui_menu_saves_set(ui_menu_t *menu, const char *saves, uint64_t blank_seed) {
    menu->borrowed_saves = saves;
    menu->blank_seed = blank_seed;
}

void ui_menu_settings_enable(ui_menu_t *menu) {
    menu->settings_enabled = 1u;
}

/* Looks at the disk again, and says on the screen if it could not, or could not read all of it. */
static void ui_menu_list_read(ui_menu_t *menu) {
    ASSERT_ALWAYS(menu->borrowed_saves != nullptr);
    const file_status_t status = ui_menu_catalog_read(menu->borrowed_saves, menu->borrowed_worlds, UI_MENU_WORLDS_MAX,
                                                      &menu->world_count, &menu->worlds_skipped);
    if (status != FILE_OK) {
        (void)snprintf(menu->message, sizeof(menu->message), "Could not read the worlds: %s", file_status_name(status));
    } else if (menu->worlds_skipped != 0u) {
        (void)snprintf(menu->message, sizeof(menu->message), "%" PRIu32 " worlds could not be listed",
                       menu->worlds_skipped);
    }
}

void ui_menu_open(ui_menu_t *menu, ui_menu_screen_t screen) {
    ui_widget_t *const widget = menu->borrowed_widget;
    menu->screen = (uint8_t)screen;
    menu->message[0] = '\0';
    widget->focus_id = 0u;
    switch (screen) {
    case UI_MENU_SCREEN_SELECT:
        menu->selected = -1;
        menu->first_row = 0u;
        ui_menu_list_read(menu);
        break;
    case UI_MENU_SCREEN_CREATE:
        menu->name_text[0] = '\0';
        menu->seed_text[0] = '\0';
        widget->focus_id = UI_MENU_ID_NAME; /* a person coming here means to type */
        break;
    case UI_MENU_SCREEN_MAIN:
    case UI_MENU_SCREEN_PAUSE:
    case UI_MENU_SCREEN_LOADING:
        break;
    }
}

void ui_menu_message_set(ui_menu_t *menu, const char *text) {
    (void)snprintf(menu->message, sizeof(menu->message), "%s", text);
}

/* ---- What several screens share -------------------------------------------------------------------------------------- */

static int32_t ui_menu_scale(const ui_menu_t *menu) {
    return (int32_t)menu->borrowed_widget->scale;
}

static ui_rect_t ui_menu_canvas(const ui_menu_t *menu) {
    const ui_widget_t *const widget = menu->borrowed_widget;
    return (ui_rect_t){0, 0, (int32_t)widget->canvas_width, (int32_t)widget->canvas_height};
}

/* The dark earth, tile by tile, over everything. */
static void ui_menu_background_draw(ui_menu_t *menu) {
    const ui_rect_t whole = ui_menu_canvas(menu);
    const int32_t side = (int32_t)menu->background.width * ui_menu_scale(menu);
    for (int32_t y = 0; y < whole.height; y += side) {
        for (int32_t x = 0; x < whole.width; x += side) {
            ui_compose_image_whole(menu->borrowed_widget->borrowed_canvas, &menu->background,
                                   (ui_rect_t){x, y, side, side});
        }
    }
}

static void ui_menu_title_draw(ui_menu_t *menu, int32_t y, const char *text) {
    const int32_t scale = ui_menu_scale(menu);
    const ui_rect_t line = {0, y, ui_menu_canvas(menu).width, UI_MENU_TITLE_HEIGHT * scale};
    ui_widget_label_centered(menu->borrowed_widget, line, text, UI_MENU_TEXT_COLOR, true);
}

static void ui_menu_message_draw(ui_menu_t *menu, int32_t y) {
    if (menu->message[0] != '\0') {
        const int32_t scale = ui_menu_scale(menu);
        const ui_rect_t line = {0, y, ui_menu_canvas(menu).width, UI_MENU_TITLE_HEIGHT * scale};
        ui_widget_label_centered(menu->borrowed_widget, line, menu->message, UI_MENU_ERROR_COLOR, true);
    }
}

/* The n-th of a column of buttons in the canvas's middle, the first of them at top. */
static ui_rect_t ui_menu_button_rect(const ui_menu_t *menu, int32_t top, int32_t n) {
    const int32_t scale = ui_menu_scale(menu);
    const int32_t width = UI_MENU_BUTTON_WIDTH * scale;
    return (ui_rect_t){(ui_menu_canvas(menu).width - width) / 2, top + n * UI_MENU_ROW_STEP * scale, width,
                       (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale};
}

/* Where the column of buttons on the main and pause screens starts. */
static int32_t ui_menu_column_top(const ui_menu_t *menu) {
    return ui_menu_canvas(menu).height * 3 / 8;
}

/* ---- The main screen ------------------------------------------------------------------------------------------------- */

static ui_menu_action_t ui_menu_main_frame(ui_menu_t *menu) {
    ui_widget_t *const widget = menu->borrowed_widget;
    const int32_t scale = ui_menu_scale(menu);
    const ui_rect_t whole = ui_menu_canvas(menu);
    ui_menu_background_draw(menu);
    const int32_t logo_width = (int32_t)menu->logo.width * UI_MENU_LOGO_SCALE * scale;
    const int32_t logo_height = (int32_t)menu->logo.height * UI_MENU_LOGO_SCALE * scale;
    ui_compose_image_whole(widget->borrowed_canvas, &menu->logo,
                           (ui_rect_t){(whole.width - logo_width) / 2, whole.height / 8, logo_width, logo_height});
    const int32_t top = ui_menu_column_top(menu);
    const bool singleplayer =
        ui_widget_button(widget, UI_MENU_ID_SINGLEPLAYER, ui_menu_button_rect(menu, top, 0), "Singleplayer", true);
    const bool settings = ui_widget_button(widget, UI_MENU_ID_SETTINGS, ui_menu_button_rect(menu, top, 1), "Settings",
                                           menu->settings_enabled != 0u);
    const bool quit = ui_widget_button(widget, UI_MENU_ID_QUIT, ui_menu_button_rect(menu, top, 2), "Quit Game", true);
    if (singleplayer) {
        ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    }
    return quit ? UI_MENU_ACTION_QUIT_GAME : (settings ? UI_MENU_ACTION_SETTINGS : UI_MENU_ACTION_NONE);
}

/* ---- The list of worlds ---------------------------------------------------------------------------------------------- */

/* One world's row: a button, and its two lines. True when clicked. */
static bool ui_menu_row_draw(ui_menu_t *menu, uint32_t index, ui_rect_t rect) {
    ui_widget_t *const widget = menu->borrowed_widget;
    const ui_menu_world_t *const world = &menu->borrowed_worlds[index];
    const int32_t scale = ui_menu_scale(menu);
    const bool clicked = ui_widget_button(widget, UI_MENU_ID_ROW + index, rect, "", true);
    if (menu->selected == (int32_t)index) {
        ui_compose_fill(widget->borrowed_canvas, rect, UI_MENU_SELECTED_COLOR);
    }
    char seed[48];
    (void)snprintf(seed, sizeof(seed), "Seed: %" PRId64, (int64_t)world->seed);
    const int32_t x = rect.x + UI_MENU_LIST_INSET * scale;
    ui_widget_label(widget, x, rect.y + 2 * scale, world->name, UI_MENU_TEXT_COLOR, true);
    ui_widget_label(widget, x, rect.y + (2 + (int32_t)UI_FONT_LINE) * scale, seed, UI_MENU_SEED_COLOR, true);
    return clicked;
}

static void ui_menu_scrollbar_draw(ui_menu_t *menu, ui_rect_t track, uint32_t visible) {
    ui_canvas_t *const canvas = menu->borrowed_widget->borrowed_canvas;
    ui_compose_fill(canvas, track, UI_MENU_SCROLL_TRACK);
    const uint32_t count = menu->world_count;
    if (count <= visible) {
        return;
    }
    const int32_t knob_height = (int32_t)((int64_t)track.height * visible / count);
    const int32_t knob_y = track.y + (int32_t)((int64_t)track.height * menu->first_row / count);
    ui_compose_fill(canvas, (ui_rect_t){track.x, knob_y, track.width, knob_height}, UI_MENU_SCROLL_KNOB);
}

static ui_menu_action_t ui_menu_select_frame(ui_menu_t *menu, bool escape) {
    ui_widget_t *const widget = menu->borrowed_widget;
    const int32_t scale = ui_menu_scale(menu);
    const ui_rect_t whole = ui_menu_canvas(menu);
    ui_menu_background_draw(menu);
    ui_menu_title_draw(menu, UI_MENU_TITLE_Y * scale, "Select World");

    const int32_t list_width = UI_MENU_LIST_WIDTH * scale;
    const int32_t list_x = (whole.width - list_width) / 2;
    const int32_t list_y = UI_MENU_LIST_Y * scale;
    const int32_t list_height = whole.height - UI_MENU_LIST_BOTTOM * scale - list_y;
    const ui_rect_t list = {list_x, list_y, list_width, list_height};
    ui_widget_panel(widget, list);
    const int32_t inset = UI_MENU_LIST_INSET * scale;
    const int32_t row_step = UI_MENU_ROW_STEP * scale;
    const int32_t rows_height = list_height - 2 * inset;
    const uint32_t visible = rows_height >= row_step ? (uint32_t)(rows_height / row_step) : 1u;

    /* The wheel works anywhere on this screen: only the list can scroll. Away from the player is up the list. */
    const int64_t past_end = menu->world_count > visible ? (int64_t)(menu->world_count - visible) : 0;
    const int64_t scrolled = (int64_t)menu->first_row - widget->input.wheel;
    menu->first_row = scrolled < 0 ? 0u : (uint32_t)(scrolled > past_end ? past_end : scrolled);

    const int32_t bar_width = UI_MENU_SCROLLBAR_WIDTH * scale;
    const int32_t row_width = list_width - 2 * inset - bar_width - scale;
    for (uint32_t i = 0u; i < visible && menu->first_row + i < menu->world_count; i++) {
        const uint32_t index = menu->first_row + i;
        const ui_rect_t row = {list_x + inset, list_y + inset + (int32_t)i * row_step, row_width,
                               UI_MENU_LIST_ROW_HEIGHT * scale};
        if (ui_menu_row_draw(menu, index, row)) {
            menu->selected = (int32_t)index;
        }
    }
    const ui_rect_t track = {list_x + list_width - inset - bar_width, list_y + inset, bar_width, rows_height};
    ui_menu_scrollbar_draw(menu, track, visible);
    if (menu->world_count == 0u) {
        ui_widget_label_centered(widget, list, "No worlds yet", UI_MENU_DIM_COLOR, true);
    }

    const int32_t buttons_y = whole.height - UI_MENU_LIST_BOTTOM * scale + UI_MENU_GAP * scale;
    const int32_t half_width = (list_width - UI_MENU_GAP * scale) / 2;
    const int32_t button_height = (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale;
    const bool can_play = menu->selected >= 0;
    const ui_rect_t play_rect = {list_x, buttons_y, half_width, button_height};
    const bool play = ui_widget_button(widget, UI_MENU_ID_PLAY, play_rect, "Play Selected", can_play);
    const ui_rect_t create_rect = {list_x + list_width - half_width, buttons_y, half_width, button_height};
    const bool create = ui_widget_button(widget, UI_MENU_ID_CREATE_NEW, create_rect, "Create New World", true);
    const ui_rect_t back_rect = {list_x, buttons_y + row_step, list_width, button_height};
    const bool back = ui_widget_button(widget, UI_MENU_ID_BACK, back_rect, "Back", true);
    ui_menu_message_draw(menu, list_y + list_height / 2 + (int32_t)UI_FONT_LINE * scale);

    if ((play || widget->input.enter != 0u) && can_play) {
        menu->chosen = menu->borrowed_worlds[menu->selected];
        return UI_MENU_ACTION_PLAY;
    }
    if (create) {
        ui_menu_open(menu, UI_MENU_SCREEN_CREATE);
    } else if (back || escape) {
        ui_menu_open(menu, UI_MENU_SCREEN_MAIN);
    }
    return UI_MENU_ACTION_NONE;
}

/* ---- Making a world -------------------------------------------------------------------------------------------------- */

/* Makes the world the fields say. False if it could not, and the screen says why. */
static bool ui_menu_create_commit(ui_menu_t *menu) {
    const uint64_t seed = ui_menu_catalog_seed_from_text(menu->seed_text, menu->blank_seed);
    const file_status_t status = ui_menu_catalog_create(menu->borrowed_saves, menu->name_text, seed, &menu->chosen);
    if (status != FILE_OK) {
        (void)snprintf(menu->message, sizeof(menu->message), "Could not make the world: %s", file_status_name(status));
        return false;
    }
    return true;
}

static ui_menu_action_t ui_menu_create_frame(ui_menu_t *menu, bool escape) {
    ui_widget_t *const widget = menu->borrowed_widget;
    const int32_t scale = ui_menu_scale(menu);
    const ui_rect_t whole = ui_menu_canvas(menu);
    ui_menu_background_draw(menu);
    const int32_t top = (whole.height - UI_MENU_SCREEN_HEIGHT * scale) / 2;
    ui_menu_title_draw(menu, top + UI_MENU_TITLE_Y * scale, "Create New World");

    const ui_rect_t first = ui_menu_button_rect(menu, 0, 0);
    const int32_t x = first.x;
    const int32_t width = first.width;
    const int32_t height = first.height;
    ui_widget_label(widget, x, top + UI_MENU_FIELD_NAME_LABEL_Y * scale, "World Name", UI_MENU_TEXT_COLOR, true);
    const ui_rect_t name_rect = {x, top + UI_MENU_FIELD_NAME_Y * scale, width, height};
    (void)ui_widget_text_field(widget, UI_MENU_ID_NAME, name_rect, menu->name_text, sizeof(menu->name_text));
    ui_widget_label(widget, x, top + UI_MENU_FIELD_SEED_LABEL_Y * scale, "Seed (blank for random)", UI_MENU_TEXT_COLOR,
                    true);
    const ui_rect_t seed_rect = {x, top + UI_MENU_FIELD_SEED_Y * scale, width, height};
    (void)ui_widget_text_field(widget, UI_MENU_ID_SEED, seed_rect, menu->seed_text, sizeof(menu->seed_text));

    char clean[UI_MENU_NAME_BYTES];
    const bool can_create = ui_menu_catalog_name_clean(menu->name_text, clean);
    const int32_t half_width = (width - UI_MENU_GAP * scale) / 2;
    const int32_t buttons_y = top + UI_MENU_CREATE_BUTTONS_Y * scale;
    const ui_rect_t create_rect = {x, buttons_y, half_width, height};
    const bool create = ui_widget_button(widget, UI_MENU_ID_CREATE, create_rect, "Create", can_create);
    const ui_rect_t cancel_rect = {x + width - half_width, buttons_y, half_width, height};
    const bool cancel = ui_widget_button(widget, UI_MENU_ID_CANCEL, cancel_rect, "Cancel", true);
    ui_menu_message_draw(menu, top + UI_MENU_CREATE_MESSAGE_Y * scale);

    const bool enter = widget->input.enter != 0u;
    if (enter && !can_create) {
        widget->focus_id = UI_MENU_ID_NAME; /* Enter took it away, and a name is what is missing */
    }
    if ((create || enter) && can_create && ui_menu_create_commit(menu)) {
        return UI_MENU_ACTION_PLAY;
    }
    if (cancel || escape) {
        ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    }
    return UI_MENU_ACTION_NONE;
}

/* ---- The pause screen ------------------------------------------------------------------------------------------------ */

static ui_menu_action_t ui_menu_pause_frame(ui_menu_t *menu, bool escape) {
    ui_widget_t *const widget = menu->borrowed_widget;
    const ui_rect_t whole = ui_menu_canvas(menu);
    ui_compose_fill(widget->borrowed_canvas, whole, UI_MENU_PAUSE_SHADE);
    ui_menu_title_draw(menu, whole.height / 4, "Game Menu");
    const int32_t top = ui_menu_column_top(menu);
    const bool resume = ui_widget_button(widget, UI_MENU_ID_RESUME, ui_menu_button_rect(menu, top, 0), "Resume", true);
    const bool settings = ui_widget_button(widget, UI_MENU_ID_SETTINGS, ui_menu_button_rect(menu, top, 1), "Settings",
                                           menu->settings_enabled != 0u);
    const bool title =
        ui_widget_button(widget, UI_MENU_ID_TITLE, ui_menu_button_rect(menu, top, 2), "Quit to Title", true);
    if (title) {
        return UI_MENU_ACTION_QUIT_TO_TITLE;
    }
    if (resume || escape) {
        return UI_MENU_ACTION_RESUME;
    }
    return settings ? UI_MENU_ACTION_SETTINGS : UI_MENU_ACTION_NONE;
}

/* ---- While a world opens ---------------------------------------------------------------------------------------------- */

static void ui_menu_loading_frame(ui_menu_t *menu) {
    const ui_rect_t whole = ui_menu_canvas(menu);
    const int32_t line = (int32_t)UI_FONT_LINE * 2 * ui_menu_scale(menu);
    ui_menu_background_draw(menu);
    ui_menu_title_draw(menu, whole.height / 2 - line, "Loading world...");
    ui_menu_title_draw(menu, whole.height / 2, menu->chosen.name);
}

ui_menu_action_t ui_menu_frame(ui_menu_t *menu, bool escape) {
    switch ((ui_menu_screen_t)menu->screen) {
    case UI_MENU_SCREEN_MAIN:
        return ui_menu_main_frame(menu); /* Esc goes back, and here there is no back */
    case UI_MENU_SCREEN_SELECT:
        return ui_menu_select_frame(menu, escape);
    case UI_MENU_SCREEN_CREATE:
        return ui_menu_create_frame(menu, escape);
    case UI_MENU_SCREEN_PAUSE:
        return ui_menu_pause_frame(menu, escape);
    case UI_MENU_SCREEN_LOADING:
        ui_menu_loading_frame(menu);
        return UI_MENU_ACTION_NONE;
    }
    return UI_MENU_ACTION_NONE;
}
