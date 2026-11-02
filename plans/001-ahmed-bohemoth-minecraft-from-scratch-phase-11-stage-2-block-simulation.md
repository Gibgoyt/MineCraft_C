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

## Wiring outside the owned paths

- `run/include/world/index.h`, `run/src/world/index.c`: `world_tick_t` in `world_t`, started by `world_open`, ended by `world_close`, and its bytes in the budget. `world_t` goes from 1344 to 2048 bytes.
- `run/include/main/index.h`: `app_t` goes from 8064 to 8768 bytes.
- `run/src/game/index.c`: `world_tick_step` in `game_sim_tick`, the terrain's seed in `game_open_parts`, and `world_tick_column_leaving` beside light's.
- `tests/run/render/upload/index.c`, `tests/run/world/column/index.c`: their fixtures' pinned sizes, for the larger `world_t`.
- `tests/run/game/index.c`: two checks that the world ticks with the game, on the terrain's seed.
- `plans/REPO_STRUCTURE.md`, `run/src/world/README.md`, `run/src/game/README.md`: the new path and part.

## Results

`run_world_sim`: 35 tests, 1278559 checks, 0 failed, on `debug` and on `sanitize`. All 66 ctests pass on `debug`. Printed by the test on `debug`:

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
