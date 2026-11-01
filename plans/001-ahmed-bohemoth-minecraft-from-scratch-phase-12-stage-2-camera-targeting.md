# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 2: Camera and targeting

**Status:** not started   (updated 2026-11-01)

## Goal

Camera, mouse look, and DDA block targeting with outline exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/camera/`
- `run/{src,include}/physics/raycast/`
- `tests/run/physics/raycast/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement camera and mouse look in `run/src/player/camera/index.c`
- [ ] Implement DDA block targeting in `run/src/physics/raycast/index.c`
- [ ] Connect the target to the block outline
- [ ] Register test `run_physics_raycast`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_physics_raycast$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
