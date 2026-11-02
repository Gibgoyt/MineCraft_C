# tests/run/terrain/hydrology

**Purpose:** ctest `run_terrain_hydrology`. Tests `run/src/terrain/hydrology/`: each column worked out by hand; the blocks against the density they come from, voxel by voxel; rivers that hold water and reach the sea; oceans and beaches in believable shares; aquifers that leave nothing able to flow; the lava level; and the same bytes for any split of the work and with 1 worker and 16.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts. Jobs report through atomics and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, `run/src/thread/`, `run/src/heap/`, and the generated `tables/blocks/`.

`raw_noise_measures_what_the_limits_were_chosen_for`, the map test, the flood test and the worker test print what they measure, so a change of shape can be judged by its numbers.

**Where the pinned number comes from.** The digest is a pin, not a proof: it was printed by this test's first passing run. What makes it believable is `a_column_is_worked_out_as_the_readme_says` and `the_blocks_are_the_density_with_the_rivers_cut_and_the_shore_laid_on`, which type the README's formulas and table of materials out again, and walk whole columns without the fill's shortcuts. The pin stops a value changing between runs and presets, which would change every world.

**What it does not say.** Nothing here says the rivers look good. That is for the phase gate's biome tour. And nothing yet opens the ground but the tests: `run/src/terrain/cave/` (Stage 10.5) is what will call the flood for real, so here whole boxes are opened by hand.
