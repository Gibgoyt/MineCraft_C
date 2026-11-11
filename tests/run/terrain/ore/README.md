# tests/run/terrain/ore

**Purpose:** ctest `run_terrain_ore`. Tests `run/src/terrain/ore/`: the table against its README, each ore at its depths, chromium (plan 005 phase 11) only from -250 to -40 and at exactly 4 veins a chunk by `terrain_ore_chunk_veins`, rock above the old top of the world having its ore up to the last layer, rows left out over the rock moving nothing, deepslate by depth, the deepslate form only in deepslate, veins worked out by hand, veins across chunks, and the same bytes with 1 worker and with 16, in any order and any split of the work.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts. Jobs report through atomics and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, and the generated `tables/blocks/`.

Several tests print what they measure, so a change to the table can be judged by its numbers.

**Where the pinned number comes from.** The digest is a pin, not a proof: it was printed by this test's first passing run. What makes it believable is `each_vein_is_laid_as_the_readme_says`, which lays a table's veins again from the README's steps, with the sizes typed out again. The pin stops a value changing between runs and presets, which would change every world.

**What it does not say.** Nothing here says that a game is well paced by how much ore there is. That is for playing it.
