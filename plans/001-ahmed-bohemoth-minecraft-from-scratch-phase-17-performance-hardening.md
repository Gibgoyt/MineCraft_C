# 001-ahmed-bohemoth-minecraft-from-scratch — phase 17: Performance hardening

**Status:** not started   (updated 2026-11-01)

## Goal

The game is profiled and tuned, and the plan's target table is filled in with measured numbers, with misses stated plainly.

**Wave:** W10.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-1-profiler.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-2-benchmark-worlds.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-3-cost-pass.md`

## Todo

- [ ] Stage 17.1: Profiler
- [ ] Stage 17.2: Benchmark worlds
- [ ] Stage 17.3: Top-cost pass
- [ ] Phase gate passed

## Verification

**Gate:** Section 2 target table filled in with measured numbers, misses stated plainly.

    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
