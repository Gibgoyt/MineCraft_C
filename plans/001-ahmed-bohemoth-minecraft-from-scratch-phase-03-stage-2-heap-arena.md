# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3, stage 2: Heap and arenas

**Status:** not started   (updated 2026-11-01)

## Goal

Reserve and release exist (2 MiB aligned, `MADV_HUGEPAGE`), with bump arenas and live counters.

**Wave:** W3. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-core-runtime.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/heap/`
- `run/{src,include}/arena/`
- `tests/run/heap/`
- `tests/run/arena/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement `heap_reserve`/`heap_release` in `run/src/heap/index.c`, the only code touching the OS allocator
- [ ] Align regions to 2 MiB, apply `MADV_HUGEPAGE`, and keep live counters
- [ ] Implement bump arenas with reset in `run/src/arena/index.c`
- [ ] Add tests in `tests/run/heap/index.c` and `tests/run/arena/index.c`, registered as `run_heap` and `run_arena`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_heap$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_arena$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
