# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4, stage 3: Noise and batch kernels

**Status:** not started   (updated 2026-11-01)

## Goal

8-wide gradient noise, batch transforms and generated struct offsets exist, and a benchmark measures them.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-math-simd.md`.

**Owned paths** (edit only these, plus this file):

- `asm/{src,include}/noise/`
- `asm/{src,include}/vec_batch/`
- `compile/{src,include}/struct_offsets/`
- `bench/simd/`
- `tests/asm/noise/`
- `tests/asm/vec_batch/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement 8-wide gradient noise in `asm/src/noise/index.asm`, with a scalar C reference
- [ ] Implement batch transforms in `asm/src/vec_batch/index.asm`
- [ ] Implement `compile/src/struct_offsets/index.c`, generating `.inc` offsets from C structs
- [ ] Write `bench/simd/index.c` comparing every kernel against its scalar reference; remove any kernel with no gain
- [ ] Register tests `asm_noise` and `asm_vec_batch`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^asm_noise$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^asm_vec_batch$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_simd
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
