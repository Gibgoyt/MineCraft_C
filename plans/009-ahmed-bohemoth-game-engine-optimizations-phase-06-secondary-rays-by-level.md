# 009-ahmed-bohemoth-game-engine-optimizations — phase 6: Secondary rays by level

**Status:** not started   (updated 2026-11-12)

Read first: the phase 00 file, then the master: sections 0 ("Why the frame is 29 ms", shade), 3.1 (Lumen and VSM rows), 3.4 (Teardown), 4.5 and 7. Then:

- `cuda/src/shade/README.md`: "The sun ray", "See-through: near to far", "AO";
- `cuda/src/shade/index.cu:28-115`: `shade_sun_seen` and `shade_surface_lit`;
- `cuda/src/lod/index.cu:157-178`: `lod_sun_reaches`, the far field's own coarse sun rays;
- `run/src/render/frame/index.c:365-405`: `render_sky_fill`, the light;
- phase 01's sun-ray and alpha-ray step bands, and its "shadows off" and "alpha off" rows.

## Goal

Rays a surface sends after the primary ray use the coarsest level that cannot change what they show. When this phase is done:

- **A shadow ray (sun or moon) starts in level 0 at its surface and moves to level k once it has travelled L<sub>k</sub>** = ε<sub>k</sub> / (0.0093 × `shadow_cone_scale`) blocks: 108 for level 1, 216 for level 2, 432 for level 3, at the default scale. It goes on through every level out to the render distance. So **near surfaces now get the shadows of far hills**, which today they do not ("What is near is not shadowed by far hills", `cuda/src/lod/README.md`).
- **A shadow ray from a level-k surface starts in level k.**
- **A dim-light cut:** where the direct light could change a lit pixel by under `shadow_skip_below` (2 / 255 after tone map), no shadow ray is sent.
- **Alpha layers by distance:** past `alpha_far_from_blocks` (48), a pixel's ray goes through at most `alpha_layers_far` (4) holes, then takes the texture's mean colour.
- **At his spot, shadow-ray steps fall by at least 50 %** against phase 05, and shade's card time by what Results record.
- **The near guarantee still holds** for every pixel's hit. A near pixel's colour may now differ from phase 05's by a far hill's shadow, and the test is changed to say so.

## Where this fits

**The vision.** Ahmed: "further optimization as we move away!!" A shadow is a ray too, and it "moves away" from its surface. Lumen does this for its rays: short and fine near the surface, the coarse global distance field far away. So does Teardown: sun and ambient rays step coarser mips as they travel. The light's own size says how much detail a shadow can carry, which makes the rule physical, not a guess.

**What reads it:** phase 07 (fewer rays to upscale); phase 08.

**What this phase must NOT build:**

- No soft shadows. Shadows stay one ray, hard-edged. The cone only says which detail a shadow could never show.
- No change to AO rays. Their reach is 3 blocks, always level 0. Plan 008 phase 04 owns their fresh rays.
- No change to water reflections' level. They stay level 0 within `SHADE_WATER_SEEN_BLOCKS`; a reflection ray that leaves is banded like a primary ray, by phase 04.
- No shadow cache (master Q8).

## Ahmed's words

- "at all times 16 blocks in all directions **VIEWABLE** from the user must be rendered properly! and further optimization as we move away!!"

---

## Background, explained

### The light's cone

The sun is about 0.53° across, and so is the full moon. A point on a surface sees the light as that small disc. So an occluder L blocks away casts a shadow whose edge is blurred over a width of

    w(L) = L × tan(0.53°) = 0.00925 L blocks.

An occluder detail smaller than about w(L) is lost in that blur in a real shadow. Today's shadows are one ray, hard, so this engine does not draw that blur. But it means a level whose surface error is under w(L) changes nothing a real light would show. Level k's error is ε<sub>k</sub> = s<sub>k</sub> / 2, so:

| Level | ε (blocks) | Usable from L = ε / 0.00925 |
|---|---|---|
| 1 (2 blocks) | 1 | 108 |
| 2 (4) | 2 | 216 |
| 3 (8) | 4 | 432 |
| 4 (16) | 8 | 865 |

**Near shadows stay exact:** a tree 20 blocks from the ground it shades is traced entirely in level 0. `shadow_cone_scale` multiplies the 0.00925: 1 is physical, above 1 coarser, below 1 finer.

### Why a surface's own band also bounds its shadow

