# 009-ahmed-bohemoth-game-engine-optimizations — phase 1: Measure where the steps go

**Status:** in progress   (updated 2026-11-12)

Read first: `plans/009-ahmed-bohemoth-game-engine-optimizations-phase-00-vision-and-plan-files.md`, then the master `plans/009-ahmed-bohemoth-game-engine-optimizations.md`: sections 0 ("Why the frame is 29 ms"), 3.4, 4.1, 5 and 7. Then plan 008's master section 3.1 and its phase 01 file. That phase builds the instruments this one stands on. Then `bench/world/README.md` and `cuda/src/traverse/README.md` ("What a ray skips").

## Goal

The causes the master lists for 5.png's 29.44 ms have numbers, at his spot, on his card as it is. Nothing is optimised. When this phase is done:

- **His view is a settled `bench_world` row:**
  - `x -385 y 347 z 2`, his seed;
  - the yaw and pitch of 5.png;
  - the sun at −13.1° (moonlight);
  - render distance 64, full detail 32, 1920 × 1080, his effects.
  - There are also variants at render distance 32 and by day (sun at +40°).
- **Every ray kind has a step count by distance band:** primary, far field, shadow, AO and alpha-hole rays. The bands are 0–16, 16–64, 64–128, 128–256, 256–512, 512–1024 blocks, and per far level. They are read back by the bench and written as a table and a heatmap PNG.
- **A reference mode** draws level 0 out to the render distance, so later phases can compare pictures to "every block everywhere".
- **One row per experiment**, each answering a line of the master's cause list, in the Results table below:
  - shadows off;
  - far field off;
  - alpha off;
  - AO off;
  - render scale 75 %;
  - full detail 16, 24, 32 and 48 at render distance 64;
  - day against night.
- **No picture changes.** Every golden picture and every existing settled row's hash is the same bytes.

## Where this fits

**The vision.** Ahmed wants the frame fast without "Full detail". The master's design is five things (2.3), and phase 01 says which of them pays at his spot, and by how much, before any is built. The master's section 5 is judgment. This phase replaces it with measurements.

**What reads it:**

