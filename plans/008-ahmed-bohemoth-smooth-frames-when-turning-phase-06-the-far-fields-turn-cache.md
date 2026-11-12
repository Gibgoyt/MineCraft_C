# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 6: The far field's turn cache (conditional)

**Status:** not started   (updated 2026-11-12)

Read first: `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-00-vision-and-plan-files.md`, then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`: section 1 (C5), 3.6, 6 and Q7. Then `cuda/src/lod/README.md` (all of it), phase 01's cost rose at render distance 64, and phase 05's results (how often the floor was hit at 64).

## Goal

**First, a decision.** This phase builds only if both of these hold, on the capped card:

1. phase 01's cost rose shows the far field's kernels (`lod_past_kernel` and `lod_frame_kernel`) costing **at least 1.5 ms** of the worst direction's picture at render distance 64;
2. after phases 02 to 05, `spot/shake-720-live-capped-r64` still misses its target (p99 > 16.7 ms), or sits at the 50 % floor for more than a fifth of its pictures.

If either does not hold, this phase records the numbers below, sets its status to `done` with "not needed", and builds nothing (Q7).

**If it builds,** turning in place at render distance 64 traces each far direction once:

- **A cube map round the eye,** 6 faces of 1024 × 1024, holds what each direction's far ray found.
- **Every far ray traced writes its texel; a pixel whose direction's texel is valid reads it instead of tracing.**
- **A texel is valid** if it was written from within 0.25 blocks of the current eye, and since the far field last changed.
- **Moving invalidates it by distance,** and costs what it costs today.
- **On `spot/shake-720-r64`** (settled) and `-live-capped-r64`, the far field's card time while shaking is at most 0.4 times its time today, and the picture is the same as today's for a still eye, byte for byte.

## Where this fits

**The vision.** Master C5: from a mountain at render distance 64, the dear directions are the ones looking out, where primary rays cross up to 512 blocks of full detail and then the far field traces on. A head shake visits them over and over from the same eye. The far field is 512 blocks and more away, so from an eye that has not moved its answer for a direction does not change. That makes it a cache keyed by direction, which only turning in place can use, and turning in place is exactly Ahmed's case.

**What reads it:**

| Reader | What |
|---|---|
| Phase 07 | Far-field ms while shaking, before and after |
| Ahmed | Whether far hills look the same while turning |

**What this phase must NOT build:**

- No cache for the near world's primary rays. Their answers change with the slightest eye movement, and the coarse pass already skips their empty space.
- No change to the far field's levels, building, or what it draws.
- No reprojection with parallax (warping the cache as the eye moves). If walking makes it useless, that is accepted: walking is not the case this phase is for.

## Ahmed's words

- "when shaking my head vigorously frame rate drops drastically we have to be smarter about what we are doing to fix this please!"

---

## Background, explained

### Why 0.25 blocks

A far surface is at least 512 blocks away (full detail 32 chunks). An eye that moved $d$ blocks sideways sees it shifted by $\tan^{-1}(d / 512)$. At the picture's centre one pixel is 0.065° (70° over 1080 rows). For $d = 0.25$: $\tan^{-1}(0.25/512) = 0.028°$, under half a pixel. So from within 0.25 blocks the cached answer is right to within half a pixel, and TAA's jitter is larger than that.

When the eye moves farther, the texel is invalid and the ray is traced again, writing the texel from the new eye. Walking at 4.3 blocks a second passes 0.25 blocks in 58 ms, about 3 pictures. So walking traces nearly as today, while standing and turning traces almost nothing twice.

### Why a cube map of 1024

One cube face covers 90°. At 1024 texels that is 0.088° a texel at the face's centre and about 0.05° at its corners, against 0.065° a pixel at the picture's centre. So a texel is about a pixel near the middle of the screen, and a little larger at the face centres.

Far surfaces are drawn with their face's mean colour (`cuda/src/lod/README.md`, "What is simple about it"). A blur of a texel there costs little, and TAA blends it anyway.

The cost is 6 × 1024² texels × 16 bytes (`t` as 4 bytes, colour as 3 halves, face and flags as the rest) = **96 MiB**. That is outside the frames budget (master section 6), so it gets a line of its own in `render_lod`'s budget.

### What "since the far field last changed" means

The far field changes when a level is rebuilt or scrolls: `render_lod_frame` follows the eye and shows new columns (`run/src/render/lod/index.c:795-821`). A revision counter in `render_lod_t`, bumped whenever any level's shown square or contents change, goes into each texel. A texel with an older revision is invalid.

Far columns change rarely while standing still (plan 005 phase 14: only on a real edit captured), so a shake keeps its texels.

### What a texel holds, and what it replaces

`lod_frame_kernel` writes a `lod_hit_t` per pixel, which shade reads where its own ray leaves the world (`shade_scene_t.borrowed_lod_hits`). The texel holds the same `lod_hit_t` fields, for the ray from the cube's centre in that texel's direction.

A pixel whose far ray would be traced (`lod_past_kernel` said it gets out) first looks up its direction's texel: if valid, it copies it into its `lod_hit_t` and does not trace. If not, it traces as today and writes the texel.

The pixel's own direction is jittered by TAA. The texel is for the texel's centre direction. The difference is under a texel, as above.

---

## Design

### `cuda/src/lod/`

- **`lod_turn_cache_t`:** the block, the eye it was last valid for (cell and fraction, as the camera holds it), and the revision.
- **`lod_frame_kernel`:** for a pixel that would trace, read the texel; if valid, use it; if not, trace and write it.
- **Writes** are plain stores: two pixels writing one texel in a frame write answers within a texel of each other, and either is fine.
- **A frame** first compares the eye with the cache's eye. If they are more than 0.25 blocks apart, it bumps a "generation" that invalidates all texels at once (no clearing pass). The texel stores the generation and the revision.

### `run/src/render/lod/`

`render_lod_t` gains `revision`, bumped in `render_lod_level_show` when a level's shown square or contents change, and the turn cache's block from `render_lod`'s budget.

### Switch

`SHADE_FEATURE_LOD_TURN_CACHE` (on when this phase lands), so `bench_world --features-off` can compare.

---

## Owned paths

This phase may edit these, plus this file:

- `cuda/src/lod/`, `run/include/gpu_bridge/lod/`
- `run/src/render/lod/`, `run/include/render/lod/`
- `run/include/gpu_bridge/shade/` (the feature bit only)
- `tests/cuda/lod/` (`render_feature_lod`), `tests/run/render/lod/`
- `bench/world/` (only if a column is needed)

## Steps

1. **The decision:** read phase 01's cost rose at render distance 64 and phase 05's results; fill in the decision table below. If not needed, close the phase. *Why: master Q7; 96 MiB and new code only if they pay.*
2. **The cube map's addressing (direction to face and texel) as pure functions** with host tests. *Why: the part that is easy to get subtly wrong.*
3. **The cache in `lod_frame_kernel`,** the generation and revision. *Why: the change.*
4. **The same-picture test for a still eye,** then the shake rows. *Why: the target.*
5. **READMEs** (`cuda/src/lod/README.md` gains "The turn cache"); tick todos as they land.

## Todo

- [ ] The decision table below filled in from phase 01's rose and phase 05's results; built or closed as it says
- [ ] (If built) direction-to-texel addressing, with host tests
- [ ] (If built) the turn cache in `lod_frame_kernel`; generation by eye distance; revision from `render_lod_t`
- [ ] (If built) `SHADE_FEATURE_LOD_TURN_CACHE`; 96 MiB in `render_lod`'s budget, measured
- [ ] (If built) a still eye's pictures byte for byte as before; shake rows' far-field ms ≤ 0.4 × today
- [ ] (If built) READMEs

## Decision (filled in by this phase)

| Measure | Value | Bar | Met? |
|---|---|---|---|
| Far field ms in the worst direction of the rose, render distance 64, capped | | ≥ 1.5 ms | |
| `spot/shake-720-live-capped-r64` p99 after phase 05 | | > 16.7 ms | |
| Share of its pictures at the 50 % floor after phase 05 | | > 20 % | |

**Built** only if the first row and one of the other two are met.

## Results (filled in if built)

| Row | Far field ms, before / after | Card ms | Interval p99 | Cache hit share |
|---|---|---|---|---|
| `spot/shake-720-r64` | | | | |
| `spot/shake-720-live-capped-r64` | | | | |
| `plains/flight-live-r128` (walking: should be ≈ today) | | | | |

## Tests

### Host tests (in `render_feature_lod`'s suite)

- **`every_direction_has_one_texel`.** 1 M random unit directions: each maps to a face and texel in range, and the texel's centre direction is within one texel's angle of it.
- **`the_six_face_centres_map_to_their_own_face`.**

### `render_feature_lod` and `run_render_lod` (extended)

- **`a_still_eye_draws_the_same_far_field`.** 60 frames at a fixed eye, cache on and off: every picture the same byte for byte. With jitter included, the texel is written and read by the same directions, as TAA's 8 jitter offsets repeat.
- **`a_turn_back_traces_nothing_far`.** Turn 60° right and back over 20 frames at a fixed eye: on the way back, the far rays traced are under 5 % of the way out's.
- **`moving_a_quarter_block_retraces`.** The eye moves 0.3 blocks: every texel read on the next frame is invalid.
- **`a_far_change_retraces`.** `render_lod_t.revision` bumped: every texel invalid.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                         # must print nothing before the bench runs
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_lod|run_render_lod|run_render_distance_live|run_render_frame)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_p06.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --render-distance 64 --record bench/world/history.tsv --label "008 phase 06: far turn cache"; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --only =plains/flight-live-r128
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"; pgrep -af gpu_watch                       # both must print nothing

If the phase closes as not needed, only the measurements already made in phases 01 and 05 are read, and none of the commands above is run.

## Gate

**If closed:** Ahmed reads the decision table and agrees, or asks for the phase anyway.

**If built:** Ahmed stands on his mountain at render distance 64, shakes his head, and says whether the far hills look the same and whether it is smoother than after phase 05.

## Risks and open questions

| Risk or question | Default |
|---|---|
| 96 MiB is too much of the card | Measured beside the rest; 768² faces (54 MiB) as the fallback, at a little more blur far away |
| A texel's direction and a pixel's jittered direction differ, so far edges shimmer | Under a texel; TAA blends; the still-eye test is byte for byte because jitter repeats |
| The far field changes without the revision noticing | Bumped in `render_lod_level_show`, which every change goes through; the far-change test |
| It is no help while walking | Expected and accepted; the walking row should equal today |
| The rose says the near world, not the far field, decides the dear directions | Then this phase closes, and the near world's cost is phase 05's to absorb; recorded |

## Commits

Appended as they land:

## Issues found

None.
