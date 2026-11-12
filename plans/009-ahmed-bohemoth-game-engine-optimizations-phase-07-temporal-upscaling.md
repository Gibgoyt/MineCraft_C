# 009-ahmed-bohemoth-game-engine-optimizations — phase 7: Temporal upscaling

**Status:** not started   (updated 2026-11-12)

Read first: the phase 00 file, then the master: sections 3.1 (TSR), 3.5, 4.7, 5 and 7. Then:

- `cuda/src/post/README.md`, "The temporal pass": Halton jitter, reprojection along the unjittered ray, Catmull-Rom history, the 1.25 σ clamp, the 0.1 blend;
- plan 008's master section 3.5 and its phase 05 file: the resolution ladder, history indexed by the previous width, the bilinear upscale kernel. **This phase builds on 008 phase 05 and waits for it;**
- phase 01's "render scale 75" row.

## Goal

The card traces fewer rays than the screen has pixels, and the picture on the screen is still 1920 × 1080, built up over frames. When this phase is done:

- **Temporal upscaling** replaces plan 008 phase 05's bilinear upscale. At an internal scale below 100 %, each frame traces `camera.width × camera.height` jittered rays, and the temporal pass accumulates them **at the output resolution**: each output pixel gathers the internal samples near it, reprojects its own history, clamps, and mixes.
- **A still camera converges to the full-resolution picture:** after 32 still frames at 75 %, the mean absolute difference from a 100 % picture is at most 1 / 255 per channel, and the 99.9th percentile at most 8 / 255.
- **A moving camera has no ghost** beyond what today's TAA clamp allows.
- **At his spot, 75 % costs at most 62 % of 100 %'s ray passes** (coarse + primary + shade). 56 % of the rays plus the accumulation's cost.
- **Checkerboard rendering** is measured beside it as an experiment, and kept only if it is better per millisecond.
- **The default internal scale stays 100 %** until Ahmed has seen 75 % (master Q9). Plan 008's ladder uses temporal upscaling for its lower rungs.

## Where this fits

**The vision.** Ahmed asked for research on top of Unreal: TSR is Unreal 5's default, and its documentation says a frame "carries" only part of the detail, with the rest built up over frames. Plan 008 left temporal upsampling "as a later plan" (its Q9); this is that plan. A ray caster's cost is per ray, so tracing 56 % of the rays is the largest single lever after the card's clock.

**What reads it:** phase 08; plan 008's resolution controller, whose rungs become cheaper to stand on.

**What this phase must NOT build:**

- No library. DLSS needs DirectX or Vulkan through NGX, and FSR 2's code is HLSL. This is an own CUDA implementation of the published ideas (coding standard section 1).
- No change to the near guarantee's *geometry*: rays still choose levels by θ, which at 75 % is 1 / 0.75 larger, so bands move nearer by the same factor. The 16-block floor holds.
- No sharpening pass. The existing TAA has none on purpose (`cuda/src/post/README.md`).
- No change to the weather drops or the outline, which draw after the history at output resolution, as now.

## Ahmed's words

- "Please only extremely thoroughly research this! research rendering optimization on top of engines like unreal engine and godot!"

---

## Background, explained

### What changes from today's TAA

Today:

- the internal and output pictures are the same size;
- each frame's camera is moved by a Halton point inside the pixel (8 points, bases 2 and 3);
- `post_taa_kernel` reprojects each pixel's history and clamps it to the 3 × 3 neighbourhood's mean ± 1.25 σ;
- it mixes in 10 % of the new frame.

With an internal picture smaller than the output:

- **Jitter covers the output pixel grid.** An internal pixel is 1 / 0.75 = 1.33 output pixels wide. The jitter sequence must visit every output pixel's area, so it gets longer: 16 Halton points (`upscale_jitter_points`).
- **Each output pixel gathers.** It takes the internal samples whose jittered centres fall within 1 internal pixel of its own centre, weighted by a Catmull-Rom (or Lanczos-2) kernel on the distance: TSR's and FSR 2's "upsample" step. With few samples near it this frame, the history counts for more.
- **The clamp uses the internal neighbourhood** around the output pixel's position.
- **History is at output resolution,** which plan 008 phase 05 already stores, indexed by the previous camera's width.

### How fast a still picture converges, worked

At 75 % per axis, each frame gives 0.5625 samples per output pixel. With a 10 % blend, history holds about 10 frames' worth, so about 5.6 samples per output pixel. That is enough for the mean to settle within 1 / 255 on flat colour. Edges need the full 16-point cycle, 16 frames, so 32 frames is the test's bound. At 120 frames a second, that is about a quarter of a second.

### Checkerboard, briefly

Rainbow Six Siege's checkerboard rendering traced half of each frame's pixels in a checker pattern, alternating, and filled the rest by reprojection with a neighbourhood clamp. Here that is internal width × height = output width × height / 2, which is close to 70 % per axis but on the output grid, so no gather is needed. It is a cheaper accumulation step with a worse failure mode under motion: every other pixel is a frame old. Measured beside temporal at 70 %, and kept only if better.

## Design

### `cuda/src/post/`

