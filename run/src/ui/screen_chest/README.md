# run/src/ui/screen_chest

**Purpose:** the screen of a chest: its 27 slots and the player's rows.
**Tier:** Arena, and it allocates nothing.
**Owns:** nothing. The chest is in `item_t.chests`, looked up by its cell for each view and each click, since a chest moves when another one goes.
**Lifetime:** Nothing outlives a call.
**Threads:** `ui_screen_chest_places` and `ui_screen_chest_draw` are `main`'s, and `ui_screen_chest_view` and `ui_screen_chest_click` the `sim`'s. See `run/src/ui/README.md`.
**Public functions** (`run/include/ui/screen_chest/index.h`):

- `ui_screen_chest_places`: where its slots are.
- `ui_screen_chest_view`: its part of what `main` draws from.
- `ui_screen_chest_draw`: its title, on top of `ui_screen_draw_base`.
- `ui_screen_chest_click`: a click, on the sim. The usual two clicks on a slot either way, and with shift `container_quick_move_in` or `container_quick_move_out`.

**May be called by:** `ui`.
**Depends on:** `item`, and its sibling `ui/screen_inventory`, whose README allows it.

A chest broken with its screen up takes the screen with it, and spills as any other: `run/src/game/` looks at the block each tick. No double chests: the plan has none.
