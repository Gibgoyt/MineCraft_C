# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 5: Skeleton

**Status:** not started   (updated 2026-11-01)

## Goal

Skeleton keeps distance, fires arrows as projectiles, and drops items.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/mob/skeleton/`
- `run/{src,include}/entity/projectile/`
- `tests/run/mob/skeleton/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement keep-distance behaviour and drops in `run/src/mob/skeleton/index.c`
- [ ] Implement arrows in `run/src/entity/projectile/index.c`
- [ ] Register test `run_mob_skeleton`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_skeleton$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
