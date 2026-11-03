/* See README.md. */
#include "ui/screen_chest/index.h"

#include <string.h>

enum : int16_t {
    UI_SCREEN_CHEST_X = 8,
    UI_SCREEN_CHEST_Y = 18,
    UI_SCREEN_CHEST_TITLE_X = 8,
    UI_SCREEN_CHEST_TITLE_Y = 7,
};

enum : uint32_t {
    UI_SCREEN_CHEST_COLUMNS = 9u,
};

uint32_t ui_screen_chest_places(ui_screen_place_t *out) {
    uint32_t count = ui_screen_player_places(out);
    for (uint32_t slot = 0u; slot < CONTAINER_CHEST_SLOTS; slot++) {
        out[count++] = (ui_screen_place_t){
            .x = (int16_t)(UI_SCREEN_CHEST_X + (int32_t)((slot % UI_SCREEN_CHEST_COLUMNS) * UI_SCREEN_SLOT)),
            .y = (int16_t)(UI_SCREEN_CHEST_Y + (int32_t)((slot / UI_SCREEN_CHEST_COLUMNS) * UI_SCREEN_SLOT)),
            .zone = UI_SCREEN_ZONE_BLOCK,
            .slot = (uint8_t)slot,
            ._pad0 = {0}};
    }
    return count;
}

void ui_screen_chest_view(const container_chest_t *chest, ui_screen_view_t *view) {
    memcpy(view->block, chest->slots, sizeof(chest->slots));
}

void ui_screen_chest_draw(ui_widget_t *widget) {
    const ui_canvas_t *const canvas = widget->borrowed_canvas;
    const ui_rect_t window = ui_screen_window(canvas->width, canvas->height);
    ui_screen_draw_title(widget, window, UI_SCREEN_CHEST_TITLE_X, UI_SCREEN_CHEST_TITLE_Y, "Chest");
}

void ui_screen_chest_click(item_t *item, container_chest_t *chest, ui_screen_action_t action, item_stack_t *cursor) {
    const bool shift = (action.flags & UI_SCREEN_FLAG_SHIFT) != 0u;
    const bool secondary = (action.flags & UI_SCREEN_FLAG_SECONDARY) != 0u;
    if (action.zone == UI_SCREEN_ZONE_PLAYER) {
        if (shift) {
            (void)container_quick_move_in(chest, &item->inventory, action.slot);
        } else {
            ui_screen_player_click(&item->inventory, action.slot, action.flags, cursor);
        }
    } else if (action.zone == UI_SCREEN_ZONE_BLOCK) {
        item_stack_t *const slot = container_slot(chest, action.slot);
        if (slot == nullptr) {
            return;
        }
        if (shift) {
            (void)container_quick_move_out(chest, &item->inventory, action.slot);
        } else if (secondary) {
            item_stack_click_secondary(slot, cursor);
        } else {
            item_stack_click_primary(slot, cursor);
        }
    }
}
