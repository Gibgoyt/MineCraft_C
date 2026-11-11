# run/src/ui/screen_crafting

**Purpose:** the screen of a crafting table or a station with a grid of its own (plan 005 phase 6: the woodworking and stoneworking benches, the smith table and the leather workbench): the grid of three, its output, and the player's rows. They differ only by their title and whose grid it is, and what that grid can make is its station's. The smith table's also has the alloy slots right of its output (plan 005 phase 11 part B): two inputs above one output, under "Alloy", as `UI_SCREEN_ZONE_BLOCK`'s slots 0 to 2, shown from the view's `block`.
**Tier:** Arena, and it allocates nothing.
**Owns:** nothing. The grid is `item_t.crafting_table` or `item_t.borrowed_crafting_bench`, and the alloy slots `item_t.borrowed_crafting_alloy`, which `ui` passes in.
**Lifetime:** Nothing outlives a call.
**Threads:** `ui_screen_crafting_places` and `ui_screen_crafting_draw` are `main`'s, and `ui_screen_crafting_view` and `ui_screen_crafting_click` the `sim`'s. See `run/src/ui/README.md`.
**Public functions** (`run/include/ui/screen_crafting/index.h`):

- `ui_screen_crafting_places`: where its slots are.
- `ui_screen_crafting_view`: its part of what `main` draws from.
- `ui_screen_crafting_draw`: its title and arrow, on top of `ui_screen_draw_base`.
- `ui_screen_crafting_click`: a click, on the sim. A slot or a cell by the usual two clicks, and with shift a slot goes across the rows, a cell to the inventory and the output `crafting_grid_take_all`.
- `ui_screen_smithing_places`, `ui_screen_smithing_view`, `ui_screen_smithing_draw`, `ui_screen_smithing_click`: the smith table's, the same with its alloy slots. An input takes the usual two clicks, the output gives one making, and with shift an input goes to the inventory and the output `crafting_alloy_take_all`.

**May be called by:** `ui`.
**Depends on:** `item`, and its sibling `ui/screen_inventory`, whose README allows it.

Closing gives the grid back to the inventory, and at the smith table the alloy inputs too. That is in `ui_screen_sim_close`, as all screens close alike.
