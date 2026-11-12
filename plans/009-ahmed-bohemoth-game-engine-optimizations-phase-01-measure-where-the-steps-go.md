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
- [x] `--look`, `--sun`, `--render-scale`, `--full-detail`, `--lod-reference` in `bench/world/index.c`, documented in its README (in `bench/world/slope/index.c`, its own program: see Issues found)
- [x] Rows `ahmed_slope/night-64`, `night-32`, `day-64`; the shot of `night-64` looks like 5.png
- [x] `--steps` writes the band table and the heatmap PNG
- [x] Experiments run alone, each with the card's MHz: shadows off, far field off, alpha off, AO off, render scale 75, full detail 16 / 24 / 32 / 48
- [x] Reference mode at 64, or at 48 if 64 does not fit, recorded
- [x] Results table filled; the master's targets' "Today" values written here
- [ ] Every golden picture and existing settled hash unchanged
- [ ] Ahmed has read the table and answered Q2, Q5, Q10

## Results (filled in by this phase)

All on Ahmed's card (RTX 2080 Super, the laptop), his game closed (`pgrep` printed nothing before every run), `build/profile/bin/bench_world_slope` (see Issues found for why it is its own program), 1920 x 1080, 30 warm and 240 measured settled pictures a row, each row alone in its own process and wrapper call, the card's clock read before every measured picture. Logs, TSVs and heatmaps were in `/tmp/mc_c_009/p01/` (not kept); every row is in `bench/world/slope/history.tsv`.

**The card's clock moved from 146 to 1041 MHz between rows** (and 55 to 1350 within some), as plan 008 phase 01 found. Mcycles (ms x MHz / 1000) is not quite clock-free either: the same base row read 6.42 to 6.59 Mcycles at 147 to 173 MHz, 6.78 to 6.91 at 365 to 437, and 7.17 at 543 (memory waits cost more cycles at a higher clock). So **every experiment was run straight after a `night-64` of its own** (the pairs below), and its Δ is against that base, at the same clock to within 30 MHz but for render scale 75 and full detail 16 (see the notes). A first pass of every row, and a second of each, are in the history too; the pairs agree with them.

| Row / experiment | MHz | Coarse | Primary (far field) | Shade (water / surface / sun / AO / fresh) | Post | Card total, ms; Mcycles | Δ against `night-64` (its pair) | Cause it answers |
|---|---|---|---|---|---|---|---|---|
| `night-64` (pair of far field off) | 410 | 1.72 | 5.86 (3.14) | 6.30 (0.35 / 1.95 / 0.62 / 2.92 / 0.45) | 1.57 | 16.73; 6.85 | — | All |
| `night-64` at the card's floor (pair of full detail 48) | 148 | 4.90 | 16.31 (8.71) | 16.93 (0.98 / 5.24 / 1.65 / 7.84 / 1.21) | 4.28 | 43.69, p99 46.61; 6.46 | — | All |
| `night-32` | 525 | 1.36 | 2.13 (0) | 5.12 (0.28 / 1.57 / 0.51 / 2.40 / 0.36) | 1.24 | 11.14; 5.84 | **−1.33 Mcycles, −19 %** (base 7.17 at 543) | The far field's whole cost |
| `day-64` (sun +40) | 438 | 1.60 | 5.47 (2.93) | 7.63 (0.32 / 1.89 / **2.23** / 2.76 / 0.43) | 1.41 | 17.39; 7.61 | **+0.63, +9 %** (6.98 at 462) | Moon against sun shadows |
| shadows off (`20`) | 420 | 1.68 | 5.72 (3.06) | 5.56 (0.34 / 1.92 / 0 / 2.85 / 0.45) | 1.52 | 15.75; 6.61 | **−0.28, −4 %** (6.89 at 436) | Master cause 3, moon rays |
| far field off (`8000`) | 404 | 1.76 | 2.81 (0) | 6.39 (0.36 / 1.99 / 0.62 / 2.97 / 0.46) | 1.58 | 13.82; 5.58 | **−1.27, −19 %** (6.85 at 410) | Cause 2, far rays |
| alpha off (`2`) | 404 | 1.75 | 5.97 (3.20) | 6.32 (0.35 / 2.32 / 0.57 / 2.81 / 0.27) | 1.59 | 16.91; 6.83 | −0.08, −1 % (6.91 at 437): noise | Leaves |
| AO off (`400`) | 430 | 1.67 | 5.60 (2.99) | 2.80 (0.33 / 1.87 / 0.59 / 0 / 0) | 1.49 | 12.83; 5.51 | **−1.31, −19 %** (6.82 at 387) | AO |
| render scale 75 (1440 x 810) | 1041 | 0.68 | 1.56 (0.89) | 2.20 (0.10 / 0.61 / 0.27 / 1.05 / 0.18) | 0.41 | 5.58; 5.79 | −1.05, −15 % (6.84 at 399); the clock differs, so see the steps: **−41 % primary steps, −44 % far steps** for 56 % of the pixels | Phase 07's prize |
| full detail 16 / 24 / 32 / 48 | 155 / 148 / 148 / 146 | 3.48 / 4.49 / 4.90 / 5.37 | 17.61 (11.14) / 17.18 (9.61) / 16.31 (8.71) / 16.53 (8.75) | 14.80 / 16.84 / 16.93 / 16.99 | 4.20 / 4.32 / 4.28 / 4.36 | 41.37 / 44.12 / 43.69 / 44.52; 6.21 / 6.52 / 6.46 / 6.50 | 16: −0.19 (−3 %, at 365 MHz in its second run: 6.60 against 6.79); 24: +0.06 (+1 %); 48: +0.04 (+1 %): **all within noise** | Phase 04's bound |
| reference (`--lod-reference`: full detail 64, no far field) | 170 | 6.21 | 7.32 (0) | 16.00 | 4.08 | 34.88; 5.51 | **−0.91, −14 %** (6.42 at 147); first runs 5.92 and 6.05 at 447 and 488 MHz, −13 % | "Every block everywhere" |

