# 001-ahmed-bohemoth-minecraft-from-scratch — phase 7, stage 3: World map and edits

**Status:** not started   (updated 2026-11-01)

## Goal

A toroidal column map, streaming by distance, edit log, dirty flags and snapshots exist.

**Wave:** W5. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-world-data.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/world/map/`
- `run/{src,include}/world/edit/`
- `tests/run/world/map/`
- `tests/run/world/edit/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement the toroidal column map and distance-based streaming in `run/src/world/map/index.c`
- [ ] Implement the edit log and dirty flags in `run/src/world/edit/index.c`
- [ ] Implement snapshots for publishing state to the renderer
- [ ] Add random-edit tests, registered as `run_world_map` and `run_world_edit`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_map$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_edit$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
