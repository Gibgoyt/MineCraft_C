# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14, stage 2: HUD

**Status:** not started   (updated 2026-11-01)

## Goal

HUD exists: crosshair, hotbar, hearts, hunger, and debug overlay with FPS and per-stage times.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-ui-settings.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/ui/hud/`
- `tests/run/ui/hud/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement crosshair, hotbar, hearts and hunger in `run/src/ui/hud/index.c`
- [ ] Implement the debug overlay with FPS and per-stage times
- [ ] Add golden images, registered as `run_ui_hud`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_ui_hud$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
