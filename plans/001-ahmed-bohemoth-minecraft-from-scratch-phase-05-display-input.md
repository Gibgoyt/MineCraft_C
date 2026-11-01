# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5: Display and input

**Status:** not started   (updated 2026-11-01)

## Goal

The game can show pixels through hand-rolled KMS on a text console, read evdev input directly, always restore the console, and run headless for agents.

**Wave:** W4.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-1-device-mode-flip.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-2-vt-guard.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-3-evdev-input.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-4-headless-backend.md`

## Todo

- [ ] Stage 05.1: Device, mode and flip
- [ ] Stage 05.2: VT guard
- [ ] Stage 05.3: Evdev input
- [ ] Stage 05.4: Headless backend
- [ ] Phase gate passed

## Verification

**Gate:** Ahmed sees the pattern, moves a cursor square, switches VT and back, and the console survives a forced crash. Running from inside X fails fast with a clear message.

    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 60 build/debug/bin/minecraft_c --test-pattern
    # Headless and agent-side:
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_(display|input)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
