# 001-ahmed-bohemoth-minecraft-from-scratch — phase 16: Saving

**Status:** in progress   (updated 2026-11-03). All an agent can run has passed. Awaiting Ahmed: pulling the plug on a real save, and seeing a far field and a refused world on a real display.

## Goal

Worlds are saved as a seed plus changed-section deltas, along with player, inventory, entity and time state, safely and without frame hitches.

**Wave:** W9.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-stage-1-world-files.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-stage-2-state-autosave.md`

## Todo

- [x] Stage 16.1: World files
- [x] Stage 16.2: State and autosave
- [x] Phase gate, all an agent can run: a kill during a save loses at most an interval, and a reload matches byte for byte
- [ ] Ahmed: power pulled during a save, on the real disk, loses at most an interval and the world opens
- [ ] Ahmed: under KMS, a far-off build is seen from a distance, and a refused world says so on the list

## Verification

**Gate:** Kill during save loses at most one autosave interval; reload matches byte for byte.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_save' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

The test names `^run_save` now include `run_save_power`, which is what says anything of a power cut short of one.

For Ahmed, who has the display and the plug. Under KMS, from a text console with no display server:

    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    build/release/bin/minecraft_c --menu    # make a world, build something tall, walk or fly over 40 chunks away, look back: it is there

    # Close, then corrupt a copy of a world's blocks/manifest in saves/ and choose it: the list says "Could not open it: damaged".

    # Dig for a minute, wait past 30 s for an autosave, and cut the mains. On reboot, choose the world again: what was there at the last autosave is there.

## Results

Debug, clean build of this tree, through `tools/test-or-build.sh`:

    cmake --build --preset debug                 no errors, lint and recursion check clean
    ctest --preset debug -j1, no filter          95 of 95 passed, 1185.41 s
    ctest --preset debug -R '^run_save'          run_save_power, run_save_state, run_save_state_game, run_save_world all passed
    pgrep for this worktree's build              nothing

Sanitize, whole suite, after a clean build: 95 of 95 passed, 1442.50 s, no sanitizer report. Release, `cmake --build --preset release` whole: builds, and `run_save_power`, `run_save_state`, `run_save_state_game`, `run_save_world` and `run_world_sim` pass. The rest of the suite was not run in release.

Thread sanitizer and power cuts: Stage 16.2's issue 10 and Stage 16.1's issue 9.

## Commits

Appended as they land:

- `58451ab` Save fluid levels with the blocks so a reloaded world does not flood, keep the block tick count, and spill a lost chest before a save.
- `6853ca5` Wake what a save held waiting, show saved changes in the far field, keep a refused world from ending the game, check what a power cut leaves against every fsync, add a thread sanitizer preset, and let the release build link.
## Issues found

1. **Hunting at the gate:** both stages said done. No stub, no TODO, and no module nothing calls; every box had code behind it. What they had left were the gaps their own files named: logged, and fixed, in Stage 16.1's issues 8 and 9 and Stage 16.2's issues 9, 10 and 11.
2. **An earlier gate check's edits to READMEs were left uncommitted in its own worktree,** and its results were not recorded. Expected: `git merge` of the integration branch has them. Actual: it has `58451ab` and no more. Its worktree was not looked into. Done: this check wrote its own READMEs and ran everything afresh.
3. **The release build did not link.** Expected: `cmake --build --preset release` works. Actual: `run/src/main/index.c:874:1: error: the frame size of 16448 bytes is larger than 16384 bytes [-Werror=frame-larger-than=]`, at link under `-flto`, with `app_t` at its integration size of 15808. The phase gate's commands are all debug, so nothing had asked; this check first made it worse, 16512, by growing `save_t` and `game_session_t`, and took that back: the far field's ring lives in the save's arena, and both are their old sizes. The integration tree itself was not built separately to compare. Done: `[[gnu::noinline]]` on what `main` calls, so that their locals stay in their own frames. Release now builds: see Results.
4. **Process:** the guard refused several compound commands, as too complex to verify as staying in this worktree. Each was re-run as plain, separate commands inside it, as the refusal said to.
