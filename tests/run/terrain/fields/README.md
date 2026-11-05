# tests/run/terrain/fields

**Purpose:** ctest `run_terrain_fields`. Tests `run/src/terrain/fields/`: all six fields stay from -1 to 1, are spread out as its README promises, and give the same bits with 1 worker and with 16, in any order and any split of the work.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts. Jobs report through atomics and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, `run/src/simd/reference/`.

`each_field_is_spread_out_as_the_readme_promises` and `fields_change_slowly_from_column_to_column` print what they measure, so
a change of shape can be judged by its numbers.

**Where the pinned numbers come from.** The digest is a pin, not a proof: it was printed by this test's first run. What makes it
believable is `each_value_is_worked_out_as_the_readme_says`, which works every value out from the README's definition, on the
scalar reference kernel, with the limits and the warp typed out again. The pin stops a value changing between runs and presets,
which would change every world.
