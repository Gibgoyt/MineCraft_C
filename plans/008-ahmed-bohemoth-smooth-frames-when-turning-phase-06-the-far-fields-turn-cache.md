# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 6: The far field's turn cache (conditional)

**Status:** done, for Ahmed's gate: built; where the far field is seen (jagged peaks, r64) the shake's far field costs 0.10 of today settled and 0.19 live-capped, 99 % of far rays read from the cache; at Ahmed's own spot the shake sends no far rays at all today, so the cache neither helps nor costs there; a still picture the same to the bit   (updated 2026-11-13)

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

- [x] The decision table below filled in from phase 01's rose and phase 05's results; built or closed as it says
- [x] (If built) direction-to-texel addressing, with host tests
- [x] (If built) the turn cache in `lod_frame_kernel`; generation by eye distance; revision from `render_lod_t`
- [x] (If built) `SHADE_FEATURE_LOD_TURN_CACHE`; 96 MiB in `render_lod`'s budget, measured
- [x] (If built) a still eye's pictures byte for byte as before; shake rows' far-field ms ≤ 0.4 × today (at jagged peaks; at Ahmed's spot there is no far-field ray to save, see Results)
- [x] (If built) READMEs

## Decision (filled in by this phase)

| Measure | Value | Bar | Met? |
|---|---|---|---|
| Far field ms in the worst direction of the rose, render distance 64, capped | 1.0 to 1.6 Mcycles a picture at r64 (phase 01: 15 to 23 % of 6.5 to 7.2 Mcycles), 1.7 to 2.7 ms at 600 MHz; and since plan 009 phase 04 the far field starts at d_1 (385 blocks at 100 %, 193 at 50 %) in place of 512, so it has only grown | ≥ 1.5 ms | yes |
| `spot/shake-720-live-capped-r64` p99 after phase 05 | 25.03 ms (dynamic on, 182 MHz; phase 05's table) | > 16.7 ms | yes (on a card at a quarter of 600 MHz; the replay at 600 MHz gave 16.0) |
| Share of its pictures at the 50 % floor after phase 05 | 99 % | > 20 % | yes |

**Built** only if the first row and one of the other two are met.

## Results (filled in if built)

| Row | Far field ms, before / after | Card ms | Interval p99 | Cache hit share |
|---|---|---|---|---|
| `spot/shake-720-r64` | 0.25 / 0.25 ms (999 to 1034 MHz): no far ray is sent | 7.25 / 7.33 | 7.75 / 8.05 | none (no far rays) |
| `spot/shake-720-live-capped-r64` | 0.29 at 313 MHz / 0.08 at 1123 MHz: 0.09 Mcycles both, the "who asks" pass alone | 10.56 / 2.97 (clock) | 25.02 / 25.03 | none (no far rays) |
| `jagged_peaks/shake-720-r64` | 33.07 at 114 MHz (3.77 Mcycles) / 2.89 at 130 MHz (0.38 Mcycles): **0.10** | 68.49 / 33.16 (7.37 / 4.07 Mcycles) | 97.18 / 63.53 | 99.4 % |
| `jagged_peaks/shake-720-live-capped-r64` | 7.92 at 111 MHz (0.88 Mcycles) / 1.24 at 135 MHz (0.17 Mcycles): **0.19** | 23.24 / 16.92 (3.02 / 2.36 Mcycles) | 41.69 / 33.37; 45 / 61 pictures a second | 94.9 % |
| `plains/flight-live-r128` (walking: should be ≈ today) | primary pass 51.73 / 52.97 | 97.48 / 98.05 | 132.17 / 132.64 | (walking: a new generation every 0.25 blocks) |

Before / after = `--features-off 100000` / the cache on, one run after the other, profile build, 1920x1080, `--dynamic-floor 50` (the game's default), recorded in `bench/world/shake/history.tsv` ("008 phase 06: turn cache, features off ..." at the spot, "... at jagged peaks, ..." at `bench_world_shake`'s own place). Ahmed's game was closed (`pgrep` before every run). **The card was thermally throttled throughout, its clock moving from 35 to 1400 MHz between and within runs** (clock64 readings in each row), so before and after compare in Mcycles, not ms. `gpu_watch.sh` was not run beside these (the bench lines are adapted: `bench_world_shake` with the rows named, not `bench_world --only shake`).

**At Ahmed's spot the far field is not what turning costs.** With plan 009 phase 04 level 0 is held out to 27 chunks (432 blocks) at r64, and from the spot looking 10 degrees down every pixel of the shake's sweep ends on blocks of level 0: the far field's kernels send no far ray at all (the cache's tally stayed at 0 for the whole row, yaw 0 and 255 alike), and their 0.09 Mcycles is the pass that finds that out. Phase 01's 15 to 23 % at r64 was measured before plan 009 phase 04. So at the spot this phase changes nothing, and the floor is still held by shade (AO and surfaces) and post, which are plan 009 phases 05 to 07's. Where the far field is seen (jagged peaks, looking out over the range), it is half the picture's card time while shaking, and the cache takes it to a tenth.

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

- `89ee7c5b` Keep each far ray by direction in a cube round the eye, plan 008 phase 06: the cache, its addressing, the feature bit, the 96 MiB, `render_feature_lod_turn`, the tests in `run_render_lod`, the share in `bench_world_shake`.
- `fd9fed1b` space the far field's revision (`turn_revise_frames`, 30) so the pyramid's steady trickle of columns does not end the cache's generation every frame; `bench_world_shake` reads the tally through the frame's far field; the shake rows measured at Ahmed's spot and at jagged peaks, and the walking row.
- `db0c7d45`: the hashes above after rebasing onto `da4d59b9` (plan 009 phases 06 and 07): `lod_far_ray` takes plan 009 phase 06's `lod_reach_t` in place of the view and factor, the level loop having moved to `cuda/include/lod/`. After the rebase `render_feature_lod_turn`, `render_feature_lod`, `render_feature_lod_bands`, `run_render_lod`, `run_render_distance_live`, `run_render_frame`, `run_game`, `standards_lint` and `clang_format` all passed. Not run: `cmake --build --preset release` (the disk had 16 GB free; nothing here is on main's frame, `render_lod_t` lives in its arena) and `gpu_watch.sh` beside the benches.
- this commit (branch `fix-turn-cache-bit`, from `f8390718`): the feature bit kept out of `shade_scene_t.features`, below.

