# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 2: Sky, shadows and stored light

**Status:** done   (updated 2026-11-02)

## Goal

Sky, day cycle, fog, sun shadows and stored light exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/shade/`
- `cuda/{src,include}/post/`
- `tests/cuda/sky/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Add sky, day cycle and fog
- [x] Add one sun shadow ray
- [x] Use stored sky and block light in shading. On real terrain it reads as open sky until Stage 11.1 stores some: see issue 1
- [x] Add a setting, a recorded cost and a golden image for each effect; register test `render_feature_sky`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_sky$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Results

Verification on 2026-11-02, in this worktree: the build, `render_feature_sky`, `standards_lint` and `clang_format` pass. All 63 ctests passed on debug, before a last change that touched only `tests/cuda/sky/`. `render_feature_sky` and `render_feature_shapes` also pass on a release build. `pgrep -a minecraft_c` prints nothing.

**Against an exact trace**, every usable pixel, in the release run, 0 wrong throughout: 1843200 of sky through 8 times of day, fog on and off at 3 times, a floor, tower, arch, slab, stairs, plant, tree, glass and water at 5 times, a floor with all 256 stored light bytes at 3 times, and each setting off. At the 5 times of day, 2.2 million pixels with 665000 sun rays: 78000 in shadow, 6996 lit in part through water, and in the debug run 11270 that met leaves and reached the sun through their holes.

**Does the test catch a broken kernel?** Making every texel stop a sun ray, and reading the light byte 4 bits off, failed 8 tests, with 147662 of 147662 hits wrong in one picture. Put back.

**With these off, the old pictures still hold:** `tests/cuda/shapes/fixtures/*.png` were not made again, and that test draws with all of this off. `shade_surface_light` is held to a face's one brightness to the bit. `tests/run/render/frame/fixtures/*.png` were made again with all on, and looked at, as were `tests/cuda/sky/fixtures/*.png`.

**Settings:** `SHADE_FEATURE_SKY`, `_DAY_CYCLE`, `_FOG`, `_SHADOW`, `_LIGHT`, through `render_frame_features_set`, and on the command line `--no-sky`, `--no-day-cycle`, `--no-fog`, `--no-shadows`, `--no-stored-light`, with `--time-of-day <0 to 1>` to choose when. No sky means no sun, so no shadows either.

**Golden pictures**, `tests/cuda/sky/fixtures/`: `noon`, `morning_shadows`, `dusk`, `night`, `no_fog`, `no_shadows`, `no_sky`, `light_noon`, `light_night`.

**Cost**, on the card's clock, mean of 30 frames at 1920x1080, on the release build, shapes and holes on throughout. Every number is one run, and differences under about 0.03 ms are noise between runs.

| Scene | Setting | Shade ms | Post ms | Frame ms |
|---|---|---:|---:|---:|
| Real terrain, render distance 10, mid-morning | none | 0.493 | 0.110 | 2.269 |
| | sky | 0.488 | 0.115 | 2.277 |
| | fog | 0.488 | 0.110 | 2.268 |
| | stored light | 0.487 | 0.111 | 2.272 |
| | sky and shadows | 0.890 | 0.116 | 2.683 |
| | all | 0.890 | 0.117 | 2.680 |
| Under 4 layers of leaves, every voxel with light of no special level, the worst case on purpose | none | 0.718 | 0.112 | 2.445 |
| | sky | 0.716 | 0.119 | 2.431 |
| | fog | 0.715 | 0.112 | 2.426 |
| | stored light | 0.740 | 0.114 | 2.459 |
| | sky and shadows | 1.291 | 0.121 | 3.016 |
| | all | 1.344 | 0.121 | 3.063 |

So the sun ray costs 0.4 ms on terrain and 0.6 ms at the worst here, the sky under 0.01 ms, fog nothing to measure, and stored light up to 0.03 ms where its levels are not 15 and 0. The day cycle is 0.041 us a frame on the host. Stored light on real terrain shows no cost, because there is none stored there yet: the last row of the leaves is the one to go by.

On that terrain view, turning off the sky changes all 2073600 pixels, the fog 24878, and shadows 994149. Stored light changes 0: issue 1.

**Wiring outside the owned paths**, the least that makes a real run draw this:

