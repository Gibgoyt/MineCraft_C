/* See README.md. */
#include "ui/screen_furnace/index.h"

#include <string.h>

enum : int16_t {
    UI_SCREEN_FURNACE_INPUT_X = 55,
    UI_SCREEN_FURNACE_INPUT_Y = 16,
    UI_SCREEN_FURNACE_FUEL_X = 55,
    UI_SCREEN_FURNACE_FUEL_Y = 52,
    UI_SCREEN_FURNACE_OUTPUT_X = 115,
    UI_SCREEN_FURNACE_OUTPUT_Y = 34,
    UI_SCREEN_FURNACE_FIRE_X = 56,
    UI_SCREEN_FURNACE_FIRE_Y = 35,
    UI_SCREEN_FURNACE_ARROW_X = 83,
    UI_SCREEN_FURNACE_ARROW_Y = 35,
    UI_SCREEN_FURNACE_TITLE_X = 8,
    UI_SCREEN_FURNACE_TITLE_Y = 7,
};

uint32_t ui_screen_furnace_places(ui_screen_place_t *out) {
    uint32_t count = ui_screen_player_places(out);
    static const int16_t where[SMELTING_SLOTS][2] = {
        [SMELTING_SLOT_INPUT] = {UI_SCREEN_FURNACE_INPUT_X, UI_SCREEN_FURNACE_INPUT_Y},
        [SMELTING_SLOT_FUEL] = {UI_SCREEN_FURNACE_FUEL_X, UI_SCREEN_FURNACE_FUEL_Y},
        [SMELTING_SLOT_OUTPUT] = {UI_SCREEN_FURNACE_OUTPUT_X, UI_SCREEN_FURNACE_OUTPUT_Y},
    };
    for (uint32_t slot = 0u; slot < SMELTING_SLOTS; slot++) {
        out[count++] = (ui_screen_place_t){.x = where[slot][0],
                                           .y = where[slot][1],
                                           .zone = UI_SCREEN_ZONE_BLOCK,
                                           .slot = (uint8_t)slot,
                                           ._pad0 = {0}};
    }
    return count;
}

void ui_screen_furnace_view(const smelting_furnace_t *furnace, ui_screen_view_t *view) {
    memcpy(view->block, furnace->slots, sizeof(furnace->slots));
    view->cook = smelting_cook_fraction(furnace);
    view->burn = smelting_burn_fraction(furnace);
}

void ui_screen_furnace_draw(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view) {
    ui_canvas_t *const canvas = widget->borrowed_canvas;
    const ui_rect_t window = ui_screen_window(canvas->width, canvas->height);
    const int32_t scale = (int32_t)widget->scale;
    ui_screen_draw_title(widget, window, UI_SCREEN_FURNACE_TITLE_X, UI_SCREEN_FURNACE_TITLE_Y, "Furnace");
    ui_screen_draw_arrow(widget, screens, window, UI_SCREEN_FURNACE_ARROW_X, UI_SCREEN_FURNACE_ARROW_Y, view->cook);
    const ui_rect_t fire = {window.x + UI_SCREEN_FURNACE_FIRE_X * scale, window.y + UI_SCREEN_FURNACE_FIRE_Y * scale,
                            (int32_t)UI_SCREEN_ICON * scale, (int32_t)UI_SCREEN_ICON * scale};
    ui_compose_image_whole(canvas, &screens->fire_empty, fire);
    /* The flame is as tall as what is left to burn, from the bottom up. */
    const int32_t lit = (int32_t)(view->burn * (float32_t)fire.height);
    if (lit > 0) {
        const ui_rect_t shown = {fire.x, fire.y + fire.height - lit, fire.width, lit};
        const ui_rect_t outer = ui_canvas_clip_set(canvas, ui_rect_intersect(shown, canvas->clip));
        ui_compose_image_whole(canvas, &screens->fire_full, fire);
        (void)ui_canvas_clip_set(canvas, outer);
    }
}

void ui_screen_furnace_click(item_t *item, smelting_furnace_t *furnace, ui_screen_action_t action,
                             item_stack_t *cursor) {
    const bool shift = (action.flags & UI_SCREEN_FLAG_SHIFT) != 0u;
    if (action.zone == UI_SCREEN_ZONE_PLAYER) {
        if (!shift) {
            ui_screen_player_click(&item->inventory, action.slot, action.flags, cursor);
        } else if (smelting_quick_move_in(furnace, &item->inventory, action.slot) == 0u) {
            /* What neither smelts nor burns goes across the rows, as on every other screen. */
            (void)inventory_quick_move(&item->inventory, action.slot);
        }
    } else if (action.zone == UI_SCREEN_ZONE_BLOCK && action.slot < SMELTING_SLOTS) {
        if (shift) {
            (void)smelting_quick_move_out(furnace, &item->inventory, action.slot);
        } else {
            smelting_click(furnace, action.slot, cursor, (action.flags & UI_SCREEN_FLAG_SECONDARY) != 0u);
        }
    }
}
