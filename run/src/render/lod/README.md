# run/src/render/lod

**Purpose:** builds the far field's coarse grids on workers and sends them to the card: each a level of voxels of 2 to 64 blocks, in a table that follows the eye.
**Tier:** Arena. Takes what it holds from the arena its parent gives it, once. Each column's working memory is its worker's Job arena. The levels' blocks on the card are the Owner's.
**Owns:** nothing of its own. In its arena: the jobs, what each has encoded, and each level's host copy of its table.
**Lifetime:** from `render_open_lod` to `render_close_lod`, within one world's frame.
**Threads:** the `gpu` thread for everything here, or whichever one thread a run has. The columns are made on workers, which read the terrain and, through an overlay if one is set, what is saved, and touch nothing but their own job and staging until `done`.
**Public functions** (`run/include/render/lod/index.h`):

- `render_lod_plan`: which levels a world has, for how far it is drawn block by block and how far in all.
- `render_lod_table_bytes`, `render_lod_pool_bytes`, `render_lod_arena_bytes`: sizes for the Owner.
- `render_lod_cells_from_column`: one sampled column to its coarse voxels, on its own for a test to hold to paper.
- `render_lod_sample_terrain`: a sample of the world's own ground.
- `render_lod_overlay_set`: what is written over each sample, with its own scratch from the job's arena.
- `render_lod_column_changed`, `render_lod_all_changed`: a column is made again, in each level, with the old one shown until the new one has come. One a worker has begun goes round again when it is done, as it may already have read past what changed.
- `render_lod_init`, `render_lod_deinit`: a pair.
- `render_lod_frame`: one frame's worth: follows the eye, sends what is finished, up to a budget, and asks for what is next. Never waits.
- `render_lod_idle`: whether all that is wanted is on the card.
- `render_lod_scene`: what this frame's kernels read.
- `render_lod_column_of`: which of a level's columns a place is in.

**May be called by:** `run/src/render/`, which starts and stops it, `run/src/game/`, which calls `render_lod_frame`, and `run/src/render/frame/`, which calls `render_lod_scene`.
**Depends on:** `arena`, `assert`, `log`, `job`, `time`, `terrain`, `tables/blocks`, `world/column`, `render/encode`, `render/upload` for its budget, `gpu_bridge/world_buffers`, `gpu_bridge/lod`, `gpu_bridge/shade` for its one light value.

## Why it is the way it is

**A level is a world.** A coarse column is 16 x 16 voxels and up to 768 high, in the same sections, table and slab pool as the blocks', so `render_encode_section` writes it, `world_buffers` holds it, `traverse_trace` walks it, and its uniform sections and masks do for it what they do for the world. Only the meaning of a voxel is different: it is `1 << shift` blocks each way. A section of air costs nothing and buried stone 24 bytes, and a level costs what its surface costs.

**One sample for each voxel's column.** One column of blocks at a voxel's middle, 256 of them for a coarse column, by the stages of `terrain_generate_box` that show from far off: no caves or ores, and what grows only for voxels of up to `1 << RENDER_LOD_FEATURES_SHIFT_MAX` blocks. Nothing about the terrain is written for this: a voxel is a pure function of the terrain and its place, so any worker gets the same bytes, and the far field is the world's own ground and not something like it. It also means one tree in a voxel of 64 can be sampled or not. That is what the half rule in `render_lod_cells_from_column` is for: a voxel is a block if at least half of what the sample has in its height is, so a level's ground is at the height nearest the truth, and thin things go. Since 256 blocks below `z = 0` is a power of two, the sea's surface is on a voxel's edge at every level.

**A level never shows a hole.** Rays are let into a square around the eye's column with every column in it built, `shown_side` wide, and past that they go to the next level out, which is why a level that is still filling makes the picture coarser and not wrong. The table stays `RENDER_LOD_MARGIN_COLUMNS` wider than that square each way, so the columns an eye steps towards are built before it gets there and what is shown does not shrink when it moves: only the one new row is asked for.

**Scrolling.** As in the world's map, a column's slot depends only on its coordinates modulo the table's side, so a move touches what left and what came. What left goes back to the pool at once. A column on a worker when the window moves is dropped when it finishes if it has left the window, and otherwise still goes where it was going.

**Order.** The coarsest level first, nearest column first: the horizon comes first, and finer levels sharpen what is already there. With `render_lod_frame` on the gpu thread nothing in the sim knows about any of this, and the map is not touched.

**Budget.** What one frame sends is held to `RENDER_LOD_FRAME_BYTES`. A column that finds no room in its pool is given up on, once and with a warning, until it leaves the window: what is shown stops at it.

## What it does not do

It draws no edits. The far field is what terrain generates, and a player's digging is only drawn by what full detail holds, which does not outlive leaving a column until Stage 16.1 saves anything. That stage is where a far column should look in the save first.
