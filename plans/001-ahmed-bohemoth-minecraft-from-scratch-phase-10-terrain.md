# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10: Terrain, all Overworld terrains

**Status:** not started   (updated 2026-11-01)

## Goal

Every Overworld biome and terrain type generates deterministically from a seed inside `z = -250 ... +500`. Structures such as villages and strongholds are a later plan.

**Wave:** W6.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-1-seed-noise.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-2-climate.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-3-density.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-4-water-features.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-5-caves-ores.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-6-biomes-surface.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-7-features.md`

## Todo

- [ ] Stage 10.1: Seed and noise integration
- [ ] Stage 10.2: Climate fields
- [ ] Stage 10.3: 3D density
- [ ] Stage 10.4: Rivers, oceans and aquifers
- [ ] Stage 10.5: Caves and ores
- [ ] Stage 10.6: Biomes and surface rules
- [ ] Stage 10.7: Terrain features
- [ ] Phase gate passed

## Verification

**Gate:** The same seed gives byte-identical columns with 1 and 16 workers; a biome-coverage test finds every biome; bench recorded; Ahmed visually reviews a biome tour.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
