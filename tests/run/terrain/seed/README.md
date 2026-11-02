# tests/run/terrain/seed

**Purpose:** ctest `run_terrain_seed`. Tests `run/src/terrain/` with `rng` and `noise`: the same seed gives the same bits with 1 worker and with 16, in any order and any split of the work.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts. Jobs report through atomics and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, `run/src/simd/reference/`.

`one_and_sixteen_workers_give_the_same_bytes` holds its jobs at a barrier until 16 different workers have each picked one up, and
deals each job a sixteenth of the tiles, so that the 16-worker run cannot fall back on a few fast workers doing it all. The mask
of who ran is checked. The one-worker run is one job, on one worker, making all the tiles.

**Where the pinned numbers come from.** The SplitMix64 values are the published vector for seed 1234567. The hash and salt
values were worked out by a separate Python program, not from this C. The tile digest is a pin, not a proof: it was printed
by this test's first run, and what makes it believable is `field_is_the_sum_of_its_octaves`, which works a field out by hand
on the scalar reference kernel. It stops a value changing between runs and presets, which would change every world.
