# 001-ahmed-bohemoth-minecraft-from-scratch — phase 11, stage 1: Light

**Status:** done   (updated 2026-11-02)

## Goal

Sky and block light flood fill across sections exists, with incremental updates on edit.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-light-block-simulation.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/world/light/`
- `tests/run/world/light/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement flood fill across sections in `run/src/world/light/index.c`
- [x] Implement incremental updates on edit
- [x] Compare incremental light to full recompute on random edits, registered as `run_world_light`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_light$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `11d03f8` Add sky and block light, with the wiring and the test.
- `bacfaed` Print how many columns the cost test rebuilds, and record the cost over two runs.

## Measured

`cost_on_this_machine`, and a temporary timer around `world_light_column_added` in `game_column_fill`, since removed. Real terrain is `--headless --test-world --ticks 1`.

| | debug | release |
|---|---|---|
| A new column, real terrain, with its neighbours | 3.5 ms (render distance 12) | 0.69 ms (render distance 32, 3209 columns) |
| A new column, 2000 scattered blocks under a holed roof | | 1.7 to 2.1 ms over two runs |
| One edit's pass, on average | | 1 to 2 us over two runs |
| A rebuild of 13 columns | | 88 to 101 ms over two runs |

Mutation check of the test, each put in and taken out by hand: spreading of dark off, free fall off, nothing leaving with a column, new column not raising its neighbours, wrong face of a neighbour, no render flags, bright pass stopping early, log never read, no retry after a full pool, and `light_section_settled` ignoring the column beside, the section above or the section below. All were caught, the last three only after `sections_of_one_value_beside_very_different_ones_are_still_raised` was added for them. **Not caught:** `value + 2u` for `value + 1u` in the check of a column beside: no test has two sections of one value exactly 2 apart.

Also passing: `run_world_light` on the sanitize preset, and all 62 ctests on debug, GPU tests included.

## Wiring outside the owned paths

- `run/include/world/index.h`, `run/src/world/index.c`, `run/src/world/README.md`: `world_t` holds a `world_light_t`, and `world_open` and `world_close` start and end it. `sizeof(world_t)` 1088 to 1344. The budgets add 16 bytes per slot and 40 MiB of queues, virtual until used.
- `run/include/main/index.h`: `sizeof(app_t)` 7040 to 7296. `tests/run/world/column/index.c` and `tests/run/render/upload/index.c`: the same 256 in their fixtures' asserts.
- `run/src/game/index.c`, its README and `run/include/game/index.h`: a column's light as it is filled, before it is unloaded, and `world_light_update` in each sim step. See issue 1 for `commit_next` and `GAME_COMMIT_BUDGET_NS`.
- `plans/REPO_STRUCTURE.md`: the test's row, and `world/light` moved from the `worker` row to the `sim` row of the threads table. See issue 2.

## Issues found

1. **Light on the sim thread made `run_render_frame` fail.** Expected: wiring light into `game_column_fill` changes no test. Actual: `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap` failed `game->columns_made > 100u` and `picture == screen`. A timer gave 9 ms a column on debug, with 64 finished columns taken in one step, so about 0.6 s with no snapshot, long enough for the test's 400 identical frames to look settled. Fixed two ways: a step now spends at most `GAME_COMMIT_BUDGET_NS` (4 ms) on finished columns and carries on from `commit_next` the next step; and `light_section_settled` looks through arrays per channel, with voxels already at 15 not looked at, which brought a column to 3.5 ms. The test passes unchanged.
2. **The plan puts light on workers, and it runs on `sim`.** `plans/REPO_STRUCTURE.md` listed `world/light` under `worker`. Actual: a section's light may need an array from `section_pool_t`, which has one thread at a time, and a pass reads across columns the sim thread is changing. Left on `sim`, at 0.69 ms a column on release, and `REPO_STRUCTURE.md` corrected. If this ever matters, the column's own flood can be done on the worker into a flat array and only the seams on `sim`. Not done.
3. **No real edit reaches light yet.** `world_light_update` runs every sim step, but nothing in `game` edits blocks until the player stages. Only the tests drive it with edits. `world/tick` (11.2) and whoever writes blocks past `world_edit_block_set` must call `world_light_sections_replaced`.
4. **The map's radius is a circle.** Expected: a table radius of 1 gives 9 columns. Actual: 5, so the test's first version failed `count == 9u`, and asked a diagonal column for its light. The random tests moved to radius 2 and so have corners. Test bug only.
5. **Pictures change.** Underground and shaded voxels now hold their real light where every voxel used to hold open sky. Nothing compares against a stored picture, so no test could say whether it looks right. Only Ahmed can judge that; the command is in the stage's final report.
