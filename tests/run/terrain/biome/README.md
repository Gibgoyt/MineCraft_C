# tests/run/terrain/biome

**Purpose:** ctest `run_terrain_biome`. Tests `run/src/terrain/biome/` and `run/src/terrain/surface/`: every biome the plan lists is in the table and turns up in every world searched; the choice and the rules worked out by hand; every voxel of a chunk of each biome against the rules typed out again and walked the slow way; each biome's own top on real columns; lined caves; the whole chain of stages; and the same bytes for any split of the work and with 1 worker and 16.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems. The one `static` is the result of a search, not a pointer to anything.
**Lifetime:** One run.
**Threads:** main, and the job workers for the test that compares worker counts. Jobs report through atomics and main does the checking, since harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/job/`, `run/src/thread/`, `run/src/heap/`, and the generated `tables/biomes/` and `tables/blocks/`.

`every_biome_turns_up_in_every_world_searched` prints each biome's share of three worlds, each 65 kilometres across, sampled every 128 blocks, so a change to the table can be judged by its numbers. The worker test prints columns a second through every stage.

**Where the pinned numbers come from.** The two digests are pins, not proofs: they were printed by this test's first passing run. What makes them believable is `the_choice_is_worked_out_by_hand`, `the_rules_are_worked_out_by_hand` and `the_ground_is_covered_as_the_readme_says_in_every_biome`, which type the README's rules out again and walk whole columns without the fill's shortcuts. A pin stops a value changing between runs and presets, which would change every world. It moves when `config/biomes/` does, and that is on purpose: a new biome moves the ids.

**What it does not say.** Nothing here says a biome looks good, or that the climate boxes make believable neighbours. That is for the phase gate's biome tour. `tests/compile/game_tables/` is what tests that the tool refuses a bad file.
