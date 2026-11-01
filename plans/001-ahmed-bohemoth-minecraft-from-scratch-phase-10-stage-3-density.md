# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 3: 3D density

**Status:** not started   (updated 2026-11-01)

## Goal

3D density with height splines rescaled to `z` in `[-250, 500]`, bedrock and sea level 0 exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/density/`
- `tests/run/terrain/density/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement density and height splines in `run/src/terrain/density/index.c`
- [ ] Place bedrock at `z = -250`, and put water's top block at `z = 0`
- [ ] Test the range limits, registered as `run_terrain_density`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_density$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
