# 001-ahmed-bohemoth-minecraft-from-scratch — phase 16, stage 2: State and autosave

**Status:** not started   (updated 2026-11-01)

## Goal

Player, inventory, entity and time persistence exists, with background autosave and no frame hitch.

**Wave:** W9. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-saving.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/save/state/`
- `run/{src,include}/save/autosave/`
- `tests/run/save/state/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Persist player, inventory, entity and time state in `run/src/save/state/index.c`
- [ ] Implement background autosave on workers in `run/src/save/autosave/index.c`
- [ ] Measure frame time during autosave
- [ ] Register test `run_save_state`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_save_state$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
