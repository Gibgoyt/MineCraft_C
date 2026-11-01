# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15: Mobs

**Status:** not started   (updated 2026-11-01)

## Goal

Zombies, skeletons and creepers spawn, behave, fight and drop items, and the player can fight back.

**Wave:** W8.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-1-entity-store.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-2-pathfinding.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-3-spawning.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-4-zombie.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-5-skeleton.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-6-creeper.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-7-combat.md`

## Todo

- [ ] Stage 15.1: Entity store
- [ ] Stage 15.2: Pathfinding
- [ ] Stage 15.3: Spawning
- [ ] Stage 15.4: Zombie
- [ ] Stage 15.5: Skeleton
- [ ] Stage 15.6: Creeper
- [ ] Stage 15.7: Combat
- [ ] Phase gate passed

## Verification

**Gate:** Bench with 100 mobs meets target (sim tick at most 5 ms); Ahmed survives (or doesn't) a night.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_(entity|mob)' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_mobs
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/release/bin/minecraft_c

## Commits

Appended as they land:

## Issues found

None.
