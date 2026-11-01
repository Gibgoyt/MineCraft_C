# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14, stage 3: Game screens

**Status:** not started   (updated 2026-11-01)

## Goal

Inventory, crafting, furnace and chest screens exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-ui-settings.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/ui/screen_inventory/`
- `run/{src,include}/ui/screen_crafting/`
- `run/{src,include}/ui/screen_furnace/`
- `run/{src,include}/ui/screen_chest/`
- `tests/run/ui/screens/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement each screen in its own directory
- [ ] Add golden images and input replay, registered as `run_ui_screens`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_ui_screens$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
