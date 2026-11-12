# 009-ahmed-bohemoth-game-engine-optimizations — phase 4: Continuous LOD replaces Full detail

**Status:** not started   (updated 2026-11-12)

Read first: the phase 00 file, then the master: sections 0, 2.2 to 2.5, 4.4, 5, 7 and 8. Then:

- `cuda/src/lod/README.md`: the far field's rays, "Where the far field stops and full detail starts", "Only where shade will look";
- `cuda/src/traverse/README.md`: the loop and `resume`;
- `run/src/game/README.md` (lines 82 and 110 to 116), `run/src/ui/screen_settings/README.md` and `run/src/settings/README.md`: what Full detail touches outside the renderer;
- phase 01's Results (the full-detail sweep) and phase 03's bands.

## Goal

"Full detail" is gone, from the screen, the settings file, the command line and the code. Every ray chooses its level by how far along it is, by the pixel-error rule, with level 0 guaranteed within 16 blocks. When this phase is done:

- **Rays walk the pyramid by distance.**
  - A primary ray traces level 0 from the coarse pass's start until its `t` reaches d<sub>1</sub> (dithered per pixel and frame), then level 1 until d<sub>2</sub>, and so on, each from where the last stopped, with `resume`.
  - Shade's own rays past see-through surfaces hand over the same way.
  - The far field's pass-by of full detail's columns (`lod_full_detail_at`, up to `LOD_SKIPS_MAX` traversals per level) is deleted.
- **The near guarantee holds:** every pixel whose surface is within 16 blocks has the same hit, voxel, face and `t` to the bit, as the level-0-everywhere reference, at every E, FOV and render scale.
- **The error bound holds:** past 16 blocks, at most 1 % of pixels on the settled rows differ from the reference by more than E pixels of surface position.
- **What the game holds at level 0 is worked out, not set:** r<sub>0</sub> = d<sub>1</sub> + `block_margin_chunks`, at least `block_radius_min_chunks`, at most the table. The map's view radius follows it.
- **The Settings screen has five sliders.** The words "Full detail" appear nowhere in the program, its READMEs or its help text. An old `settings.yaml` with `full_detail:` loads with one log line.
- **His spot at render distance 64** is measured against phase 01, and Ahmed has chosen E.

## Where this fits

**The vision.** Ahmed:

- ""Full detail" should never ever be a thing!"
- "resolution drops from really close to us!! at all times 16 blocks in all directions **VIEWABLE** from the user must be rendered properly! and further optimization as we move away!!"

This is the phase that does it. The master's section 2.2 is the honest part: how close "close" is depends on E, and Ahmed sets E.

**What reads it:**

| Reader | What |
|---|---|
| Phase 05 | The coarse pass learns to step coarse levels past d<sub>1</sub> |
| Phase 06 | Shadow rays use the same bands and hand-over |
| Phase 07 | A smaller internal picture makes θ larger, so bands move nearer, by the same function |
| Phase 08 | The targets |
| Ahmed | Choosing E by eye |

**What this phase must NOT build:**

- No change to shadow, AO or reflection rays' levels (phase 06). They still trace level 0, which is now r<sub>0</sub> wide instead of full detail.
- No change to the far field's sampling, sun rays or shading of far surfaces.
- No new effect or setting. `render_distance` stays the one distance setting.
- No change to what the sim does inside its radius.

## Ahmed's words

- ""Full detail" should never ever be a thing! if there are is a 2x2x1 group of blocks, they should be rendered only as triangle lines of the larger rectangle that it has created as a result of being one shape!!"
- "this should be for all blocks, even blocks that are close to us!! resolution drops from really close to us!! at all times 16 blocks in all directions **VIEWABLE** from the user must be rendered properly! and further optimization as we move away!!"

---

## Background, explained

### Pixel error, once more, with his numbers

At 1080 rows and FOV 70°, θ = 1.297 × 10⁻³ rad per pixel. A level-k surface can sit ε<sub>k</sub> = s<sub>k</sub> / 2 blocks from the truth. At distance d that is ε<sub>k</sub> / (d θ) pixels. Level k is allowed where that is at most E:

    d_k = max(near_guarantee, s_k / (2 E θ))

