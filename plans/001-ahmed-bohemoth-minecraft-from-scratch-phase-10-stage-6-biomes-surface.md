# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 6: Biomes and surface rules

**Status:** not started   (updated 2026-11-01)

## Goal

The biome table and surface rules exist for: plains, sunflower plains, all forests, taigas, snowy biomes, ice spikes, swamp, mangrove, desert, savanna, windswept, badlands, jungles, meadow, cherry grove, grove, slopes, all peaks, shores, rivers, all oceans, mushroom fields, dripstone, lush caves, deep dark.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/biome/`
- `run/{src,include}/terrain/surface/`
- `config/biomes/*`
- `tests/run/terrain/biome/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Define every listed biome in `config/biomes/<name>/index.yaml`
- [ ] Implement selection in `run/src/terrain/biome/index.c`
- [ ] Implement surface rules in `run/src/terrain/surface/index.c`
- [ ] Add the biome-coverage test, registered as `run_terrain_biome`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_biome$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
