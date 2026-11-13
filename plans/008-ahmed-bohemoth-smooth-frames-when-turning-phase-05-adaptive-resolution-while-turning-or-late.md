# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 5: Adaptive resolution while turning or late

**Status:** built and tested; the shake rows and the shots next   (updated 2026-11-13)

Read first: `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-00-vision-and-plan-files.md`, then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`: sections 2.3, 3.5, 4 and 8. Then phase 01's cost rose and frame logs (this phase's controller is tested on them), and phase 02's $\hat{T}$ and late flag.

## Goal

When the card cannot finish a picture inside the slot, which happens while turning through the dear directions on a capped card, the game draws fewer pixels for a moment instead of losing whole frames. A still camera always draws at full size, and its picture is exactly today's. When this phase is done:

- **The picture can be drawn at a smaller internal size,** one of four rungs per axis: 100, 85, 70 and 50 %. It is upscaled on the card into the full-size picture that goes home.
- **A controller picks the rung each picture,** from phase 02's $\hat{T}$, the late flag and the turn speed, with hysteresis.
  - It steps down within 2 pictures of a late one.
  - It goes back to 100 % within 15 still pictures (250 ms at 60).
  - It never goes below the floor.
- **AO's and TAA's history work across size changes.** Each is indexed by the previous camera's width and height, and `post_reproject` maps between the two.
- **The CPU's nearest-pixel upscale goes** (`game_view_bring_up`, `run/src/game/view/index.c:71-90`). The static render scale becomes the ladder's top rung.
- **Two settings:** `dynamic_resolution` (on) and `dynamic_resolution_floor_percent` (50), on the Settings screen. F3 shows the rung.
- **On `spot/shake-720-live-capped-r64`, the picture interval p99 is ≤ 16.7 ms and the worst ≤ 25 ms** (master section 4), on the capped card.
- **Golden pictures and every settled still row's hash are unchanged.**

## Where this fits

**The vision.** Master 2.3. Phases 02 to 04 take away the waiting, the idle card and the work that turning adds. What is left is the plain fact that some directions cost more than the slot on a card held at 50 W (master C5, phase 01's cost rose). Lowering resolution only while moving is what console and PC engines do. Motion hides it, and Ahmed's still picture is untouched.

**What reads it:**

| Reader | What |
|---|---|
| Phase 06 | The far field's turn cache is only needed if this phase's floor is hit often at render distance 64 |
| Phase 07 | Rungs used during the shake rows; intervals |
| Ahmed | Whether he sees the softness while turning (Q1) |

**What this phase must NOT build:**

- No temporal upsampling (TAA accumulating at output size from jittered smaller frames). That is better and harder: Q9, a later plan.
- No resolution change while the camera is still. A late picture with a still camera is answered by phase 02's rule alone.
- No change to the HUD's resolution. It is drawn by main at output size, as today.

## Ahmed's words

- "we have to be smarter about what we are doing to fix this please!"

---

## Background, explained

### Why the blocks are already big enough

The kernels take `traverse_camera_t.width` and `height` as the picture's size, and index every per-pixel block by them. The blocks are sized for the full picture once (`render_frame_arena_bytes`, `run/src/render/frame/index.c:142-147`, and the targets in `render_frame_blocks_t`). A camera of 1344 × 756 (70 %) uses the first 1344 × 756 entries of each block and leaves the rest.

So a smaller picture needs no new memory. What must change is everything that assumes this frame's size is last frame's size.

### What assumes the size does not change

- **TAA's history read** indexes `history_last` by this frame's `camera.width` and `height` (`post_history_at`, `cuda/src/post/index.cu:89-120`, called at `:157`). It must index by the previous camera's.
- **AO's history read** indexes `ao->last` by `camera->width` (`shade_ao_at`, `cuda/src/shade/index.cu:232-240`). The same.
- **`post_reproject`** maps a direction into the previous camera's pixels, through `temporal.previous`, which holds that camera's own width, height and vectors. It already works across sizes, since `right` and `up` carry the aspect and the field of view. Its test is added here anyway.
- **The readback** copies `post_xrgb_bytes(camera->width, camera->height)` (`cuda/src/frame/index.cu:155`). It must copy the output size, after the upscale.
- **Main** assumes the picture is the display's size (`render_frame_present_full`, `run/src/render/frame/index.c:777-780`). It stays so, as the upscale makes it so.
- **Entity overlays and the outline** draw into `xrgb` by pixel (`entity_rays_overlay`). They move after the upscale, at output size, so the outline stays one pixel wide and sharp.

### The upscale

A last kernel, `post_upscale_kernel`, reads the tone-mapped internal picture, bilinear in sRGB bytes. That is simple and cheap, and the TAA history stays in linear light at the internal size. It writes the full-size XRGB picture.

At 100 % it is skipped and post writes the full-size picture directly, as today. So a still picture is byte for byte the same.

Its cost is one read of 4 texels and one write per output pixel, about 0.1 ms at 1080p on a full card, so about 0.3 ms capped. Measured.

### The controller

A pure function of:

- the rung now;
- the last 16 pictures' $P + G$ and the rung each was drawn at;
- this picture's turn speed at the centre;
- the late flag;
- the floor and the switch.

It returns the next rung.

1. **Cost per pixel.** Each picture's $G$ is scaled to the full size by its pixel share. So $\hat{G}_{100}$ is the 90th percentile of $G / \text{share}$. The prediction at any rung is $P + \hat{G}_{100} \cdot \text{share}$. This assumes the card's time is mostly per pixel, and phase 01's per-kernel numbers check that. The coarse pass is per tile, so it scales too.
2. **Step down one rung** if the last picture was late, or if turning faster than 180°/s with the prediction at this rung over 85 % of the slot (14.2 ms at 60).
3. **Step up one rung** if the prediction at the next rung up has been under 85 % of the slot for the last 8 pictures.
4. **Go straight to 100 %** after 15 pictures with turn speed under 2 pixels a picture at the centre (phase 04's "still" line).
5. **Never below the floor; never above the static render scale; never change if `dynamic_resolution` is off.**

**Why 85 %:** it leaves room for the 90th percentile's misses and for phase 02's 1 ms margin. **Why 180°/s:** below it, a turn is slow enough to see detail on, and a late picture alone should decide. Both are this file's choices, tuned from the frame logs at the gate.

### Worked example

On the capped card, $P = 1$ ms and $\hat{G}_{100} = 18$ ms looking over the valley: 19 ms, over the slot.

- **Turning at 720°/s:** step down to 85 % (share 0.72): $1 + 18 \cdot 0.72 = 14.0$, under 14.2.
- **Still facing the valley:** after 15 pictures the controller returns to 100 %. Phase 02's rule then gives about 19 ms intervals, 52 a second, with the full picture, as Ahmed is looking at it.

That is the trade master 2.3 describes.

---

## Design

### `run/src/render/frame/` and `cuda/src/post/`

- **`render_frame_t` gains `rung` and `output_width` and `output_height`.** `width` and `height` become this picture's internal size, set before each draw.
- **`render_camera_to_traverse`** takes the internal size; the field of view and aspect stay the output's.
- **`post_temporal_t.previous`** already holds the previous camera. TAA and AO index their last history by its `width` and `height`.
- **`post_upscale_kernel`** and `post_upscale` in `cuda/src/post/`, run after `post_frame_temporal` when the rung is not 100 %. The outline and entity overlays run after it, at output size, with their pixel rectangles from the output-size camera.
- **The readback** copies the output size.
- **Jitter** stays a fraction of the internal pixel, so TAA's accumulation is as today at each rung.

### `run/src/game/pace/` (phase 02's module)

`game_pace_rung` is the controller, pure, beside `game_pace_decide`, with the samples it needs in `game_pace_t`.

### Settings (`run/src/settings/`, `run/src/ui/screen_settings/`, `run/src/game/session/`)

- `dynamic_resolution`: true or false, default true.
- `dynamic_resolution_floor_percent`: 50, 70, 85 or 100, default 50.

Both go in `settings.yaml` with the existing keys' rules (a wrong line is told about and ignored), and on the Settings screen beside "Render scale".

`render_scale_percent` stays and is the top rung. At 100 % and dynamic off, nothing here runs.

### F3

`res 70% (dynamic)` on the "pictures" line when the rung is not 100 %.

### `game_view_bring_up`

`game_view_bring_up` goes, and `run/src/game/view/README.md` says why. The static render scale now upscales on the card as well.

---

## Owned paths

This phase may edit these, plus this file:

- `run/src/render/frame/`, `run/include/render/frame/`
- `cuda/src/post/`, `run/include/gpu_bridge/post/`, `cuda/src/shade/` (AO's history index only), `cuda/src/entity_rays/` (overlay at output size only), `cuda/src/frame/` (the readback's size, the upscale's place)
- `run/src/game/pace/`, `run/src/game/view/`, `run/src/game/index.c` (setting the rung before each draw)
- `run/src/settings/`, `run/src/ui/screen_settings/`, `run/src/game/session/`, the F3 composer
- `tests/run/game/pace/`, `tests/run/render/frame/`, the `render_feature_post` and `render_feature_entities` suites, `tests/run/settings/`
- `bench/world/` (rung columns)

## Steps

1. **History indexed by the previous camera's size,** in TAA and AO, with a test that changes size between two frames. *Why: everything else stands on this.*
2. **The upscale kernel; overlays after it; readback at output size.** The static render scale through it; `game_view_bring_up` gone. *Why: a smaller picture must reach the screen at full size, and the static scale tests it at once.*
3. **`game_pace_rung` with its tests,** fed phase 01's frame logs. *Why: the controller decides what Ahmed sees.*
4. **The rung set before each draw; F3; settings.** *Why: the feature.*
5. **Phase 01's shake rows, live-capped, at render distance 32 and 64, with dynamic resolution on and off.** *Why: the targets.*
6. **Shots of a turn at 70 % and 50 %** beside 100 %; describe them below. *Why: how soft it looks.*
7. **READMEs** (`run/src/render/frame/README.md`, `cuda/src/post/README.md`, `run/src/game/pace/README.md`, `run/src/game/view/README.md`); tick todos as they land.

## Todo

- [x] TAA's and AO's history read indexed by the previous camera's width and height
- [x] `post_upscale_kernel`; outline and entity overlays after it at output size; readback at output size
- [x] The static render scale through the card's upscale; `game_view_bring_up` removed
- [x] `game_pace_rung`: the ladder, the four rules, the floor; tests fed phase 01's frame logs
- [x] The rung set before each draw; F3's `res` field
- [x] Settings `dynamic_resolution` and `dynamic_resolution_floor_percent`, file and screen
- [ ] Shake rows with dynamic on and off; `spot/shake-720-live-capped-r64` p99 ≤ 16.7 ms and worst ≤ 25 ms, or why not, below
- [ ] Shots at each rung looked at and described below
- [ ] Golden pictures and every settled still row's hash unchanged
- [ ] READMEs

## Results (filled in by this phase)

| Row | Interval p50 / p99 / worst, dynamic off | Dynamic on | Rungs used (share of pictures at 100 / 85 / 70 / 50) | Back to 100 % after stopping, ms |
|---|---|---|---|---|
| `spot/shake-720-live-capped` | | | | |
| `spot/shake-720-live-capped-r64` | | | | |
| `spot/shake-1440-live-capped-r64` | | | | |
| Ahmed's frame log | | | | |

## Tests

### `run_game_pace` (extended)

- **`a_late_picture_steps_down_within_two`.** A late flag on picture 0: rung 85 by picture 1 or 2.
- **`a_still_camera_returns_to_full_in_fifteen`.** From rung 50, 15 still pictures: rung 100 on the 15th, not before the cost allows unless still.
- **`a_still_camera_never_steps_down`.** Late pictures with turn speed 0: the rung stays 100.
- **`the_floor_holds`.** Floor 70: never 50.
- **`off_is_off`.** Dynamic off: always the top rung.
- **`phase_01s_shake_logs_stay_under_the_slot`.** Replaying the recorded logs' costs through the controller with the cost model: predicted intervals p99 ≤ 16.7 ms.

### `render_feature_post` and `run_render_frame` (extended)

- **`history_survives_a_size_change`.** A still scene: 30 frames at 100 %, 1 at 50 %, 30 at 100 %. The last frame differs from a run that stayed at 100 % by no more than 2 in 255 on any channel: no ghost, no smear.
- **`the_upscale_of_a_flat_colour_is_that_colour`.** A sky-only picture at 70 % upscales to every output pixel the same as at 100 %, within 1 in 255.
- **`the_outline_is_one_pixel_at_any_rung`.** An outlined block at 50 %: the outline's width in the output picture is one pixel, as at 100 %.
- **`render_scale_seventy_matches_its_old_picture_in_size`.** The static setting at 70 % gives a full-size picture whose content differs from the old nearest-pixel picture only by the upscale's filtering.
- **Golden pictures** unchanged (100 %, still).

### `run_settings` (extended, `tests/run/settings/`)

- **The two keys** read and write, wrong values are told about and ignored, and an old file without them reads with the defaults.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                         # must print nothing before the bench runs
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_game_pace|run_settings.*)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_.*|run_render_frame)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_p05.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --record bench/world/history.tsv --label "008 phase 05: dynamic resolution" --shots /tmp/mc_c_shake/p05; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --only shake --render-distance 64
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"; pgrep -af gpu_watch                       # both must print nothing

## Gate

Ahmed plays at render distance 64 with dynamic resolution on (the default), shakes his head hard at his spot, then stops and looks. He says:

1. Does it feel smooth while shaking?
2. Does he see the picture go soft while turning? Does it come back sharp at once when he stops?
3. Q1: keep it on? Which floor?

He records a frame log; its summary goes in the results.

## Risks and open questions

| Risk or question | Default |
|---|---|
| The softness is visible and bothers him | The floor and the switch are settings; Q1; still pictures are untouched |
| The controller hunts between rungs, and the picture pulses | Hysteresis (8 pictures to step up); stepping up is only to the next rung; the hunting test on recorded logs |
| Per-pixel cost is not linear in pixels (the coarse pass, far field per pixel) | Phase 01's per-kernel numbers check it; the cost model is fitted per rung from logs if needed |
| History across a size change ghosts | Reprojection through both cameras; the size-change test |
| Overlays at output size disagree with the internal picture's edges by half a pixel | The outline is drawn by its own rays from the output camera, as today; the test at 50 % |
| Removing `game_view_bring_up` changes static render scale pictures | They are filtered now, not nearest; said in the README; Ahmed sees it only if he uses a scale under 100 |
| The upscale costs more than 0.3 ms capped | Measured; it runs only below 100 % |

## Commits

Appended as they land:

## Issues found

None.
