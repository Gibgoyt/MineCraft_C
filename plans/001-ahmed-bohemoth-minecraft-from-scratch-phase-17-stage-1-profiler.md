# 001-ahmed-bohemoth-minecraft-from-scratch — phase 17, stage 1: Profiler

**Status:** not started   (updated 2026-11-01)

## Goal

A built-in profiler exists, with per-thread scopes, kernel timers and trace dump. `perf` is not installed, so we build our own.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-performance-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/profiler/`
- `cuda/{src,include}/frame/` timers
- `tests/run/profiler/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement per-thread scopes and trace dump in `run/src/profiler/index.c`
- [ ] Add kernel timers
- [ ] Register test `run_profiler`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_profiler$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
