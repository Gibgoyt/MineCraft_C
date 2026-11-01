# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13, stage 2: Crafting, furnace and chest

**Status:** not started   (updated 2026-11-01)

## Goal

2x2 and 3x3 crafting, a full basic recipe set, furnace and chest exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-items-survival.md`.

**Owned paths** (edit only these, plus this file):

- `config/recipes/*`
- `run/{src,include}/item/crafting/`
- `run/{src,include}/item/smelting/`
- `run/{src,include}/item/container/`
- `tests/run/item/crafting/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Define recipes in `config/recipes/<name>/index.yaml`
- [ ] Implement 2x2 and 3x3 matching in `run/src/item/crafting/index.c`
- [ ] Implement the furnace in `run/src/item/smelting/index.c`
- [ ] Implement the chest in `run/src/item/container/index.c`
- [ ] Register test `run_item_crafting`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_item_crafting$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
