# 001-ahmed-bohemoth-minecraft-from-scratch — phase 11, stage 2: Block simulation

**Status:** done   (updated 2026-11-02)

## Goal

Water and lava flow, falling sand and gravel, leaf decay, and crop and grass spread on random ticks exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-light-block-simulation.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/world/tick/`
- `tests/run/world/tick/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement water and lava flow in `run/src/world/tick/index.c`
- [x] Implement falling sand and gravel, and leaf decay
- [x] Implement crop and grass spread on random ticks
- [x] Register test `run_world_sim`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_sim$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `7092cf2` Add the block simulation: water and lava flow, falling sand and gravel, leaf decay, and wheat and grass on random ticks.

From the phase's gate check, where the first two touch this stage's test:

- `1622c2f` Test light against the running simulation, pin the simulation's thresholds, and fix a format breach the merges left in main.
- `9cf0102` Pin how far fluids look for a way down, the edge of a sea, the limit on fires and stale wakes with tests that fail when each is changed.
- `81783f8` Have each opaque block hold what its faces see, as the shader was promised, so that the world is not black, and take the whole log before any light pass.
- `6f0f052` Look only at the outer layers of opaque sections in a new column, and make the frame test's pictures again with light and the water stage both in.

## Wiring outside the owned paths

- `run/include/world/index.h`, `run/src/world/index.c`: `world_tick_t` in `world_t`, started by `world_open`, ended by `world_close`, and its bytes in the budget. `world_t` goes from 1344 to 2048 bytes.
- `run/include/main/index.h`: `app_t` goes from 8064 to 8768 bytes.
- `run/src/game/index.c`: `world_tick_step` in `game_sim_tick`, the terrain's seed in `game_open_parts`, and `world_tick_column_leaving` beside light's.
- `tests/run/render/upload/index.c`, `tests/run/world/column/index.c`: their fixtures' pinned sizes, for the larger `world_t`.
- `tests/run/game/index.c`: two checks that the world ticks with the game, on the terrain's seed.
- `plans/REPO_STRUCTURE.md`, `run/src/world/README.md`, `run/src/game/README.md`: the new path and part.

## Results

`run_world_sim`: 35 tests, 1278559 checks, 0 failed, on `debug` and on `sanitize`. All 66 ctests pass on `debug`. After the gate check: 45 tests, 0 failed, on `debug` and `sanitize`, and all 71 ctests on `debug`, from a clean build. Printed by the test on `debug`:

- Real terrain, 21 columns: 0.013 to 0.020 ms a tick of random ticks with light. Over 3 places and 400 ticks, 2177 voxels got a random tick; 3988 leaves stayed 3988; water, lava, sand and gravel counts did not change, and nothing was waiting.
- Wheat over 80 crops in 20000 ticks, 1 pick a section: 42 stages with water and 29 without, against 56 and 30 worked out from the odds.
- A fall of 18432 sand: the most one tick wrote was 16384 blocks, and no log record was lost.

## Issues found

**1. A flowing level has nowhere to go in a block state.** Expected: a state per level, as the plan's "water flow" might suggest. Actual: `compile/src/game_tables/blocks/README.md` says one block per state, and terrain, the player and the renderer all compare with `BLOCK_ID_WATER`, so new blocks would mean changing all of those and every id after them. Done: levels live in `world/tick`, half a byte a voxel, in pages per section, with no page meaning source, so generated water costs nothing. Left for others, and written in `run/src/world/tick/README.md`:

- Stage 16.1 must save `world_tick_fluid_level` with the blocks. A load without it makes every flowing voxel a source, which floods.
- The renderer draws flowing water at full height, since it does not read levels. `world_tick_fluid_level` is there for it.
- A bucket (Stage 13) needs a call to turn flowing water into a source: `world_edit_block_set` with the same block changes nothing.

**2. No items or entities yet, so nothing drops, and falling is not smooth.** Expected from the original game: a plant washed away or a leaf decayed drops an item, and sand falls as an entity. Actual: dropped items are Stage 12.4 and the entity store Stage 15.1, and neither was on the integration branch when this was written. Done: what is washed away or decays is gone with no drop, and a falling block moves as a block, one voxel per 2 ticks. Whoever adds drops can do it where `tick_block_write` is called.

**3. Leaves a player places decay.** Nothing says who placed a block, since the block is the same either way. Left alone: mining and placing (Stage 12.3) can add a mark if it matters, and half a byte per voxel here has room.

**4. Saplings, sugar cane and cactus do not grow.** The todo names crops and grass, and only wheat is a crop in `config/blocks/`. Left alone, as out of this stage.

**5. The test name does not follow its directory.** Expected: `tests/run/world/tick` gives `run_world_tick`, and the stage asks for `run_world_sim`. Done: `tests/run/world/tick/test.cmake` sets `MC_TEST_NAME`, which `cmake/targets.cmake` provides for this. `plans/REPO_STRUCTURE.md` says so.

