/* See run/src/ui/screen_crafting/README.md. */
#ifndef MC_RUN_UI_SCREEN_CRAFTING_INDEX_H
#define MC_RUN_UI_SCREEN_CRAFTING_INDEX_H

#include "item/index.h"
#include "ui/screen_inventory/index.h"

#include <stdint.h>

/* Its places, into out, UI_SCREEN_PLACES_MAX of room. Returns how many. */
uint32_t ui_screen_crafting_places(ui_screen_place_t *out);

/* The smith table's: the crafting places, then its alloy slots as UI_SCREEN_ZONE_BLOCK's 0 to 2, the two inputs and the output. */
uint32_t ui_screen_smithing_places(ui_screen_place_t *out);

/* What is its own, on top of ui_screens_view_next: the grid of three, a table's or a bench's. */
void ui_screen_crafting_view(const crafting_grid_t *grid, ui_screen_view_t *view);

/* The smith table's: the grid, and its alloy slots in the view's block. */
void ui_screen_smithing_view(const crafting_grid_t *grid, const crafting_alloy_t *alloy, ui_screen_view_t *view);

/* What is its own, on top of ui_screen_draw_base, with the name of what it is the screen of. */
void ui_screen_crafting_draw(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view,
                             const char *title);

/* The smith table's: the grid of three as any station's, and beside it the alloy slots' title (plan 005 phase 11 part B). */
void ui_screen_smithing_draw(ui_widget_t *widget, const ui_screens_t *screens, const ui_screen_view_t *view);

/* The sim applies a click: the player's slots and this grid of three. */
void ui_screen_crafting_click(item_t *item, crafting_grid_t *grid, ui_screen_action_t action, item_stack_t *cursor);

/* The smith table's: its alloy slots, and the rest as ui_screen_crafting_click. A click on an input takes or puts as on any cell, and on the
 * output takes one making; with shift an input goes to the inventory and the output as many makings as fit. */
void ui_screen_smithing_click(item_t *item, crafting_grid_t *grid, crafting_alloy_t *alloy, ui_screen_action_t action,
                              item_stack_t *cursor);

#endif