| E | d<sub>1</sub> | d<sub>2</sub> | d<sub>3</sub> | r<sub>0</sub> (chunks, + 2 margin) |
|---|---|---|---|---|
| 2 | 386 | 771 | 1542 | 27 |
| 4 | 193 | 386 | 771 | 15 |
| 8 | 96 | 193 | 386 | 9 → 12 (`block_radius_min_chunks`) |

θ is worked out every frame from `half_fov_tangent` and the picture's rows (render scale and plan 008's ladder counted), so everything above moves with FOV and resolution by itself.

### Why a ray can hand over by distance with no pass-by

Today a far ray starts at the eye and walks a coarse level through the near region. Every coarse hit inside full detail must be passed by, because full detail already drew that place (`lod_trace`, `cuda/src/lod/index.cu:106-155`), and that is where the 32 re-traversals per level come from.

With bands, a ray never enters level k before d<sub>k</sub>, so there is nothing to pass by: it walks level 0 to d<sub>1</sub>, then level 1 from d<sub>1</sub>. The existing hand-over between far levels already works this way: `t_from = t_exit − LOD_HANDOVER_BACK`, then `resume`, so a ray takes only what it enters after the hand-over.

### The seam, and why dithering fixes what nesting cannot

Levels nest (phase 03), so at the hand-over the coarse surface is within ε<sub>k+1</sub> of the fine one. But a grazing ray that slipped just above a fine surface can be inside a coarse voxel the moment it hands over, and hit it at once. Across the picture that draws a contour line where d<sub>1</sub> crosses the ground.

Moving d<sub>1</sub> by a hash of the pixel and the frame, within ±`band_dither` of itself (8 % by default: ±31 blocks at 386), spreads that line over a band, and TAA's history averages the band into a gradient. That is the voxel version of Godot's dithered visibility-range fade, and of Unreal's dithered LOD transitions. With TAA off (a test that wants one exact picture), the hash uses the pixel alone, so the picture is still deterministic. The dither never takes d<sub>1</sub> below `near_guarantee`.

### The near guarantee, precisely

The test compares two pictures of the same scene:

- **The pyramid:** this phase's build.
- **The reference:** phase 01's `--lod-reference`, level 0 out to render distance.

**The guarantee is about what a pixel sees:** for every pixel whose reference hit `t` is under 16 blocks, the primary hit (voxel, face, value, `t` to the bit) must be identical. That holds by construction, since both trace level 0 from the same start until 16 blocks at least.

**Its colour must be identical too, against a reference whose level 0 is as wide as the pyramid's.** A near surface's shadow ray reaches as far as level 0 does in both, so it meets the same voxels. Against a level-0-everywhere reference, a far mountain's shadow on a near surface can differ. That is today's behaviour too ("What is near is not shadowed by far hills", `cuda/src/lod/README.md`), and phase 06 improves it.

## Design

### The ray loop: `cuda/include/traverse/index.hu`

- **`traverse_trace_ray_t` gains `t_end`.** The ray stops when the next step would begin at or past it. It then reports a new face value, `TRAVERSE_FACE_PAST`, with `t = t_end`, so the caller hands over. `+∞` means no limit, as today.
- **The struct's size and the asserts change together.** 44 → 48 bytes, with `static_assert(sizeof(traverse_trace_ray_t) == 48)`.
- **No new cost.** The check is one compare per step, against a value already in a register.

### Primary rays: `cuda/src/traverse/`, `cuda/src/lod/`

