# 001-ahmed-bohemoth-minecraft-from-scratch — phase 16, stage 1: World files

**Status:** not started   (updated 2026-11-01)

## Goal

World files exist from seed plus changed-section deltas, with a versioned header and crash-safe rename.

**Wave:** W9. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-saving.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/save/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/save/world/`
- `tests/run/save/world/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `save` Owner tier: `run/src/save/index.c` with paired `save_init` and `save_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement delta files with a versioned header in `run/src/save/world/index.c`
- [ ] Write through a crash-safe rename
- [ ] Test reload byte for byte, and kill during save, registered as `run_save_world`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_save_world$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
