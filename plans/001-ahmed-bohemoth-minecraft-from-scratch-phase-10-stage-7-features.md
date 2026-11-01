# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 7: Terrain features

**Status:** not started   (updated 2026-11-01)

## Goal

All tree types, grass, flowers, cactus, sugar cane, ice, snow and lakes exist, with cross-chunk placement as a pure function of seed.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/feature/`
- `bench/terrain/`
- `tests/run/terrain/feature/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement all features in `run/src/terrain/feature/index.c`, placing across chunks purely from the seed
- [ ] Test identical output regardless of generation order and worker count, registered as `run_terrain_feature`
- [ ] Write `bench/terrain/index.c`, and record results against the 1000 columns/s target
- [ ] Give Ahmed the biome-tour command for the visual review
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_feature$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
