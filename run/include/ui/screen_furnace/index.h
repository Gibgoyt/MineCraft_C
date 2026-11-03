/* See run/src/ui/screen_furnace/README.md. */
#ifndef MC_RUN_UI_SCREEN_FURNACE_INDEX_H
#define MC_RUN_UI_SCREEN_FURNACE_INDEX_H

#include "item/index.h"
#include "ui/screen_inventory/index.h"

#include <stdint.h>

/* Its places, into out, UI_SCREEN_PLACES_MAX of room. Returns how many. */
uint32_t ui_screen_furnace_places(ui_screen_place_t *out);

/* What is its own, on top of ui_screens_view_next: three slots and two gauges. The furnace is only read. */
void ui_screen_furnace_view(const smelting_furnace_t *furnace, ui_screen_view_t *view);

/* What is its own, on top of ui_screen_draw_base. */
void ui_screen_furnace_draw(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view);

/* The sim applies a click: the player's slots and this furnace's. */
void ui_screen_furnace_click(item_t *item, smelting_furnace_t *furnace, ui_screen_action_t action,
                             item_stack_t *cursor);

#endif
