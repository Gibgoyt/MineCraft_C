# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9: Renderer features

**Status:** in progress: every check an agent can run passes, and the gate is met. What awaits Ahmed is only how it looks and runs on the real display: see "Waiting for Ahmed"   (updated 2026-11-02)

## Goal

The renderer has non-cube shapes, sky, shadows, water, AO, TAA, entities and LOD to R=512, and each effect can be switched and measured.

**Wave:** W7.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-1-non-cube-shapes.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-2-sky-shadows-light.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-3-water-glass.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-4-ao-taa-godrays.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-5-entities-outline.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-6-lod-clipmaps.md`

## Todo

- [x] Stage 09.1: Non-cube shapes
- [x] Stage 09.2: Sky, shadows and stored light
- [x] Stage 09.3: Water and glass
- [x] Stage 09.4: AO, TAA and god rays
- [x] Stage 09.5: Entity boxes and outlines
- [x] Stage 09.6: LOD clipmaps
- [x] Phase gate passed
- [ ] Ahmed has watched it move on the real display: see "Waiting for Ahmed"

## Verification

**Gate:** Each effect has a setting, a measured cost, and a golden image.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^render_feature_' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Gate check

By a skeptical checker, 2026-11-02, on a clean build of this branch with the integration branch merged, on the RTX 2080 Super.

**Results:**

| Run | Result |
|---|---|
| The gate, exactly as written, debug | 6 of 6 pass, 546 s |
| Every other ctest, debug | 68 of 68 pass, before and after the fix |
| `render_feature_*`, release | 6 of 6 pass, with the cost test at R=512 |
| `render_feature_*`, sanitize | 6 of 6 pass |
| `standards_lint`, `clang_format` | pass |
| `pgrep -a minecraft_c` | prints nothing |

**Each effect has its setting, cost and golden picture:**

| Effect | Bit, and flag | Golden pictures | Cost, release, 1080p, this branch |
|---|---|---|---|
| Shapes | `SHAPES`, `--no-shapes` | `shapes/all_shapes`, `near` | 0.014 ms primary among plants |
| Alpha | `ALPHA`, `--no-alpha` | `shapes/near` | 0.72 ms shade among plants, none on terrain |
| Sky | `SKY`, `--no-sky` | `sky/noon`, `no_sky` | under 0.01 ms |
| Day cycle | `DAY_CYCLE`, `--no-day-cycle` | `sky/morning_shadows`, `dusk`, `night` | 0.206 us on the host |
| Fog | `FOG`, `--no-fog` | `sky/noon`, `no_fog` | under 0.01 ms |
| Sun shadow | `SHADOW`, `--no-shadows` | `sky/morning_shadows`, `no_shadows` | 0.28 ms on terrain, 0.70 ms under leaves |
| Stored light | `LIGHT`, `--no-stored-light` | `sky/light_noon`, `light_night` | under 0.01 ms on terrain, 0.03 ms under leaves |
| Transparency | `TRANSPARENCY`, `--no-transparency` | `water/lake_*`, `no_transparency`, `through_glass`, `under_water` | 0.55 ms on terrain, 4.2 ms over a sea |
| Reflection | `WATER_REFLECTION`, `--no-water-reflection` | `water/lake_*`, `no_reflection` | 0.49 ms on terrain, 0.48 ms over a sea |
| Depth tint | `WATER_TINT`, `--no-water-tint` | `water/lake_*`, `no_tint` | saves 0.1 ms on terrain, 1.1 ms over a sea |
| AO | `AO`, `--no-ao` | `post/ao_plain`, `ao_morning`, `play_morning` | 2.3 ms |
| TAA | `TAA`, `--no-taa` | `post/taa_morning`, `play_morning` | 0.37 ms |
| God rays | `GOD_RAYS`, `--god-rays` | `post/god_rays_only`, `god_rays_wood`, `every_morning` | 3.13 ms |
| Entity boxes | `ENTITIES`, `--no-entities` | `entity_rays/play`, `plain`, `no_boxes` | 0.04 ms primary, 0.08 ms lists, for 256 items |
| Outline and crack | `OUTLINE`, `--no-outline` | `entity_rays/play`, `no_outline` | 0.04 ms post |
| LOD to R=512 | `LOD`, `--no-lod`, `--full-detail` | `lod/hills`, `hills_plain`, `hills_off`, `real_near`, `real_far` | 1.2 to 1.4 ms primary. 4885 columns, ready in 127 s, 2164 MiB on the card |

The stage files' terrain rows from 09.1 to 09.4 were measured while the land was nearly black, and so are short. This table is what the integrated branch does. Frames with AO and TAA on come to 7.6 ms on terrain, and 8.9 to 10.3 ms at R=512, which is past the 8.33 ms Stage 09.4 holds them to. No check of this phase asks for a frame rate.

**What was looked for and not found:** no stub, TODO or placeholder under `cuda/`, `run/*/render`, `run/*/gpu_bridge` or `tests/cuda`. Every `render_feature_*` test has checks, directly or through its golden compare, but for two cost tests that only print, which their READMEs say. No `int`, `float`, `double` or `size_t` in this phase's sources, no `cudaMalloc` outside `device_memory`, no heap call in `run/src/render/{lod,entities,frame}/`, and every device-side struct has its padding and a `sizeof` check, read by hand as the compiler's `-Wpadded` does not reach nvcc. Each of the 16 effects' bits is read by code that a real run reaches, each flag is run by a test on the built program, and every public function of this phase's headers has a caller outside tests, but `traverse_hit_face` and `traverse_hit_steps`, which are for tests and are one-liners on the format.

**Do the tests catch a broken kernel?** Breaks of the checker's own, not those in the stage files. All were caught, and put back:

| Broken | Caught by |
|---|---|
| Stairs' upper box a unit wider | shapes: by hand, the trace, settings, golden |
| No mirror on water | water: 6 tests, golden among them |
| God rays' phase dropped | post: the exact walk, golden |
| TAA keeps no history | post: 4 times the size, golden |
| Crack one cell over | entities: the crack, golden |
| A coarse voxel kept on any block | lod: on paper |

A first break of the stairs, 1 unit lower on z, passed. It was no break: that box's new unit lies inside the slab under it, so the shape was the same.

**Waiting for Ahmed**, as only he can see KMS, and nothing here decides it:

- All of it in motion: TAA's shimmer or ghosts, god rays' look, the outline and the crack, plants as a `+`.
- The far field at R=512: its seam while moving, far mountains with no snow, the frame rate while far columns build.

The command is in this branch's result, and needs a text console.

## Commits

Appended as they land:

- `c4c6b9c` Make far water look like near water that deep so a sea has no edge at full detail, and record the Phase 9 gate check with costs measured on the integrated branch.

## Issues found

### A sea changed colour at the edge of full detail

- **Expected, actually, evidence and fix:** in Stage 09.6 issue 9. Every test passed with it there, as far water was held to its own function on both sides. Found by looking at a real run.

### The phase gate has little time to spare on a debug build

- **Expected:** a gate well inside its `timeout 600`.
- **What actually happened:** 570 s, then 546 s, on a quiet machine: 219 s of LOD, 170 s of sky and 112 s of water. `timeout` starts after the lock is held, so waiting for other builds does not count, but a busy card or CPU could pass 600 and read as a failure.
- **What was done:** tried `-O2` for the sky's and water's test files alone, since most of their time is their exact trace on the CPU at -O0. `standards_lint` refused it, `cmake_flags_outside`: compile flags live only in `cmake/flags.cmake`, for every source alike, by section 9 of the standard. Taken out, and not worked around. Left as it is: if the gate ever times out, run it by stage, each of which has its own 300 s in its Verification.

### Left behind by Stage 09.5, outside this phase's paths

- **Expected:** nothing in the game to do work for a picture nobody draws.
- **What actually happened:** `player_camera_outline_draw` is called only by `tests/run/physics/raycast/index.c`, and the sim's hundred short casts a tick for `edge_visible`, with `picture_cameras` and `shown_camera` in `game_t`, are read by nothing else. Stage 09.5 issue 2 says so.
- **What was done:** left: it wastes a little and does nothing wrong, and `run/src/player/camera/` and the tests of it are Phase 12's. Whoever next touches that should take it out.

### Stages overlap on `cuda/*/shade`, so they cannot run in parallel

- **Expected:** plan section 7 defines a wave as stages whose paths do not overlap.
- **What actually happened:** stages 09.1 to 09.4 all own `cuda/{src,include}/shade/`, and several also share `traverse` or `post`.
- **Evidence:** the Owned paths lists in the four stage files, compared during Stage 00.2 on 2026-11-01.
- **What was done:** 09.1, 09.2, 09.3 and 09.4 run one after another. 09.5 and 09.6 own separate paths and may run in parallel with them. The verbatim plan is unchanged.
