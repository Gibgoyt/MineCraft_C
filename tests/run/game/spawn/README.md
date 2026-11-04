# tests/run/game/spawn

**Purpose:** ctest `run_game_spawn`. Tests `run/src/game/spawn/`: what counts as land on made-up columns; the search's walk with a probe that finds nothing, or one place, including places past where the old search stopped; and on real terrain that seeds start on real land and that a column's fields never rule out land that every stage makes.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own `terrain_t`.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/game/spawn/`, `run/src/terrain/`.

No display, no card and no input device.

A seed with no land within reach cannot be found on purpose, so `no_land_anywhere_ends_at_the_middle_having_looked_everywhere` gives the walk a probe that finds nothing. That walk is what `game_spawn_find` runs, through a probe that reads real blocks, which `every_seed_written_down_starts_on_land` and `many_seeds_start_on_land` hold to `terrain_generate_box`.