## Issues found

- **The far field is plan 009's pyramid now.** Since plan 009 phase 04 there is no Full detail: level 0 ends at each pixel's d_1 (`band_base` 2^1 by a dithered factor, 385 blocks at E 2 and 1080 rows, 193 at the 50 % rung) and the levels take over from there. So "the far field" this phase caches is every ray past level 0, from where level 0 stopped for that pixel. The texel therefore also holds `from`, where its ray started, and a pixel takes a texel only if its own ray starts within `2 band_dither + 2 %` (18 %) of that: the dither of d_1 frame to frame is allowed, a pixel whose ray leaves level 0 somewhere else (a hole in level 0, shade's ray through water, a rung that moved d_1 by half) traces. The 0.25 block rule was argued for 512 blocks; at d_1 = 193 it is 0.074 degrees against 0.13 a pixel at 540 rows, still under one.
- **Only a turn reads the cache** (`LOD_TURN_PIXELS`: the camera's middle moved by more than 2 pixels since the last frame; TAA's jitter moves it by under one). The plan had every frame read, and said a still eye's pictures would be byte for byte because the jitter repeats; they would not be, as a texel is written by whichever pixel last fell in it, at its own jittered direction and its own dithered d_1, and read by another. Every frame still writes, so a turn's first frame reads what the still ones wrote. The still picture is then the same to the bit by construction, as with plan 008 phase 04's first guess.
- **A texel holds `lod_hit_t` (8 bytes) + `from` + generation = 16 bytes**, not a colour: shade works the colour out from `lod_hit_t` as before, so a texel stays right as the day goes on. Its sun bit is kept, so a generation also ends when the sun moves a quarter of a degree or shadows come or go.
- **The generation** is one counter for all that ends it: the eye 0.25 blocks from where it began, the far field's `revision`, the sun. `revision` moves on in `render_lod_frame` (not inside `render_lod_level_show`, which returns early when nothing is stale) whenever a hash of every level's shown square and `generation` (bumped by every column sent) changes.
- **Memory**: the block is taken from the voxels' pool at the first `render_lod_frame` (not at init: `render_replan_lod` inits the new far field before giving back the old one's, so at init the card would briefly hold two). Measured by `the_cache_takes_what_its_side_says_from_the_voxels`: 96.0 MiB beside 66.2 MiB the test's far field and world held of the 6144 MiB pool. The side is `turn_cache_side` in `compile/config/render/lod/index.yaml` (768 the fallback, 0 none).
- **Fixed after landing: the bit leaked into `shade_scene_t.features`.** `render_frame_features_set` kept `SHADE_FEATURE_LOD_TURN_CACHE` beside `SHADE_FEATURES_EVERY` in `frame->scene.features`, which the features enum promises holds only `SHADE_FEATURES_EVERY`'s bits (`render_feature_sky`, `_water`, `_shapes` and `_post` check `scene.features == SHADE_FEATURES_EVERY` / `_PLAY` after `render_frame_features_set(0xFFFFFFFF)` or the game's set, and failed with 1507327 != 458751). It now goes where `SHADE_FEATURE_AO_FIRST_GUESS` goes: a bit of `post_temporal_t.flags`, `POST_TEMPORAL_LOD_TURN` (`1u << 6`), kept from frame to frame as `FIRST_GUESS` is, and `run/src/render/frame/` gives the far field its cache only when that bit is set. My verification had run only the lod suites, `run_render_frame` and `run_game`, not every `render_feature_.*`.
- **The feature bit** is `1u << 20`, asked for by `game_frame_features` unless `features_off` has it, like `SHADE_FEATURE_AO_FIRST_GUESS`; `render_frame_features_set` keeps it beside `SHADE_FEATURES_EVERY`, and `run/src/render/frame/` drops the cache from a frame without it (two lines in that shared file, one in `run/src/game/`). It is not in `SHADE_FEATURES_PLAY`, so a settings file saved before it does not turn it off. Plan 009's parallel phases may take bit 20 too; whoever lands second moves theirs.
- **Tests are their own ctest, `render_feature_lod_turn` (`tests/cuda/lod/turn/`)**: `render_feature_lod`'s file is at 1517 lines. The addressing tests are in `run_render_lod`, as they need no card. The turn tests use a side of 256, a texel about a pixel of the test's 180 rows, as 1024 is of 1080. `moving_a_quarter_block_retraces` and `a_far_change_retraces` are one test, `moving_a_quarter_block_or_a_far_change_retraces`. Results (debug build, after the revision spacing): still eye, 60 frames with TAA, 0 of 60 pictures differ, 0 texels read, 591848 far rays written; turn back, 29969 far rays traced on the way out and 242 on the way back (0.8 %), with 0.10 % of the way back's pixels off by more than 24 from the same camera traced; same eye 161 traced / 9703 read, 0.3 blocks off and a far change 0 read. The test's settle draws `turn_revise_frames` more far-field frames after the far field is idle, so that a revision put off for its last columns lands before the test and not in the middle of the turn (it did, once, before that).
- **`bench_world_shake`** prints `far turn cache: N % of far rays read from it` for each row (a field in the place of its result's last padding); `--features-off 100000` is the cache off. It reads the tally through `render.frame.borrowed_lod`.
- **The revision is spaced** (`turn_revise_frames`, 30, in `compile/config/render/lod/index.yaml`), found by measuring: at Ahmed's spot the pyramid sends about 3 columns a frame long after filling (levels made from the blocks taking over from terrain), and ending the generation for each kept the cache from ever being read. Columns sent now end it at most once every 30 frames; a level's square shown moving ends it at once. A far column sent shows in what a turn reads from the cache at most 30 frames late, and at once in what is traced. Even so, at the spot the revision still moved every frame in the bench, so a level's square shown moves there every frame while columns come in: harmless there, as no far ray is sent, but worth a look by whoever next works on the pyramid (`render_lod_level_show`).
- **The decision, measured again**: the first row of the decision table is met by phase 01's numbers but not by today's at the spot (0.09 Mcycles, about 0.15 ms at 600 MHz, under the 1.5 ms bar), as plan 009 phase 04 moved what the spot sees into level 0. It is met where the far field is seen (3.77 Mcycles, 6.3 ms at 600 MHz, at jagged peaks). The cache was built on phase 01's and 05's numbers as the phase says, and is kept on: it costs nothing measurable where no far ray is sent, and takes the far field to a tenth where one is. Ahmed decides at the gate; `turn_cache_side: 0` takes it and its 96 MiB out.
