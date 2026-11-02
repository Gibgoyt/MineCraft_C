# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 3: Mining and placing

**Status:** done   (updated 2026-11-02)

## Goal

Timed mining by hardness and tool, drops spawned from block tables, and block placing exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/interaction/`
- `tests/run/player/interaction/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement timed mining from table hardness and tool in `run/src/player/interaction/index.c`
- [x] Spawn drops from block tables; dirt must drop dirt
- [x] Implement block placing
- [x] Test drop tables for every block, registered as `run_player_interaction`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_player_interaction$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Verification results

- `cmake --build --preset debug`: builds.
- `ctest --preset debug -R '^run_player_interaction$'`: passes, 28 tests. On the sanitize preset: passes, with no report.
- `the_real_threads_mine_and_place_a_real_world_and_the_picture_changes`, 5 more times alone: 0 failures.
- `ctest --preset debug -R '^standards_lint$'` and `'^clang_format$'`: pass.
- `ctest --preset debug`, all of it: 68 of 69 pass. `run_render_frame` fails, and fails the same with none of this stage: Issue 4.
- `pgrep -af "$PWD/build"`: nothing of this worktree left running.
- Six deliberate bugs built in at once (harvest always true, no body check, hardness rounded up, chance ignored, letting go not resetting, no delay): 9 tests failed, including one for each bug. The source was then restored.
- **Scripted headless play**, at 960x540 with `--no-sky`, the script being the one in `pi_mine_script`: walks 20 ticks, looks straight down, holds the left button, then turns uphill, mines the hole's wall and clicks the right button. The program reports `the player mined 3 blocks and placed 1; the last mined was dirt, and the hand holds 2 of dirt`, with `0 bytes live` at exit. By eye: `before.png` shows grass under the feet, `grass_gone.png` a dirt face where it was, `notch.png` a one-block hole in the wall, and `placed.png` that hole filled with dirt again. The test runs the same script twice at 160x90: the same bytes both times, and each picture differs from the one before.
- Not checked: the buttons on a real mouse and playing under KMS. Only Ahmed can run those. Real input is tested only as far as `game_loop_source` is compiled; the buttons are exercised through a script and `game_hands_send`, not through the fake kernel.

## Commits

Appended as they land:

## Wiring outside the owned paths

The smallest needed to call this stage's module, as the task allows:

- `run/{src,include}/player/index.*`: `player_hands_send`, `player_interact_tick`, and `player_interaction_t` in `player_t`, which goes from 960 to 2112 bytes.
- `run/{src,include}/game/index.*`: `game_hands_send`, the hands' tick in `game_sim_tick`, BTN_LEFT and BTN_RIGHT from a script and from real input, and a line in `game_report`. `game_t` keeps its size.
- `run/include/main/index.h`: the sum in `app_t`'s `static_assert`, 9536 to 10688.
- `run/src/main/index.c`: the `--walk` usage text.
- The READMEs of `run/src/player/`, `run/src/game/`, `run/src/main/`, and `plans/REPO_STRUCTURE.md`.

## Issues found

### Issue 1: nothing exists yet to receive drops or hold what is placed

- **Expected:** drops become items in the world, and placing takes a block from an inventory.
- **Actual:** Stage 12.4's entity and Stage 13.1's inventory are not on the integration branch at `8864855`.
- **Done:** drops go in a ring, `player_drop_t`, for Stage 12.4 to take through `player_interaction_drop_next`. Until then `player_interact_tick` calls `player_interaction_drops_to_hand`, which gives one slot of up to 64 blocks. A different block replaces the slot, and a drop that is no block, like coal, is lost and counted. When Stage 12.4 merges, that one call is what it replaces. Tools have only the entry `player_interaction_hold_tool` until Stage 13.3, so a real run mines by hand; the tests hold every tool and tier.

### Issue 2: the crack on a block being mined is not drawn

- **Expected:** a player sees how far along a block is.
- **Actual:** Stage 09.5 owns the breaking overlay and has not landed. While a block is being mined the picture does not change.
- **Done:** left to Stage 09.5. `player_interaction_progress` has the number, sim-side, and is tested; it still has to be carried to `main`, for example in `player_view_t`.

### Issue 3: the default seed's spawn is a steep, dark hillside

- **Expected:** a level, sunlit field to mine in the scripted proof.
- **Actual:** `--headless --test-world --walk` starts at -7.5, -7.5, 85, on a spire. A first script looking 1 rad down from there mined nothing: `the player mined 0 blocks`, as the ground falls away faster than reach. Grass and dirt also draw very dark in every screenshot, at any `--time-of-day` tried (0.35 and 0.5), and with `--no-sky`.
- **Done:** the script looks straight down, and turns uphill for the wall. So it does not depend on the slope, but it does depend on the default seed's terrain under that place: a change to terrain may need new numbers in `pi_mine_script`. The darkness was not looked into, as nothing in this stage shades anything; the cause is unknown, and whoever owns the look should see it.

### Issue 4: `run_render_frame` fails on the integration branch, without this stage

- **Expected:** `golden_pictures_match` passes, as Stage 12.2 recorded with 65 of 65.
- **Actual:** `tests/run/render/frame/index.c:1145: CHECK failed: wrong == 0u`: `start.png` 53247 of 57600 pixels past the tolerance, `north_low.png` 55941, `down.png` 54263.
- **Evidence it is not this stage:** `git archive` of `8864855`, the merged integration branch with none of this stage, built in a scratch directory and run with `TEST_FILTER=golden_pictures_match`, gives exactly those three numbers.
- **Cause:** not found. Nearly every pixel differs, so the look changed, or the world did, after these pictures were made. It may be the same thing as the darkness in Issue 3, but that was not established.
- **Done:** left alone and reported. The fixtures are outside this stage's paths, and writing new ones over an unexplained change would hide it. Someone has to look at the pictures, then fix the cause or rewrite them with `MC_GOLDEN_WRITE=1`.
