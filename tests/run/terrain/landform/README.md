# tests/run/terrain/landform

**Purpose:** ctest `run_terrain_landform`. Tests `run/src/terrain/landform/` (plan 007 phase 03): the plateau spline against master 3.4's table on a synthetic ramp of its input, the back edge drawn 4 to 6 times wider than the seaward face, each column blended from its four nodes x first as the README says, the same plateau, lift and height to the bit in any box, batch or through the height estimate, the fields' limits against their measured spread, what the lattice costs a chunk's box, the height limits on 8 seeds' real columns over 64 km, and plateau tops choosing lowland biomes.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and 16 job workers for the limits on real columns. Jobs write their own slot of the run and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, `run/src/thread/`, `run/src/time/`, and the generated `tables/biomes/`.

The `measure/` directory beside this is `run_terrain_landform_measure`, `bench_landform`'s self-test (plan 007 phase 01).

`raw_noise_measures_what_the_limits_were_chosen_for`, `the_lattice_costs_little_per_chunk_box` and the limits test print what they measure, so a change of shape can be judged by its numbers. The cost is a debug build's unless the test is run from the release preset.
