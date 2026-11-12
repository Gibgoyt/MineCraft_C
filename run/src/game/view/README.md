# run/src/game/view

**Purpose:** what a `game_config_t` comes to for what is drawn: how far block by block, which far levels, what fog, what size and which effects; and bringing a picture drawn smaller up to the display's size. Not in the plan's list: `game` is at the size where a part must be split off, and this has no state.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept.
**Threads:** any. `run/src/game/` calls it on `main`.
**Public functions** (`run/include/game/view/index.h`):

- `game_view_table_chunks`: how far the map must reach for a config.
- `game_view_work_out`: a `game_view_t` from a config and a table.
- `game_view_pyramid`: plan 009 phase 03: the pyramid's levels for a config and its view, banded by the tuning file's pixel error and near guarantee, the field of view and the picture's height (`render_lod_bands`, `render_lod_plan_by_bands`). Nothing draws them yet.
- `game_view_fog_end`: the fog for a reach.
- `game_view_half_fov_tangent`: a field of view as what the camera holds.
- `game_view_look_scaled`: mouse counts at a sensitivity, with the rest kept.
- `game_view_bring_up`: a smaller picture to a larger, by the nearest pixel.

**May be called by:** `run/src/game/`.
**Depends on:** `game`'s header for its types, `render/lod` for the plan, `render/frame` for the camera's constants, `gpu_bridge/shade` for the effects' bits.

`game_view_bring_up` is scalar on purpose. Nearest pixel at 1080p costs a few milliseconds at most on `main`, only with a render scale under 100, and half of its rows are copies. Two loops of `uint32_t` are not worth a kernel until a measurement says otherwise.
