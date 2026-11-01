# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14, stage 5: Settings

**Status:** not started   (updated 2026-11-01)

## Goal

Settings exist for render distance (2-512 chunks), full-detail radius, FOV, sensitivity, vsync/uncapped, each graphics effect, render scale and worker count (up to the compile-time maximum), saved by our own runtime parser, since `compile/` is not linked at runtime.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-ui-settings.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/settings/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/settings/*`
- `run/{src,include}/ui/screen_settings/`
- `tests/run/settings/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `settings` Owner tier: `run/src/settings/index.c` with paired `settings_init` and `settings_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement every listed setting and the runtime parser under `run/src/settings/`
- [ ] Implement the settings screen in `run/src/ui/screen_settings/index.c`
- [ ] Apply render distance without restarting
- [ ] Test save and reload, registered as `run_settings`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_settings$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/debug/bin/minecraft_c

## Commits

Appended as they land:

## Issues found

None.
