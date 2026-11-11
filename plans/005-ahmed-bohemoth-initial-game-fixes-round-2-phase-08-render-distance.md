# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 8: How far you can see

**Status:** in progress: labels, explanations, the mountain check and their tests done. Far rays past see-through faces are now sent only where shade will look, with the same pictures: 2 to 3 times cheaper on leaves and water. **The default still stays 32:** a far field still costs 17 to 31% of the frame rate against 32 on the bench rows, well past the 10% allowed (Issues found, "Far rays only where shade will look")   (updated 2026-11-11)

## Goal

Settings says plainly how far you'll see, in blocks as well as chunks, and the default lets you see well into the distance at a good frame rate.

**Ahmed's words:** "Not sure what a chunk size is, how many blocks that is, but I am not able to see really really far away, not sure if 32 chunks is not actually 32, or a chunk is a lot smaller than I think, or if there is a rendering issue??"

## What it is today

**It's working as set. There's no evidence of a rendering bug.**

- **A chunk is 16 blocks wide,** the same as Minecraft. Render distance 32 means **512 blocks** in each direction.
- Ahmed's `settings.yaml` has `render_distance: 32` and `full_detail: 32`. The far field (level of detail) only draws beyond the full-detail distance, so with both at 32, **nothing is drawn past 512 blocks.**
- The render distance slider goes to **512 chunks, which is 8192 blocks**. Past the full-detail distance, terrain is drawn coarser. Measured in plan 001: about 150 FPS at render distance 128, and about 107 at 512 (`bench_world`).
- The slider shows only "Render distance: 32 chunks", which doesn't say how far that is.

## Todo

