# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14: UI and settings

**Status:** not started   (updated 2026-11-01)

## Goal

The HUD, all game screens, menus and settings work, including render distance in chunks.

**Wave:** W8.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-1-font-widgets.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-2-hud.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-3-game-screens.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-4-menus.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-5-settings.md`

## Todo

- [ ] Stage 14.1: Font and widgets
- [ ] Stage 14.2: HUD
- [ ] Stage 14.3: Game screens
- [ ] Stage 14.4: Menus
- [ ] Stage 14.5: Settings
- [ ] Phase gate passed

## Verification

**Gate:** Changing render distance in the menu takes effect without restarting, and persists.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_ui|run_settings)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/debug/bin/minecraft_c

## Commits

Appended as they land:

## Issues found

None.