A surface hit in level k sits within ε<sub>k</sub> of the truth itself. A shadow ray from it cannot be finer than the surface it leaves. So it starts in level k, from the coarse voxel's face, as `lod_sun_reaches` already does for far surfaces.

### The dim-light cut, worked

`render_sky_fill` gives `sun->direct`. A lit pixel's colour in linear light is albedo × (… + direct × cos × seen), so the most a shadow can change it by is direct × 1 × 1 for a white texel facing the light. After the tone map (linear to 0.8, then sRGB), the steepest part of sRGB is its linear foot, 12.92 × the value, so a change of `direct` is at most 12.92 × 255 × direct bytes. For 2 / 255 that is direct ≤ 2 / (12.92 × 255) = **6.1 × 10⁻⁴**.

The night's direct light is 0.05 to 0.10, far over that. **So the cut does not fire on 5.png's moonlight.** It fires only while the light fades through the horizon (`fade`, within about 7° of it) and under heavy overcast, where `render_weather_apply` takes direct light down by 0.75 of the cloud. This is a small saving, kept because it is free and exact to 2 / 255.

### Alpha layers by distance

Leaves draw by holes: a ray landing on an alpha-0 texel traces on from there, up to `SHADE_ALPHA_LAYERS` = 16 surfaces (`cuda/src/shade/README.md`, "The sun ray"). A leaf texel is 1 / 16 of a block, so it covers a pixel at 771 / 16 = **48 blocks** at 1080p and FOV 70. Past that, the holes a ray finds are sub-pixel: which hole it goes through is noise that TAA averages anyway. Stopping at 4 holes and taking the mean colour, as the code already does at 16, changes a far canopy's look little and costs a quarter of the traces there. Phase 01's "alpha off" row bounds the prize.

## Design

### `cuda/src/shade/`

**`shade_sun_seen` gains the pyramid.** It loops over levels from the surface's level, with `t_end = L_{k+1}` in each, and hands over with `resume` as phase 04's primary rays do. Water and partly covered texels keep their rules in level 0. In coarser levels a far surface is its mean colour, and water is water, as `lod_sun_reaches` treats them.

- **The shade context gets the level views.** The far field's `lod_view_t` is already built per frame; it is passed into `shade_frame_entities`.
- **The dim-light cut** is a flag in `shade_sun_t`: `cast_shadows`, set by `render_sky_fill` and `render_weather_apply`. `shade_surface_lit` reads it beside `SHADE_FEATURE_SHADOW`.
- **Alpha by distance:** in `shade_kernel`'s layer loop, `may_go_on` (`index.cu:857`) becomes `layer + 1 < (hit.t > alpha_far_from_blocks ? alpha_layers_far : alpha_layers)`.
- **`shade_sun_kernel`,** the last surface's sun ray, uses the same function, so both paths agree.

### `cuda/src/lod/`

- **`lod_sun_reaches` and the new path share one hand-over function,** so the far field's sun rays and shade's agree at every level.

### Tuning (`compile/config/render/shade/index.yaml`)

- `shadow_cone_scale` (1.0, ratio, 0.25 to 8);
- `light_angular_diameter` (0.53, degrees, 0.1 to 5);
- `shadow_skip_below` (0.0078, ratio, 0 to 0.05);
- `alpha_far_from_blocks` (48, blocks, 16 to 512);
- `alpha_layers_far` (4, count, 1 to 16).

## Owned paths

| Path | Change |
|---|---|
| `cuda/src/shade/`, `cuda/include/shade/index.hu`, `run/include/gpu_bridge/shade/index.h` | Shadow rays through the pyramid; the cut flag; alpha by distance |
| `cuda/src/lod/`, `run/include/gpu_bridge/lod/index.h` | The shared hand-over |
| `run/src/render/frame/` | `cast_shadows` from the light and the weather |
| `compile/config/render/shade/index.yaml` | Keys |
| `tests/cuda/sky/`, `tests/cuda/lod/`, `tests/cuda/shapes/`, `tests/run/render/frame/` | Tests |

## Steps

1. **The hand-over function, shared by far and near sun rays.** *Why:* two paths that disagree would draw a seam in shadows at d<sub>1</sub>.
2. **The cone rule in `shade_sun_seen`;** the near-pillar and far-tower tests. Measure at his spot by day and by night.
3. **The surface's band as the start level.**
4. **The dim-light cut,** with the bound test at dusk.
5. **Alpha by distance,** measured on `jungle/orbit` and his tree.
6. **Golden pictures that move are regenerated only after looking**, each named in Issues found with why it moved.

