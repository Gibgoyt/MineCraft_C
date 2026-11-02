# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4: Math and SIMD

**Status:** done   (updated 2026-11-02)

## Goal

The scalar vector library and NASM batch kernels exist. Every kernel matches a scalar reference and has proven its gain.

**Wave:** W4.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-stage-1-vector-library.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-stage-2-copy-blend-bitmask.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-stage-3-noise-vec-batch.md`

## Todo

- [x] Stage 04.1: Vector library
- [x] Stage 04.2: Copy, blend and bitmask kernels
- [x] Stage 04.3: Noise and batch kernels
- [x] Phase gate passed

## Verification

**Gate:** Each NASM kernel matches a scalar C reference in tests; benchmarks prove the gain, or the kernel is removed.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_math|asm_).*' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_simd
    pgrep -a minecraft_c    # must print nothing

## Gate check

Run on 2026-11-02 by the skeptical gate checker, from a deleted `build/`, on the integration branch merged in:

- Debug: the build is clean. `ctest --preset debug` is 35 of 35, and the gate's `^(run_math|asm_).*` is 17 of 17.
- Release: the build is clean, and `run_math`, `asm_`, `compile_struct_offsets` and `run_types` are 19 of 19. So are they in sanitize.
- `build/release/bin/bench_simd` rc 0: "Every gated row gained, and every kernel gave its reference's bytes."
- `pgrep -a minecraft_c` rc 1, and `pgrep -af` on this worktree's `build/` shows only its own shell.

Beyond the listed commands:

- Every kernel's source was read against its reference and its prototype. No stub, TODO or empty test was found. Every function in `run/include/math/` is called from `tests/run/math/`. Every kernel runs at startup through `simd_self_check`, from `simd_init`, from `main`, and `math_init` likewise. No caller outside tests and the self-check exists yet for the vector library or the kernels, which is expected: their callers come in phases 7 to 10.
- The pinned values in `asm_noise` were recomputed by a new throwaway Python program with exact rationals, rounded to binary32 at each step, written from `simd_reference_noise3` and not from the earlier one. All 6 matched.
- Ten more defects, none from the stages' own tables, were put in together, built and undone: premul sum wrapping, a dropped `bzhi` in the u16 tail, a wrong column in the vec4 tail, a lost copy remainder, truncation for floor in the noise, a wrong perspective depth term, a wrong `quat_from_mat4` sign, a raw remainder in floor-mod, swapped clamps in `aabb_closest_point`, and a wrong `t_out` for a negative ray. `asm_bitmask`, `asm_blend`, `asm_copy`, `asm_noise`, `asm_simd`, `asm_vec_batch`, `run_math_aabb`, `run_math_ivec3`, `run_math_mat4`, `run_math_quat` and `run_math_ray` all failed, each on a test about that defect.

Nothing in this phase waits on Ahmed. The copy's speed on real scanout memory is measured by Stage 01.4's Ahmed-only run, and stays recorded there.

## Commits

Appended as they land:

## Issues found

The gate checker's defects are logged in the stage they belong to: Stage 04.2 issues 7 and 8, and Stage 04.3 issues 9 and 10.
