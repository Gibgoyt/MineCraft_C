# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 1: Entity store

**Status:** not started   (updated 2026-11-01)

## Goal

SoA entity store, spatial grid, shared AABB physics and part-based animation data exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/entity/store/`
- `run/{src,include}/entity/grid/`
- `run/{src,include}/entity/animation/`
- `bench/mobs/`
- `tests/run/entity/store/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement the SoA store in `run/src/entity/store/index.c`
- [ ] Implement the spatial grid in `run/src/entity/grid/index.c`
- [ ] Share AABB physics with `run/src/physics`
- [ ] Add part-based animation data in `run/src/entity/animation/index.c`
- [ ] Write `bench/mobs/index.c` for the 100-mob target
- [ ] Register test `run_entity_store`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_entity_store$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
