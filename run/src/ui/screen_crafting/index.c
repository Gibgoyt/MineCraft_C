/* See README.md. */
#include "ui/screen_crafting/index.h"

#include <string.h>

enum : int16_t {
    UI_SCREEN_CRAFTING_GRID_X = 30,
    UI_SCREEN_CRAFTING_GRID_Y = 17,
    UI_SCREEN_CRAFTING_ARROW_X = 90,
    UI_SCREEN_CRAFTING_ARROW_Y = 36,
    UI_SCREEN_CRAFTING_OUTPUT_X = 118,
    UI_SCREEN_CRAFTING_OUTPUT_Y = 35,
    UI_SCREEN_CRAFTING_TITLE_X = 30,
    UI_SCREEN_CRAFTING_TITLE_Y = 7,
};

uint32_t ui_screen_crafting_places(ui_screen_place_t *out) {
    uint32_t count = ui_screen_player_places(out);
    for (uint32_t cell = 0u; cell < CRAFTING_SIDE_TABLE * CRAFTING_SIDE_TABLE; cell++) {
        out[count++] = (ui_screen_place_t){
            .x = (int16_t)(UI_SCREEN_CRAFTING_GRID_X + (int32_t)((cell % CRAFTING_SIDE_TABLE) * UI_SCREEN_SLOT)),
            .y = (int16_t)(UI_SCREEN_CRAFTING_GRID_Y + (int32_t)((cell / CRAFTING_SIDE_TABLE) * UI_SCREEN_SLOT)),
            .zone = UI_SCREEN_ZONE_GRID,
            .slot = (uint8_t)cell,
            ._pad0 = {0}};
    }
    out[count++] = (ui_screen_place_t){.x = UI_SCREEN_CRAFTING_OUTPUT_X,
                                       .y = UI_SCREEN_CRAFTING_OUTPUT_Y,
                                       .zone = UI_SCREEN_ZONE_OUTPUT,
                                       .slot = 0u,
                                       ._pad0 = {0}};
    return count;
}

void ui_screen_crafting_view(const item_t *item, ui_screen_view_t *view) {
    memcpy(view->grid, item->crafting_table.cells, sizeof(view->grid));
    view->output = item->crafting_table.output;
}

void ui_screen_crafting_draw(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view) {
    const ui_canvas_t *const canvas = widget->borrowed_canvas;
    const ui_rect_t window = ui_screen_window(canvas->width, canvas->height);
    ui_screen_draw_title(widget, window, UI_SCREEN_CRAFTING_TITLE_X, UI_SCREEN_CRAFTING_TITLE_Y, "Crafting");
    ui_screen_draw_arrow(widget, screens, window, UI_SCREEN_CRAFTING_ARROW_X, UI_SCREEN_CRAFTING_ARROW_Y,
                         item_stack_is_empty(&view->output) ? 0.0f32 : 1.0f32);
}

void ui_screen_crafting_click(item_t *item, ui_screen_action_t action, item_stack_t *cursor) {
    switch ((ui_screen_zone_t)action.zone) {
    case UI_SCREEN_ZONE_PLAYER:
        if ((action.flags & UI_SCREEN_FLAG_SHIFT) != 0u) {
            (void)inventory_quick_move(&item->inventory, action.slot);
        } else {
            ui_screen_player_click(&item->inventory, action.slot, action.flags, cursor);
        }
        break;
    case UI_SCREEN_ZONE_GRID:
    case UI_SCREEN_ZONE_OUTPUT:
        ui_screen_grid_click(&item->crafting_table, &item->inventory, action, cursor);
        break;
    case UI_SCREEN_ZONE_WINDOW:
    case UI_SCREEN_ZONE_OUTSIDE:
    case UI_SCREEN_ZONE_ARMOR:
    case UI_SCREEN_ZONE_BLOCK:
        break;
    }
}
