# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3, stage 3: Threads, time and file

**Status:** not started   (updated 2026-11-01)

## Goal

Pinning, rings, monotonic clock and file wrappers exist.

**Wave:** W3. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-core-runtime.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/thread/`
- `run/{src,include}/time/`
- `run/{src,include}/file/`
- `tests/run/thread/`
- `tests/run/time/`
- `tests/run/file/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement pinned threads, physical cores first, in `run/src/thread/index.c`, using counts from `machine.yaml`
- [ ] Implement single-producer rings under `run/src/thread/`
- [ ] Implement the monotonic clock in `run/src/time/index.c`
- [ ] Implement file wrappers in `run/src/file/index.c`
- [ ] Add tests registered as `run_thread`, `run_time`, `run_file`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_thread$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_time$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_file$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
