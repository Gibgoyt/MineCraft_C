# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 4: Dropped items

**Status:** not started   (updated 2026-11-01)

## Goal

The dropped-item entity, its physics, merging and pickup exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/entity/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/entity/item_drop/`
- `tests/run/entity/item_drop/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `entity` Owner tier: `run/src/entity/index.c` with paired `entity_init` and `entity_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement the entity and physics in `run/src/entity/item_drop/index.c`
- [ ] Implement merging and pickup
- [ ] Register test `run_entity_drop`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_entity_drop$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
