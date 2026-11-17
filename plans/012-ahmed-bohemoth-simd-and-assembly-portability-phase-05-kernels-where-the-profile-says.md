# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 5: Kernels where the profile says

**Status:** not started   (updated 2026-11-17). Waits for Ahmed's go on plan 012, and for phases 02 to 04.

Read first: `plans/012-ahmed-bohemoth-simd-and-assembly-portability-phase-00-vision-and-plan-files.md`, then the master
`plans/012-ahmed-bohemoth-simd-and-assembly-portability.md` (sections 1.3 item 7, 6 and 7), then phase 04's verdicts and
`bench/simd/history.tsv`.

## Goal

**The CPU's hottest scalar loops that a batch kernel can speed up get one, chosen by a profile and not by guessing.**
Every new kernel:
- beats its scalar reference in `bench/simd` at the lengths the game uses;
- makes its caller faster in the caller's own bench;
- gives exactly the reference's bytes, so terrain, saves and pictures do not move;
- is written with phase 02's `MC_FUNC`;
- passes phase 03's `asm_abi` on both ABIs.

A candidate that does not win is recorded here with its numbers and not kept.

The first expected kernel is **`noise3_fbm_batch8`**. It takes a field's whole octave table and keeps each batch of 8
points in registers through every octave. That removes the per-octave narrowing loop, the per-octave call, and the scalar
`out[i] += value[i] * octave->amplitude` (`run/src/terrain/noise/index.c:145-162`).

## Where this fits

**The vision.** "Please use compile time windows/linux detection so that we can optimize for either": the point of the
platform work is speed. This phase spends it where it counts.