- **The primary kernel:** sets `t_end = d1(pixel, frame)` from a small `lod_bands_t` passed by value.
- **A hit before `t_end`:** the pixel's hit, as today.
- **`TRAVERSE_FACE_PAST`:** the far field's kernel continues the ray from `t_end` through levels 1..K, each with its own `t_end = d_{k+1}`.
- **`lod_past_kernel` keeps its three questions** (does shade go on past the surface; does anything opaque stand behind it; does shade's ray get through water), with "gets out of full detail" now meaning "reaches `t_end`".
- **`lod_full_detail_at` and the skip loop are deleted.**
- **`lod_view_level_t.shown_*`:** a level's rays are let into its square. The square must reach the band's far edge, which phase 03 sized. A ray past a level's square goes to the next level, as today.

### Shade's rays: `cuda/src/shade/`

- **Rays going on past holes, glass and water** (`shade_kernel`, the water walk) use the same `t_end`. On `TRAVERSE_FACE_PAST` they read the far field's hit for that pixel, as they read `borrowed_lod_hits` today where a ray leaves the world.
- **Sun, AO and reflection rays** keep tracing level 0 with no `t_end` (phase 06 changes them).

### The game: `run/src/game/view/`, `run/src/game/`

- **`game_view_t.full_detail` becomes `block_radius`:** r<sub>0</sub> in chunks, from `render_lod_bands` and the tuning keys `block_margin_chunks` (2), `block_radius_min_chunks` (12) and `block_radius_max_chunks` (48).
- **`game_config_t.full_detail` goes. `table_chunks` stays:** it is sized at world open as the r<sub>0</sub> of the narrowest FOV allowed (`SETTINGS_FOV_MIN`, 30°), capped at `block_radius_max_chunks` (Q4 of the master). So a FOV change never needs a new world.
- **The map follows r<sub>0</sub>:** `world_map_set_view_radius(..., block_radius)` at `run/src/game/index.c:412` and `:856`.
- **The fill report** (`run/src/game/fill/`) counts rings out to r<sub>0</sub>.
- **`render_lod_plan`'s old wrapper from phase 03 is deleted.**

### Settings, screen and command line

- **`settings_values_t.full_detail`** (`run/include/settings/persist/index.h:46`) becomes `_reserved0` with its comment, keeping the 4-byte slot. Offsets and asserts stay, since `app_t` is at main's frame limit and nothing should move. The row in `run/src/settings/persist/index.c:27` goes.
- **Reading an old file:** `full_detail:` in an old file logs `settings: full_detail is no longer a setting; the game works it out (plan 009)` once, and is ignored.
- **The Settings screen** (`run/src/ui/screen_settings/index.c:41`, `:121`, `:135-136`):
  - the Full detail row is removed, and the rows below move up one;
  - the click points in its README are worked out again;
  - `full_detail_live_max` and `UI_SCREEN_SETTINGS_EXPLAIN_FULL_DETAIL` are deleted;
  - the render distance tooltip says: "How far you see. Near blocks are exact; far ones coarser, by distance."
- **The command line:** `--full-detail` (`run/src/main/index.c:60`, `:293`) is deleted, with its help lines. `--no-lod` stays, meaning "the world stops where level 0 does".
- **`RENDER_LOD_FULL_DETAIL_DEFAULT`** is deleted.

### Documents

Every README that says "full detail" says "level 0" or "the blocks" instead:

- `cuda/src/lod/`, `run/src/render/lod/`, `run/src/render/frame/`, `run/src/game/` (lines 49, 82, 110, 112, 116), `run/src/game/fill/`, `run/src/game/session/`;
- `run/src/settings/`, `run/src/settings/persist/`, `run/src/ui/screen_settings/`, `run/src/main/`;
- `bench/world/` (`--full-detail` stays in the bench only as a reference tool, renamed `--block-radius`).

### Tuning (`compile/config/render/lod/index.yaml`)

New keys:

- `band_dither` (0.08);
- `block_margin_chunks` (2);
- `block_radius_min_chunks` (12);
- `block_radius_max_chunks` (48).

`pixel_error` and `near_guarantee` come from phase 03.

## Owned paths

| Path | Change |
|---|---|
| `cuda/include/traverse/index.hu`, `run/include/gpu_bridge/traverse/index.h` | `t_end`, `TRAVERSE_FACE_PAST` |
| `cuda/src/traverse/`, `cuda/src/lod/`, `cuda/src/shade/`, `run/include/gpu_bridge/lod/index.h` | Bands, hand-over, pass-by deleted |
| `run/src/render/lod/`, `run/src/render/frame/`, `run/src/render/` | Bands per frame into the kernels |
| `run/src/game/view/`, `run/src/game/`, `run/src/game/fill/`, `run/src/game/session/`, `run/include/game/` | r<sub>0</sub> |
| `run/src/settings/persist/`, `run/src/ui/screen_settings/`, `run/src/main/` | Full detail removed |
| `compile/config/render/lod/index.yaml` | Keys |
| `tests/cuda/lod/`, `tests/cuda/traverse/`, `tests/run/settings/`, `tests/run/ui/screens/`, `tests/run/render/distance_live/`, `tests/run/game/focus/`, `tests/run/save/state/game/` | Tests, and every existing test that names full detail |
| READMEs listed above; `plans/REPO_STRUCTURE.md` if any row says full detail | Words |

## Steps

1. **`t_end` and `TRAVERSE_FACE_PAST` in the loop**, with `cuda_traverse` proving that `t_end = +∞` changes no hit and that a finite `t_end` stops exactly at the first step past it. *Why:* every hand-over depends on it.
2. **Primary hand-over to the pyramid, with the pass-by deleted, behind a temporary switch** that keeps today's ring, so both can be compared on the same build. *Why:* the near-guarantee and error tests need both pictures.
3. **Shade's continuing rays use the same `t_end`.**
4. **The dither.**
5. **The near-guarantee, error and seam tests;** then delete the switch.
6. **r<sub>0</sub> into the game and the map.**
7. **Remove Full detail from settings, screen, command line and code; fix every test that named it; regenerate the Settings golden pictures after looking at them.**
8. **Measure** at his spot and on every bench row at render distance 32 and 64, at E = 2, 4 and 8 (three builds, one tuning edit each), against phase 01.

## Todo

- [ ] `t_end` and `TRAVERSE_FACE_PAST`; `traverse_trace_ray_t` 48 bytes with its asserts; `cuda_traverse` tests
- [ ] Primary rays hand over at d<sub>1</sub>..d<sub>K</sub>; `lod_full_detail_at` and the skip loop deleted
- [ ] Shade's continuing rays and the water walk hand over the same way
- [ ] Dither per pixel and frame, never under `near_guarantee`; deterministic with TAA off
- [ ] `render_feature_lod`: near guarantee at E = 2 and 8, FOV 70 and 30, render scale 100 and 50, on six places
- [ ] `render_feature_lod`: error share ≤ 1 % at E = 2 on the settled rows' places
- [ ] `render_feature_lod`: the seam on a plain and a 45° slope
- [ ] r<sub>0</sub> drives the map; table sized at open for FOV 30, capped at 48
- [ ] `settings_values_t.full_detail` reserved; old files load with one log line; the defaults file has no `full_detail`
- [ ] Settings screen: five sliders, README click points, golden pictures regenerated after looking
- [ ] `--full-detail` and `RENDER_LOD_FULL_DETAIL_DEFAULT` deleted; `grep -rni "full.detail" run cuda compile bench` finds nothing but this plan's history notes
- [ ] Every existing test that named full detail updated (`run_settings`, `run_settings_session`, `run_game_focus`, `run_save_state_game`, `run_render_distance_live`, `render_feature_lod`)
- [ ] His spot and the bench rows at E = 2 / 4 / 8 measured; Results filled
- [ ] Ahmed has played and chosen E

## Results (filled in by this phase)

| Row | E | Card ms (primary / shade / total) | Primary steps a pixel | Error share | Against phase 01 |
|---|---|---|---|---|---|
| `ahmed_slope/night-64` | 2 | | | | |
| `ahmed_slope/night-64` | 4 | | | | |
| `ahmed_slope/night-64` | 8 | | | | |
| `jagged_peaks/orbit` at 64 | 2 | | | | |
| `jungle/orbit` at 64 | 2 | | | | |
| `deep_ocean/orbit` at 64 | 2 | | | | |
| Rows at 32 | 2 | | | | ≤ 3 % slower |

## Tests

### `tests/cuda/traverse/` — ctest `cuda_traverse`

- `an_endless_t_end_changes_nothing`: every reference scene, bitwise.
- `a_ray_stops_at_its_t_end_and_says_past`: an empty world and a wall at 50; `t_end` 30 gives `TRAVERSE_FACE_PAST` at 30; `t_end` 60 gives the wall.

### `tests/cuda/lod/` — ctest `render_feature_lod`

- **`the_near_guarantee`.** Six places: plains, jungle, jagged peaks, deep ocean, a cave, and Ahmed's slope. Each is drawn twice, the pyramid and the reference, each run for E = 2 and E = 8, FOV 70 and 30, render scale 100 and 50. For every pixel whose reference hit is under 16 blocks, the hit is bitwise identical. Its colour is identical against the same-radius reference. The count of such pixels is printed, and must be over 0 at every place.
- **`the_error_bound`.** Same places, E = 2. A pixel counts as wrong when its hit's distance differs from the reference's by more than E pixels of projected position (|Δt| × the sine of the angle to the surface / (d θ) > E), or its far surface's material differs past the near guarantee. At most 1 % wrong. The share is printed per place.
- **`the_seam`.** A flat plain and a 45° slope crossing d<sub>1</sub> and d<sub>2</sub>, TAA on, 32 frames still. No pixel at a band edge further from the reference than ε<sub>k+1</sub> projected. The final picture's row through the band has no step larger than the colour noise of the rows beside it. The picture goes to Ahmed at the gate.
- **The existing golden pictures move.** `far_hills_show_through_glass_and_water...` keeps its far-ray count logic, with "leaves full detail" now meaning "reaches `t_end`". Its new numbers are recorded after looking.

### Settings and screens

- **`run_settings`:**
  - `an_old_file_with_full_detail_loads_and_says_so_once`;
  - `the_file_for_the_defaults_is_exactly_this` updated, with no `full_detail`;
  - `the_defaults_are_the_plans_and_every_effect_has_its_own_bit` updated.
- **`run_ui_screens`:** `settings_main.png` and `settings_explain.png` regenerated after looking: five sliders, no "Full detail".

### Game

- **`run_render_distance_live`:** opens at render distance 64; level 0 fills to r<sub>0</sub>; the far field fills; deadlines as before (2 s, 15 s).
- **A new `game_view` host test:** r<sub>0</sub> at E = 2, FOV 70, 1080 rows is 27 chunks; at E = 8 it is 12 (the floor); render distance 8 gives r<sub>0</sub> = 8.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(cuda_traverse|render_feature_lod|render_feature_water|render_feature_post)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_settings|run_settings_session|run_ui_screens|run_game_focus|run_save_state_game|run_render_frame)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_render_distance_live$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --steps /tmp/mc_c_009/p04/e2.tsv --shots /tmp/mc_c_009/p04/e2
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --render-distance 64 --record bench/world/history.tsv --label "009 phase 04: continuous LOD, E 2"
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 phase 04: rows at 32"
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"                                                       # must print nothing