- [x] Show blocks on both sliders in `run/src/ui/screen_settings/index.c`: "Render distance: 32 chunks (512 blocks)". Shipped as "Render distance: 32 (512 blocks)": see Issues found
- [x] Explain the two settings in one line each on the Settings screen: full detail, and how far to draw at all. A tooltip by the pointer over each slider
- [ ] **Only after phase 14 is fixed,** raise the default render distance, keeping full detail at 32, to the highest that still holds the frame target on `bench_world`. Measure, and record the numbers here. **Measured 2026-11-11, not raised:** no distance above 32 holds it. 48 costs as much as 128, and 2 to 4 times the frame on leaves and water. The live test's opening targets hold at 64 and 128. See "The raised default, measured". Waits for the todo below
- [ ] **New, 2026-11-11:** make the far field's rays past see-through faces cheap in `cuda/src/lod/`. Today every pixel that shows water, leaves or grass sends a far-field ray from the eye, and that ray steps through every near column. Turning those rays off made `jungle/orbit` at 48 cost what it costs at 32. Then measure again and raise the default. **Done in part, 2026-11-11:** a far ray is now sent only where shade's own ray leaves full detail, with every golden and bench picture the same bytes, and a test for far hills through glass and water. Its cost fell from 20.6 to 6 to 7 ms on `jungle/orbit` and from 27.9 to 9 to 13 ms on `deep_ocean/orbit`, at 48. Not cheap enough to raise the default: what is left is the far field's own rays for what hits nothing, and the walk through water. See "Far rays only where shade will look"
- [x] Check the far field looks right from a mountain, with screenshots checked by eye. Taller terrain from plan 003 phase 8 may need it tuned. Done at 128 and 32, as the default did not move: nothing to tune. See "From a mountain"
- [x] Add a test that the label shows blocks, and that a `settings.yaml` without the new default still loads as before. The default is not raised yet, so the second half pins that a file without the distances gets today's defaults and Ahmed's 32 and 32 loads as written; when the default is raised, that test needs no change

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^(run_ui_screens|run_settings|render_feature_lod|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 900 build/release/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

Ahmed: in Settings, set render distance to 128 or more, keep full detail at 32, and look out from a mountain. Then try the new default.

## Commits

Appended as they land:

- 66a5eb90 Show render distance and full detail in blocks, and explain each in a tooltip.
- 3979d1c0 Measure a raised default render distance and keep it at 32: the far field's rays past see-through faces cost too much.
- 110cda4c Send a far ray past a see-through face only where shade's own ray leaves full detail, with the same pictures.

## Issues found

**The label drops the word "chunks", 2026-11-10.** "Render distance: 32 chunks (512 blocks)" does not fit the 200-pixel slider: measured with the game's font at scale 1 it is 204 pixels at value 2 and wider from there. The shipped label is "Render distance: 32 (512 blocks)" and "Full detail: 32 (512 blocks)"; the widest at any value, "Render distance: 512 (8192 blocks)", is 189 of 200. Full detail past what the open world holds still reads "Full detail: 40 (new world)". What the first number counts is said by the tooltip.

**The one-line explanations are tooltips.** The Settings screen has no free row (nine rows fill its 240 pixels), so each line shows by the pointer while it is over that slider: "How far you see, in chunks of 16 blocks." (216 pixels) and "Chunks drawn block by block; coarser past." (232 pixels), both inside the 320-pixel screen. They are `UI_SCREEN_SETTINGS_EXPLAIN_*` in `run/include/ui/screen_settings/index.h`.

**Golden pictures.** `tests/run/ui/screens/fixtures/settings_main.png` changed for the new labels, and the new `settings_explain.png` is the main page with the pointer over render distance and its tooltip. Both looked at by eye. `settings_hud.png` did not change.

**Ahmed can see further today,** with no code change, by raising the render distance in Settings.

**Raising render distance is what broke the game, 2026-11-09.** Ahmed raised it to 64, as this file suggested. That turned on the far field, whose builds then took every worker, so near chunks and mined blocks waited for minutes. See phase 14, "Identified". Until phase 14 is fixed, **render distance 32 with full detail 32 is the setting that plays properly**, and this phase's raised default waits for it.


### The raised default, measured, 2026-11-11

**Verification, 2026-11-11, release:** `run_ui_screens`, `run_settings`, `render_feature_lod`, `run_render_distance_live`, `standards_lint` and `clang_format` pass. `bench_world` exited 0 in 235.5 s, with 2 of 16 rows at 32 meeting 120 fps (`plains/orbit` 73.6 fps) on today's slow card, the same as at the start of this work (67.5 and 73.7 fps). `pgrep -a minecraft_c` printed nothing.

**The default stays 32, with full detail 32.** No distance above 32 holds the frame target, and phase 14 is not what stands in the way any more: the world fills in fast at any of them. What costs is drawing the far field every frame. Ahmed's `settings.yaml` keeps what it says either way.

**How it was measured.** `bench_world` has a new flag, `--render-distance <chunks>`, which moves the rows held to the frame target (32 until now) to another distance and leaves full detail at 32. `run_render_distance_live` has a new variable, `MC_DISTANCE_LIVE_OPEN=<chunks>`, which opens its world at another distance. Release preset, through the wrapper, 1920 by 1080. Logs are in `scratchpad/p8/` (`bench_*.log`, `one.log`, `live_*_out.log`).

**The card is slow today, and it gets slower within a run.** At 32, `plains/orbit` made 67 to 75 fps (150.7 in plan 001 stage 17.3), and only 2 of 16 rows met 120 fps. Its `shade` pass took 7.8 ms, against 3.4 ms then. Whole runs at a raised distance were worse again. Their heavy rows slowed the card for the rest of the run: `post`, which the far field does not touch, went from 2.0 to 7.8 ms. The 512 row, which the flag does not move, went from 38 to 14 fps. Those whole runs overstate the cost by 2 to 4 times. The same row run twice alone came within 4% (`plains/orbit` at 32: 75.3 and 72.4 fps; at 64: 52.8 and 55.2). So the table is each row in a process of its own, one after another, and it reads the cost against 32 rather than against 120.

| Row, alone | 32 | 48 | 64 | 128 |
|---|---|---|---|---|
| `plains/orbit` fps (primary pass, ms) | 75.3, 72.4 (1.45, 1.52) | 56.8 (5.61) | 52.8, 55.2 (6.36, 6.05) | 49.2 (7.19) |
| `jungle/orbit` | 51.7 (1.66) | 24.3 (20.56) | 21.8 (23.24) | 21.7 (23.67) |
| `deep_ocean/orbit` | 37.8 (1.72) | 16.9 (27.85) | 6.4 (77.81), the card slowed as above | 15.5 (30.98) |
| `cave/orbit` | 79 in a whole run (1.47) | | 89.7 (1.45) | |

- **Any far field costs about as much as any other.** 48 is one level of voxels of 4, and 128 is three levels. On plains a raise adds 4 to 6 ms a frame, on leaves 19 to 22 ms, and on water 26 to 29 ms. On the card at its best (stage 17.3: `plains/orbit` 6.64 ms, `jungle/orbit` 8.10 ms, `deep_ocean/orbit` 8.54 ms, already over 8.33), even a third of that misses 120 fps on every one of those rows. Underground nothing changes.
- **Why: rays past see-through faces.** A pixel's ray goes to the far field if it hit nothing, or if it hit a face with anything to see through: water, leaves, grass, glass (`lod_ray_goes_on`). Such a ray starts at the eye. Each coarse voxel it meets inside full detail is passed over one 16-block column at a time, up to `LOD_SKIPS_MAX` (32) fresh traversals a level. Almost every one of those rays then ends on near ground, so the far result goes unused. **The experiment:** with far rays for misses only, `jungle/orbit` at 48 made 51.5 fps with a primary pass of 2.82 ms, against 24.3 fps and 20.56 ms, and against 51.7 fps at 32. So nearly all the cost is these rays. That change is wrong as it stands, because a window or water would no longer show far hills. It was reverted and is not committed. The fix is the new todo: trace those rays only when the shade pass's own ray really leaves the world, or start them where they leave full detail.
- **Older numbers agree.** Plan 001 stage 17.3 had `plains/orbit` at 150.7 fps at 32 and `plains/orbit-r512` at 109.6, on the card at its best.

**A new world still fills in fast at a raised distance** (`run_render_distance_live`, report only, its world at 640 by 360, seconds from opening):

| Opens at | Rings out to 4 chunks | All 33 rings | Far field | All | Deadlines (2 s, 15 s) |
|---|---|---|---|---|---|
| 32 | 0.09 | 3.43 | none | 3.43 | met |
| 64 | 0.09 | 5.58 | 7.66 (1618 columns) | 7.66 | met |
| 128 | 0.09 | 5.52 | 9.56 | 9.56 | met |

The headless game at 128 on Ahmed's seed opened in 7.68 s: 2707 far columns, 29 ms each of workers' time. Each run's later steps, at 64 and back, met their deadlines as phase 14 recorded. So once the frame cost is fixed, 64 or 128 needs nothing more from phase 14.

**Not changed, so nothing to regenerate.** `MC_GAME_RENDER_DISTANCE_DEFAULT` (`config/machine.yaml`'s `game.render_distance_default`) is still 32. So are `the_defaults_are_the_plans_and_every_effect_has_its_own_bit`, `the_file_for_the_defaults_is_exactly_this` and the Settings golden pictures. For the change later: those two tests, `settings_main.png` and `settings_explain.png` draw the default, and `bench_world`'s `BENCH_DISTANCE_TARGET` is the plan's 32, not the default.

**`bench_world`'s `jagged_peaks` rows look at haze.** At 32 their last pictures are sky and fog only, at 48 and 64 too. The eye stands high over a peak and looks a little down at ground out past the fog. Their 150 fps measures little; whoever next touches the bench's places should lower that eye.

### From a mountain, 2026-11-11

The default did not move, so this was done at what Ahmed would set by hand: render distance 128 with full detail 32, and 32 to compare. The headless game ran on Ahmed's seed (`--headless --test-world --seed 11107447384583428913`, 1280 by 720). The eye rose 300 blocks from where the world opens, tipped about 9 degrees down, and took a picture each quarter turn, 2 ticks and again 40 ticks after each turn. The pictures were the same, so TAA has no part in them. Each was looked at by eye. Scripts and pictures are in `scratchpad/p8/` (`mountain.script`, `mountain_128_*/`, `mountain2_128_*/`, `mountain_32_*/`).

- **At 32**, from that height, almost everything is fog: the hill under the eye, and sky. That is Ahmed's "not able to see really far away", seen.
- **At 128 the far field looks right.** You can see lakes, coasts, badlands and islands out to the horizon. Full detail hands over to the coarse grids with no seam or gap, and the coarse shores, sea and mountains are the colours of the near ones. **Nothing was tuned.**
- **The tallest peaks fade into fog at the top.** Fog starts at 70% of the render distance: about 1420 blocks at 128. A tall peak's top is further along the ray than its foot, so it melts into the sky first. That is the fog doing its job of hiding the edge of what is drawn. It is not the taller terrain drawn wrong: the coarse columns hold all 1536 blocks of height (`RENDER_LOD_HEIGHT_BLOCKS`).
- **Forests past about 900 blocks are drawn as plain ground.** Phase 14b chose this (`RENDER_LOD_FEATURES_SHIFT_MAX` 2) so the horizon comes in within a second. From 300 blocks up, the far shores are green or sand with no trees, and nearer ones have their forests. It reads as distance, not as a fault. Putting trees back at voxels of 8 costs about 51 ms more a far column: Ahmed's choice.
- **The frame at 128 from up there:** a mean of 8.7 ms at 1280 by 720, against 5.2 ms at 32, on today's slow card.

### Far rays only where shade will look, 2026-11-11

**What changed** (`cuda/src/lod/`, see its README, "Only where shade will look"). A pixel whose first surface is not a miss gets a far ray only if shade's own ray would leave full detail: (1) the texel shade reads there is not opaque, or it is water (`shade_surface_read`, moved with the water walk into `cuda/include/shade/index.hu` so both use the same lines); (2) one `traverse_trace_stopping` from that surface, through every block but the opaque ones and water (a bit table worked out once from the texture table by `frame_lod_stops`, sent at the end of the face table), gets out of full detail; (3) if water stopped it, shade's own ray followed as `shade_kernel` follows it gets out (`lod_goes_out`, the cursor walk, cut at `SHADE_WATER_SEEN_BLOCKS` with tint). 1 and 2 are a small kernel of their own, `lod_past_kernel`. Shade's ray can stop sooner than these say, never later, so a far pixel is what it was.

**Pictures are the same bytes.** Every `bench_world` row's picture digest at 32, 48, 64, 96 and 128 is the one before the change (`plains/orbit` 3D516DE1..., `jungle/orbit` 07CF41FA..., `deep_ocean/orbit` 0B016007..., and so on), and `render_feature_lod`'s five golden pictures are exact, 0 past the tolerance. Nothing was regenerated.

**New test:** `far_hills_show_through_glass_and_water_and_nothing_opaque_behind_them_sends_a_far_ray` (`render_feature_lod`). Over the rolling hills, a pane of glass with water two deep behind it: 15857 far rays, and 15857 pixels show the hills, of 28823 in the open. Then leaves and stone behind them: no far ray at all, and the picture is the bytes of one with no far field.

**The cost, measured** (each row alone, release, 1920 by 1080, logs `scratchpad/p8b/one.log`; card slowing through the run, so read against the 32 run beside it). Primary pass, which holds the far rays, in ms, and fps:

| Row | 32 | 48 before | 48 now | 64 now | 96 now | 128 now |
|---|---|---|---|---|---|---|
| `plains/orbit` primary ms | 1.4 to 1.8 | 5.61 | 3.4 to 3.7 | 4.30 | 4.60 | 5.05 |
| `plains/orbit` fps | 71.0, 76.8 | 56.8 | 59.2, 62.8 | 55.6 | 53.9 | 51.3 |
| `jungle/orbit` primary ms | 1.6 to 2.0 | 20.56 | 5.7 to 7.3 | 7.62 | 7.89 | 8.28 |
| `jungle/orbit` fps | 46.7, 52.5 | 24.3 | 35.8 to 44.0 | 35.2 | 34.8 | 34.0 |
| `deep_ocean/orbit` primary ms | 1.5 to 2.0 | 27.85 | 9.4 to 12.8 | 13.73 | 14.37 | 15.06 |
| `deep_ocean/orbit` fps | 35.3, 42.0 | 16.9 | 24.2, 32.2 | 23.1 | 22.7 | 22.0 |

`cave/orbit` draws the same picture at every distance; its run came last on a card that had slowed by half (32 itself fell from 71.1 to 32.6 fps), so it says nothing here.

**The default stays 32.** Against 32 on the same run, 48 loses 17% on plains, 23% on jungle and 31% on water, where 10% is allowed. Even the experiment's bound, far rays for misses alone (which is wrong), measured today at `jungle/orbit` 48 lost 10% (47.4 against 52.5 fps). What is left: (a) the far rays for pixels that hit nothing, sky and the far ground itself, about 1.2 to 1.6 ms; (b) step 2's trace, about 2.5 ms on `jungle/orbit`, as it goes leaf voxel by leaf voxel through a canopy; (c) on water, step 3's walk for every pixel of sea, 6 to 10 ms, the same walk `shade_water_kernel` takes after it. A next step for (c): run the far field after `shade_water_kernel`, whose `TRAVERSE_INFO_CAPPED` already says which sea pixels shade will never look past; that needs `shade_frame` split around the far field. `MC_GAME_RENDER_DISTANCE_DEFAULT`, `run_settings`'s default tests and the Settings pictures are unchanged, so nothing there was regenerated, and the live test at a new default was not run.

**Tried and dropped:** following shade's ray for every pixel past leaves, in place of step 2 (10.1 ms on `jungle/orbit` 48, as it is shade's own walk through a canopy); working out the stop table per tile in shared memory (no faster than the table in the face table, which is simpler); and a second trace on through water to its bed (20 ms on `deep_ocean/orbit`, as a ray towards the horizon crosses hundreds of water voxels one at a time).

**Layout:** `lod_light_t` gained `borrowed_pixels`, `water_material`, `water_tint` and `stops_after_faces` (48 bytes). `render_frame_face_table_bytes` grew by the stop table. `render_frame_t` did not grow: putting the table there grew `render_t` and `app_t`, and main's frame went past 16384 bytes.
