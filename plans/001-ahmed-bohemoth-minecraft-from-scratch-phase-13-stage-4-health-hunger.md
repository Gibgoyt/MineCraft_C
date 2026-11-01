# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13, stage 4: Health and hunger

**Status:** not started   (updated 2026-11-01)

## Goal

Health, hunger, food, damage, death and respawn, and bed exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-items-survival.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/survival/`
- `tests/run/player/survival/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement health, hunger, food and damage in `run/src/player/survival/index.c`
- [ ] Implement death and respawn
- [ ] Implement the bed
- [ ] Register test `run_player_survival`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_player_survival$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
