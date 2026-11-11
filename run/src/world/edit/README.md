# run/src/world/edit

**Purpose:** every change to a live column: the edit log, dirty flags per section and per consumer, and the snapshots the renderer reads.
**Tier:** Arena (depth 2): `world_edit_init` and `world_edit_snapshot_init` take once from an `arena_t *` the Owner gives.
**Owns:** nothing. It holds its tables as `borrowed_` from the arena.
**Lifetime:** World.
**Threads:** `sim`, which holds the map, for everything but one function. `gpu` calls `world_edit_snapshot_acquire`, and reads the view it returns.
**Public functions** (`run/include/world/edit/index.h`):

- `world_edit_init`, `world_edit_deinit`: the log, the flags and their queues.
- `world_edit_block_set`, `world_edit_light_set`: one voxel, logged and flagged, or why not.
- `world_edit_sections_mark`: for whoever wrote a live column's sections directly.
- `world_edit_column_filled`: for a column just built: flags without logging.
- `world_edit_log_position`, `world_edit_log_read`: the log, each reader with its own cursor.
- `world_edit_dirty_next`, `world_edit_dirty_restore`, `world_edit_dirty_peek`, `world_edit_dirty_clear`: a consumer's flags.
- `world_edit_column_revision`, `world_edit_column_top_z`: what is known of a live column.
- `world_edit_snapshot_init`, `world_edit_snapshot_deinit`, `world_edit_snapshot_publish`, `world_edit_snapshot_acquire`: the snapshots.
- `world_edit_view_column_at`, `world_edit_view_block`, `world_edit_view_light`: reading a view.

**May be called by:** `run/src/world/`, `world/light`, `world/tick`, `entity/explosion`, `item`, `item/equipment`, `mob/creeper`, `mob/path`, `player/interaction`, `render` and its `encode` and `upload`, `save/autosave`, `save/world`, and `game`.
**Depends on:** `arena`, `machine`, its siblings `world/map` and `world/column`, and through them `world/section`, and the generated `tables/blocks/`, for air.

## The log

A ring of `{x, y, z, before, after}`, one per block that really changed. A reader keeps its own cursor and gets records oldest first. If it falls more than the ring behind, `world_edit_log_read` says how many it lost, and it must look at the world itself. Light does not go in: a pass writes too much of it to be worth reading back. Writing what is already there logs and flags nothing. `before` and `after` are whole voxels, id and state (`run/include/types/block_state/`), and "already there" means the whole voxel: opening a door, the same id, is a change, as is a block's id alone over the same block turned (plan 005 phase 2).

## Dirty flags

One bit per section of a column, one mask per consumer, so that no consumer clears another's. Each consumer has a queue of columns in the order they got dirty, in which a column is at most once, so a queue as long as the table cannot overflow.

| Change | RENDER | SAVE | LIGHT |
|---|---|---|---|
| `world_edit_block_set` | yes | yes | yes |
| `world_edit_light_set` | yes | no: a save rebuilds light | no: the light pass wrote it |
| `world_edit_column_filled` | sections not all air under open sky | no: generation repeats from the seed | those same sections |
| `world_edit_sections_mark` | as the caller says | as the caller says | as the caller says |

Flags belong to one load of a column. The map knows nothing of this module, so each slot here stores the generation it describes, and a new one wipes the flags when next looked at. An unloaded column's flags are gone with it: `world_edit_dirty_peek` on what `world_map_stream_leaving` offers is how a save gets to look first.

`world_edit_column_top_z` is the top of the highest section with a block, for the renderer's sky rays. It is never below the true top. It can be above: a section in direct form, 16 bits a voxel, has no counts, so it does not know it has gone back to all air until `section_compact`.

## Snapshots

`sim` must write while `gpu` reads, and a slow tick must never stall a frame, nor a slow frame a tick. So `gpu` never touches the map or a section. It reads a **view**: a copy of the window, a record per slot, and the sections that have changed.

There are three views, and one atomic word they swap through (a triple buffer): one being written, one newest and waiting, one being read. Neither side ever waits.

- `world_edit_snapshot_publish` fills the back view and swaps it in. Nothing changed since the last means no view at all, so an idle world costs a few compares.
- `world_edit_snapshot_acquire` swaps out the newest view. It stays whole until the next acquire that returns one.

Sections are what make a plain triple buffer not enough: a view has only what was dirty since the last publish, and the publisher may replace a view nobody read. So a replaced view's sections go back to being dirty, and the next publish sends them, newer. A section can come late, and cannot be lost. `run_world_edit` holds this with a reader that skips views at random and a mirror that must match the world at the end.

A view's table is whole, so what it says about columns is always the latest. To read one:

1. For each slot of the table, a generation not seen before means the old column is gone, and this one starts as all air under open sky.
2. Then apply the view's sections. Each carries the generation it belongs to.

One publish carries at most `section_capacity` sections, 1024 from `world_open`. The rest stay dirty, and a column cut short goes behind the others.

**Cost**, from `snapshot_cost_on_this_machine` in a release build, one run on 2026-11-02: a publish with one section is 0.020 ms at a table of radius 32 and 0.239 ms at radius 128, and it grows with the table, because the table is cleared and written whole. Eight sections add about 0.02 ms. Radius 512 was not timed.

## Scope

This publishes the world, and only that. A camera, entities and whatever else a frame needs are `game`'s to publish beside it.