E = 4 and E = 8 are each one edit of `compile/config/render/lod/index.yaml`, one profile build and the same bench line.

## Gate

Ahmed gets three release builds, E = 2, 4 and 8, built one after another by an agent (he runs each himself):

    ./build/release/bin/minecraft_c --menu

At his slope at night, at render distance 64, he plays each for a minute. He says:

1. Is anything within 16 blocks different from before? It must not be.
2. Does he see a ring, a band, or ground that steps where it should be smooth?
3. Is the far slope and the ridge against the sky acceptable?
4. Which E? It is written to the YAML and committed.

His answers are recorded below.

## Risks and open questions

| Risk or question | Default |
|---|---|
| A ring at d<sub>1</sub> on grazing ground | Dither + TAA; `band_dither` up to 0.25; the seam test |
| Ridges against the sky look notched at E = 4 or 8 | Master Q6: a smaller E for grazing pixels, only if the error share on peaks is over 1 % |
| `traverse_trace_ray_t` growing to 48 bytes costs registers | Measured with `-Xptxas -v`; `t_end` can share the `resume` word as a float with a sign bit if needed |
| The sim's radius shrinks from 32 to 27 chunks, or 12 at E = 8 | `block_radius_min_chunks` keeps 12; mobs and ticking unchanged inside it; recorded |
| Old saves or settings break | Settings key ignored with a log line; nothing in saves names full detail (checked in step 7 with `grep` over `run/src/save/`) |
| Plan 006 phase 01 edits `run/src/game/index.c` and F3 at the same time | Rebase onto it before step 6 |
| Plan 008 phase 05's ladder changes θ every few frames | Bands follow θ; dither and TAA cover the move; tested at 50 % |

## Commits

Appended as they land:

## Issues found

None.
