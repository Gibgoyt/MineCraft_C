# tests/run/world/tick

**Purpose:** ctest `run_world_sim`. Tests `run/src/world/tick/`: water and lava worked out by hand and against a separate flood fill, consistency on rough ground, water and lava meeting, falling blocks, leaf decay, crops, grass, columns coming and going, running out of nodes, pages and pool, the same world from the same seed, and real terrain left alone.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and `world_open`'s test from `heap`.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/world/`, `run/src/terrain/`, `run/src/heap/`, `run/src/time/`, and the generated `tables/blocks/`.

The name comes from `test.cmake`: the plan calls this test `run_world_sim`, while its directory follows the module.

`on_a_flat_floor_with_pillars_levels_match_a_separate_flood_fill` keeps its sources 16 apart, so that they never make a third, and then the levels are exactly the steps from the nearest source, which a plain fill in the test works out.

Tests of random ticks use a large `random_per_section`, except `wheat_grows_faster_with_water_at_the_rate_the_readme_says`, which needs the real odds. Results follow from the seed, so none of this can flake.

The real-terrain test prints what a tick cost, for the stage file, and checks nothing about that.
