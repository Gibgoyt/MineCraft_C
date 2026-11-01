# 001-ahmed-bohemoth-minecraft-from-scratch — phase 17, stage 3: Top-cost pass

**Status:** not started   (updated 2026-11-01)

## Goal

A pass has been made over the top costs: memory layout, ray coherence, tile order, thread placement and upload budget.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-performance-hardening.md`.

**Owned paths** (edit only these, plus this file):

- Paths named by profiler results, declared in this file before editing

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Rank costs from the profiler, and record them here
- [ ] Work through memory layout, ray coherence, tile order, thread placement and upload budget
- [ ] Record before and after numbers for every change
- [ ] Record measured numbers for each row of the plan's section 2 target table in this file, with misses stated plainly

## Verification

    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world
    tools/test-or-build.sh timeout 600 ctest --preset debug --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
