# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 7: Combat

**Status:** not started   (updated 2026-11-01)

## Goal

Player attack, knockback, mob damage and death exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/combat/`
- `run/{src,include}/mob/combat/`
- `tests/run/mob/combat/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement player attack in `run/src/player/combat/index.c`
- [ ] Implement knockback, mob damage and death in `run/src/mob/combat/index.c`
- [ ] Register test `run_mob_combat`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_combat$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
