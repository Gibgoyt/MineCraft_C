# run/src/render/entities

**Purpose:** the boxes the sim knows about, on their way to the card: a snapshot from the sim to the gpu thread each tick, and from it each frame's list of which boxes each tile of the picture may see. Also what the player aims at and mines, and the textures of what has no block's faces.
**Tier:** Arena. It takes what it needs from `arena_frame`, from `run/src/render/`, and never touches the heap. On the card it borrows two blocks from the same Owner.
**Owns:** nothing.
**Lifetime:** from `render_open_frame` to `render_close_frame`.
**Threads:** `render_entities_begin` to `render_entities_publish` on the sim, or whichever one thread holds the map. `render_entities_frame` on the `gpu` thread, or the one thread of a run with no threads. Between them only `exchange`, a triple buffer: pointers do not cross, and a slot is one side's until it is published.
**Public functions** (`run/include/render/entities/index.h`):

- `render_entities_init`, with `render_entities_arena_bytes` and `render_entities_lists_bytes` for what it takes. Sends the frame's own textures once. `render_entities_deinit` forgets it all, with no kernel running.
- `render_entities_begin`, `render_entities_add`, `render_entities_add_item_drop`, `render_entities_add_mob_part`, `render_entities_target_set`, `render_entities_preview_set`, `render_entities_publish`: one tick's snapshot.
- `render_entities_preview_show`: main, at any time: the Settings box for the preview of placing.
- `render_entities_crack_frame`: how far along mining is, as a frame of the crack.
- `render_entities_pixels`: which pixels a box can touch on a camera.
- `render_entities_frame`: the newest snapshot to this frame's lists, on the card, and the scene the kernels take.

**May be called by:** `run/src/render/`, `run/src/render/frame/`, `run/src/game/`, and a test.
**Depends on:** `arena`, `log`, `machine`, `texture`, `tables/blocks`, `render/frame` for the exchange, and `gpu_bridge/*`.

## Why it is the way it is

The lists are for this frame's eye and this frame's camera, so the gpu thread makes them right before drawing, and the sim knows nothing of pictures. Each box goes through `render_entities_pixels`: its eight corners, on the picture, plus a margin for the jitter that moves a ray by up to half a pixel. That is the tightest rectangle a camera with no skew gives and it is always large enough. Part of a box behind the eye is cut off first, at each edge that crosses the plane just ahead of it, so that a box beside the head does not list on every tile.

Every list is in one array of links, as `starts` for each tile and a run of box numbers, filled in two passes. So it never grows, costs no pointer, and a tile's list is one contiguous read on the card. The boxes nearer than `RENDER_ENTITIES_NEAR_BLOCKS` go first, so that if `ENTITY_RAYS_LINKS_MAX` ever runs out it is the far boxes that are left out, whole, and `stats.out_of_links` counts them. The same goes for `ENTITY_RAYS_BOXES_MAX` and `stats.out_of_snapshots`.

A box past the fog is sky whatever it is: nothing is sent for it.

**Coordinates are from the eye, in `float64_t` until the last moment.** So a box a kilometre out is as exact as one at the origin, and the kernels never see a large number.

**The crack is the same mark as the outline** because it is one thing a player sees and one setting. Its block is named by its cell, and shade looks for that cell among the surfaces it draws: through water or leaves it shows as it should.

**The outline's numbers come from the sim's target,** which is at most a tick behind the aim. A picture carries the outline of whichever tick the gpu thread had last.

**The preview of placing** (plan 005 phase 10) is the cells and voxels of `player_place_plan_t`, as the sim found them that tick, and whether placing would fail. The frame turns them into cells from the eye's cell and a rectangle of pixels around them all, a whole cell each, as no block's shape reaches out of its cell. It is a setting of its own and not an effect, so turning it off is one relaxed byte, `preview_hidden`, that main writes and the gpu thread reads, and not a change of the frame's features, which would stop the threads. A frame opened again starts with it shown, so `run/src/game/` says it again on every open and reconfigure. `render_entities_t` grew by a snapshot's preview in each slot, and holds up to 29 mobs where it held 30.

**A mob is one box for each part of its body,** leaning by its pitch and with `skin_u`, `skin_v` and `skin_size` saying which share of the mob's skin is on it. `render_entities_init` puts each mob's skin in the frame's own table, and one with none draws in the colour that says so. What the corners of a leaning box reach is worked out in `entities_keep`, so that `render_entities_pixels` still takes a box turned by yaw alone.
