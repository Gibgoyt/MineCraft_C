# 001-ahmed-bohemoth-minecraft-from-scratch — phase 16: Saving

**Status:** not started   (updated 2026-11-01)

## Goal

Worlds are saved as a seed plus changed-section deltas, along with player, inventory, entity and time state, safely and without frame hitches.

**Wave:** W9.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-stage-1-world-files.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-stage-2-state-autosave.md`

## Todo

- [ ] Stage 16.1: World files
- [ ] Stage 16.2: State and autosave
- [ ] Phase gate passed

## Verification

**Gate:** Kill during save loses at most one autosave interval; reload matches byte for byte.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_save' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
