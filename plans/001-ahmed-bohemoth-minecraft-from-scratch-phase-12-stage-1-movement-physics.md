# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 1: Movement and physics

**Status:** not started   (updated 2026-11-01)

## Goal

Swept AABB collision, walk, sprint, sneak, jump, swim, climb and fall exist, on a fixed tick with render interpolation.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/physics/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/physics/*`
- `run/{src,include}/player/movement/`
- `tests/run/physics/`
- `tests/run/player/movement/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `player` Owner tier: `run/src/player/index.c` with paired `player_init` and `player_deinit`, owning the regions and arenas its parts borrow
- [ ] Create the `physics` Owner tier: `run/src/physics/index.c` with paired `physics_init` and `physics_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement swept AABB collision under `run/src/physics/`
- [ ] Implement all movement modes in `run/src/player/movement/index.c`
- [ ] Run on the 20 Hz tick with render interpolation
- [ ] Register tests `run_physics` and `run_player_movement`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_physics$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_player_movement$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
