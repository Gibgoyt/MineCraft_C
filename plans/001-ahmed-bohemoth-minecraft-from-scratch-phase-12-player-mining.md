# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12: Player and mining

**Status:** not started   (updated 2026-11-01)

## Goal

First playable: the player walks through the world, mines blocks, picks up the correct drops, and places blocks back.

**Wave:** W7.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-1-movement-physics.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-2-camera-targeting.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-3-mining-placing.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-stage-4-dropped-items.md`

## Todo

- [ ] Stage 12.1: Movement and physics
- [ ] Stage 12.2: Camera and targeting
- [ ] Stage 12.3: Mining and placing
- [ ] Stage 12.4: Dropped items
- [ ] Phase gate passed

## Verification

**Gate:** Ahmed spawns, walks, mines dirt and picks up dirt, and places it back. Tests cover drop tables for every block.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_player|run_physics|run_entity_drop)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c

## Commits

Appended as they land:

## Issues found

None.