| Reader | What |
|---|---|
| Phase 04 | The full-detail sweep gives the real price of level 0's radius; the step bands say what coarser bands save |
| Phase 05 | Primary steps inside occupied bricks (coalescing's prize); water-walk steps |
| Phase 06 | Shadow-ray steps by length (the cone's prize); the night share |
| Phase 07 | The render-scale row: how ray cost scales with pixels |
| Phase 08 | Every "Today" in the master's section 7 |
| Ahmed | The cost table, the heatmap, and Q2, Q5 and Q10 |

**What this phase must NOT build:**

- No optimisation, however small.
- No instrument plan 008 phase 01 builds: per-kernel shade clocks, the frame log, the MHz kernel, the nvidia-smi sidecar, `--at` and `--seed`. If 008 phase 01 has not landed, this phase waits for it, or adds the smallest stand-in, names it in Issues found, and removes it when 008's lands.
- No tuning YAML (phase 02).
- Nothing that costs anything in a play build: the counters are behind a null pointer the game never sets.

## Ahmed's words

- "Please look at ~/Pictures/5.png"
- "Please only extremely thoroughly research this!"

---

## Background, explained

### What a step is, and why it is the cost

`traverse_trace_stopping` (`cuda/include/traverse/index.hu:305-455`) is one loop. Each pass of the loop is one step, and each step does three things:

1. It reads at most a column, a section header and a brick mask, all cached across steps.
2. It picks the node holding the current cell: a whole region above `region_top`, a column above its top, a 16³ section, a 4³ brick, or one voxel.
3. It moves to the next node along the ray.

A ray's time is close to steps × the cost of a step, and a step's cost barely depends on the node's size. So **a picture's primary cost is the sum of every pixel's steps**, and the same holds for every other kind of ray. Triangles do not appear anywhere.

`traverse_hit_t.info` already carries each primary ray's steps, up to `TRAVERSE_INFO_STEPS_MAX` = 4095 (`run/include/gpu_bridge/traverse/index.h:55-56`, read by `traverse_hit_steps` at `:189`). The other rays are traced inside shade and the far field and throw their counts away. That is what this phase adds.

### Distance bands

The master's design hands rays from level to level by distance. So the useful question is not "how many steps" but "how many steps between 256 and 512 blocks". A step's band is the ray's `t` when the step began. A ray that hits at 300 blocks after 900 steps spreads those steps over the bands it crossed: the counter adds into the band of each step's `t`, by a small per-thread array flushed with one `atomicAdd` per band at the end. That costs nothing when the pointer is null, because the inlined loop drops the code.

### The full-detail sweep is a free preview of phase 04

Full detail is still a setting in this phase. At render distance 64, moving it from 48 to 16 chunks (768 to 256 blocks) moves where rays leave level 0, and changes what the map loads.

Its card time against full detail is an upper bound on what "level 0 only to d<sub>1</sub>" can buy. It is a bound and not an estimate, because today's far field starts at 4-block voxels where the pyramid would use 2-block ones, and has the pass-by on top. Plan 005 phase 8 ran the opposite sweep (render distance up, full detail fixed). This is the first time full detail itself is moved.

### The reference mode

`--lod-reference` sets full detail to the render distance and turns the far field off. Level 0 then reaches as far as the picture does: "every block everywhere", the picture phase 04 compares against.

At render distance 64 that is a table 129 columns wide, 16641 slots. The pool is the voxels budget, 6144 MiB in `config/machine.yaml`; `bench_world` takes at most 3584 MiB for itself. So the reference may not fit at 64. If it does not, it runs at render distance 48 and phase 04's comparisons use 48. That is recorded in Issues found.

---

## Design

### `bench_world`

- **New row family `ahmed_slope`**, settled, at the place, yaw, pitch and sun given by flags:
  - `--at X Y Z --seed S` from plan 008 phase 01;
  - new: `--look YAW PITCH` in degrees, the game's own yaw convention (`run/src/render/frame/README.md`, "The camera");
  - new: `--sun ELEVATION`, which sets `render_frame_sun_set` from an elevation in degrees, azimuth as the world clock would give at his latitude 0.82° N.

  Rows: `ahmed_slope/night-64`, `ahmed_slope/night-32`, `ahmed_slope/day-64`.
- **`--steps <file>`:** with one `--only` row, writes the band table (per ray kind: rays, steps, mean, p50, p99 per band) as TSV, and `<file>.png`, a heatmap of primary steps per pixel. The colour scale goes 0 to 512 on a log scale, with a legend strip in the image.
- **`--lod-reference`:** the reference mode above.
- **`--full-detail <chunks>`:** moves full detail for the rows held to the frame target, as `--render-distance` already moves render distance.
- **Experiments by existing flags:** `--features-off` with `SHADE_FEATURE_SHADOW`, `SHADE_FEATURE_LOD`, `SHADE_FEATURE_ALPHA` and `SHADE_FEATURE_AO`; render scale 75 % through a new `--render-scale <percent>`.

### The counters

- **A struct on the card:** `traverse_counts_t`, in `run/include/gpu_bridge/traverse/index.h`. It holds `uint64_t steps[TRAVERSE_COUNT_KINDS][TRAVERSE_COUNT_BANDS]` and `rays[...]`, and has static_asserts like every struct there.
  - The kinds: primary, far, sun, AO, alpha.
  - The band edges: 16, 64, 128, 256, 512 and 1024, then the far levels.
- **Wiring:** a pointer in `traverse_view_t` would grow a 64-byte struct that is asserted at 64. So the counters go in by a separate kernel argument, `const traverse_counts_t *counts`, passed down to `traverse_trace_stopping` as a template-free `__forceinline__` parameter, null in play. The compiler removes the branches when inlined with a constant null; `shade_kernel`'s register count, read from `-Xptxas -v` before and after, must not grow in the null build.
- **Per-thread bands** are kept in registers and flushed at ray end: one `atomicAdd` per non-zero band.

### What is not changed

The loop's arithmetic, the node choice, every result bit. The `cuda_traverse` tests compare hits and `t` to the bit with counters on and off.

## Owned paths

| Path | Change |
|---|---|
| `bench/world/index.c`, `bench/world/README.md` | Rows, flags `--look`, `--sun`, `--steps`, `--lod-reference`, `--full-detail`, `--render-scale` |
| `run/include/gpu_bridge/traverse/index.h` | `traverse_counts_t` |
| `cuda/include/traverse/index.hu` | The counts parameter in the loop |
| `cuda/src/traverse/`, `cuda/src/lod/`, `cuda/src/shade/` | Pass the counts through; null in play |
| `cuda/src/frame/`, `run/include/gpu_bridge/frame/index.h` | `frame_args_t` gains the counts block, null in play |
| `tests/cuda/traverse/` | Counters on and off give identical hits; counts add up to `info`'s steps |
| This file | Results |

## Steps

1. **Wait for, or check, plan 008 phase 01.** *Why:* its `--at`, `--seed`, shade clocks and MHz kernel are the ground this stands on, and building them twice would be two of each.
2. **Add the counts parameter and the struct; prove it costs nothing.** Register counts before and after (`-Xptxas -v` in a profile build); `bench_world --only orbit` within 1 % of its last history line. *Why:* a counter that slows the loop measures a different loop.
3. **Add `--look`, `--sun`, `--render-scale`, `--full-detail`, `--lod-reference` and the `ahmed_slope` rows.** *Why:* his view, reproducibly. Q2 (his seed and view) is asked now. Until he answers, the default seed from plan 005 phase 8, `11107447384583428913`, is used with a yaw and pitch fitted by comparing the row's shot to 5.png by eye.
4. **Add `--steps`.** *Why:* the bands are the design's currency.
5. **Run every row and experiment**, with Ahmed's game closed, 240 settled frames each, each row alone in its own process (plan 005 phase 8's lesson: whole runs slow the card for later rows). Record the card's MHz beside each.
6. **Fill in Results**, and the "Today" column of the master's section 7 (in this file, not the master).

## Todo

- [x] Plan 008 phase 01's instruments are on `main`, or the gap is in Issues found
- [x] `traverse_counts_t` and the counts parameter, null in play; register counts unchanged in the null build
- [x] `cuda_traverse`: counters on and off give identical hits; band sums equal `info`'s steps for primary rays
- [ ] `--look`, `--sun`, `--render-scale`, `--full-detail`, `--lod-reference` in `bench/world/index.c`, documented in its README
- [ ] Rows `ahmed_slope/night-64`, `night-32`, `day-64`; the shot of `night-64` looks like 5.png
- [ ] `--steps` writes the band table and the heatmap PNG
- [ ] Experiments run alone, each with the card's MHz: shadows off, far field off, alpha off, AO off, render scale 75, full detail 16 / 24 / 32 / 48
- [ ] Reference mode at 64, or at 48 if 64 does not fit, recorded
- [ ] Results table filled; the master's targets' "Today" values written here
- [ ] Every golden picture and existing settled hash unchanged
- [ ] Ahmed has read the table and answered Q2, Q5, Q10

## Results (filled in by this phase)

| Row / experiment | MHz | Coarse | Primary | Shade (water / surface / sun / AO / fresh) | Post | Card total | Δ against `night-64` | Cause it answers |
|---|---|---|---|---|---|---|---|---|
| `night-64` | | | | | | | — | All |
| `night-32` | | | | | | | | The far field's whole cost |
| `day-64` | | | | | | | | Moon against sun shadows |
| shadows off | | | | | | | | Master cause 3, moon rays |
| far field off | | | | | | | | Cause 2, far rays |
| alpha off | | | | | | | | Leaves |
| AO off | | | | | | | | AO |
| render scale 75 | | | | | | | | Phase 07's prize |
| full detail 16 / 24 / 32 / 48 | | | | | | | | Phase 04's bound |

Step bands (from `--steps`): one table per ray kind, written here.

## Tests

### `tests/cuda/traverse/` — ctest `cuda_traverse`, on the card

- `counting_steps_changes_no_hit`: for the existing reference scenes, every hit's voxel, face, `t` (bitwise) and value are the same with a counts block and without.
- `primary_band_sums_equal_each_hits_steps`: over a 256 × 256 camera, the sum of primary steps over all bands equals the sum of `traverse_hit_steps` over all hits (no ray capped at 4095 in that scene).
- `a_ray_at_300_blocks_counts_into_the_bands_it_crossed`: a straight ray over an empty world to a wall at 300 blocks: steps fall in bands 0–16 to 256–512 and none past 512.

### `bench_world`

Not a ctest. Its rows' own soundness checks (8 colours, no heap calls) hold for the new rows.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing before card work
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(cuda_traverse|render_feature_lod|render_feature_water)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --steps /tmp/mc_c_009/p01/night64.tsv --shots /tmp/mc_c_009/p01
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --features-off <SHADE_FEATURE_SHADOW hex>
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --full-detail 16
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --only =ahmed_slope/night-64 --lod-reference --shots /tmp/mc_c_009/p01/ref
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 phase 01: counters, no change"
    pgrep -af "$PWD/build"                                                       # must print nothing

One experiment per wrapper call, as the rule says: the lines above show one of each kind, and the rest follow the same form.

## Gate

Ahmed reads:

1. the Results table, with the card's MHz beside every row;
2. the heatmap of his view;
3. the step bands.

He answers:

- Q2: his seed and view, if the fitted ones are wrong.
- Q5: the dim-light cut of shadows (near the horizon crossing and under overcast), now that "shadows off" has a number.
- Q10: the targets.

His answers are recorded below.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Plan 008 phase 01 is not built yet | Wait; or a minimal stand-in named in Issues found |
| The counters change register allocation even when null | Measured with `-Xptxas -v`; if they do, the counted loop becomes a second inlined copy used only by the bench |
| The reference mode does not fit at 64 | Run at 48; phase 04 compares at 48 |
| His seed or view is unknown | Fitted by eye to 5.png (Q2) |
| The capped card's clock drifts during a row | MHz recorded per row; rows run alone; repeated twice, and a pair more than 4 % apart is run a third time |

## Commits

Appended as they land:

## Issues found

**Plan 008 phase 01 is on `main`** (its four commits, up to `ec4554c9`), and this branch was cut from it. Reused: shade's five spans and the fresh count in `frame_marks_t`, the `clock64` reading before each settled picture (rows say Mcycles), `--at`/`--seed` as `bench_world_shake` has them. Nothing of 008's was built again.

**How the counters are wired, and what changed from the plan:**

- `traverse_counts_t` (18376 bytes, asserted) in `run/include/gpu_bridge/traverse/index.h`, with eight kinds, not five: primary, far, sun, AO, alpha, and three the master's causes need apart, `far_past` (the far field asking, past a leaf or pane, whether shade's ray leaves full detail: master cause 2, "leaves on the right"), `far_sun` (the far field's own sun rays) and `reflect` (water's reflection). Seven distance bands (0-16, 16-64, 64-128, 128-256, 256-512, 512-1024, 1024+), steps and calls by far level, a 64-bucket histogram of a call's steps per band (p50/p99 to within a quarter octave), and after the struct four `uint16_t` planes per pixel (primary; far field; sun; the rest of shade), from which the heatmaps are drawn.
- **Not a kernel argument of the play kernels.** A runtime pointer argument cannot be folded away by the compiler, and templates are banned (standard section 8). So each kernel that traces rays became a `__forceinline__` body and two `__global__` copies: the play copy passes a literal `nullptr`, the counted copy the block. `traverse_trace_counted(view, ray, stops, hit, tally)` is the loop; `traverse_trace` and `traverse_trace_stopping` call it with no tally. The block reaches the frame through `frame_marks_t.borrowed_counts` (104 to 112 bytes; it lives in the World arena, so nothing on main's stack grows) and shade through `shade_marks_t.borrowed_counts` (40 to 48), not through `frame_args_t`. `traverse_primary_counted`, `traverse_rays_counted` and `lod_frame_counted` are the counted entry points; the old ones call them with `nullptr`.
- **The counts go down shade's helpers as arguments, not in `shade_context_t`.** A first try put them in the context (72 to 88 bytes): the context lives on the kernel's stack, and the extra stores changed the play kernels' code. Passing `counts, pixel` as trailing arguments (constant `nullptr` in play) and the kernel's struct parameters to the body by pointer kept them.
- **64-bit atomics:** CUDA's `atomicAdd` for 64 bits takes `unsigned long long`, which the standard bans by name; `traverse_count_add` adds to the two 32-bit halves with a carry, exact once the kernels end.
- **Register counts, before and after** (`cuobjdump -res-usage`, profile build, sm_75): `traverse_primary_kernel` 60, `traverse_rays_kernel` 63, `traverse_coarse_kernel` 72, `shade_kernel` 142, `shade_water_kernel` 93, `shade_ao_kernel` 93, `shade_ao_fresh_kernel` 94, `shade_sun_kernel` 78, `shade_shafts_kernel` 93, `lod_frame_kernel` 90, `lod_past_kernel` 59, `lod_rays_kernel` 80: **all unchanged**. Stack: unchanged but for `shade_water_kernel` 224 to 176 and `shade_ao_fresh_kernel` 160 to 144 bytes (smaller). **SASS** (`cuobjdump -sass`, instruction text compared): the traverse, coarse, `lod_past`, `lod_rays`, entity and frame kernels are **the same instructions**; `shade_kernel`, `shade_water_kernel`, `shade_ao_kernel`, `shade_ao_fresh_kernel`, `shade_sun_kernel`, `shade_shafts_kernel` and `lod_frame_kernel` have **the same instruction count**, and differ only in how the kernel's struct parameters are copied to its stack at the start (STL against STL.64/STL.128 packing, register names, a few NOPs): no loop changed. The counted copies: `traverse_primary_counted_kernel` 60 registers, `shade_kernel_counted` 149, `shade_sun_kernel_counted` 91, `lod_frame_kernel_counted` 95.
- **Tests** (`cuda_traverse`, in the existing file, 1477 lines, under the 1500 split): `counting_steps_changes_no_hit` (seven paths of both stress worlds at 640x360 with the coarse pass, every hit byte for byte), `primary_band_sums_equal_each_hits_steps` (256x256 flyover: 295293 steps over 65536 rays, bands, planes and calls all agree, the most 51), `a_ray_at_300_blocks_counts_into_the_bands_it_crossed` (2, 3, 4, 8, 5 steps in the bands from 0 to 256-512, none past; and the host's buckets checked against their least counts up to 70000). The random-ray part the plan's "existing reference scenes" might also mean was left out to keep the file under 1500 lines; `traverse_rays_counted` is exercised by the 300-block test.
