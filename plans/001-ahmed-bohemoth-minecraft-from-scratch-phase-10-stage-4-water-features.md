# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 4: Rivers, oceans and aquifers

**Status:** not started   (updated 2026-11-01)

## Goal

Rivers, oceans, beaches, aquifers and the lava level exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/hydrology/`
- `tests/run/terrain/hydrology/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement rivers, oceans and beaches in `run/src/terrain/hydrology/index.c`
- [ ] Implement aquifers and the lava level
- [ ] Register test `run_terrain_hydrology`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_hydrology$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
