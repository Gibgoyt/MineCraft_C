# tests/run/terrain/cave

**Purpose:** ctest `run_terrain_cave`. Tests `run/src/terrain/cave/`: all three kinds and their shares, thin against wide, tubes that join up, a solid floor over the bedrock, no cave touching the sea, each voxel worked out by hand, cutting with a mask against carving and by hand, and the same bytes with 1 worker and with 16, in any order and any split of the work.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts. Jobs report through atomics and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, `run/src/simd/reference/`, and the generated `tables/blocks/`.

Several tests print what they measure, so a change of shape can be judged by its numbers.

**Where the pinned number comes from.** The digest is a pin, not a proof: it was printed by this test's first passing run. What makes it believable is `each_voxel_is_worked_out_as_the_readme_says`, which works voxels out from the README's sums, on the scalar reference kernel, with every number typed out again, away from a cave's very edge, where the scalar and batch kernels may differ in the last bit. The pin stops a value changing between runs and presets, which would change every world.

**What it does not say.** Nothing here says the caves look good, or that a player can walk through them. That is for the phase gate's biome tour.
