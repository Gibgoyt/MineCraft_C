# 001-ahmed-bohemoth-minecraft-from-scratch — phase 11: Light and block simulation

**Status:** not started   (updated 2026-11-01)

## Goal

Sky and block light update correctly on edits, and fluids, falling blocks and random ticks work.

**Wave:** W7.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-stage-1-light.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-stage-2-block-simulation.md`

## Todo

- [ ] Stage 11.1: Light
- [ ] Stage 11.2: Block simulation
- [ ] Phase gate passed

## Verification

**Gate:** Incremental light matches full recompute on random edits.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_(light|sim)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
