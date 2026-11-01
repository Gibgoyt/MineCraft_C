# 001-ahmed-bohemoth-minecraft-from-scratch — phase 11, stage 2: Block simulation

**Status:** not started   (updated 2026-11-01)

## Goal

Water and lava flow, falling sand and gravel, leaf decay, and crop and grass spread on random ticks exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-light-block-simulation.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/world/tick/`
- `tests/run/world/tick/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement water and lava flow in `run/src/world/tick/index.c`
- [ ] Implement falling sand and gravel, and leaf decay
- [ ] Implement crop and grass spread on random ticks
- [ ] Register test `run_world_sim`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_sim$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
