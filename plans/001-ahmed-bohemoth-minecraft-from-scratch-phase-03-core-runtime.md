# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3: Core runtime

**Status:** not started   (updated 2026-11-01)

## Goal

The sanctioned types, entry point, heap, arenas, threads, rings, clock, file wrappers and job system exist under `run/`, and they obey the memory law.

**Wave:** W3.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-1-types-assert-log-main.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-2-heap-arena.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-3-thread-time-file.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-4-job-system.md`

## Todo

- [ ] Stage 03.1: Types, assert, log and main
- [ ] Stage 03.2: Heap and arenas
- [ ] Stage 03.3: Threads, time and file
- [ ] Stage 03.4: Job system
- [ ] Phase gate passed

## Verification

**Gate:** Sanitize preset clean; job stress test on all workers; heap counters zero at exit.

    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 600 ctest --preset sanitize --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
