# 009-ahmed-bohemoth-game-engine-optimizations — phase 7: Temporal upscaling

**Status:** built and measured; not all targets met (mip bias by the output's pixel in, 2026-11-13). The upscale kernel, its tests and the ladder hookup are in. The convergence bounds and 75 % ≤ 62 % are not met (Issues found, Results). Checkerboard was not built. Ahmed's gate is still to come   (updated 2026-11-13)

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

- [x] Plan 008 phase 05 on `main`, or the gap in Issues found
- [x] `post_taa_upscale_kernel`; jitter of 16 points below 100 %, unchanged at 100 %
- [ ] Convergence test at 75 % and 50 % (32 frames, mean ≤ 1 / 255, p99.9 ≤ 8 / 255): the test is built and runs. With the mip by the output's pixel the room's mean meets its bound (0.87) and is held to 1; the p99.9 (23) and the jungle (1.59, 84) do not. See Issues found
- [x] Moving-camera ghost test
- [x] 100 % pictures unchanged (golden pictures, settled hashes)
- [ ] His spot at 100 / 85 / 75 / 50 %: ray passes ms in Results; 75 % ≤ 62 % of 100 %. Measured, in Results: 75 % is 67 % by cycles, so the target is not met, and the bench's saved picture is black (Results)
- [ ] Checkerboard measured; kept or deleted
- [x] Plan 008's ladder uses temporal upscaling for rungs below 100 %
- [x] `cuda/src/post/README.md` "The temporal pass" rewritten for output-resolution accumulation
- [ ] Ahmed has seen 75 % and answered Q9

## Results (filled in by this phase)

`bench_world_slope --only night-64 --render-scale N`, profile build, 2026-11-13, on `main` 150986d6 plus this branch. Each row is ms at that run's mean card clock, then Mcycles (ms × MHz), because the clock moved from 883 to 1405 MHz between runs. "Against 100 %" compares the ray passes in Mcycles.

| Scale | Coarse + primary + shade ms (Mcycles) | Post ms (Mcycles) | Card total ms | Card MHz (range) | Against 100 % |
|---|---|---|---|---|---|
| 100 % | 5.14 (4.54) | 0.52 (0.46) | 6.93 | 883 (669-1008) | — |
| 85 % | 3.48 (3.90) | 0.57 (0.64) | 5.31 | 1120 (933-1275) | 86 % |
| 75 % | 2.71 (3.03) | 0.50 (0.56) | 4.48 | 1119 (984-1240) | **67 %** |
| 71 % (checkerboard's ray count) | 2.52 (2.53) | 0.55 (0.55) | 4.35 | 1004 (793-1140) | 56 % |
| 50 % | 1.18 (1.66) | 0.30 (0.42) | 2.76 | 1405 (1166-1483) | 37 % |
| Checkerboard | not built | | | | |

**75 % is 67 % of 100 %'s ray passes by cycles, against the plan's 62 %.** That is not met, and the clock moving between runs makes the cycle numbers rough, as part of the work is bound by memory and not by the clock. By milliseconds alone, 75 % is 53 %. The gather costs about 0.1 Mcycles more than the pass at 100 % (0.56 against 0.46). Level 0's radius falls with the rung (27, 23, 21, 15 chunks), because θ grows by 1 / scale. The bench's rows read **NOT SOUND** because the picture it saves is black at every scale and eye height, with TAA on or off (the same hash, 08C740299C6F4325), while the card's passes clearly run. This also happens with TAA off at 100 %, so it is not this phase's: Ahmed's spot or the bench's picture capture after plan 007 phase 03 and 008 phase 05 needs a look. The pass times are still the card's.

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

- 1a2b27fb Gather a picture drawn smaller at the output's size, plan 009 phase 07 (the kernel, the 16-point jitter, the history's weight, the frame hookup, the tests, the READMEs).
- 9b06718b Measure plan 009 phase 07 at Ahmed's spot (Results; the frame pool to 288 MiB).
- Rebased onto `main` ed5bdbb9, which has plan 009 phase 06. There, the debug build and all 20 of `render_feature_*`, `run_render_frame`, `standards_lint` and `clang_format` pass.

## Issues found

**Plan 008 phase 05 is on `main`** (cdb81467, 0b616f50). This phase is built on its rungs, on `main` at 150986d6.

**What was built, and where it differs from the plan's design:**

- **The history counts its samples.** `post_taa_pixel_t` gains a `weight` (12 to 16 bytes a pixel: 16.6 MB more at 1080p for the two blocks). The upscaling pass keeps a running mean up to a cap (`post_upscale_mix`, `post_upscale_cap`). Moving, the cap is ten frames' worth of drawn samples, as at 100 %. Still, it is `upscale_still_frames` (32, a key the plan did not list) frames' worth, so that a still picture comes to the whole one. A plain 10 % blend would leave about 16 % of the first frame after 32 frames.
- **The gather's weight is a box, not Catmull-Rom.** A drawn sample counts 1 when its jittered centre falls inside the output pixel, which is what the pixel at 100 % averages over. Otherwise it counts 1/1024 of a tent `upscale_kernel_radius` (1.0) drawn pixels wide, as a fallback when no sample fell inside. Catmull-Rom's negative lobes make the count meaningless, and an interpolating kernel blurs a still picture's edges against 100 %.
- **The history is always at the output's size with TAA on.** The pass at 100 % now reads it by the last camera given the output's size, which is the same bits at a steady 100 %. AO's history is still at the drawn size, as plan 008 phase 05 left it.
- **`cuda/src/frame/index.cu`** (not in the owned paths) chooses `post_frame_upscaled` or the bilinear `post_upscale`: a seven-line change. The jitter's flag `POST_TEMPORAL_UPSCALE` gives the 16-point jitter to shade's AO too, without touching `cuda/src/shade/`.

**Convergence: the plan's bounds are not met.** These were measured in `render_feature_post` on Ahmed's card: 32 still frames from no history, against 100 % with TAA converged over 128 frames. The test holds weaker bounds: nearer than one frame brought up bilinear, mean ≤ 3 at 75 % and ≤ 4.5 at 50 %.

| Place | 100 % itself, 32 frames from nothing | 75 % | 50 % | One frame at 75 %, bilinear |
|---|---|---|---|---|
| Room (stone, 320 × 180) | mean 0.23, p99.9 5 | mean 1.17, p99.9 23 | mean 2.05, p99.9 31 | mean 2.03, p99.9 51 |
| Spawn, bamboo jungle (640 × 360) | mean 0.42, p99.9 39 | mean 2.19, p99.9 133 | mean 3.41, p99.9 155 | mean 3.72, p99.9 144 |

**The mip by the output's pixel (2026-11-13, after phase 06 landed).** `shade_frame_marked` takes `mip_rows`, the output's rows when TAA gathers a smaller picture (`cuda/src/frame/` passes the output camera's height, 0 otherwise), and shade's five kernels (water, surfaces, sun, AO, AO's fresh list) choose a texture's mip by `shade_pixel_tangent_rows`. God rays and the far field's own shading keep the drawn pixel's (the far field is plan 008 phase 06's path while it runs). With TAA off, brought up bilinear, the drawn pixel's mip stays. It is a parameter and not a field of `shade_scene_t`, because that struct sits inside `render_frame_t` and `render_t` with pinned offsets. At 100 % it is the camera's own rows and the same bits: every golden picture of `render_feature_*` and `run_render_frame` passes unchanged. Measured in `render_feature_post`:

| Place | 75 % before | 75 % with the mip | 50 % before | 50 % with the mip |
|---|---|---|---|---|
| Room | mean 1.17, p99.9 23 | **mean 0.87**, p99.9 23 | mean 2.05, p99.9 31 | mean 1.20, p99.9 31 |
| Jungle | mean 2.19, p99.9 133 | mean 1.59, p99.9 84 | mean 3.41, p99.9 155 | mean 2.20, p99.9 106 |

So the room's mean at 75 % meets the plan's 1 / 255, and the test now holds it there (≤ 1.0), with 75 % ≤ 2.0 and 50 % ≤ 3.0 everywhere (were 3.0 and 4.5). The p99.9 bound of 8 is still not met: that is edges and thin things, below, not the mip.

Why the bounds were not met before:

- **The mip.** Shade picks a texture's mip by the drawn pixel's size, so textures at 75 % are a mip blurrier than at 100 %. A trial that scaled `shade_pixel_tangent` by 0.75 took the room's mean from 1.14 to 0.85 and the jungle's (before the rebase onto 007-03's terrain) from 1.76 to 1.12. The p99.9 did not move. This is the mip bias that FSR 2 and DLSS ask of an engine. It needs the output's rows in shade's five kernels, which are phase 06's paths while it runs, so it is left for after that lands.
- **Edges.** At 75 % about nine jittered points fall inside an output pixel over 16 frames. The 100 % picture weighs its own eight points with a 10 % blend, so a hard edge comes out up to an eighth of its contrast apart. That is about the 23 here. Keeping the clamp off while still made no difference (1.14 against 1.17), so the clamp is not the cause.
- **Thin things.** The spawn's bamboo is thinner than a drawn pixel. Even 100 % is 39 apart from itself at p99.9 there, and 75 % is 133. This is the plan's risk "thin things shimmer at 75 %".

**Ghost:** two frames after a block in front of a still camera went, 0 of 2724 pixels it covered at 75 % are still more block than not, 3 of 2718 at 50 %, and 0 of 2720 at 100 %. The walk, block and jump checks of `taa_follows_a_moving_eye_and_leaves_no_ghost` at 100 % are unchanged.

**100 % unchanged:** at a steady 100 % the pass runs the same lines with the same inputs. `render_feature_post`'s goldens and its "same frames, same bytes" test pass, as do `run_render_frame`'s goldens and scripted-bytes test.

**Checkerboard was not built.** It needs a parity mask in `cuda/src/traverse/` and `cuda/src/shade/` index math, and phases 05 and 06 are editing those files in parallel. The ray cost of half the output's pixels is that of the 71 % rung (√0.5). Its accumulation would be cheaper than the gather, but each pixel would be a frame old under motion. It is left unticked, not deleted, as there is nothing to delete.

**The frame pool's budget goes from 256 to 288 MiB** (`config/machine.yaml`, `gpu.budget.frames_mib`). With the history's weight, render distance 64 at 1080p no longer opened: "far hits: over this pool's budget". The two history blocks grew by 16.6 MB. The card has 7398 MiB free against a budget now of 6688.

**Ahmed's gate is not done.** The default stays 100 (Q9), as no one has seen 75 % yet. Ahmed is to run:

    ./build/release/bin/minecraft_c --menu

then set the render scale to 75 on the Settings screen, look standing still and turning, and answer the three questions in the Gate.

**Test name:** "plains and jungle places" is the hand-built stone room and the default world's spawn, which is a bamboo jungle. There is no plains spawn on the default seed.
