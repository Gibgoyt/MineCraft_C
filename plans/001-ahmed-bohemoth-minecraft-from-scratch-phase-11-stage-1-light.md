# 001-ahmed-bohemoth-minecraft-from-scratch — phase 11, stage 1: Light

**Status:** not started   (updated 2026-11-01)

## Goal

Sky and block light flood fill across sections exists, with incremental updates on edit.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-light-block-simulation.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/world/light/`
- `tests/run/world/light/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement flood fill across sections in `run/src/world/light/index.c`
- [ ] Implement incremental updates on edit
- [ ] Compare incremental light to full recompute on random edits, registered as `run_world_light`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_light$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
