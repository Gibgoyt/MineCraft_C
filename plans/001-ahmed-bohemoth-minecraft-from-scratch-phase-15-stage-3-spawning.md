# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 3: Spawning

**Status:** not started   (updated 2026-11-01)

## Goal

Spawning by light level, surface and distance, plus despawn rules, exists.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/mob/spawn/`
- `config/mobs/*`
- `tests/run/mob/spawn/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Define mobs in `config/mobs/<name>/index.yaml`
- [ ] Implement spawn and despawn rules in `run/src/mob/spawn/index.c`
- [ ] Register test `run_mob_spawn`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_spawn$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
