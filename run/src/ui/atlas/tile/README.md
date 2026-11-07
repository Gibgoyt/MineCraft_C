# run/src/ui/atlas/tile

**Purpose:** what the Atlas (plan 004 phase 09) knows of the ground, as pure functions on buffers: the pixel (RGB565 colour
in bits 0 to 15, the top's height from sea level as `int16_t` in bits 16 to 31, `INT16_MIN` for not generated), a
column's top, writing a column into its level-0 tile, halving a tile into its parent's quarter, and a tile file's encode
and decode.
**Tier:** Borrow (depth 3): pointer and length from the caller, stack locals, nothing allocated.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any. `ui_atlas_column_top` runs on the column job's worker; the rest on whoever holds the tile.

**Public functions:**

- `ui_atlas_pixel_pack`, `_z`, `_rgb565`, `_generated` (inline), `ui_atlas_rgb565_of`, `ui_atlas_rgb_of`.
- `ui_atlas_tile_of`, `ui_atlas_pixel_column_of`, `ui_atlas_pixel_row_of`, `ui_atlas_tile_x_of_column`,
  `ui_atlas_tile_y_of_row`, `ui_atlas_in_tile`: where a block lands. At level L a tile covers 512 × 2^L blocks; a level
  pixel's column is `floor(x / 2^L)` and its row `floor((-1 - y) / 2^L)`, so row 0 of every tile is its north edge.
- `ui_atlas_column_top`: for each of a column's 256 (x, y), walking down from the top section, the first voxel whose
  block's shape is not none (air), cross, crop or a torch (wall torches too), its colour from the table by block id and
  its height `16 × section + z + COLUMN_Z_STORAGE_MIN - MC_WORLD_Z_SEA`. Water and lava count, so the sea is the sea.
- `ui_atlas_tile_column_put`: the 16 rows of 16 pixels into their place.
- `ui_atlas_tile_halve`, `ui_atlas_pixel_mean`: each parent pixel the rounded mean `(sum + floor(n / 2)) / n`, floored, of
  its generated children, channel by channel and height; not generated only when all four are not.
- `ui_atlas_tile_encode`, `ui_atlas_tile_decode`, `ui_atlas_decode_name`, `ui_atlas_tile_file_name`: the `.tile` file, a
  64-byte header (`ui_atlas_file_header_t`, magic `MCATLAST`, the world's seed, level, tile position, version 1, both
  checksums FNV-1a 64 by `texture_blob_hash`) then runs of `(pixel, count)` where they are smaller than the raw 1 MiB,
  else raw. Decode refuses any wrong field, a flipped byte, another seed, level or position, and runs that do not cover the
  tile exactly; the caller then treats the tile as never saved.

**May be called by:** `run/src/ui/atlas/` (the store), `run/src/ui/` (the colour table's packing), `run/src/game/` (the
column's top on the worker), tests and benches.
**Depends on:** `tables/blocks` (shapes), `texture/blob` (the hash), `world/column`, `world/section`, `machine`, libc.