## Todo

- [ ] One hand-over function for shadow rays, used by `shade_sun_seen`, `shade_sun_kernel` and `lod_sun_reaches`
- [ ] Shadow rays move to level k at L<sub>k</sub>; tuning keys `shadow_cone_scale`, `light_angular_diameter`
- [ ] A shadow ray from a level-k surface starts in level k
- [ ] Near surfaces get far hills' shadows (test)
- [ ] `cast_shadows` from `render_sky_fill` and `render_weather_apply`; `shadow_skip_below`; bound test
- [ ] Alpha layers by distance; keys `alpha_far_from_blocks`, `alpha_layers_far`
- [ ] Near guarantee still holds for hits; its colour clause updated for far shadows
- [ ] His spot (night and day) and `jungle/orbit`: shade ms and shadow-ray steps in Results
- [ ] Moved golden pictures regenerated after looking, each listed in Issues found
- [ ] `cuda/src/shade/README.md` "The sun ray" and `cuda/src/lod/README.md` "The sun" rewritten

## Results (filled in by this phase)

| Row | Shade ms (sun / AO / total) | Sun-ray steps a pixel | Against phase 05 |
|---|---|---|---|
| `ahmed_slope/night-64` | | | |
| `ahmed_slope/day-64` | | | |
| `jungle/orbit` at 64 | | | |
| `plains/orbit` at 32 | | | |

## Tests

### `tests/cuda/sky/` and `tests/cuda/lod/`

- **`a_near_pillars_shadow_is_unchanged`:** a 1-block pillar 20 blocks from the floor it shades, sun at 30°. The shadow's pixels are identical to phase 05's.
- **`a_far_towers_shadow_follows_the_cone`:** a 1 × 1 × 40 tower whose shadow falls 300 blocks away. With scale 1, the shadow is traced through level 2 past 216 blocks, so it may widen to the 4-block voxel or vanish. The test records which, and holds it there so a change is seen.
- **`near_ground_gets_a_far_hills_shadow`:** a hill 600 blocks away between the low sun and a field at the eye. The field is in shadow where the reference (level 0 everywhere) has it, give or take the cone's widening at 600 blocks (5.6 blocks). Today it is lit.
- **`far_and_near_sun_rays_agree_at_the_band_edge`:** a flat plain lit at 20°. No step in brightness across d<sub>1</sub>.
- **`the_dim_light_cut_moves_no_pixel_by_more_than_2`:** sun 3° above the horizon, where `fade` takes direct below the bound. The picture with the cut and without differ by at most 2 / 255 in any channel.
- **`far_leaves_stop_after_four_holes`:** a forest at 100 blocks. Each pixel's layer count is at most 4. The mean colour differs from 16 layers by at most what TAA's clamp allows, measured and recorded.

### The near guarantee (`render_feature_lod`)

Hits stay bitwise identical. The colour clause now compares against a reference whose shadow rays use the same pyramid, so only the primary path is held to level 0.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_sky|render_feature_lod|render_feature_shapes|render_feature_water)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_.*|run_render_frame|cuda_traverse)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --steps /tmp/mc_c_009/p06/night.tsv --shots /tmp/mc_c_009/p06
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/day-64 --steps /tmp/mc_c_009/p06/day.tsv --shots /tmp/mc_c_009/p06
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 phase 06: secondary rays by level"
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"                                                       # must print nothing

## Gate

Ahmed plays his slope at night, then by day (`--time`, or waiting), at render distance 64:

    ./build/release/bin/minecraft_c --menu

He says:

1. Do the shadows read right: the tree on the ground, the ridge on the far slope?
2. Is the far forest's canopy acceptable after 4 holes?
3. Does a far hill's shadow on nearby ground look right? It is new.

His answers are recorded below.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Thin far shadows (a tower's) vanish | Physical under the cone; `shadow_cone_scale` below 1 keeps them; test records it |
| Far hills' new shadows on near ground look wrong where the far level's terrain is sampled (no caves, no trees past features shift) | They are the far field's own ground; judged at the gate |
| Passing the level views into shade grows its parameters past registers | Measured with `-Xptxas -v`; views by pointer to constant memory if needed |
| The dim-light cut flickers on and off at dusk | It turns on below a bound under which no pixel moves 2 / 255; tested |
| Alpha 4 makes far trees look solid | `alpha_layers_far` is tuning; judged |

## Commits

Appended as they land:

## Issues found

None.
