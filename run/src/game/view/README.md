# run/src/game/view

**Purpose:** what a `game_config_t` comes to for what is drawn: how far block by block, which far levels, what fog, what size and which effects, and the top and floor of plan 008 phase 05's resolution ladder. Not in the plan's list: `game` is at the size where a part must be split off, and this has no state.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept.
**Threads:** any. `run/src/game/` calls it on `main`.
**Public functions** (`run/include/game/view/index.h`):

- `game_view_table_chunks`: how far the map must reach for a config.
- `game_view_work_out`: a `game_view_t` from a config and a table. Its `width` and `height` are the config's: the pictures are always the display's size. `scale_percent` is the render scale, the ladder's top, and `floor_percent` the dynamic floor (0 off).
- `game_view_pyramid`: plan 009 phase 03: the pyramid's levels for a config and its view, banded by the tuning file's pixel error and near guarantee, the field of view and the picture's height at the render scale (`render_lod_bands`, `render_lod_plan_by_bands`), as before plan 008 phase 05. Nothing draws them yet.
- `game_view_fog_end`: the fog for a reach.
- `game_view_half_fov_tangent`: a field of view as what the camera holds.
- `game_view_look_scaled`: mouse counts at a sensitivity, with the rest kept.

**May be called by:** `run/src/game/`.
**Depends on:** `game`'s header for its types, `render/lod` for the plan, `render/frame` for the camera's constants, `gpu_bridge/shade` for the effects' bits.

`game_view_bring_up`, the CPU's nearest-pixel bring-up of a picture drawn at a render scale under 100, went in plan 008 phase 05. The frame is now opened at the display's size and the render scale is the top rung of the resolution ladder, drawn smaller and brought up bilinear on the card (`post_upscale`, `cuda/src/post/README.md`), which the ladder's lower rungs need anyway. A render scale under 100 therefore looks filtered, not blocky, and costs main nothing; the picture drawn is the same one as before, to the byte (`render_feature_resolution`).
