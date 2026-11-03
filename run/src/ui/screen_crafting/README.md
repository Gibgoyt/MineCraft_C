# run/src/ui/screen_crafting

**Purpose:** the screen of a crafting table: the grid of three, its output, and the player's rows.
**Tier:** Arena, and it allocates nothing.
**Owns:** nothing. The grid is `item_t.crafting_table`.
**Lifetime:** Nothing outlives a call.
**Threads:** `ui_screen_crafting_places` and `ui_screen_crafting_draw` are `main`'s, and `ui_screen_crafting_view` and `ui_screen_crafting_click` the `sim`'s. See `run/src/ui/README.md`.
**Public functions** (`run/include/ui/screen_crafting/index.h`):

- `ui_screen_crafting_places`: where its slots are.
- `ui_screen_crafting_view`: its part of what `main` draws from.
- `ui_screen_crafting_draw`: its title and arrow, on top of `ui_screen_draw_base`.
- `ui_screen_crafting_click`: a click, on the sim. A slot or a cell by the usual two clicks, and with shift a slot goes across the rows, a cell to the inventory and the output `crafting_grid_take_all`.

**May be called by:** `ui`.
**Depends on:** `item`, and its sibling `ui/screen_inventory`, whose README allows it.

Closing gives the grid back to the inventory. That is in `ui_screen_sim_close`, as all screens close alike.
