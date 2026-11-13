# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 5: Adaptive resolution while turning or late

**Status:** done, for Ahmed's gate: dynamic resolution on by default at floor 50; at his spot on the card at its 148 to 182 MHz floor the r64 shake goes from 28 to 75 pictures a second, p99 50 to 25 ms; the targets at 600 MHz are met in the replay, not measurable on this card today; still pictures the same to the bit   (updated 2026-11-13)

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
- [x] Shake rows with dynamic on and off; `spot/shake-720-live-capped-r64` p99 ≤ 16.7 ms and worst ≤ 25 ms, or why not, below
- [x] Shots at each rung looked at and described below
- [x] Golden pictures and every settled still row's hash unchanged
- [x] READMEs

## Results (filled in by this phase)

| Row | Interval p50 / p99 / worst, dynamic off | Dynamic on | Rungs used (share of pictures at 100 / 85 / 70 / 50) | Back to 100 % after stopping, ms |
|---|---|---|---|---|
| `spot/shake-720-live-capped` | 33.33 / 66.70 / 83.36 (29.5 a second; 175 MHz, 148 to 468) | 16.64 / 33.28 / 41.63 (67.1 a second; 512 MHz, 148 to 1006) | 17 / 20 / 15 / 48 | 257 |
| `spot/shake-720-live-capped-r64` | 33.33 / 50.01 / 58.33 (28.3 a second; 177 MHz) | 8.37 / 25.03 / 33.34 (74.6 a second; 182 MHz) | 0 / 0 / 0 / 99 | 347 |
| `spot/shake-1440-live-capped-r64` | 33.33 / 50.03 / 50.05 (28.9 a second; 175 MHz) | 8.38 / 25.00 / 25.03 (83.8 a second; 169 MHz) | 0 / 0 / 1 / 99 | 184 |
| Ahmed's frame log | (his gate) | | | |

All at Ahmed's spot (`bench_world_shake --at -80 134 418 --seed 14148492376765118422`), 1920x1080, profile build, his game closed (`pgrep` before every run), `gpu_watch.sh` beside each, recorded in `bench/world/shake/history.tsv` with the labels "008 phase 05: dynamic floor 0", "... 50" and "... 50, no still tail". "Off" is `--dynamic-floor 0` (the build's code with the ladder of one rung: every picture at 100 %, as before this phase), with a 2 s still tail; "on" is `--dynamic-floor 50`, 10 s of shaking with no still tail; the rungs and the way back are from the on run with the 2 s still tail (`--still-tail 2`, 12 s). **The card was at its thermal floor, 148 to 182 MHz by the clock64 kernel, in every r64 row** (the r32 on row's mean of 512 MHz is a few readings at 1006 while the card cooled between runs; its card ms are not comparable to the off row's). Camera to screen p50: 36.8 / 38.9 / 38.7 ms off, 11.0 / 16.5 / 17.1 ms on.

**Against the targets.** At 148 to 182 MHz the r64 shake has p99 25.0 ms and worst 33.3 ms on, against 50.0 and 58.3 off: twice the pictures a second (28 to 75 at 720, 29 to 84 at 1440), camera to screen less than half. The master's targets (p99 16.7 ms, worst 25 ms) are on the capped card at about 600 MHz; this card spent the whole run at a quarter of that. At 50 % the shake's pictures cost 13.2 ms of card at 170 to 182 MHz (2.3 Mcycles: 3.8 ms at 600 MHz), so at 600 MHz they would be far inside the slot, with room to climb to 70 or 85 %. At 148 MHz even 50 % is near the slot, so the floor holds 99 % of the r64 pictures and the 1 % past 25 ms are the first pictures of the run (at 100 % before the controller had samples) and phase 03's flight pictures. Phase 01's shake rebuilt at 600 MHz and replayed through the controller (`run_game_pace`) gives p99 16.0 ms, worst 16.3, with the rungs spread over 85, 70 and 50.

