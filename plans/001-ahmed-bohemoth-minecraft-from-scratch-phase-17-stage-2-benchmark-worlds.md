# 001-ahmed-bohemoth-minecraft-from-scratch — phase 17, stage 2: Benchmark worlds

**Status:** not started   (updated 2026-11-01)

## Goal

Standard benchmark worlds and paths exist, with a results history in `bench/`.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-performance-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `bench/world/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Define standard worlds and camera paths in `bench/world/index.c`
- [ ] Record results history under `bench/world/`
- [ ] Add `bench/world/README.md`

## Verification

    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
