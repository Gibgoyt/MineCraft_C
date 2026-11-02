# run/src/world/map

**Purpose:** the toroidal table of columns around the player, and which columns stream in and out by distance.
**Tier:** Arena (depth 2): `world_map_init` takes its tables once from an `arena_t *` its Owner gives. Column arrays come from the `section_pool_t` it is given.
**Owns:** nothing. It holds its tables as `borrowed_` from the arena, for as long as the Owner keeps that.
**Lifetime:** World.
**Threads:** one at a time, the one holding the section pool, which is `sim`. Terrain on workers builds elsewhere and brings the result to that thread, as `run/src/world/section/README.md` says. Others read through `world/edit`'s snapshots, never from here.
**Public functions** (`run/include/world/map/index.h`):

- `world_map_radius_for`: the largest table a number of columns can hold.
- `world_map_init`, `world_map_deinit`: the tables, and returning every live column's arrays.
- `world_map_slot_in_window`, `world_map_slot_of`: a chunk's slot, with no division.
- `world_map_column_at`: the live column at a chunk, or `nullptr`.
- `world_map_set_center`, `world_map_set_view_radius`: move the window, and how far columns are wanted.
- `world_map_stream_take`, `world_map_stream_commit`, `world_map_stream_cancel`: requests for the nearest missing columns, and what comes back.
- `world_map_stream_leaving`, `world_map_unload`: columns out of range, and ending one.
- `world_map_block_get`, `world_map_light_get`: one voxel at world coordinates, or why not.

**May be called by:** `run/src/world/`, `world/edit`, and later `game`, `terrain`, `physics`, `save`.
**Depends on:** `arena`, `assert`, and its siblings `world/column` and `world/section`, whose types it stores.

## Toroidal

A chunk's slot is its coordinates modulo the side of the table, so a column never moves in memory, and moving the center touches only what changes. Inside the window of `2 * table_radius + 1` chunks each way, every chunk has its own slot, so nothing wanted is ever out of place. A slot can still hold a column from a window ago, which `world_map_column_at` tells apart by the coordinates stored with it.

## Streaming

Nothing here builds a column. It says what is wanted, nearest first by circular distance between chunks, and hands the rest to whoever does:

1. `world_map_stream_take` gives requests, as many as the caller has room for. Each place is then `PENDING`.
2. `world_map_stream_commit` turns a request that came back into an empty live column, ready to fill, or returns `nullptr` if the center has moved on, so that a slow job's result is simply dropped. A request carries the slot's generation, so an old one is never taken for a new occupant.
3. `world_map_stream_cancel` is for one that will never come back. If still wanted, it will be offered again.
4. `world_map_stream_leaving` offers each live column out of range until it is unloaded, so that the caller can save it. It stops offering one the center has come back to. `world_map_unload` ends it, and a place it held is offered again.

Columns stay `WORLD_MAP_KEEP_EXTRA` chunks past the view radius, so that walking to and fro over one line does not stream the same ones out and in. The table caps that: nothing outside the window is kept, since its slot belongs to someone else.

What each costs: `set_center` one look at each live or pending column. `stream_take` walks an offset table sorted by distance, with a cursor, so a whole table's walk is shared over every call until the next change. `slot_of`, `column_at` and reads are a few compares and adds, because they run per voxel. None of these allocates after init: `run_world_map` holds the heap's counters flat.

## Memory

`WORLD_MAP_BYTES_PER_SLOT` is 36 bytes for each slot beside the 1160 of its `column_t`. `borrowed_columns` is not touched until a column streams in, so a large table costs little where nothing is loaded. The other tables are touched by init, in proportion to the table: so ask for the radius you mean to use.
