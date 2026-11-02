# tests/run/terrain/feature

**Purpose:** ctest `run_terrain_feature`. Tests `run/src/terrain/feature/` and `terrain_generate_box`: any split of a box, section by section, one layer at the tip of a tree, and any order of chunks give the same bytes, as do 1 worker and 16; each tree stays within its reach and is whole across chunk lines; a column's ground against the stages walked from the sky; everything stands where it may; lakes hold their water; ice and snow are where each biome's file says.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, `run/src/thread/`, `run/src/time/`, and the generated `tables/biomes/` and `tables/blocks/`.

**Where the pinned numbers come from.** The two digests are pins, not proofs: they were printed by this test's first passing run, and hold the bytes still so that a change to any world is seen. What makes them believable is the other tests.

**What it does not say.** Nothing here says a biome looks good. That is the phase gate's tour, in `bench/terrain/tour/`.
