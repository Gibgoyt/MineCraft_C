# run/src/ui/atlas/tile

**Purpose:** what the Atlas (plan 004 phase 09) knows of the ground, as pure functions on buffers: the pixel, a column's
top, writing a column into its level-0 tile, halving a tile into its parent's quarter, and a tile file's encode and decode.
**Tier:** Borrow (depth 3): pointer and length from the caller, stack locals, nothing allocated.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any. `ui_atlas_column_top` runs on the column job's worker; the rest on whoever holds the tile.

**The pixel** (plan 007 phase 02), one `uint32_t`:

| Bits | What |
|---|---|
| 0 to 15 | RGB565 colour of the top block |
| 16 to 26 | The top's height from sea level + 256, 0 to 1535 (-256 to 1279); 2047 is not generated (`UI_ATLAS_PIXEL_NONE` is `0x07FF0000`) |
| 27 to 31 | Cover depth `d`, 0 to 31: the top's height minus the height of the first block below it that counts and is not `map_cover` (leaves, logs, vines, snow layers, cactus: `config/blocks/README.md`). The ground is `z - d` |

Colour and hill shading read the top, trees and all; contour lines read the ground (`ui_atlas_pixel_ground_z`). Cover
deeper than 31, as under the tallest jungle trees, is stored as 31, so the ground there reads up to the excess too high.

**Public functions:**

- `ui_atlas_pixel_pack` (cover 0), `ui_atlas_pixel_pack_cover`, both holding the height to -256 to 1279 and the cover to
  31; `_z` (the top), `_cover`, `_ground_z` (the top less the cover), `_rgb565`, `_generated`;
  `ui_atlas_pixel_from_version_1` (all inline); `ui_atlas_rgb565_of`, `ui_atlas_rgb_of`.
- `ui_atlas_tile_of`, `ui_atlas_pixel_column_of`, `ui_atlas_pixel_row_of`, `ui_atlas_tile_x_of_column`,
  `ui_atlas_tile_y_of_row`, `ui_atlas_in_tile`: where a block lands. At level L a tile covers 512 × 2^L blocks; a level
  pixel's column is `floor(x / 2^L)` and its row `floor((-1 - y) / 2^L)`, so row 0 of every tile is its north edge.
- `ui_atlas_column_top`: for each of a column's 256 (x, y), walking down from the top section, the first voxel whose
  block's shape is not none (air), cross, crop or a torch (wall torches too), its colour from the table by block id and
  its height `16 × section + z + COLUMN_Z_STORAGE_MIN - MC_WORLD_Z_SEA`. Water and lava count, so the sea is the sea.
  Each section is first made a bit a voxel, set where it is not air, by `simd/bitmask`'s kernel: an all-air section is
  passed over after one look at its 64 words, and in the rest only voxels holding something are looked at (12 µs a
  column in `bench_atlas`, against 98 µs testing every voxel). Where the top is `map_cover` it walks on down, in the same
  pass, to the first block that counts and is not cover, at most 31 layers, and stores the difference as the cover depth;
  air and plants under a canopy are passed over. Cover with nothing under it is 31.
- `ui_atlas_tile_column_put`: the 16 rows of 16 pixels into their place.
- `ui_atlas_tile_halve`, `ui_atlas_pixel_mean`: each parent pixel the rounded mean `(sum + floor(n / 2)) / n`, floored, of
  its generated children, channel by channel, height and cover depth; not generated only when all four are not. So a
  zoomed-out forest's ground is about its mean ground.
- `ui_atlas_tile_encode`, `ui_atlas_tile_decode`, `ui_atlas_decode_name`, `ui_atlas_tile_file_name`: the `.tile` file, a
  64-byte header (`ui_atlas_file_header_t`, magic `MCATLAST`, the world's seed, level, tile position, version 2, both
  checksums FNV-1a 64 by `texture_blob_hash`) then runs of `(pixel, count)` where they are smaller than the raw 1 MiB,
  else raw. Decode refuses any wrong field, a flipped byte, another seed, level or position, and runs that do not cover the
  tile exactly; the caller then treats the tile as never saved. **Version 1** (plan 004 phase 09: the height as `int16_t`
  in bits 16 to 31, `INT16_MIN` not generated) is still read, each pixel turned into today's with cover 0, so worlds
  explored before plan 007 phase 02 keep their maps; a tile is written as version 2 the next time it is dirty.

**May be called by:** `run/src/ui/atlas/` (the store), `run/src/ui/` (the colour table's packing), `run/src/game/` (the
column's top on the worker), tests and benches.
**Depends on:** `tables/blocks` (shapes, `BLOCK_FLAG_MAP_COVER`), `texture/blob` (the hash), `world/column`, `world/section`, `machine`,
`simd/bitmask` (a section's not-air bits), libc.
