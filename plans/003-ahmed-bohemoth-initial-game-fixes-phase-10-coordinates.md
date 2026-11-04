# 003-ahmed-bohemoth-initial-game-fixes — phase 10: Coordinates on screen

**Status:** not started   (updated 2026-11-04)

## Goal

A setting shows your x, y and z on screen while you play, and is remembered.

**Ahmed's words:** "co-ordinates option also available in settings somewhere please"

## How it is today

Nothing shows your position. The F3 overlay (`run/src/ui/hud/index.c:222`) shows only FPS and timings. Your position appears only in the terminal, when you leave a world.

## Todo

- [ ] Add `show_coordinates` to `settings_values_t` in `run/include/settings/persist/index.h`, using a spare byte in `_pad0`, and read and write it in `run/src/settings/persist/`
- [ ] Add a checkbox in `run/src/ui/screen_settings/index.c`, beside VSync
- [ ] Send the player's position from the sim to the HUD in `run/src/game/`, as the other HUD values are sent
- [ ] Draw `x y z` in the top-left corner in `run/src/ui/hud/`, as whole blocks. z is up, with 0 at sea level, so the numbers match the world limits
- [ ] Also add them to the F3 overlay, so they show whatever the setting says
- [ ] Add tests in `tests/run/settings/` that the setting survives a save and load, and that a settings file without it still loads
- [ ] Add a golden picture in `tests/run/ui/hud/`, on and off, and a test in `tests/run/ui/screens/` that the checkbox changes it

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_settings|run_ui_hud|run_ui_screens|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: turn it on in Settings, walk and dig down to see the numbers change, then restart and check it's still on.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**On by default, for now.** It's useful while you're testing the world. It's one line to change.

**Needs no new blocks or items,** so it can be built any time. It's small, and helps judge phase 8's terrain, so I'd build it early.
