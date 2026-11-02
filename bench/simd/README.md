# bench/simd

**Purpose:** times every NASM kernel in `asm/` against its scalar reference, and fails if a kernel has no gain or does not give
its reference's output, since `plans/CODING_STANDARD.md` section 7 says a kernel with no gain gets deleted.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, released before it exits.
**Lifetime:** Process.
**Threads:** one, pinned to the CPU it started on.
**Public functions:** `main`. Run `build/release/bin/bench_simd` under `tools/test-or-build.sh`, so no build shares the CPU.
Exit status 0, 1 if a row failed, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `run/include/simd/` and `simd/reference`, `heap`, `thread`, `time`, `math/mat4`.

Only a release build's numbers mean anything: debug has the references at `-O0`.

Each side of a row runs for 30 ms of rounds, and at least 5, and keeps its best round, which is the one the least else
interfered with. Runs shorter than 16384 elements repeat inside a round so the clock can read them. A row is gated if it is
a length the kernel is for; a "not gated" row is printed to watch, and fails only if the output differs. Copies go to ordinary
cached memory, so this says nothing about a scanout buffer.

Two results are easy to misread. `noise3_batch8` at 256 points repeats the same points, so the branch predictor learns them
and the reference has no mispredictions: its 4x is the gain against scalar code at its best. Fresh data, as terrain has, is
closer to the 4096 row's 19x. And `bitmask_popcount_batch` loses at 8 words, at about 0.84x: its prototype says it is for 16
words and up, and the 24-word row is gated to hold it to that. Measured gains by length, on 2026-11-02: 1 word 0.46x, 2 0.50x,
4 0.66x, 7 0.67x, 12 1.00x, 15 0.94x, 16 1.07x, 24 1.27x, 32 1.40x. A scalar path for short arrays inside the kernel did not
cure it (8 words 0.95x, 7 words 0.58x), because the reference has the same instruction at one per cycle and no call to make.

## Results

### 2026-11-02, Stage 04.3

i9-10900K, release preset, one run under the build lock, with Ahmed's desktop up. A second run moved no gain in the
kernels of this stage by more than about 0.5x, and none across 1.0.

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