**Settled rows, the cost per rung** (spot `shake-0`, still, settled, `--scale`): 100 % 31.78 ms at 149 MHz (4.74 Mcycles); 70 % 18.11 ms at 147 MHz (2.66 Mcycles, 56 %, for 49 % of the pixels); 50 % 4.53 ms at 470 MHz (2.13 Mcycles, 45 %, for 25 %). Each pass falls with the pixels but the readback, 1.27 ms, which is the output's size whatever the rung, and the coarse pass's per-tile and the far field's fixed parts: the cost model's "mostly per pixel" holds to 70 % and over-promises at 50 % (it predicts 25 %). The controller fits its G-hat from what each rung really cost, so it errs safe, but it means 50 % buys less than the ladder says. **The upscale** (post at 70 % 2.68 ms against 4.20 x 0.49 = 2.06 at 100 %'s share): about 0.6 ms at 147 MHz, 0.09 Mcycles, 0.15 ms at 600 MHz; run only under 100 %.

**The still picture.** `spot/shake-0`'s settled hash is `BA28CEC20AD658FA` with dynamic resolution off and on, as in every phase 04 row: unchanged. Golden pictures pass (`run_render_frame`, `render_feature_*`).

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

- `cdb81467` Draw a picture smaller while turning or late and bring it up on the card, plan 008 phase 05: TAA and AO read the last frame's history at its own size, post_upscale takes the drawn picture to the output and the outline is drawn after it at the output's size, the render scale is the ladder's top rung in place of the CPU's nearest pixel, game_pace_rung picks each picture's rung from the cost model, the turn and lateness, and dynamic_resolution with its floor is a setting, on the Settings screen and in F3.
- `93a4939c` Measure plan 008 phase 05 at Ahmed's spot at 148 to 182 MHz: the shake rows, the shots, the measurements, and the release build's fix for the ladder's LTO warning.
- this commit: after rebasing onto `4c147bd9` (plan 009 phase 04, continuous LOD, which took Full detail out of the settings and the screen): `game_view_t` keeps plan 009's `block_radius` beside this phase's `scale_percent` and `floor_percent`, and its bands are worked out for the rows drawn at the render scale (`post_scaled_side`), as they were with the old drawn size; the dynamic button sits on the render scale's row, now row 3 (its middle at (456, 212) at 640 by 480); the settings goldens made again on the five-slider screen and looked at. Rebased, `run_game_pace`, `run_settings`, `run_settings_session`, `run_game`, `run_render_frame`, `render_feature_.*` (14, `render_feature_lod_bands` among them), `run_ui_hud`, `run_ui_hud_corner`, `run_ui_screens`, `standards_lint` and `clang_format` all passed.

**Verification, as run (2026-11-13, on `db999feb`):** `pgrep` for his game printed nothing before every GPU run. `tools/test-or-build.sh cmake --build --preset debug`: clean. `run_game_pace`, `run_settings`, `run_settings_session`: passed. `render_feature_.*` (13, `render_feature_resolution` among them) and `run_render_frame` (goldens unchanged): passed. `run_game`, `run_ui_hud`, `run_ui_hud_corner`, `run_ui_screens` (settings goldens regenerated, above), `standards_lint`, `clang_format`: passed. `--preset profile`: `bench_world_shake` alone (disk, above); the bench lines ran as `bench_world_shake` at the spot with the sidecar (the shake rows have been its own program since phase 01), off and on, recorded in `bench/world/shake/history.tsv`. `--preset release`: `minecraft_c` built clean, the 16 KiB frame limit held (the whole release build's test binaries were not linked, for the disk). `pgrep -af "$PWD/build"` and `pgrep -af gpu_watch` printed nothing after.

## Issues found

**The shots** (`bench_world_shake --only =shake-720 --scale 100|70|50 --shots`, the settled 720 degree a second shake's last picture at the spot, each upscaled to 1920x1080 by `post_upscale`; in `/tmp/mc_c_shake/p05_scale_*/spot-shake-720.png`, not kept). At 70 % the block faces read as before and each texel of the dirt and snow is still a square, a little soft at its edges; the silhouettes of the snow steps are smooth where 100 % has hard stairs. At 50 % the texels are visibly blurred (two output pixels a drawn pixel, bilinear), the stone floor further off smears into grey and the leaves' fine edge at the top right goes to a haze; nothing ghosts, nothing doubles, and the HUD (drawn by main at output size) and the outline stay sharp. At 720 degrees a second the turn hides most of it; still, the picture is back at 100 % within 15 pictures (184 to 347 ms measured, at 25 to 30 pictures a second on this card; 250 ms at 60).

**What changed from the plan, and why:**

- **The drawn picture goes into the hits block, not a new block.** Post writes the drawn picture into `frame_targets_t.borrowed_hits` (16 bytes a pixel of the output, and nothing reads it once shade is done), and `post_upscale` takes it to the frame's device picture. A block of its own would have cost 6 MiB of the frames' 256 MiB budget (already at about 245) and a pointer in `render_t`, which has no room (`app_t` is at main's frame limit). Plan 009 phase 07 (temporal upscaling at the output's size) has no need of it.
- **`render_frame_t` gained one byte, `scale_percent`, not `rung`, `output_width` and `output_height`.** `width` and `height` stay the output's (what the blocks were sized for, what goes home); the drawn size is worked out each frame with `post_scaled_side`, the static render scale's old arithmetic, so a picture at a render scale is the very picture it was, to the byte (`render_scale_seventy_matches_its_old_picture_in_size`). `render_frame_t` had 3 bytes spare.
- **The upscale is timed under the overlay kernel's mark** (`FRAME_KERNEL_OVERLAY`, its `ran` bit set): `frame_timings_t` is 64 bytes inside `render_frame_t` and has no room for a tenth kernel. Post's own time is then as at the full size, of fewer pixels.
- **Slot share 0.80, not 0.85** (`compile/config/render/resolution/index.yaml`, where every number of the ladder and the controller is). Phase 01's shake rebuilt at 600 MHz and replayed through the controller gave p99 17.4 ms at 0.85: the cost changes with direction faster than 16 pictures' 90th percentile follows, and a rung stepped up on the cheap side of the shake is late on the dear one. 0.80 gives 16.0. The worked example then reads P 1 + G 16 ms: 17 at 100 %, 12.6 at 85 %, under 13.3.
- **The late rule counts a late picture only if it was drawn at this rung (or below).** With phase 03's two frames in flight, a picture queued at 100 % lands late after the rung has already stepped to 85; counting it again would step twice for one miss.
- **"Phase 01's frame logs" do not exist at 600 MHz**: phase 01 recorded rows at 148 MHz and no per-picture log of the capped card. So `phase_01s_shake_stays_under_the_slot` rebuilds the shake from phase 01's cost rose at r64 (9 to 24 ms at 100 % across the +-60 degrees, at 600 MHz) and replays it through the controller and the cost model. Ahmed's own frame log (his gate) is the real one; the release build's `--frame-log` now has a `rung` column.
- **The history test is TAA's; AO has its own.** With AO on, a pixel that takes its history across a change of size takes a neighbour's running mean, and so that neighbour's sample noise, which is more than 2 in 255; that is not a ghost. So `history_survives_a_size_change` draws with every effect but AO (0 pixels differ after 30 frames back at 100 %), and `ao_history_survives_a_size_change` checks that at least 97 % of the surface pixels keep their AO samples across 100 to 50 to 100 % (98.5 and 97.5 % measured; indexed by this frame's width they would land on other surfaces and start again).
- **The tests are a suite of their own, `render_feature_resolution` (`tests/cuda/resolution/`)**, as `tests/cuda/post/index.c` (1435 lines) and `tests/run/render/frame/index.c` (1864) are near the standard's split.
- **F3 shows "res 70% dyn" on the `turn` line**, not a "pictures" line (there is none); "dyn" when the rung is under the render scale. `ui_hud_debug_t.skipped` became 16 bits to make room for the two bytes (it counts a second's skipped puts).
- **The settings are two bytes**, in `settings_values_t`'s last padding and the 4 bytes `settings_t` had after it; `game_config_t.render_scale_percent` became a byte (it is at most 100) and `dynamic_floor_percent` (0 for off) took the other. On the Settings screen the render scale's slider is 132 wide, and beside it a button, "Dyn 50%", steps the floor up (70, 85, 100), then "Dyn off", then 50 again; the settings page goldens (`tests/run/ui/screens/fixtures/settings_main.png`, `settings_explain.png`) were regenerated with `MC_GOLDEN_WRITE=1` and looked at: the button sits at the right of row 4, the rest as before.
- **`bench_world_shake`** gained `--dynamic-floor`, `--scale`, `--still-tail`, a row `shake-1440-live-capped-r64` (this file's table), and rung and back-to-the-top columns at the end of `history.tsv` (its header, written by the first run, names none of the columns added since; `card_mcycles` was the first). Its rows default to dynamic resolution off, so they compare with the history before this phase; the game defaults to on.
- **`game_world_parts_t` grew 128 bytes** (`game_pace_t` 256 to 384: each sample's card time and rung, the ladder, the counts), in the World arena.
- **The settled `shake-720` hash moves from run to run** (`FAD239496F15C083`, `E298452B217D3A38`, `E989FE4F8A0893C6` for the same build at 100 %): the settled turning rows were never one picture per build (phase 04's rows differ by label too); still rows are, and `shake-0` is unchanged.

**Disk:** `/home` was at 99 % (2.9 GiB free) during this phase, from the worktrees; the profile build's test binaries were deleted after a link ran out of space, and the profile build was then made for `bench_world_shake` alone.
