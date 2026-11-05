# tests/run/terrain/feature/own

**Purpose:** ctest `run_terrain_feature_own`. `terrain_generate_box` lets features read a chunk's own tops from the blocks it already has, rather than walk down that chunk again: this holds that shortcut to the walk it replaces, so that it can never change the land. The tops must be the walk's, column for column, and the blocks of a whole chunk must be those of every stage by hand with `terrain_feature_fill_box` finding its own, on 3 seeds, on a spread of ground and on chunks with a lake. A box that is not one whole chunk, or that does not reach all its ground, is refused.
**Tier:** test.
**Owns:** the test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/` and every stage under it, and `run/src/world/column/`'s layers.

**It also holds what the mask says after `terrain_generate_box`.** `bench_world_map` and `bench_cave_walk` read `opened` to know where a cave truly cut. Once the mask also kept what caves wanted and did not cut, both tools counted that too, so each chunk of ground compares it with the by-hand mask.

**What it does not say.** These are samples, of 3 worlds: a place where the two ways differ and no chunk here lands would not show. The digests in `tests/run/terrain/` hold the land itself as well.
