/* See run/src/ui/screen_chest/README.md. */
#ifndef MC_RUN_UI_SCREEN_CHEST_INDEX_H
#define MC_RUN_UI_SCREEN_CHEST_INDEX_H

#include "item/index.h"
#include "ui/screen_inventory/index.h"

#include <stdint.h>

/* Its places, into out, UI_SCREEN_PLACES_MAX of room. Returns how many. */
uint32_t ui_screen_chest_places(ui_screen_place_t *out);

/* What is its own, on top of ui_screens_view_next: 27 slots. The chest is only read. */
void ui_screen_chest_view(const container_chest_t *chest, ui_screen_view_t *view);

/* What is its own, on top of ui_screen_draw_base. */
void ui_screen_chest_draw(ui_widget_t *widget);

/* The sim applies a click: the player's slots and this chest's. */
void ui_screen_chest_click(item_t *item, container_chest_t *chest, ui_screen_action_t action, item_stack_t *cursor);

#endif
