# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 5: Caves and ores

**Status:** not started   (updated 2026-11-01)

## Goal

Caves (cheese, spaghetti, noodle) and ore tables by depth exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/cave/`
- `run/{src,include}/terrain/ore/`
- `tests/run/terrain/cave/`
- `tests/run/terrain/ore/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement cheese, spaghetti and noodle caves in `run/src/terrain/cave/index.c`
- [ ] Implement ore tables by depth in `run/src/terrain/ore/index.c`
- [ ] Register tests `run_terrain_cave` and `run_terrain_ore`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_cave$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_ore$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
