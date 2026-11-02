# 001-ahmed-bohemoth-minecraft-from-scratch — phase 11, stage 1: Light

**Status:** in progress: everything an agent can run passes, and only Ahmed's look at the pictures is left   (updated 2026-11-02)

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
- [ ] Ahmed looks at lit terrain and says it looks right. See issue 5

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_light$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `11d03f8` Add sky and block light, with the wiring and the test.
- `bacfaed` Print how many columns the cost test rebuilds, and record the cost over two runs.

From the phase's gate check:

- `1622c2f` Test light against the running simulation, pin the simulation's thresholds, and fix a format breach the merges left in main.
- `9cf0102` Pin how far fluids look for a way down, the edge of a sea, the limit on fires and stale wakes with tests that fail when each is changed.
- `81783f8` Have each opaque block hold what its faces see, as the shader was promised, so that the world is not black, and take the whole log before any light pass.
- `6f0f052` Look only at the outer layers of opaque sections in a new column, and make the frame test's pictures again with light and the water stage both in.

## Measured

`cost_on_this_machine`, and a temporary timer around `world_light_column_added` in `game_column_fill`, since removed. Real terrain is `--headless --test-world --ticks 1`.

| | debug | release |
|---|---|---|
| A new column, real terrain, with its neighbours | 3.5 ms (render distance 12) | 0.69 ms (render distance 32, 3209 columns) |
| A new column, 2000 scattered blocks under a holed roof | | 1.7 to 2.1 ms over two runs |
| One edit's pass, on average | | 1 to 2 us over two runs |
| A rebuild of 13 columns | | 88 to 101 ms over two runs |

Mutation check of the test, each put in and taken out by hand: spreading of dark off, free fall off, nothing leaving with a column, new column not raising its neighbours, wrong face of a neighbour, no render flags, bright pass stopping early, log never read, no retry after a full pool, and `light_section_settled` ignoring the column beside, the section above or the section below. All were caught, the last three only after `sections_of_one_value_beside_very_different_ones_are_still_raised` was added for them. **Not caught:** `value + 2u` for `value + 1u` in the check of a column beside: no test has two sections of one value exactly 2 apart. The gate check closed this: see issue 7.

**After the gate check's fix** (issue 6), which all the numbers above are from before: a new column on real terrain, release, render distance 32, 3209 columns: mean 1.31 ms, max 6.2 ms, up from 0.69 ms. Debug, in `run_render_frame`: mean 6.6 ms, max 18 ms. 2000 scattered blocks under a holed roof, release: 2.3 to 2.4 ms. One edit's pass: 2 us. A rebuild of 13 columns: 163 ms, up from 88 to 101 ms: a rebuild is the fallback, so its sweeps were left to look at each opaque section whole.

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
5. **Pictures change.** Underground and shaded voxels now hold their real light where every voxel used to hold open sky. Nothing compares against a stored picture, so no test could say whether it looks right. Only Ahmed can judge that; the command is in the stage's final report. Gate check: that last was wrong, and `run_render_frame` does compare. See issue 6. The command, from a text console and never inside X: `tools/test-or-build.sh cmake --build --preset release`, then `build/release/bin/minecraft_c --test-world --walk`. Or look at `tests/run/render/frame/fixtures/*.png`. The gate check looked at those and they show lit land, but water's surface comes out darker than it did, since water's own opacity of 2 leaves its top voxel 13 of sky.
6. **This stage was ticked done, and the world it lit was black. Found and fixed by the gate check.** Expected: what Stage 09.2 issue 1 and `cuda/src/shade/README.md` asked of this stage: "Stage 11.1 must leave in each voxel with a block the light its faces should show, the brightest of its neighbours in the air". Actual: solid voxels held 0, and the shader reads the byte of the voxel a ray stops in, as air is not on the card, so land and hills drew near black at noon with only leaves and water lit. Evidence: `tests/run/render/frame/index.c:1145`, `golden_pictures_match`: `start.png` 53247 of 57600 pixels past the tolerance, `north_low.png` 55941, `down.png` 54263. Stages 12.3 and 09.3 reported the same numbers and left them; this stage's gate and all its tests passed, because each module was right by its own account: the request reached 09.2's stage file and never this one's, and `run_world_light`'s reference held the same wrong rule. Fixed in `run/src/world/light/`, under "What a block holds" in its README:
    - An opaque block holds the brightest of its neighbours that are not opaque, for showing only. A neighbour takes from it only what it gives off, so nothing leaks through a wall. In block light, a block that gives some off holds just that, since otherwise light it gave could not be told from light it saw, and so a lit furnace beside a torch shows 13 and not 14: a small departure from what 09.2 asked.
    - A pass can no longer run before a block's own record is read, since what an opaque block holds would then be taken for what was there before had given. So all records come first, as one pass per channel, and `world_light_column_added`, `_column_leaving` and `_sections_replaced` start by taking the log. The first version without this failed `batches_longer_than_one_read_of_the_log_match_a_full_recompute` (`got 3a, want 1a`) and `a_block_put_in_a_column_not_lit_yet_is_seen_when_it_is`.
    - That made a column 30 ms on debug, and `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap` failed `columns_made > 100u` and `picture == screen`, as in issue 1, since 118 columns took 3.5 s. Each section all of one opaque block was being looked at whole. `light_opaque_section_seed` now asks only about layers with something to see, which gave 6.6 ms and the test passes.
    - `tests/run/render/frame/fixtures/*.png` made again with `MC_GOLDEN_WRITE=1`, after looking at them, as they were made with every voxel at open sky and could not pass. They also hold Stage 09.3's water. 57600 of 57600 exact on a second run. `run_world_light`'s reference and its hand-worked values give opaque blocks what their faces see, and `an_opaque_block_holds_what_its_faces_see_and_passes_none_of_it_on` is new.
    - Mutation check of the new code, each put in and taken out by hand: faces of a section beside not asked about, faces never going dark, faces not raised, walls passing light on, no catching up before a new column, faces left lit by a column leaving. All caught. `cuda/src/shade/README.md` no longer says that nothing writes light.
7. **Gate check's other results.** `value + 2u` is now caught, as is `gives + 2u` in `light_column_added_seed`, by `sections_of_one_value_two_apart_across_a_column_edge_are_still_raised`, which writes the numbers in by hand because no block in `config/blocks/` can make that state. The random-edit and random-walk tests on other seeds, half their blocks drawn from all of `BLOCK_COUNT`, with the test edited by hand and put back: 14 runs before issue 6's fix and 10 after, 0 differences. Nothing but tests calls `world_light_sections_replaced`, since `game` only imports whole columns before lighting them: it stays for whoever writes past the log (issue 3). Issue 3's "no real edit reaches light yet" is out of date: `world/tick`'s writes do, checked by `light_stays_right_while_the_simulation_changes_real_terrain`.
8. **A breach left by a merge, fixed.** `clang_format` failed on `run/src/main/index.c:54`, a comment's alignment from merging the sky and walking branches. Formatted in `1622c2f`. That is outside this phase's paths, and this stage's Verification does not run `clang_format`, but it failed the whole ctest run.