**What the table says, cause by cause** (master section 0):

1. **The card's clock** is still the largest factor: the same picture is 13.2 ms at 543 MHz and 43.7 ms at 148. 5.png's 29.44 ms is this row at about 230 MHz. In Mcycles the spot is **6.5 to 7.2 Mcycles a picture: 11 to 12 ms at 600 MHz.**
2. **The far field is a fifth of the picture and draws nothing at this spot.** Off, −19 %; and the pictures prove it is invisible here: far field off and `night-32` are the same bytes (`8F0497EA8B0CD221`), and full detail 48 and the reference are the same bytes (`BACEA5861D680717`), so past the ridge there is only sky. Its 22.1 M steps a frame (more than primary's 18.8 M) are sky rays walking level 0 of the far field to its edge, and 63 % of them (13.9 M) are taken **inside** full detail's 512 blocks, passing by. Full detail 48 does not reduce them at all (22.07 M), as the far ray still starts at the eye. The reference, every block to 1024, is **cheaper than today** by 13 to 14 %.
3. **Grazing primary rays are real but not dominant.** 9.1 primary steps a pixel on average, p99 80; the heatmap is red along the ridges and the silhouette (64 to 256 steps a pixel) and blue (2 to 8) on the slope's face. The band 128-256 has the dearest calls (17 steps each in it); 256-512 holds 3.1 M of the 18.8 M.
4. **Moon shadows are cheap here: 4 % of the picture.** Facing the moon at −13.1° are few faces: 100 k sun calls a frame, 0.24 M steps, 0.62 ms at 410 MHz. By day (+40°) the same view sends 692 k sun rays, 22.9 M steps (33 a ray, almost all within 128 blocks of their surface: 11.6 M in 0-16, 8.4 M in 64-128), and the sun kernel is 2.23 ms: **+9 %**. So master cause 3 ("moon rays at 13°, uncapped") is not where 5.png's time went, but shadows by day are.
5. **Leaves (alpha) cost nothing measurable** at this view: 0.41 M alpha steps and 0.43 M far_past steps a frame; off, −1 %, inside noise. The tree on the right is close and its holes lead to near ground.
6. **AO is the largest single shade cost: −19 % off**, though its rays take only 4.0 M steps a frame (402 k calls, 9.9 steps each, 3.3 M within 16 blocks). The AO kernel is 2.9 ms of shade's 6.3 at 410 MHz: its time is not steps (plan 008 phase 04 owns AO).
7. **Coarse is 10 % (1.7 ms at 410 MHz)**, not the "proven small" of the master, and 6.2 ms in the reference.
8. **Full detail 16 to 48 changes the card's time by less than the noise (±3 %).** Fewer primary steps at 16 (16.3 M against 18.8 M) are paid back in far steps (32.0 M against 22.1 M: levels 0 to 2 now, 5.8 M on the 2-block level). So today's knob buys nothing at this spot; phase 04's bound from this sweep is "within 3 %", and the gain has to come from the far field's pass-by and its sky walk (cause 2), which the reference shows is worth 13 to 19 %.

### Step bands (from `--steps`, `night-64`, a frame each, 8 counted frames)

A step's band is where along its own ray it began, in blocks: primary, far, alpha and far_past from the eye; sun, AO and far_sun from their surface. "Calls" are traversals. Mean and p99 are a call's steps in that band (p99 to within a quarter octave).

| Kind | 0-16 | 16-64 | 64-128 | 128-256 | 256-512 | 512-1024 | 1024+ | All |
|---|---|---|---|---|---|---|---|---|
| primary | 5.96 M (6.8, p99 24) | 1.98 M (6.7, 24) | 4.81 M (8.4, 48) | 2.65 M (17.0, 64) | 3.13 M (10.9, 48) | 0.26 M (4.6, 24) | 0 | **18.79 M**, 2.07 M calls, 9.1 a pixel, p99 80 |
| far | 3.46 M (6.6, 9) | 3.00 M (5.7, 11) | 1.79 M (3.9, 14) | 2.16 M (4.1, 24) | 3.45 M (6.6, 32) | 3.71 M (7.1, 12) | 4.55 M (6.1, 16) | **22.11 M**, 755 k calls, 29 a call, p99 80; level 0 (4-block voxels) 21.24 M, level 1 (8-block) 0.87 M |
| sun (moon) | 0.22 M (2.2, 16) | 0.02 M (9.7, 32) | 0.4 k | 10 | 1 | 0 | 0 | **0.24 M**, 100 k calls |
| AO | 3.33 M (8.3, 12) | 0.55 M (3.0, 6) | 0.06 M | 0.02 M | 0.02 M | 4 k | 0 | **3.98 M**, 402 k calls |
| alpha | 0.14 M (11.6, 20) | 0.14 M (20.0, 32) | 0.08 M (7.6, 40) | 0.02 M | 0.02 M | 2 k | 0 | **0.41 M**, 32 k calls |
| far_past | 0.14 M | 0.14 M | 0.08 M | 0.02 M | 0.03 M | 4 k | 0 | **0.43 M**, 23 k calls |
| far_sun | 562 | 0 | 0 | 0 | 0 | 0 | 0 | 562 |
| reflect | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 (no water in view) |

By day (`day-64`), sun: 11.60 M (16.8 a call) / 2.95 M / 8.38 M / 727 / 399 / 0 / 0, **22.93 M**, 692 k calls, 33 a call. Full detail 16, far: 5.43 / 1.98 / 3.67 / 4.22 / 6.57 / 5.73 / 4.43 M, **32.04 M**, levels 25.38 M (2-block), 5.80 M (4-block), 0.86 M (8-block); primary **16.31 M**. Reference, primary: as `night-64` to 512, then 0.39 M and 0.28 M: **19.20 M**. Render scale 75: primary 11.05 M (9.5 a pixel), far 12.44 M.

**The heatmaps** (`night64.tsv.png` and its kin): primary is black in the sky (the coarse pass says sky), blue on the slope's face, and yellow to red (64 to 256 steps a pixel a frame) along every ridge and silhouette and over the close tree's edge; the far field's map is the mirror image, 30 to 60 steps a pixel over all of the sky and red just above the ridge, and nothing below it.

### Master section 7, "Today" (in this file, as the phase asks)

| Measure | Today |
|---|---|
| Card time at his spot, capped, p99 over 240 settled frames | **46.6 ms at 148 MHz** (the floor; mean 43.7); 18.0 ms p99 at 410 MHz; 6.5 to 7.2 Mcycles, 11 to 12 ms at 600 MHz |
| The same, mean | 43.7 ms at 148 MHz; 16.7 ms at 410 |
| Near guarantee, error past 16 blocks | n/a until phase 04; the reference is `bench_world_slope --lod-reference` at render distance 64 (it fits: 5639 MiB of voxels) |
| Mean primary steps a pixel at his spot | **9.06** (primary alone); **19.9** with the far field and far_past |
| Shadow-ray steps at his spot | **0.24 M a frame** at night (moon, 100 k rays); **22.9 M** by day at +40° |
| Water walk on `deep_ocean/orbit` at 64 | not counted here (the walk is not a traversal; see Issues found) |
| `bench_world` rows at render distance 32 | `history.tsv`, `009 phase 01: counters, no change` (taken with the card at its floor, so a like-with-like base for phase 08 needs a cool card) |
| VRAM | 2776 MiB of voxels at render distance 64, full detail 32 (2650 at 32) in the bench's pool; 5499 at full detail 48, 5639 for the reference |

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

**The bench is its own program, `bench_world_slope` (`bench/world/slope/`), not rows of `bench_world`.** `bench/world/index.c` is at 1494 lines, the standard's split point is 1500, and plan 008 phase 01 made `bench_world_shake` for the same reason. So the Verification's `bench_world --only =ahmed_slope/night-64 ...` lines ran as `bench_world_slope --only night-64 ...` (the `=` form works too; the rows keep the names `ahmed_slope/night-64`, `-32`, `day-64`). Its history is `bench/world/slope/history.tsv`. Its one indirect call is listed in `compile/src/standards_lint/indirect_calls.yaml` beside the shake bench's.

**His seed and view (Q2), as taken until he answers.** The seed is **not** the plan's default `11107447384583428913` but `14148492376765118422`, `saves/test_7/world.meta`'s: that save's state was written from 14:35 to 15:18 on 2026-11-12 and 5.png at 14:51, and its region `r.-1.0` holds x -385, y 347 (plan 008 phase 01 found the same world for its spot). The view, `--look 8 34` (yaw 8: east, a little north; pitch 34 up), was fitted by eye over five shots: the peak with red flowers, the striped slope, the shadowed hollow and the tree at the right fall where they do in 5.png, to within about 70 pixels. The ground differs in detail (the bench generates the world from the seed, it does not load the save, and plan 006 has changed the terrain since that world was made), and the eye may have been higher than F3's feet block says: with the eye 1.62 over z 2, water lies just behind it. The date is day 92 as on 5.png; the sun is put at −13.1° by the world clock that day, in the **evening** (the moon in the east); a dawn at −13.1° would light other faces. Q2 stays open for Ahmed.

**The reference fits at render distance 64, but not in the bench's usual pool.** At full detail 48 and 64 the 3456 MiB pool (the rest of the bench keeps under 4 GiB) refused the world (`section pool: over this pool's budget`). Rows past full detail 32 now take the game's own voxel budget, `MC_GPU_BUDGET_VOXELS_MIB` = 6144 MiB: full detail 48 used 5499 MiB and the reference at 64 5639 MiB. So phase 04 can compare at 64, not 48. With 256 + 256 MiB of textures and frames and the desktop's 277 MiB that is 6.9 of the card's 8 GiB.

**No picture changed.** The settled rows of `bench_world_shake` at plan 008's spot, `shake-90` and `shake-90-r64` (render distance 64: the far field and every shade kernel), give `20757A93FA745900`, the hash plan 008 phase 01 recorded on `main`'s code. `bench_world`'s full run is in its history as `009 phase 01: counters, no change` (with the card at its floor, 0 of 16 rows at 120 fps); its earlier profile lines are from 2026-11-08, before plans 005 to 008 changed the renderer, so its hashes compare only with later runs. The golden pictures: see the Verification. Counting itself changes no picture: the measured pictures are drawn before the counted ones, and the base row's hash (`C28033CF2426D1B5`) is the same with and without `--steps`.

**Not counted, and why.** Water's walk (`shade_water_through`) is a cell walk of its own, not a traversal, so "water-walk steps" (phase 05's prize) are not in the table; there is no water in this view. The entity rays and god rays (off in his settings) are not counted either. Step 2's "orbit within 1 % of its last history line" cannot be done by ms on this card (the clock moved 146 to 1041 MHz between rows); the instruction-level check above and the unchanged hashes stand for it.
