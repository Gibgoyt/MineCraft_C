# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3, stage 4: Job system

**Status:** not started   (updated 2026-11-01)

## Goal

A work-stealing job system exists, with per-thread Job arenas.

**Wave:** W3. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-core-runtime.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/job/`
- `tests/run/job/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement work-stealing queues in `run/src/job/index.c`, using `MC_THREADS_WORKERS`
- [ ] Give each worker a Job arena that resets at its job boundary
- [ ] Add a stress test on all workers in `tests/run/job/index.c`, registered as `run_job`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_job$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 600 ctest --preset sanitize -R '^run_job$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
