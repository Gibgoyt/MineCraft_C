# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3, stage 1: Types, assert, log and main

**Status:** not started   (updated 2026-11-01)

## Goal

Sanctioned types, a CPUID check against build flags, and the entry point exist.

**Wave:** W3. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-core-runtime.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/types/`
- `run/{src,include}/assert/`
- `run/{src,include}/log/`
- `run/{src,include}/main/`
- `tests/run/types/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Define `float32_t` and `float64_t` in `run/include/types/index.h`, the single typedef site, using the Stage 01.1 result for CUDA
- [ ] Implement `run/src/assert/index.c` and `run/src/log/index.c`
- [ ] Write `run/src/main/index.c` with `int32_t main(int32_t, char **)` and `app_t app` on its stack
- [ ] Check CPUID against the build's SIMD flags at startup, and exit with a clear message on mismatch
- [ ] Add `tests/run/types/index.c`, registered as ctest `run_types`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_types$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