- **`post_taa_upscale_kernel`:** one thread per output pixel. It gathers up to 4 × 4 internal samples (`post_catmull_rom` exists), reprojects history with `post_reproject` along the output pixel's unjittered ray, clamps and mixes. It writes the output history and the tone-mapped output picture.
- **`post_jitter`:** takes `upscale_jitter_points` and the scale; at 100 % it gives today's 8-point sequence, so a 100 % picture is unchanged.
- **Weather and outline:** applied after, at output resolution, as now.

### `run/src/render/frame/`, plan 008's ladder

- **The internal camera at the rung's scale; the output at the display's size.**
- **The bilinear upscale is replaced** when TAA is on. With TAA off (a test), the bilinear stays.
- **`render_scale_percent`** sets the top rung, default 100 (Q9).

### Checkerboard (experiment)

A switch `checkerboard` in `render/temporal`. Primary and shade run on half the pixels by a parity mask in the kernels' index math; post fills the rest. It is deleted if not kept.

### Tuning (`compile/config/render/temporal/index.yaml`)

- `upscale_jitter_points` (16);
- `upscale_kernel_radius` (1.0, internal pixels);
- `checkerboard` (false, while measuring).

## Owned paths

| Path | Change |
|---|---|
| `cuda/src/post/`, `run/include/gpu_bridge/post/index.h` | The upscale kernel, jitter |
| `run/src/render/frame/` | Internal and output sizes; ladder hookup |
| `cuda/src/traverse/`, `cuda/src/shade/` | Checkerboard parity (experiment only) |
| `compile/config/render/temporal/index.yaml` | Keys |
| `tests/cuda/post/`, `tests/run/render/frame/` | Tests |

## Steps

1. **Wait for plan 008 phase 05,** or record why not.
2. **The upscale kernel at 75 %; the convergence test.** *Why:* a still picture is the quality bar.
3. **The moving-camera test:** no ghost past the clamp.
4. **Measure at his spot:** 100, 85, 75 and 50 %.
5. **Checkerboard experiment;** keep or delete.
6. **Hook into 008's ladder;** Ahmed sees 75 %.

## Todo

- [ ] Plan 008 phase 05 on `main`, or the gap in Issues found
- [ ] `post_taa_upscale_kernel`; jitter of 16 points below 100 %, unchanged at 100 %
- [ ] Convergence test at 75 % and 50 % (32 frames, mean ≤ 1 / 255, p99.9 ≤ 8 / 255)
- [ ] Moving-camera ghost test
- [ ] 100 % pictures unchanged (golden pictures, settled hashes)
- [ ] His spot at 100 / 85 / 75 / 50 %: ray passes ms in Results; 75 % ≤ 62 % of 100 %
- [ ] Checkerboard measured; kept or deleted
- [ ] Plan 008's ladder uses temporal upscaling for rungs below 100 %
- [ ] `cuda/src/post/README.md` "The temporal pass" rewritten for output-resolution accumulation
- [ ] Ahmed has seen 75 % and answered Q9

## Results (filled in by this phase)

| Scale | Coarse + primary + shade ms | Post ms | Card total | Against 100 % |
|---|---|---|---|---|
| 100 % | | | | — |
| 85 % | | | | |
| 75 % | | | | |
| 50 % | | | | |
| Checkerboard | | | | |

## Tests

### `tests/cuda/post/` — ctest `render_feature_post`

- `a_still_picture_at_75_converges_to_100`: plains and jungle places, 32 frames still at 75 %, against one 100 % picture with TAA converged. Mean absolute difference ≤ 1 / 255, 99.9th percentile ≤ 8 / 255, per channel.
- `the_same_at_50`: bounds 2 / 255 and 16 / 255 (recorded, not a target).
- `a_moving_camera_leaves_no_ghost`: a block removed in front of the camera mid-run; 2 frames later no pixel where it was differs from a fresh render by more than the clamp's range.
- `at_100_nothing_changes`: every existing golden picture with TAA, bitwise.

### `tests/run/render/frame/` — ctest `run_render_frame`

`the_output_is_always_the_displays_size`: at each rung, the picture handed to `render_frame_present` is 1920 × 1080.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_post|run_render_frame|render_feature_lod)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^render_feature_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --render-scale 75 --shots /tmp/mc_c_009/p07/s75
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 phase 07: temporal upscaling"
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"                                                       # must print nothing

## Gate

Ahmed plays at his spot with render scale 100, then 75, set on the Settings screen:

    ./build/release/bin/minecraft_c --menu

He looks standing still, and turning. He says:

1. Can he tell 75 from 100 standing still?
2. Turning?
3. Q9: the default stays 100, or goes to 75.

His answers are recorded below.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Plan 008 phase 05 is not built | This phase waits |
| Thin things (torches, leaves' holes) shimmer at 75 % | 16-point jitter; the clamp; judged; 85 % rung |
| The gather costs more than the rays it saves at 85 % | Measured; the ladder skips a rung that does not pay |
| The bands moving nearer at 75 % (θ larger) makes LOD visible | It is the same rule; the 16-block floor holds; judged at the gate |
| Checkerboard ghosts under motion | Expected; it is an experiment |

## Commits

Appended as they land:

## Issues found

None.
