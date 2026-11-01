# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 2: Pathfinding

**Status:** not started   (updated 2026-11-01)

## Goal

Bounded A* pathfinding, batched on workers, with path caching exists.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/mob/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/mob/path/`
- `tests/run/mob/path/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `mob` Owner tier: `run/src/mob/index.c` with paired `mob_init` and `mob_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement bounded A* in `run/src/mob/path/index.c`
- [ ] Batch on workers, and cache paths
- [ ] Register test `run_mob_path`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_path$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
