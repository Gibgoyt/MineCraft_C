# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13, stage 1: Items and inventory

**Status:** not started   (updated 2026-11-01)

## Goal

Item tables, stacks, hotbar and a 36-slot inventory exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-items-survival.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/item/index.*` and `README.md` (the Owner tier)
- `config/items/*`
- `run/{src,include}/item/stack/`
- `run/{src,include}/item/inventory/`
- `tests/run/item/inventory/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `item` Owner tier: `run/src/item/index.c` with paired `item_init` and `item_deinit`, owning the regions and arenas its parts borrow
- [ ] Define items in `config/items/<name>/index.yaml`, and generate tables through `compile/src/game_tables`
- [ ] Implement stacks in `run/src/item/stack/index.c`
- [ ] Implement the hotbar and 36-slot inventory in `run/src/item/inventory/index.c`
- [ ] Register test `run_item_inventory`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_item_inventory$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
