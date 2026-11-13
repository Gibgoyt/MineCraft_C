# bench/lod

**Purpose:** times one far-field column, as `run/src/render/lod/` builds it on a worker, at every level from voxels of 2 to 64 blocks, on Ahmed's seed, against plan 005 phase 14's target of 50 ms of worker time a column.
**Tier:** none; benchmarks are not tiered.
**Owns:** one terrain, in static storage, released before it exits.
**Lifetime:** Process.
**Threads:** main only: a column is one worker's job.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_lod`, so no build shares the CPU. Exit status 0, 1 if a level's slowest column missed the target, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `render/lod`, `render/octant`, `render/encode`, `heap`, `arena`, `terrain`, `time`, `world/column`, `world/section`, and the generated `tables/blocks/`.

Only a release build's numbers mean anything: debug is at `-O0`.

A column here is a level's own: 16 by 16 voxels, one sample of the ground each, at six places round the spawn and further out. Each place keeps its best of 2 rounds. Each level is timed two ways: **before**, as the far field sampled until plan 005 phase 14, each line walking the 9 chunks of what grows round it again, and **now**, `render_lod_sample_terrain`, with those chunks kept in a cache for the whole column. "Same tops" compares each line's top voxel and its block both ways, and "chunks loaded" is how many the cache loaded for a column.

## Results

### 2026-11-10, plan 005 phase 14: the cache

Release build, through the build lock, on the code of this phase's commit. Milliseconds of one thread for one column, mean and worst of the 6 places:

| Level | Before, mean | Before, worst | Now, mean | Now, worst | Chunks loaded |
|---|---|---|---|---|---|
| voxels of 2 | 378.7 | 439.7 | 13.8 | 16.1 | 16 |
| voxels of 4 | 384.5 | 462.3 | 24.0 | 31.2 | 36 |
| voxels of 8 | 374.5 | 428.9 | 58.6 | 71.2 | 100 |
| voxels of 8, growing nothing (now) | 8.6 | 9.2 | 8.6 | 9.2 | 0 |
| voxels of 16 | 7.7 | 8.2 | 7.7 | 8.2 | 0 |
| voxels of 32 | 7.7 | 8.3 | 7.7 | 8.0 | 0 |
| voxels of 64 | 7.9 | 8.1 | 7.8 | 8.3 | 0 |

The same tops at every level. With trees, voxels of 8 missed the 50 ms target, as its 100 chunks are each loaded and walked once, at about 0.6 ms each, so that level grows nothing now (`RENDER_LOD_FEATURES_SHIFT_MAX` 2), and the bench exits 0: its "before" for that level is then the plain stages too. Above voxels of 8 nothing grows, and all 1536 layers cost under 8 ms, so working out only the layers the 2D height estimate allows was tried and saved nothing.

### 2026-11-13, plan 009 phase 03: the pyramid's level 1 from the blocks

A second table: at each of the six places the four chunks under one level 1 column are generated as the game generates them (`terrain_generate_box_fallback`, open-sky light), then each of their 384 sections is reduced to its octant (`render_encode_section_octant`: what the upload's worker adds to a section's encode), the column is put together from `render/octant`'s store and encoded (what the gpu thread does for an edit), its 48 sections are reduced again for level 2, and its bytes are set against the same column sampled from terrain and against the four chunks' own. Profile build, one thread, through the build lock:

| Place | Octant, µs a section | Assemble and encode, µs a column | Level 2 octants, µs | Blocks' bytes, KiB | Level 1 from octants, KiB | Level 1 from terrain, KiB | Octant blocks live |
|---|---|---|---|---|---|---|---|
| -8, -8 | 7.63 | 195.1 | 147.8 | 895.1 | 112.4 | 26.4 | 78 |
| 1500, 300 | 7.29 | 140.6 | 140.1 | 701.6 | 89.2 | 40.2 | 78 |
| -2600, 1900 | 7.47 | 117.7 | 107.7 | 545.7 | 72.5 | 36.7 | 55 |
| 4000, -4000 | 7.45 | 148.8 | 144.0 | 823.0 | 103.6 | 30.8 | 75 |
| -6000, -500 | 8.52 | 257.7 | 229.8 | 1400.8 | 176.7 | 36.7 | 126 |
| 9000, 7000 | 9.31 | 349.1 | 296.7 | 1894.9 | 238.8 | 35.7 | 163 |
| mean | 7.95 | 201.5 | 177.7 | 1043.5 | 132.2 | 34.4 | |

Level 1 from the blocks is 12.7 % of the bytes of the blocks under it, the master's 1/8; level 2 is an eighth of that again, so the pyramid adds about 1/7 to what the blocks send, as master 4.3 worked out. It is four times what terrain sampling gives, as it holds the caves and ores sampling skips: hence `octant_pool_kib_per_column`. 14 to 41 octants a chunk are not one block all through, 1536 bytes each: `octant_store_mib`.
