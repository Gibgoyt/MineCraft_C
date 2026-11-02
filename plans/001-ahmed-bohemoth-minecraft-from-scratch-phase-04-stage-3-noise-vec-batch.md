# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4, stage 3: Noise and batch kernels

**Status:** done   (updated 2026-11-02)

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

- [x] Implement 8-wide gradient noise in `asm/src/noise/index.asm`, with a scalar C reference
- [x] Implement batch transforms in `asm/src/vec_batch/index.asm`
- [x] Implement `compile/src/struct_offsets/index.c`, generating `.inc` offsets from C structs
- [x] Write `bench/simd/index.c` comparing every kernel against its scalar reference; remove any kernel with no gain
- [x] Register tests `asm_noise` and `asm_vec_batch`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

Wiring outside the owned paths, each the smallest that lets this module be called:

- `run/include/simd/{noise,vec_batch}/`: the prototypes, where `plans/CODING_STANDARD.md` section 7 puts them.
- `run/{src,include}/simd/reference/`: the scalar references.
- `run/src/simd/index.c`: `simd_self_check` now runs noise and vec_batch, since it promises every kernel. `README.md` there too.
- `cmake/targets.cmake`: runs `struct_offsets` before `mc_asm`, and finds `bench/**/index.c`.
- `cmake/standards_lint.cmake`: benchmarks wait for the lint and go through the recursion check; see Issue 2.
- `tests/compile/struct_offsets/`: the tool's own test, where `plans/REPO_STRUCTURE.md` section 10 puts it.
- `plans/REPO_STRUCTURE.md`: the benchmark row, its output path, and where the headers are listed.

## What was built

- `noise3_batch8(out, xs, ys, zs, count, seed)`: 3D gradient noise with Perlin's improved gradients, FMA, and a hash in place of
  a table. Bit for bit what `simd_reference_noise3` gives.
- `vec_batch_transform_points`, `vec_batch_transform_directions`, `vec_batch_mul_vec4`: an array by one matrix. Bit for bit what
  `mat4_transform_point`, `mat4_transform_direction` and `mat4_mul_vec4` give, so no FMA there.
- `struct_offsets`: reads `typedef struct` from `math/vec3`, `math/vec4` and `math/mat4`. `asm/include/vec_batch/index.inc` takes
  every offset from its `index.inc`, and `tests/asm/vec_batch/` compiles its `index.h`, which repeats each number as a `static_assert`.
- `bench_simd`: every kernel against its reference, comparing outputs too. It exits 1 if a gated row has no gain.

## Results

Run on 2026-11-02. Verification ran from a deleted `build/`, and the two ctest lines for other presets ran just before that
delete, on the same code:

- Debug: the build, and every line of Verification, pass. The whole suite is 31 of 31. `pgrep -a minecraft_c` rc 1.
- `ctest --preset sanitize -R '^(asm_|compile_struct_offsets$|run_types$|run_math)'`: 18 of 18. `ctest --preset release` on
  `^(asm_|compile_struct_offsets$|run_types$)`: 9 of 9, so the bit-for-bit claims hold at `-O3 -flto` too.
- `bench_simd` rc 0, `NEEDED` is libc, libm and ld-linux only. No kernel had to be removed. Numbers below.
- The pinned values in `asm_noise` were also computed by a separate throwaway Python program with exact rational FMA, and all 6
  gave the same bits as the kernel and the reference.

Tests catch broken kernels. Each change was made, built and undone, by script:

| Change | Result |
|---|---|
| Wrong negate bit in the gradient | `asm_noise` and `asm_simd` fail |
| Last noise lerp as multiply then add, not fused | `asm_noise` and `asm_simd` fail |
| Second hash round dropped from the kernel only | `asm_noise` and `asm_simd` fail |
| Noise tail mask of all ones | `asm_noise` fails |
| A vec3 store shuffle swapped | `asm_vec_batch` and `asm_simd` fail |
| The tail loses its z translation | `asm_vec_batch` and `asm_simd` fail |
| The 8-wide load reads one float past the end | `asm_vec_batch` dies of SIGSEGV at the guard page |
| The vec4 tail stores a whole YMM | `asm_vec_batch` fails |
| `struct_offsets` gives every offset 4 too far | The build stops at `%fatal` in `asm/include/vec_batch/index.inc` |

`compile_struct_offsets` also changes one generated `static_assert` to a wrong number and checks gcc refuses it.

`build/release/bin/bench_simd`, one run under the build lock, nanoseconds per element:

