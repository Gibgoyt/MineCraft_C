# run/src/ui/screen_inventory

**Purpose:** the inventory screen, with its 36 slots, armor and the grid of two, and what every game screen shares: the road between `main` and `sim`, the item pictures, the window, its slots and what is drawn in them.
**Tier:** Arena. `ui_screens_make` allocates from the arena it is given, once. Everything else allocates nothing, and the tooltip goes into the frame arena through `ui_widget_tooltip`.
**Owns:** nothing. `ui_screens_t` lives in its Owner's table arena.
**Lifetime:** `ui_screens_t` lasts as long as the `ui_t`, and `ui_screens_begin` starts it over for each world. A view lasts until `main` takes a newer one.
**Threads:** two, and the header says which function is whose. `main` draws and sends; `sim` applies and publishes. Nothing is shared but the ring and the triple buffer, each one writer and one reader. See `run/src/ui/README.md`.
**Public functions** (`run/include/ui/screen_inventory/index.h`):

- `ui_screens_make`, `ui_screens_begin`: the pictures, once; nothing up, for a new world.
- `ui_screens_view_take`, `ui_screens_send`, `ui_screen_window`, `ui_screen_hit_test`, `ui_screen_player_places`: `main`'s side.
- `ui_screen_draw_base`, `ui_screen_draw_stack`, `ui_screen_draw_cursor`, `ui_screen_draw_title`, `ui_screen_draw_arrow`: what a screen is drawn with.
- `ui_screens_next`, `ui_screens_view_next`, `ui_screens_view_publish`, `ui_screen_player_click`, `ui_screen_grid_click`: `sim`'s side.
- `ui_screen_inventory_places`, `ui_screen_inventory_view`, `ui_screen_inventory_draw`, `ui_screen_inventory_click`: the inventory screen itself. Each of the other three has these four too.

**May be called by:** `ui`, and its siblings `screen_crafting`, `screen_furnace` and `screen_chest`, which are built on it, and this README is what allows that (`plans/REPO_STRUCTURE.md` section 11).
**Depends on:** `arena`, `assert`, `item`, `texture`, `render/frame` for `render_exchange_t`, `ui/compose`, `ui/widget`, `tables/items`.

## What is on it

The window is 176 by 166 texture pixels, as every screen's is, with the same rows of the player's at the bottom: the hotbar's nine at y 142, under the three others at y 84. Armor runs down the left, head to feet, each slot showing what goes there while it is empty. The grid of two has its output to the right. There is no picture of the player: nothing can draw one yet.

| Click | Slot | Armor | Grid cell | Output |
|---|---|---|---|---|
| Left | `item_stack_click_primary` | `equipment_armor_click` | `crafting_grid_click_primary` | `crafting_grid_take` |
| Right | `item_stack_click_secondary` | the same | `crafting_grid_click_secondary` | the same |
| Shift | armor to its slot, or else across the rows | `equipment_armor_quick_move_out` | to the inventory | `crafting_grid_take_all` |

## The books

`inventory_slot` says that writing to a slot goes past the inventory's books. `ui_screen_player_click` does that, and so keeps them itself: what the pointer holds does not count as held, so going onto the pointer is removed and coming off it is added. `run_ui_screens` holds them to `added - removed == held` through 20000 random clicks on each screen.

## Why a click goes across

All of an inventory is the sim's, as `run/src/item/README.md` says, and `main` draws at a rate of its own. So `main` never reads or writes one. It draws from a whole copy, and a click is three bytes in a ring. The price is a click showing a little later, and the sim looks at the ring in each of its steps and not only each tick, for that reason: `run_ui_screens` prints what it took on the real threads.
