# run/src/ui/screen_furnace

**Purpose:** the screen of a furnace: input, fuel, output, the two gauges, and the player's rows.
**Tier:** Arena, and it allocates nothing.
**Owns:** nothing. The furnace is in `item_t.furnaces`, looked up by its cell for each view and each click, since a furnace moves when another one goes.
**Lifetime:** Nothing outlives a call.
**Threads:** `ui_screen_furnace_places` and `ui_screen_furnace_draw` are `main`'s, and `ui_screen_furnace_view` and `ui_screen_furnace_click` the `sim`'s. See `run/src/ui/README.md`.
**Public functions** (`run/include/ui/screen_furnace/index.h`):

- `ui_screen_furnace_places`: where its slots are.
- `ui_screen_furnace_view`: its part of what `main` draws from, with `smelting_cook_fraction` and `smelting_burn_fraction`.
- `ui_screen_furnace_draw`: its title; the arrow filling from the left as it cooks; the flame falling as it burns.
- `ui_screen_furnace_click`: a click, on the sim: `smelting_click` for its slots. With shift, a slot goes by `smelting_quick_move_in`, or across the rows if it neither smelts nor burns, and a furnace's slot by `smelting_quick_move_out`.

**May be called by:** `ui`.
**Depends on:** `item`, and its sibling `ui/screen_inventory`, whose README allows it.

A furnace goes on burning with its screen closed, and with it open: `item_world_tick` does that with no idea a screen exists, and the gauges only read it.
