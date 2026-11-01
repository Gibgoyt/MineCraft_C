# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4: Math and SIMD

**Status:** not started   (updated 2026-11-01)

## Goal

The scalar vector library and NASM batch kernels exist. Every kernel matches a scalar reference and has proven its gain.

**Wave:** W4.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-stage-1-vector-library.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-stage-2-copy-blend-bitmask.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-stage-3-noise-vec-batch.md`

## Todo

- [ ] Stage 04.1: Vector library
- [ ] Stage 04.2: Copy, blend and bitmask kernels
- [ ] Stage 04.3: Noise and batch kernels
- [ ] Phase gate passed

## Verification

**Gate:** Each NASM kernel matches a scalar C reference in tests; benchmarks prove the gain, or the kernel is removed.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_math|asm_).*' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_simd
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
