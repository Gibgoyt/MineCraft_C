# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 1: Seed and noise integration

**Status:** not started   (updated 2026-11-01)

## Goal

Seeded RNG, per-position hashing and NASM noise integration exist, with fixed-seed determinism across worker counts.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/terrain/rng/`
- `run/{src,include}/terrain/noise/`
- `tests/run/terrain/seed/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `terrain` Owner tier: `run/src/terrain/index.c` with paired `terrain_init` and `terrain_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement seeded RNG and per-position hashing in `run/src/terrain/rng/index.c`
- [ ] Wrap `asm/src/noise` for bulk sampling in `run/src/terrain/noise/index.c`
- [ ] Test identical output with 1 and 16 workers, registered as `run_terrain_seed`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_seed$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
