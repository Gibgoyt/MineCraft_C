# run/src/render/octant

**Purpose:** the octant store of plan 009 phase 03: every loaded section reduced to its eighth of the pyramid's level 1, and every level 1 section made from them reduced to its eighth of level 2, so that levels 1 and 2 are made from the blocks where they are loaded and follow an edit at once.
**Tier:** Arena. Takes what it holds from the arena its parent gives it, once, at `render_open`.
**Owns:** nothing of its own. In its arena: a table per level, slot for slot as that level's own table, and the blocks of the octants that are not one block all through.
**Lifetime:** World: from `render_open` to `render_close`.
**Threads:** the `gpu` thread for everything, or whichever one thread a run has: the upload writes level 0's octants as it sends each section, and the pyramid (`render/lod`) reads them and writes the levels above. `render_octant_watch_arm` is a test's, with those threads stopped, and the watch's atomics are read by it while they run.
**Public functions** (`run/include/render/octant/index.h`):

- `render_octant_arena_bytes`, `render_octant_init`, `render_octant_deinit`: the store for a map table of so many slots. `render_octant_bytes_used`: what it holds now.
- `render_octant_column_set`, `render_octant_column_clear`, `render_octant_section_set`: level 0 as the upload changes the card's table and sends sections, and any level's section as the pyramid makes it.
- `render_octant_changed_next`, `render_octant_changed_lost_take`: the level 0 columns that changed, for the pyramid to make its level 1 column over them again.
- `render_octant_levels_forget`: every level above 0 forgotten, when the pyramid's levels change.
- `render_octant_column_ready`, `render_octant_column_assemble`, `render_octant_level_column_set`: whether the four columns a column of the level above stands on are all here, the sections of that column from them, and a level above 0 starting a column's octants.
- `render_octant_watch_arm`, `render_octant_watch_note`: a test's eye on one place, as each level is sent.

**May be called by:** `render`, which opens it, and its siblings `render/upload` and `render/lod`.
**Depends on:** `arena`, `assert`, `render/encode` for the octant and its rule, `gpu_bridge/world_buffers` for the tables it follows, `world/section` for the order of a section's voxels.

## Why it is the way it is

**An octant is a section's eighth of the section above.** A level 1 voxel is 2 x 2 x 2 blocks, so a level 1 section of 16 x 16 x 16 such voxels stands on 2 x 2 x 2 level 0 sections: 2 x 2 chunks and two sections of height. `render_encode_section_octant` reduces one section to its 8 x 8 x 8 by the half rule and the grass-top rule (`run/src/render/encode/README.md`), on the worker that encodes the section already, from blocks it already holds. Here they are only put together.

**Kept, not worked out again.** A level 1 column stands on four chunks. When one block of one of them changes, only that section's octant is new; the other 7 of each level 1 section are here already, so the column is made again from what is stored, on the gpu thread, without the blocks: those are on the card and the sim's.

**Handles.** Most sections are buried stone or open air, and so are their octants. Air is 0 and one block all through is 4 bytes, `RENDER_OCTANT_UNIFORM | light << 16 | block`; only the rest take a block of 1536 bytes, 512 blocks and 512 lights. Blocks come off a list of those given back, or from the front of the arena, so pages past the most ever used are never touched. `compile/config/render/budget/index.yaml`'s `octant_store_mib` caps them: a section that finds none leaves its column not complete, and the pyramid makes that level 1 column from terrain samples, as it would outside the blocks, until the column loads again.

**Generations say what is current.** A level 0 column is used only if the card's table holds it at the very generation stored here, which is the map's load: a column loaded again starts all air, as the card's does (`world_edit_snapshot_column_t.generation`), and its sections come after it. Above level 0 the generation is the one the pyramid's level gave the column when it was sent, so a level 1 column made again from terrain is no longer one level 2 may be made from. `render_octant_levels_forget` drops the levels above 0 when the pyramid's levels change, as their generations start again.

**What changed.** Each level 0 column set, cleared or given a section is pushed on a list, its old place too when a slot changes hands. The pyramid takes the list each frame and marks the level 1 column over each. The list holds twice the map's slots; past that, one flag says everything may have changed.

## What it does not do

It holds no blocks of its own and makes nothing the card sees: `render/lod` encodes and sends what it assembles. It does not sample terrain.