| Kernel | Input | Kernel ns/element | Scalar | Gain |
|---|---|---|---|---|
| `copy_stream_batch` | 1080p frame | 0.028 | 0.236 | 8.32x |
| `copy_stream_rows_batch` | 1080p frame | 0.028 | 0.227 | 8.05x |
| `blend_over_batch` | runs, no partial alpha | 0.201 | 2.273 | 11.32x |
| `blend_over_batch` | runs, 20% partial | 0.266 | 2.268 | 8.53x |
| `blend_over_batch` | all partial | 0.286 | 2.311 | 8.07x |
| `blend_premul_batch` | runs, no partial alpha | 0.151 | 3.824 | 25.25x |
| `blend_premul_batch` | all partial | 0.238 | 3.820 | 16.06x |
| `bitmask_popcount_batch` | 8 words, not gated | 0.434 | 0.366 | **0.84x** |
| `bitmask_popcount_batch` | 64 words, a section | 0.160 | 0.282 | 1.76x |
| `bitmask_popcount_batch` | 4096 words | 0.130 | 0.278 | 2.14x |
| `bitmask_and_batch` | 8 / 64 / 4096 words | 0.337 / 0.142 / 0.144 | 0.443 / 0.298 / 0.352 | 1.31x / 2.10x / 2.44x |
| `bitmask_or_batch` | 8 / 64 / 4096 words | 0.289 / 0.142 / 0.154 | 0.448 / 0.304 / 0.358 | 1.55x / 2.14x / 2.33x |
| `bitmask_andnot_batch` | 8 / 64 / 4096 words | 0.351 / 0.142 / 0.144 | 0.466 / 0.302 / 0.351 | 1.33x / 2.12x / 2.44x |
| `bitmask_from_nonzero_u8_batch` | 4096 values | 0.011 | 0.501 | 43.68x |
| `bitmask_from_nonzero_u16_batch` | 4096 values | 0.017 | 0.492 | 28.67x |
| `bitmask_from_equal_u16_batch` | 4096 values | 0.017 | 0.492 | 28.56x |
| `noise3_batch8` | 5, all tail, not gated | 7.214 | 15.166 | 2.10x |
| `noise3_batch8` | 256, repeated | 4.077 | 17.591 | 4.31x |
| `noise3_batch8` | 4096, a section | 4.093 | 79.108 | 19.33x |
| `vec_batch_transform_points` | 8 / 64 / 1024 | 0.654 / 0.396 / 0.369 | 2.059 / 2.055 / 2.055 | 3.15x / 5.19x / 5.57x |
| `vec_batch_transform_directions` | 8 / 64 / 1024 | 0.567 / 0.349 / 0.330 | 1.753 / 1.752 / 1.745 | 3.09x / 5.02x / 5.28x |
| `vec_batch_mul_vec4` | 8 / 64 / 1024 | 0.648 / 0.492 / 0.487 | 3.005 / 3.043 / 2.988 | 4.64x / 6.19x / 6.13x |

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

1. **The first hash was not good enough, and only the test saw it.** Expected: one round of xorshift and multiply to pick gradients with no visible pattern. Actual: `asm_noise` failed `the_hash_has_no_pattern_a_correlation_could_see` on its first run, at `tests/asm/noise/index.c:323` and `:330`. A throwaway measurement of gradient correlation over 64^3 cells gave 0.298 between a seed and the same seed with its top bit flipped, -0.096 for seed + 1, and -0.076 for one flipped bit in the middle. So nearby seeds would give related worlds. Done: a second round (`NOISE_HASH_MIX_2`, 8 more `vpmulld` per batch), which brings all of those under 0.01 in that measurement, and more seeds and distances in the test. The swap checks had a wrong limit, which was the test's bug: where x equals y a swap changes nothing, so they start near 0.016, and they now allow 0.05 and say why.
2. **The release build's recursion check compiled a test before its generated header existed.** Expected: `add_dependencies(mc_test_asm_vec_batch mc_struct_offsets)` to be enough, as it was in debug. Actual: `mc_test_asm_vec_batch_callgraph ... fatal error: struct_offsets/index.h: No such file or directory`, since under `-flto` `cmake/standards_lint.cmake` compiles each source again as its own target. Done: those targets wait for `mc_struct_offsets` too. Re-run from a deleted `build/` on debug and release.
3. **`bitmask_popcount_batch` loses at 8 words: 0.84x, on three runs.** Stage 04.2 Issue 4 asked this benchmark to judge it at the lengths real callers use, and measured 1.07x at 8 words, so this is thin either way. Expected from the rule: a kernel with no gain gets deleted. Done: kept, since a section is 64 words and there it gains 1.76x, and at 4096 words 2.14x. That row prints as "not gated", which was decided before these numbers, from Stage 04.2's. `bench/simd/README.md` says not to give it much shorter arrays. Left for review: if some caller does have short arrays, the kernel needs a scalar path for them, and it belongs to Stage 04.2's directory.
4. **The noise gain depends heavily on the data, and the big number flatters it.** Expected: one figure. Actual: 4.31x at 256 points and 19.33x at 4096, because the benchmark repeats short runs, so the reference's branch predictor learns 256 points but not 4096 (17.6 against 79.1 ns per point). The kernel has no such branches: 4.08 and 4.09 ns. Left alone, and written down: 4x is the floor against scalar code with no mispredictions, and terrain's fresh data should be nearer the high figure. The reference is plain C, written to be readable, and a branchless one would narrow the gap.
5. **Some things are not measured or not tested.** The comment in `asm/src/noise/index.asm` that gathers are slow under the Downfall microcode comes from what is published about this CPU, not from a measurement here; no gather version was written to compare. Dirty YMM state is covered by `standards_lint`, and `noise3_batch8`'s `rbp`, the only callee-saved register in this stage, by review. The noise has been checked by numbers (range, seams, correlation), but nobody has looked at it as a picture, or as terrain: that comes with Stage 10.
6. **`struct_offsets` reads headers as text.** A tool may not include `run/` (`plans/REPO_STRUCTURE.md` section 11), and NASM cannot read C, so it works layouts out itself. It refuses what it does not understand, and the generated `static_assert`s are compiled in `tests/asm/vec_batch/`, but only in a target that builds tests: `minecraft_c` alone does not compile them. Every preset's default build does. A struct with an enum-typed field or a constant as a dimension is refused, and will need the tool extended when something in NASM needs one.
7. **`bench_t` needed written-out padding.** `error: padding struct to align 'matrix' [-Werror=padded]`. Done: `_pad0[16]`, as the standard says.
8. **A tool refused some compound shell commands** ("too complex to verify that it stays inside the worktree"), including heredocs and a chain of ctest runs. Done: wrote edit scripts to the scratchpad and re-ran the commands one at a time, as the refusal says to. Nothing was skipped.
