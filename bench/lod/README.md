# bench/lod

**Purpose:** times one far-field column, as `run/src/render/lod/` builds it on a worker, at every level from voxels of 2 to 64 blocks, on Ahmed's seed, against plan 005 phase 14's target of 50 ms of worker time a column.
**Tier:** none; benchmarks are not tiered.
**Owns:** one terrain, in static storage, released before it exits.
**Lifetime:** Process.
**Threads:** main only: a column is one worker's job.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_lod`, so no build shares the CPU. Exit status 0, 1 if a level's slowest column missed the target, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `render/lod`, `terrain`, `time`, `world/column`, and the generated `tables/blocks/`.

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
| voxels of 16 | 7.7 | 8.2 | 7.7 | 8.2 | 0 |
| voxels of 32 | 7.7 | 8.3 | 7.7 | 8.0 | 0 |
| voxels of 64 | 7.9 | 8.1 | 7.8 | 8.3 | 0 |

The same tops at every level. Exit status 1: voxels of 8 miss the 50 ms target, as its 100 chunks are each loaded and walked once, at about 0.6 ms each. Above voxels of 8 nothing grows, and all 1536 layers cost under 8 ms, so working out only the layers the 2D height estimate allows was tried and saved nothing.