- `run/include/gpu_bridge/shade/index.h`: the feature bits, `shade_sun_t` in `shade_scene_t`, and the light's arithmetic. `run/include/gpu_bridge/post/index.h`: the sun's and moon's fields in `post_settings_t`, the glow, and `post_sky_discs`.
- `run/{include,src}/render/frame/`: `render_day_fill`, `render_frame_time_set`, and the settings reaching what the kernels read. `run/include/render/index.h`: sizes.
- `run/{include,src}/game/`: the clock, `game_day_set`. `sizeof(game_t)` went from 896 to 960.
- `run/src/main/index.c`: the flags. `run/include/main/index.h`: `sizeof(app_t)` went from 7104 to 7296.
- `tests/cuda/shapes/`: draws with this stage's effects off, as its reference knows only a face's brightness. Its pictures did not change.
- `tests/run/render/frame/`: its reference world likewise, its threads with the clock stopped, since it waits for two seconds of identical frames, and its goldens made again. `tests/run/game/`: the clock stopped, for the same reason. `tests/run/render/upload/`: only the size of a struct holding these.
- READMEs of those directories, and `plans/REPO_STRUCTURE.md`.

## Commits

Appended as they land:

## Issues found

### 1. Decided: Stage 08.2 issue 3. Stage 11.1 must store on each block the light its faces should show

- **Expected:** that a hit voxel's own stored light is what shading wants.
- **What actually happened:** a flood fill usually leaves opaque voxels at 0 with the light in the air beside them, and air is not on the card. Nothing has gone wrong yet, since nothing writes light on real terrain: `run/src/world/column/index.c` starts every section at `SECTION_LIGHT_OPEN_SKY`, and no `run/src/world/light/` exists. So on real terrain today, a cave is as bright as under open sky but for the sun ray, and `--no-stored-light` changes 0 of 2073600 pixels.
- **What was done:** decided for the first of the two choices Stage 08.2 gave, keeping the format, and written in `cuda/src/shade/README.md`: **Stage 11.1 has to write each voxel with a block the brightest light of its neighbours, before the section goes out, or every face comes out dark.** The stage file of 11.1 is not this stage's to edit, so this needs carrying there: see this stage's result. The other choice, light for air next to blocks, changes the format and costs bytes on every surface. The price of this one: one byte for all six faces, so a wall one block thick shows the brighter side's stored light on its dark side too. The sun ray is not fooled by that. The stored path is tested on a hand-made floor with all 256 values, through the real upload.

### 2. The moon hung in the air by day

- **Expected:** no moon by day.
- **What actually happened:** looking at `morning_shadows.png`, with every test passing, there was a white disc low on the left, on the side away from the sun. The moon is always opposite the sun, so by day it is under the horizon, where the sky is still drawn wherever the world is not loaded.
- **What was done:** fixed: `post_sky_discs` draws nothing below the horizon, with a check by hand. The reference shares that function, which is why the exact trace could not have seen it. Pictures made again and looked at.

### 3. `run_game` failed on this stage's first full run: the first picture was not the one expected

- **Expected:** a picture drawn before the threads start to match the threads' first.
- **What actually happened:** `tests/run/game/index.c:299` to `:302`, `run.first_picture_true` 0. `game_open` set the game's time to 0.35, but only the next `game_day_apply` passes it on, so a draw straight after opening was at `render_frame_init`'s noon.
- **What was done:** a bug in this stage's code and not the test: `game_open` now applies it at once.

### 4. A test of mine counted wrongly at first

- **Expected:** with stored light off, every hit reads as open sky.
- **What actually happened:** `CHECK failed: tally->light_levels[SHADE_LIGHT_OPEN_SKY] == tally->hits`. A ray through a hole with sky behind it was counted as a hit with no light.
- **What was done:** a bug in the test's count, and fixed. No pixel was wrong.

### 5. Left as they are

- **Hard-edged shadows.** One ray, as the plan says. **No stars.** The moon is one flat disc with no phases.
- **Water and glass in the sun's way.** A part-covered texel takes its share and lets the ray on, with no colour. Glass here has only alpha 0 and 255, so it shades as its pattern says; water is the one with part-covered texels. A ray stops at water as a primary ray does until Stage 09.3, whose stage may want to look at this.
- **Sideways-facing plants are lit as their plate faces.** A plant's two plates get the sun by their own normals, so a side away from the sun has only the sky's light. Stage 09.1 issue 4 has why they are plates.
- **The clock lives in `game`, on the main thread.** There is no world time to keep it in yet. A saved world's time needs it moved to the sim: written in `run/src/game/README.md`.
- **The harness refused two compound shell commands** of mine, as it could not show that they stayed in this worktree. Nothing ran. They were run as separate plain commands.