**6. Fixed on the way, all found by this test or the sanitize build, none reached a commit.** The map wants a circle, not a square, so a radius 1 map has no corners: `sim_floor` got `NOT_LOADED` 1024 times, and the fixture went to radius 2. Water beside a column not loaded retries every 20 ticks and is never quiet, which is intended, so tests that settle wall their floor in. The `sanitize` preset rejected `int & unsigned` on a promoted `uint8_t` that `debug` accepted: `-Wsign-conversion` at `run/src/world/tick/index.c`, fixed with casts.

**7. A command refused by the harness.** One long compound shell command was refused as "too complex to verify that it stays inside the worktree". It was split into plain commands from the same directory, as the message said to do. No effect on the work.

**8. Found by the gate check: tests that a wrong number got past.** Expected: changing a rule in `run/src/world/tick/index.c` fails `run_world_sim`. Actual, with each mutation put in and taken out by hand, these passed: `TICK_LIGHT_GROW` 10, `TICK_LIGHT_GRASS_TAKES` 5, `TICK_HYDRATE_REACH` 5, `TICK_FARMLAND_DRY_ODDS` 32, `TICK_SLOPE_WATER` 3, `flows_down ? false`, no generation check on a node, no limit on fires, `WORLD_TICK_RANDOM_RADIUS_DEFAULT` 1 and `WORLD_TICK_RANDOM_PER_SECTION_DEFAULT` 1. So the README's numbers for those were not held to anything. The code was right in each case: no bug was found by this. Done: tests at each number, on both sides of it where that means anything, in `wheat_grows_at_light_9_and_not_at_light_8`, `grass_needs_light_9_over_itself_and_light_4_over_what_it_takes`, `water_keeps_farmland_from_4_away_and_one_above_and_no_further`, `dry_farmland_goes_at_the_rate_the_readme_says`, `random_ticks_reach_as_far_as_their_radius_and_no_further`, `water_looks_4_blocks_for_a_way_down_and_lava_2_and_no_further`, `water_going_down_runs_sideways_too_only_with_3_sources_beside_it`, `a_wake_for_a_column_that_left_does_not_run_on_the_one_that_came_back` and `one_tick_runs_no_more_than_its_limit_even_when_nothing_writes`, and the two defaults in the `world_open` test. All of those mutations now fail. Also caught, before and after: both delays, lava's step, `sources >= 2u`, the two rocks, lava on water, leaf reach, a leaf by an unloaded column, wheat's odds, sand into fluid, no wake on a level change, a column's pages kept, no second log drain, the seed unused, kelp washing away, no retry beside an unloaded column, no limit on writes, grass under fluid.

Two still pass, and were left: taking the level reset out of `tick_block_write`, since `tick_log_drain` does the same for the same record, and random ticks past gameplay's height, since nothing up there can do anything and a write is refused.

**9. Found by the gate check: nothing tested light and the simulation together.** Expected: a test that light stays right while this module writes. Actual: tests ran `world_light_update` beside the ticks, and none compared its result. Done: `light_stays_right_while_the_simulation_changes_real_terrain`: real terrain, 400 random edits of water, lava, sand, gravel, torches and digging, 20093 fired ticks and fast random ticks, then every voxel against `world_light_rebuild`, with no rebuild allowed on the way. 0 voxels differ. No bug found by this, and it also holds after Stage 11.1 issue 6.

**10. Left open, measured by the gate check: generated trees lose a few leaves.** Expected from `real_terrain_runs_on_its_own_without_flooding_or_stripping_its_trees`: none go. That test allows 1% in 400 ticks, at 0.3 picks a voxel. Actual, 40 places on the default seed, 21 columns each, 32768 picks a section, about 10900 ticks' worth at the real rate: jungle leaves 14501 to 14442, dark oak 7224 to 7195, and oak, spruce, acacia and cherry none. Birch and mangrove had none to lose in these places. Cause for jungle: `feature_blob` of across 5 reaches further than 6 steps from a trunk: a leaf at (-3, -3, +1) from the trunk's top corner is 7. Dark oak's was not traced. Left alone: a few outer leaves go in the first minutes and trees stay trees, and the fix is in Phase 10.7's shapes, which changes the world from each seed. For Ahmed to decide if it matters.

**11. Left open, not traced: grass under things.** In that same run, grass blocks went 13384 to 12459 and dirt the other way, 7%, none of it spreading back. This module's rule is grass under anything opaque or any fluid goes to dirt, and terrain does make grass under full snow blocks, logs and lakes, which are the likely places. Which they were was not checked. Nothing would show from above in those places, so it was left.

**12. Commands refused by the harness during the gate check.** Five commands were refused: three long compound ones as too complex to verify as staying inside the worktree, and two for handing text to `sh` or `python3`. Each was split into plain commands from the same directory, as the messages said. A script in the shared scratchpad, `mutate.py`, was overwritten by another agent's of that name mid-run; it failed with no effect, since its relative path did not exist here, and scripts then got names of their own.
