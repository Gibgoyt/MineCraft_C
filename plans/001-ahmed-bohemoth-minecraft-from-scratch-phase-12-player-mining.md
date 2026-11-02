# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12: Player and mining

**Status:** in progress   (updated 2026-11-02). Every check an agent can run has passed. Waiting for Ahmed's play on a text console: see Verification and Issue 2.

## Goal

First playable: the player walks through the world, mines blocks, picks up the correct drops, and places blocks back.

**Wave:** W7.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-1-movement-physics.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-2-camera-targeting.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-3-mining-placing.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-4-dropped-items.md`

## Todo

- [x] Stage 12.1: Movement and physics
- [x] Stage 12.2: Camera and targeting
- [x] Stage 12.3: Mining and placing
- [ ] Stage 12.4: Dropped items: all of it but Ahmed's check with a real mouse
- [ ] Phase gate passed: the tests have, Ahmed's play has not been done

## Verification

**Gate:** Ahmed spawns, walks, mines dirt and picks up dirt, and places it back. Tests cover drop tables for every block.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_player|run_physics|run_entity_drop)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --test-world --walk    # not the bare command the plan had: Issue 1

## Commits

Appended as they land:

- `3b5541a`, `68cb32b`, `a3c0476`, `bb8a800`: the four stages, each in its own stage file.
- `1f5a7e8` Give drops to what water washes away, what sand buries and what decays, test real mouse buttons through to the hands, and stop returning 48-byte boxes by value.
- `f00b838` Record the Phase 12 gate check: what it found, what it fixed, and the play with a real mouse that waits for Ahmed.

## Gate check results

Run on 2026-11-02 in a clean build of this branch, which holds the integration branch at `7e14576`.

- `cmake --build --preset debug`: builds, with `standards_lint: clean` and `no recursion`.
- `ctest --preset debug`, all of it: 72 of 72 pass, including `standards_lint`, `clang_format`, `run_player_interaction`, `run_player_movement`, `run_physics`, `run_physics_raycast` and `run_entity_drop`. The gate's one `-R` with alternatives was refused by this session's command checker, so its three names were run one at a time, and within the full run.
- `run_player_interaction`, 4 more times: 0 failures.
- `cmake --build --preset release`, on this same tree: builds, and the gate's tests pass there.
- Before that last merge, at `352d842` with these fixes: the same tests pass on `sanitize` with no report, as does `run_world_sim`. That preset was not built again after the merge.
- Three deliberate bugs, one each for the new tests: a click between polls no longer reaching the right button, drops of what went by itself not rolled, lava giving a drop. Each failed the test meant for it, and the sources were restored.
- Looked for and not found: stubs, TODOs, modules nothing calls, tests that assert nothing. Every module of this phase is called from `game_sim_tick`, `game_loop_source` or `game_scene_tick`. A `README.md` with all its fields is in every new source directory. The earlier mutation runs of Stages 12.1, 12.3 and 12.4 were read, and only spot checks repeated.
- By eye: `--headless --test-world --walk` with a script that looks down and holds the left button. Dirt, grass and a sky are lit, and the outline sits on the block aimed at. The report says `the player mined 2 blocks and placed 0; the last mined was dirt, and the hand holds 2 of dirt`, and `0 bytes live`.
- `pgrep -af "$PWD/build"`: nothing of this worktree left running.
- **Not run:** KMS, a real mouse and real keys. Agents may not. Waiting for Ahmed.

## Issues found

Found by the gate check of 2026-11-02. The stage files have the rest: Stage 12.1 issues 3 and 7, Stage 12.3 issues 4 and 5, Stage 12.4 issues 4 and 7.

### Issue 1: the plan's gate command does not start a game

- **Expected:** `./build/release/bin/minecraft_c` lets Ahmed spawn and walk.
- **Actual:** with no flags `main_run` runs one round of jobs, logs `no game systems exist yet`, and exits. Its comment says that holds until a menu opens a world, which is Stage 14.4.
- **Done:** the Verification above now gives `--test-world --walk`, which is the program's way to walk. Left the default alone: `run_types` runs the bare program, which must stay away from the card. The master plan is unedited.

### Issue 2: what cannot be seen yet

- **Actual:** items on the ground are not drawn and a block being mined shows no crack, as Stage 09.5 is `not started`. So at Ahmed's check a mined block just goes, and the proof of pick up is that the right button then places it. The outline is this phase's CPU one: Stage 12.2 issue 1.
- **Done:** left for Stage 09.5, which is where the plan has them.
