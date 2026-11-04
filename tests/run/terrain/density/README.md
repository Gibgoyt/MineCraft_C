# tests/run/terrain/density

**Purpose:** ctest `run_terrain_density`. Tests `run/src/terrain/density/`: the range limits, on every corner of the climate and on real columns of real seeds; bedrock at `z = -250`; water's top block at `z = 0`; each value worked out by hand; and the same bits with 1 worker and with 16, in any order and any split of the work.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts. Jobs report through atomics and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/world/`, for a real column to store it in, `run/src/job/`, `run/src/simd/reference/`, and the generated `tables/blocks/`.

`raw_noise_measures_what_the_limits_were_chosen_for`, `the_world_has_seas_plains_and_mountains_in_believable_shares` and the
worker test print what they measure, so a change of shape can be judged by its numbers.

**Where the pinned number comes from.** The digest is a pin, not a proof: it was printed by this test's first passing run. What
makes it believable is `each_value_is_worked_out_as_the_readme_says`, which works values out from the README's definition, on the
scalar reference kernel, with the sizes and the limits typed out again. The pin stops a value changing between runs and presets,
which would change every world.

`mountain_ranges_run_on_for_a_distance_and_are_common` and `the_land_is_not_mostly_walls` hold the shape plan 003 phase 8 stage 4 asked for, by the fields: at least 3 joined pieces at 150 or more of 1 km^2 on each of 8 seeds, 2 at least 4 km long, one at least 5 km, and on 3 seeds under 15% of the land rising 3 for 1 and under 2% rising 6 for 1.

**What it does not say.** Nothing here says the land looks good. That is for the phase gate's biome tour.