**What it reads:**
- phase 02 (`MC_FUNC`) and phase 03 (the harness, `history.tsv`);
- phase 04's rules: popcount below 16 words is scalar; constants live where phase 04 decided;
- the in-game profiler. `perf` is not on this machine (`run/src/profiler/README.md`: "the built-in profiler, as `perf` is
  not on this machine").

**What later phases read from it:** phase 07's before-and-after table.

**What this phase must NOT build:**
- No kernel without a profile line that shows its loop is hot.
- No change to what the world is: same seed, same bytes (`run_terrain_*`, `run_save_world`, and the generator digests
  stay where plan 010 left them).
- No GPU work. A loop that belongs on the card is recorded for a later plan, not moved here.
- No AVX-512, no gather (the Downfall note in `asm/src/noise/index.asm:5-6` still holds).

## Ahmed's words

- "Please use compile time windows/linux detection so that we can optimize for either"
- "I am not sure if we are properly writing assembly SIMD!"

---

## Background, explained

### Why noise first (from the code, 2026-11-17)

`noise_sample_batch_split` (`run/src/terrain/noise/index.c:138-163`), for up to `NOISE_BATCH_POINTS` = 256 points
(`:15`), does this for each octave:
1. In scalar C, narrows each coordinate: `cell_x[i] = (float32_t)(xs[i] * octave->frequency_x + octave->offset_x)`
   (`:151-153`). That is 3 float64 multiply-adds and 3 conversions per point.
2. Calls `noise3_batch8(value, cell_x, cell_y, cell_z, count, octave->seed)` (`:155`), which loads the constants, builds a
   544-byte frame and walks the points.
3. In scalar C, accumulates `out[i] += value[i] * octave->amplitude` (`:156-158`).
4. Optionally copies the first octaves' sum out (`:159-161`).

So each octave costs 2 scalar passes over the points, a kernel call, and 4 memory round trips. `noise3_batch8`'s 19.33x
(4096 points, `bench/simd/README.md`) is the kernel alone, not the field.

### What makes a fused kernel bit-exact

Every operation must be the C's own, in the C's order:

| C (`-std=c23`, `-O3`, `-march=skylake`) | Kernel |
|---|---|
| `xs[i] * octave->frequency_x + octave->offset_x` in float64. **Not contracted**: GCC under `-std=c23` emits `vmulsd` then `vaddsd` (checked 2026-11-17, GCC 16.2.1; `-std=gnu23` would give `vfmadd`) | `vmulpd` then `vaddpd`, never `vfmadd*pd` |
| `(float32_t)` of that | `vcvtpd2ps`, under MXCSR's round-to-nearest (phase 03 checks MXCSR is kept) |
| `noise3_batch8`'s arithmetic | The same macros from `asm/include/noise/index.inc`, unchanged |
| `out[i] += value[i] * octave->amplitude` in float32, not contracted | `vmulps` then `vaddps` |
| `out[i] = 0.0f32` first, octaves in order `0..octave_count-1` | The same; each point is independent, so swapping the loops (points outside, octaves inside) changes nothing |
| `memcpy(out_first, out, ...)` after `first_octaves` octaves | A store of the running sum when the octave index reaches `first_octaves` |

The octave table is `terrain_noise_octave_t`, 56 bytes (`run/include/terrain/noise/index.h:35-45`). Its offsets come from
`compile/src/struct_offsets/` ("Numbers are never typed by hand").

**Proposed prototype**, to be confirmed by the profile:

```c
void noise3_fbm_batch8(float32_t *out, float32_t *out_first, const float64_t *xs, const float64_t *ys,
                       const float64_t *zs, uint64_t count, const terrain_noise_octave_t *octaves,
                       uint64_t octave_count, uint64_t first_octaves);
```

That is 9 arguments: on SysV, 3 come on the stack, and phase 02's layer handles both ABIs. The reference is
`simd_reference_noise3_fbm`: today's C loop moved into `run/src/simd/reference/`.

### The other candidates (to profile, not assumed)

| Loop | Where | Kernel idea |
|---|---|---|
| Brick scatter | `encode_bricks_from_occupied`, `run/src/render/encode/index.c:26` | BMI2 `pdep`/`pext` over the 64 section words, re-sorting voxel bits by brick |
| Uniform scan | `encode_is_uniform`, `run/src/render/encode/` | `bitmask_from_equal_u16`-style compare over 4096 voxels, early out |
| Section palette pack and unpack | `run/src/world/section/` | Bit-width unpack with `vpsrlvd`/`vpshufb` |
| Save encode | `run/src/save/` | Depends on the format; profile first |
| Light sweeps | `run/src/world/light/` | Row-wise max-minus-one over 16-wide rows |
| LOD downsample | `run/src/render/lod/` | 2×2×2 reduce |
| Tick nibble unpack | `run/src/world/tick/` | `vpshufb` nibble split |

## Owned paths

This phase may edit these, plus this file:
- For noise: `asm/src/noise/`, `asm/include/noise/`, `run/include/simd/noise/`, `run/src/simd/reference/`,
  `run/src/simd/index.c`, `tests/asm/noise/`, `tests/asm/abi/` (recipe), `bench/simd/`, `compile/src/struct_offsets/`
  (the octave row), `run/src/terrain/noise/index.c` (the call site only)
- For each further kernel: the same set for its own directory, plus its caller's call site. **Each is added to this list,
  with the profile line that justifies it, before its work starts.**

**Not touched:** `config/`, `cuda/`, the terrain's maths outside the call site (`run/src/terrain/` beyond `noise/index.c`).

## Steps

1. **Profile.** Run the profile preset with `--profile` over world generation and a settled frame. Read the table; record
   the top CPU scopes with their share of a frame or a column, and name the loops inside them.
   - If a hot loop has no profiler scope, add one in its own commit.
   - *Why: "where the profile says" means a number, written here.*
2. **For each chosen loop, in order of share:**
   - the reference first (today's C, moved);
   - then the kernel;
   - then the asm test against the reference (guard pages, misalignment, every tail), the `asm_abi` recipe, and the bench
     row;
   - then the call site;
   - then the caller's bench before and after, in `history.tsv`.
   - *Why: the reference is what makes "bit-exact" checkable.*
3. **Keep or drop.** A kernel stays only if its bench row and its caller's bench both win. A dropped one is recorded here
   with its numbers and removed in the same phase. *Why: the standard's rule, applied to new code too.*

## Todo

From the master (section 8, phase 05):

- [ ] Profile first, game-side; the top scopes and their loops recorded here
- [ ] `noise3_fbm_batch8`, if the profile confirms: the per-octave accumulate and the float64 narrowing inside the kernel
- [ ] Render encode's uniform scan and brick scatter (BMI2 `pdep`), if the profile confirms
- [ ] Section palette pack and unpack, if the profile confirms
- [ ] Light-section sweeps, if the profile confirms
- [ ] Each kernel beats its reference in `bench/simd`, or it is not kept

Expanded:

- [ ] Each kept kernel's caller is faster in its own bench (`bench_terrain`, `bench_world`, ...), recorded with before and after
- [ ] Terrain and save digests unchanged; `run_terrain_*`, `run_save_world` pass
- [ ] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test (ctest) | Checks | Numbers | Fails when |
|---|---|---|---|
| `asm_noise` (extended) | `noise3_fbm_batch8` against `simd_reference_noise3_fbm` | counts {0, 1, 7, 8, 9, 255, 256}; octave counts {1, 4, 8, max the fields use}; `first_octaves` {0, 1, all}; 8 misalignments of each array; guard pages after every array; 0 differing bytes | Any bit differs |
| `asm_abi` | The new recipes | both ABIs, all checks of phase 03 | A convention broken |
| `asm_simd` / `simd_self_check` | The new kernel at startup | passes | |
| `run_terrain_*`, `run_save_world` (existing) | The world's digests | unchanged | The terrain moved |
| Each further kernel's `asm_<dir>` test | Against its reference | the same shape as above | |
| `bench_simd` (release) | The new rows | gated rows ≥ 1.0x at the lengths the caller uses | No gain |
| `bench_terrain` (release) | Columns a second | after > before beyond 3 runs' spread, recorded in this file | No caller gain |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(asm_.*)$' --output-on-failure
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^(run_terrain_.*|run_save_world)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    tools/test-or-build.sh cmake --preset profile
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 build/release/bin/bench_simd
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    pgrep -af "$PWD/build"    # must print nothing

The profile run itself is the game with `--profile` from the profile build, started by Ahmed in his own terminal (his
standing rule: no agent leaves the game running). The agent writes the exact command here for him, and reads the table he
saves.

## Gate

Ahmed:
1. Makes a new world on the release build, flies across it, and says whether loading feels faster.
2. Reads the profile table before and after, and the `bench_terrain` numbers.

## Risks and open questions

| Risk or question | Default |
|---|---|
| The profile shows noise is not hot any more (a cache, or plan 010's work) | Then it is not written; the next hot loop goes first |
| `vcvtpd2ps` rounding differs from the C cast | It cannot under round-to-nearest; the asm test proves it on every count and octave |
| 9 arguments are awkward on Win64 (5 on the stack) | Pass a small `noise3_fbm_args_t` by pointer instead, if phase 02's stack loading proves clumsy; recorded either way |
| A kernel wins in `bench_simd` and loses in its caller (call overhead, cache) | Not kept (Step 3) |
| A candidate belongs on the GPU | Recorded for a later plan; not moved here |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
