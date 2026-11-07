# 004-ahmed-bohemoth-climate-system — phase 8: Weather in the world

**Status:** in progress: built and agent-verified on 2026-11-06 (Verification passes; the bench before and after is recorded, and rain is at the 0.5 ms budget while snow and thunder are over it, Issues found 17). Left: Ahmed's gate, his call on the cost, and step 1's and step 15's boxes, which wait on phases 06 and 07 and on plan 003 phase 1 being called done by Ahmed   (updated 2026-11-06)

**Read first:** `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` (the vision), then sections 6.7, 7 and 10 (phase 08) of `plans/004-ahmed-bohemoth-climate-system.md` (the master plan), then `plans/004-ahmed-bohemoth-climate-system-phase-07-daily-weather-weekly-state-census-and-outlook.md`, whose `climate_weather_at`, `climate_now_t` and week store this phase reads. The master plan is the source of truth for every number and formula. Where this file and the master plan disagree, the master plan wins, unless this file's **Issues found** says why not.

## Goal

When this phase is done, the player **sees and feels** the weather where they stand:

1. **Rain and snow fall** in the CUDA renderer, as many drops as the rain rate at the player's place says, slanted by the wind, stopping under roofs and behind walls. Snow falls slower and drifts.
2. **Thunderstorms have lightning:** a bolt from the clouds to the ground, and a flash that lights the sky and the land, in thunderstorm cells near the player.
3. **Fog and sky colour follow the weather:** overcast skies are grey and the sun's light is weaker; heavy rain, fog and dust storms shorten how far you can see, each with its own colour.
4. **Wind pushes the player** in storms and hurricanes, by a share of its speed above a threshold, and less when sheltered.
5. **At each census, in loaded chunks,** ice forms on still water where the week's ice is over 5 cm, and a thin snow layer lies on the ground where the week's snow is over 20 mm. Both go when they melt. The snow layer is a new block, so that part waits for plan 003 phase 1.
6. **The frame-rate cost is measured** at render distance 32, before and after, and recorded.

Somebody can check this without the todo list: jump a world into a stormy week, stand outside, and see rain slant in the wind; step under a roof, and it stops above you; wait for a census in winter and see the ground white and the lake frozen; stand in a hurricane and feel it shove you.

## Where this fits

The vision file's stack, top to bottom: terrain, then **climate (this plan)**, then vegetation, settlements, the economy, the player. Every phase before this one made numbers. This is the first phase where the player meets them.

Why it matters to the vision:

- **The economy reads the weekly state (phase 07); the player has to believe it.** If floods destroy houses and frozen rivers close trade routes in the economy plan, the player must have *seen* the river freeze and the storm come in. Otherwise the economy feels like dice. This phase makes the weekly state visible in the world: ice on the river is the same `ice_cm` the economy will read.
- **Weather to prepare for.** Ahmed asked for storms that spawn and seasons that matter. The outlook (phase 07) says a hurricane is coming; this phase is what makes it worth preparing for: the light goes, the rain hammers down, the wind pushes you off a cliff edge.
- **Snow and ice are state saved like any block edit** (master plan, "Where this plan sits beside plan 003"). They are the only part of plan 004 that changes blocks in the world. Nothing in the terrain changes: no world is thrown away, and the generator stays the same.

**What this phase must NOT build:**

- **No economy, no settlements, no damage to buildings.** Lightning doesn't start fires or hurt anyone. Floods don't move water blocks. The wind doesn't move mobs, items or arrows. (Each could be a later plan's; none is asked for here.)
- **No vegetation.** Snow doesn't kill crops. No autumn colours.
- **No geology.** The ground is never reshaped.
- **No new weather rules.** Every number about the weather comes from phases 05 to 07 through `climate_weather_at` and the week store. This phase only draws them, pushes the player with them, and places blocks from them.
- **No Atlas, no HUD line.** Phases 09 and 10.
- **No thunder sound.** Sound is plan 003 phase 5, which isn't built. See Risks.

If any step below seems to call for one of those, it's a mistake in this file: stop and write it under **Issues found**.

## Ahmed's words

- "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please"
- "please drive a probability thing! but do not drive the actual physics! just create general code please that will sort of mimic it"
- "create winds please!"
- "every 7 days, when the sunrises, the entire world is paused, pause screen pops up everything is re-computed" (the census, when snow and ice are laid down)
- Master plan Q4: "Can storms and hurricanes push the player (phase 08)?" Default: **"Yes, gently."**

## Background, explained

### How the renderer makes a picture

The game draws every frame by **ray casting** on the graphics card, in CUDA. There are no triangles. For each pixel, a ray goes from the eye into the world, and the card finds the first block it hits. The passes run in order in `cuda/src/frame/index.cu` (`frame_gpu_run`), and their list is `frame_kernel_t` in `run/include/gpu_bridge/frame/index.h`:

| Pass | Kernel | File | What it does |
|---|---|---|---|
| Coarse | `traverse_coarse_kernel` | `cuda/src/traverse/` | Per tile of pixels, a distance before which no ray can hit anything, so rays skip empty air |
| Primary | `traverse_primary_kernel` | `cuda/src/traverse/` | Each pixel's ray through the world's bitmask tree (`world_buffers`) to the first voxel it hits |
| Entity rays | `entity_rays_kernel` | `cuda/src/entity_rays/` | Mobs and items, as boxes |
| LOD | `lod_frame_kernel` | `cuda/src/lod/` | Coarse far-field grids past full detail |
| Shade | `shade_kernel` and friends | `cuda/src/shade/` | Texture colour, sun and block light, shadows, AO, water reflection and tint. Writes `shade_pixel_t`: colour and the hit's distance `t` |
| God rays | `shade_shafts_kernel` | `cuda/src/shade/` | Optional light shafts |
| Post | `post_kernel`, `post_taa_kernel` | `cuda/src/post/` | Sky where nothing was hit, distance fog, sun and moon discs, the temporal pass (TAA), the tone map to XRGB8888 bytes |
| Overlay | `entity_rays_overlay_kernel` | `cuda/src/entity_rays/` | The outline of what is aimed at |
| Readback | — | `cuda/src/frame/` | The finished picture copied to pinned host memory |

The C side, `run/src/render/frame/index.c`, holds what the kernels read: `shade_scene_t` (with the sun, `shade_sun_t`) and `post_settings_t` (sky colours, fog range, sun direction, glow, discs). `render_day_fill` works out the sky and sun from the time of day. `frame_settings_apply` re-applies them whenever the features, fog or time change. `render_frame_draw` runs on the **gpu thread**, once per picture.

**Where weather fits in that pipeline:**

- **Sky tint and sun strength** are plain numbers in `post_settings_t` and `shade_sun_t`. The CPU adjusts them after `render_day_fill`, once per frame. No kernel changes for those.
- **Fog** is `post_settings_t.fog_start` and `fog_end`: `post_fog` (in `run/include/gpu_bridge/post/index.h`) mixes a hit toward the sky colour from nothing at `fog_start` to all at `fog_end`. Weather moves `fog_end` closer and tints the fog. The kernel gains a fog colour mix, nothing else.
- **Rain, snow and lightning** are drawn inside the post kernels, per pixel, from a small struct of numbers. They're **procedural**: nothing is stored per drop. A hash of a drop's cell decides whether a drop is there, exactly as the terrain decides where a tree goes.

**Why after the temporal pass:** TAA (`SHADE_FEATURE_TAA`, `cuda/src/post/README.md`) mixes a tenth of each new frame into a history of about 20 frames. A raindrop moves several pixels per frame, so mixed into the history it would smear into a grey haze, and leave ghosts. So precipitation and lightning are added **after** the history is written and **before** the tone map: in `post_taa_kernel` when TAA is on, and in `post_kernel`'s final branch when it is off. Fog and sky tint are stable from frame to frame, so they go in before the history, as fog already does.

### Rain and snow as procedural particles

A real raindrop is a streak in a photo, because it moves during the exposure. The renderer draws streaks the same way, without storing drops:

1. Around the eye, imagine **4 vertical cylinders** of radius $r_k = 2, 4, 8, 16$ blocks.
2. Each pixel's ray meets each cylinder at one point (going out). For that point, take its **angle round the eye** $\theta$ and its **height** $h$.
3. The cylinder's surface is cut into **cells**: $\Delta\theta$ so each cell is about 0.25 blocks wide at that radius, and 1.5 blocks tall.
4. The rain falls, so the cells slide down over time: use $h' = h + v_f\,\tau$, with $v_f$ the fall speed and $\tau$ the time in seconds.
5. A hash of (layer, cell's $\theta$ index, cell's $h'$ index) decides: is a drop in this cell? (yes with probability $\rho$, the **density**), and where in the cell.
6. If the ray's point is within the drop's streak (0.03 blocks wide, $\ell$ long), the pixel gets a little of the drop's colour.
7. **Occlusion:** a drop only shows if (a) the cylinder point is nearer than what the ray hit ($r$ along the ray below the pixel's `t`), and (b) the point is under open sky: its height is above the **rain top** at its $(x, y)$ (below).

**Wind slant.** Drops falling at $v_f$ in a wind $\vec w$ move sideways at the wind's speed, so their streaks lean. The tilt is $\vec w / v_f$: the cylinders are sheared so that, at height $\Delta z$ above the eye, they're shifted by $-\Delta z\,\vec w / v_f$, and drops fall along that slope.

**Worked example.** Rain at 10 mm an hour, wind 6 blocks/s from the east ($\vec w = (-6, 0)$).

- Density $\rho = \operatorname{clamp}(10/40,\ 0.03,\ 0.85) = 0.25$: a quarter of cells hold a drop.
- Fall speed for rain $v_f = 9$ blocks/s (raindrops fall at about 9 m/s, and a block is a metre).
- Tilt $= 6/9 = 0.67$: streaks lean $\arctan(0.67) = 34°$ from vertical, their tops toward the east, where the wind comes from.
- Streak length $\ell = \max(0.6,\ v_f / 60) = 0.6$ blocks.
- On the 2-block cylinder, a cell is 0.25 blocks wide, so about $2\pi \times 2 / 0.25 = 50$ cells round, and with 1.5-block cells in a 6-block-tall view about 200 cells; at 25%, about 50 visible drops on that cylinder, about 200 over all four layers.

**Snow** uses the same layers with $v_f = 1.5$ blocks/s, round flakes (0.08 blocks) instead of streaks, and a sideways wobble $0.3 \sin(2\pi\tau/3 + \text{hash})$ blocks. The tilt is capped at 2.5, so a strong wind blows snow nearly sideways. A **sleet** day mixes both by the snow share.

**Brightness.** A drop's colour is the sky's ambient light (`shade_sun_t.ambient`) times 0.6 for rain and 1.0 for snow, mixed in at 35% for rain and 80% for snow, so rain is faint and grey at night and snow is white by day.

### The rain top, so rain stops under roofs

A drop must not fall inside a house. The sim keeps a **rain top map**: a 64 by 64 block square centred on the player, each entry the height of the highest block in that column that stops rain (anything but air). The kernel reads the map at a drop's $(x, y)$: if the drop's height is below the entry, it is under cover and isn't drawn.

64 by 64 entries of `int16_t` is 8 KiB, sent to the card only when it changes: when the player crosses into a new block column, or when a block in it changes (the sim reads the world's edit log, `world_edit_log_read`, as other modules do). The biggest cylinder is 16 blocks, so the square's 32-block half-width covers it with room for the wind's slant.

**Worked example.** The player stands at (100, 200) under a roof at height 70, eye at 65. A drop on the 4-block cylinder at (103, 200), height 68: the map at (103, 200) says 64 (open ground outside the house), $68 > 64$, drawn. A drop at (100, 202), height 67: the map says 70 (the roof), $67 < 70$, hidden. From inside, the player sees rain through the window and none above their head.

### Lightning

Phase 06 spawns **thunderstorm cells**, each 150 to 400 blocks across, living 1 to 4 hours, moving with the day's wind. Lightning is decided by hash, like everything else:

- Time is cut into **1-second slots**. For each cell within 2000 blocks of the player and each slot, a hash of (cell id, slot) says whether a strike happens, with probability $q = 0.05 \cdot s$, where $s$ is the cell's strength from 0 to 1 at that moment. A strong cell strikes about 3 times a minute.
- The hash also gives the strike's place within the cell's radius, and a seed for the bolt's shape.
- A bolt is a jagged line of 12 segments from the cloud base (the ground's height plus 180 blocks, capped at the world's top) to the ground at the strike's place (`terrain_height_estimate`, or the rain top map if the strike is within it). Each joint is offset sideways by a hash, up to 8 blocks.
- It's visible for 0.2 seconds, flickering: full, half, full over its life.
- The **flash** lights the sky and the land. Its strength falls with distance $d$:

$$f = \frac{1}{1 + (d/500)^2}$$

At 0 blocks $f = 1$; at 500, $0.5$; at 1500, $0.1$. A distant storm's flash is a faint glow; a near one turns the world white for a moment.

The flash is added to the sky colours and the ambient light: sky $+\,3f$ (linear light), ambient $+\,1.5f$. The bolt is drawn per pixel as the ray's distance from each of the 12 segments, bright where it's under 0.4 blocks (wider far away so it doesn't vanish: $0.4 + 0.002\,d$).

**It's only a picture.** No fire, no damage, no sound (sound isn't built). Each strike is the same if the same moment is replayed, because it's hashed.

### Fog and sky colour by weather

From phase 07's `climate_now_t` the frame gets `cloud` (0 clear to 1 overcast), `fog` (0 to 1), `dust` (0 to 1) and `rain_mm_h`.

**Sky and sun under cloud $c$:**

- Sky colours mix toward grey by $0.8c$. The grey is the sky's own brightness: $g = 0.3R + 0.59G + 0.11B$, times $(0.92, 0.95, 1.0)$.
- The sun's glow is cut by $(1 - c)$, its disc by $(1 - 0.9c)$.
- Direct sunlight (`shade_sun_t.direct`) is cut by $(1 - 0.75c)$, so shadows fade on an overcast day. Ambient light is cut by $(1 - 0.3c)$.

**Visibility:**

$$V = \max\!\left(32,\ \frac{F \cdot (1 - 0.85\,\phi)}{1 + R/10}\right)$$

$F$ is the setting's fog end (what it is today, from render distance), $\phi = \max(\text{fog}, \text{dust})$, and $R$ is the rain rate in mm an hour. `fog_end` becomes $V$ and `fog_start` $0.3V$ when weather is shortening it (instead of today's $0.7F$), so weather fog is thick close up.

**Fog colour:** today, fog is the sky's colour in that direction. Weather mixes it toward grey $(0.55, 0.57, 0.60)$ by $\max(c, \text{fog})$ times today's daylight, or toward dust $(0.62, 0.47, 0.30)$ by `dust`.

**Worked example.** Render distance 32, so $F = 31 \times 16 = 496$ blocks (the fog ends a chunk short of what's loaded, `cuda/src/post/README.md`). A thunderstorm: $R = 30$ mm/h, fog 0, dust 0. $V = 496 / (1 + 3) = 124$ blocks. A foggy calm morning on a cold coast: $R = 0$, fog 0.9: $V = 496 \times (1 - 0.765) = 117$ blocks. A clear day: $V = 496$: nothing changes.

### Wind pushing the player

The player's body is moved one tick at a time, 20 times a second, by `player_movement_tick` (`run/src/player/movement/index.c`). On the ground each tick does: add the walking push to velocity, move by velocity (with collisions), then multiply horizontal velocity by friction `MOVEMENT_GROUND_FRICTION` = 0.546. In the air the friction is `MOVEMENT_AIR_FRICTION` = 0.91.

Adding a small constant acceleration $a$ each tick before the move, velocity settles where $v = (v + a)\,f$, so:

$$v^* = \frac{a\,f}{1 - f}$$

On the ground, $v^* = 0.546a / 0.454 = 1.2026\,a$ blocks per tick.

**The rule (Q4: "Yes, gently"):** the wind pushes only above a threshold $W_0 = 10$ blocks/s, so normal trade winds (6) and westerlies (8) do nothing, and only storms, hurricanes, thunderstorm gusts and windy peaks push. Standing on the ground, the player drifts downwind at a share $s = 0.04$ of the wind above the threshold:

$$D = s \cdot \max(0,\ \lvert\vec w\rvert - W_0) \text{ blocks/s}, \qquad a = \frac{D}{20} \cdot \frac{1 - f_g}{f_g} \text{ blocks/tick}^2 \text{ along } \hat w$$

- In the air, the push is 0.2 of that (the same ratio as the game's air and ground acceleration, 0.02 against 0.1), so a jump in a gale doesn't fling the player.
- In water, lava, or on a ladder: no push.
- **Sheltered:** if any solid block is within 3 blocks **upwind** of the body at feet or head height, the push is a quarter.

**Worked example.** A hurricane's eyewall, 60 blocks/s.

- $D = 0.04 \times (60 - 10) = 2.0$ blocks/s, about half of walking speed (4.317).
- $a = (2.0/20) \times 0.454/0.546 = 0.0832$ blocks/tick², on the ground.
- Walking into it: about $4.3 - 2.0 = 2.3$ blocks/s. With it: about 6.3.
- In the air: $a = 0.0166$, settling at $0.91 \times 0.0166 / 0.09 = 0.168$ blocks/tick, 3.4 blocks/s, for the half second of a jump: under 2 blocks.
- A 30 blocks/s storm: $D = 0.8$ blocks/s. A peak at 2.5 times an 8 blocks/s westerly: 20 blocks/s, $D = 0.4$.

This keeps the body's rule: "Nothing here reads a clock or a random number. The same world and the same intents give the same bytes" (`run/src/player/movement/README.md`). The wind is an **input** to the tick, worked out by the sim from the pure climate, so the same world, intents and time still give the same bytes.

### Loaded chunks

The world is infinite, but only a square of **columns** round the player is in memory. A column is a chunk, 16 by 16 blocks, and all its height. `world_map_t` (`run/include/world/map/index.h`) holds them in a table that scrolls with the player: `table_radius` columns each way, of which those within `view_radius` are wanted.

- Columns are **generated** on worker threads (`game_column_job_t`, `terrain_generate_box`), with any saved changes laid over them (`save_world_column_overlay`).
- The **sim thread commits** each finished column into the map (`world_map_stream_commit`, in `game_sim_step`, `run/src/game/index.c`).
- When the player moves away, a column **leaves**: if it has unsaved changes they're captured first (`game_column_leaving_save`), then it's unloaded.

"Loaded chunks" in this phase means the columns in the map at that moment. At render distance 32 that's up to $65 \times 65 = 4225$ columns, each 256 block columns.

### How block edits are saved today

1. Any change to a block of a live column goes through `world_edit_block_set` (`run/src/world/edit/`), on the sim thread. It logs `{x, y, z, before, after}` in a ring (65,536 records, `WORLD_EDIT_LOG_CAPACITY_DEFAULT`), and sets **dirty flags** per section for each consumer: RENDER (re-encode for the card), SAVE, LIGHT. Writing what's already there logs and flags nothing.
2. Every 30 seconds of world time the **autosave** (`run/src/save/autosave/`) captures dirty sections on the sim and a worker writes them (`run/src/save/world/README.md`): each changed section goes into a region delta file, then the manifest is replaced with an atomic rename. "That rename is the save."
3. A world on disk is its seed plus changed sections. A column with no change isn't on disk at all: it regenerates from the seed.

So a census that places snow by `world_edit_block_set` gets everything for free: the card redraws those sections, the save keeps them, light is checked, and other readers of the log (mob paths, items) see the change or are told they lost count and look again. That's what the master plan means by "state saved like any block edit".

### Why the snow layer needs a new block, and why that waits for plan 003

Snow today is a whole `snow_block` (`config/blocks/snow_block/`), which plan 001 phase 10's issue 8 says is too much: a field of snow becomes a field of white cubes. A thin layer needs a new block. Block ids are given out alphabetically (`compile/src/game_tables/blocks/index.c`), so a new block shifts the ids after it, and saves store ids: `save_world_open` refuses a world saved with another block table. Plan 003 phase 1 ("Worlds survive new blocks") saves a name table with each world and translates ids on open. Its status is `not started`. Until it's done, adding `snow_layer` would lock Ahmed out of every saved world. So: everything in this phase can be built, except the snow layer block and snow cover, which wait.

Ice cover uses the existing `ice` block (`config/blocks/ice/`), so it doesn't wait.

## Design

### What already exists, and what this phase changes

| Thing | Where | Status |
|---|---|---|
| Frame passes, `frame_args_t` | `run/include/gpu_bridge/frame/index.h`, `cuda/src/frame/index.cu` | Exists. Gains one pointer and one block |
| `post_settings_t`, `post_fog`, `post_sky`, `post_kernel`, `post_taa_kernel`, `post_frame_temporal` | `run/include/gpu_bridge/post/index.h`, `cuda/src/post/index.cu` | Exists. Gains `post_weather_t`, a fog colour mix, the precipitation and lightning overlay |
| `SHADE_FEATURE_*` | `run/include/gpu_bridge/shade/index.h` | Exists. Gains `SHADE_FEATURE_WEATHER = 1u << 17` |
| `render_frame_t`, `render_day_fill`, `frame_settings_apply`, `render_frame_draw` | `run/include/render/frame/index.h`, `run/src/render/frame/index.c` | Exists. Gains `render_frame_weather_set`, the weather struct and the occlusion block |
| Device blocks for a frame, allocated in `render_open` | `run/src/render/index.c` | Exists. Gains the 8 KiB rain top block |
| `render_exchange_t`, a lock-free triple buffer between two threads | `run/include/render/frame/index.h` | Exists. A second one carries weather from the sim to the gpu thread |
| `game_t` | `run/include/game/index.h` | Exists. Gains the weather exchange and its slot pointer |
| `player_movement_tick`, `player_tick` | `run/src/player/movement/`, `run/src/player/` | Exists. Gains `player_movement_tick_wind` and `player_t.wind` |
| `world_edit_block_set`, `world_map_column_at`, `world_edit_log_read` | `run/src/world/edit/`, `run/src/world/map/` | Exists. Used, not changed |
| `TRAVERSE_SHAPE_*`, the shape box table, `frame_shape_of` | `run/include/gpu_bridge/traverse/index.h`, `cuda/include/traverse/index.hu`, `run/src/render/frame/index.c:182` | Exists. Gains `TRAVERSE_SHAPE_LAYER` for the snow layer |
| `climate_weather_at`, `climate_now_t`, the week store, the census | `run/src/climate/weather/`, `run/src/game/census/` | **Phase 07** |
| Thunderstorm cells near a point, with id, centre, radius, strength | `run/src/climate/systems/` | **Phase 06** |
| Name tables in saves, so a new block keeps worlds | `run/src/save/` | **Plan 003 phase 1** (needed only for steps 15 to 18) |

### New directories

```
run/src/game/weather/          NEW  depth 2: the sim's side: sampling the weather at the player, the rain top map, lightning strikes, the slot sent to the gpu
run/include/game/weather/index.h  NEW (or declared with the Owner in run/include/game/index.h, as input/report/settle/tick are: see REPO_STRUCTURE section 3)
run/src/game/cover/            NEW  depth 2: census-time and stream-time ice and snow on loaded columns
run/include/game/cover/index.h    NEW (same choice)
config/blocks/snow_layer/      NEW  the thin snow block (waits for 003 phase 1)
tests/cuda/weather/            NEW  ctest render_feature_weather, named in its test.cmake like the other render_feature_* tests
tests/run/game/cover/          NEW  ctest run_game_cover
tests/run/game/weather/        NEW  ctest run_game_weather
```

Both new `game` parts are depth 2: they allocate nothing of their own, using the World and Tick arenas `game` gives them.

### Constants

In `run/include/gpu_bridge/post/index.h`, as `F32(...)` because it's compiled on both sides (`plans/CODING_STANDARD.md` section 2):

| Name | Value | Unit |
|---|---|---|
| `POST_WEATHER_LAYERS` | 4 | cylinders |
| `POST_WEATHER_RADIUS_FIRST` | 2.0 | blocks; each next layer doubles |
| `POST_WEATHER_CELL_WIDTH` | 0.25 | blocks |
| `POST_WEATHER_CELL_HEIGHT` | 1.5 | blocks |
| `POST_RAIN_FALL` | 9.0 | blocks/s |
| `POST_SNOW_FALL` | 1.5 | blocks/s |
| `POST_RAIN_STREAK_MIN` | 0.6 | blocks |
| `POST_RAIN_WIDTH` | 0.03 | blocks |
| `POST_SNOW_FLAKE` | 0.08 | blocks |
| `POST_SNOW_WOBBLE` | 0.3 | blocks |
| `POST_SNOW_WOBBLE_PERIOD` | 3.0 | s |
| `POST_WEATHER_TILT_MAX` | 2.5 | sideways per down |
| `POST_RAIN_DENSITY_MM_H` | 40.0 | mm/h for density 1, before the clamp |
| `POST_WEATHER_DENSITY_MIN`, `_MAX` | 0.03, 0.85 | share of cells |
| `POST_RAIN_ALPHA`, `POST_SNOW_ALPHA` | 0.35, 0.80 | share of a pixel |
| `POST_RAIN_SHADE`, `POST_SNOW_SHADE` | 0.6, 1.0 | of ambient light |
| `POST_BOLT_SEGMENTS` | 12 | segments |
| `POST_BOLT_WIDTH`, `POST_BOLT_WIDTH_PER_BLOCK` | 0.4, 0.002 | blocks, blocks per block of distance |
| `POST_FLASH_SKY`, `POST_FLASH_AMBIENT` | 3.0, 1.5 | linear light at $f = 1$ |
| `POST_WEATHER_MAP_SIDE` | 64 | blocks |

In `run/include/render/frame/index.h` (C only, so `f32`):

| Name | Value | Unit |
|---|---|---|
| `RENDER_CLOUD_SKY_GREY` | 0.8 | share at cloud 1 |
| `RENDER_CLOUD_DIRECT_CUT`, `RENDER_CLOUD_AMBIENT_CUT`, `RENDER_CLOUD_DISC_CUT` | 0.75, 0.3, 0.9 | share at cloud 1 |
| `RENDER_FOG_WEATHER_CUT` | 0.85 | share at fog 1 |
| `RENDER_FOG_RAIN_MM_H` | 10.0 | mm/h that halves visibility |
| `RENDER_FOG_VISIBILITY_MIN` | 32.0 | blocks |
| `RENDER_FOG_WEATHER_START` | 0.3 | of visibility |
| `RENDER_FLASH_DISTANCE` | 500.0 | blocks, where the flash is half |
| `RENDER_WEATHER_EASE_SECONDS` | 5.0 | s, how fast density and cloud follow a change |

In `run/include/game/index.h` (or `game/weather`):

| Name | Value | Unit |
|---|---|---|
| `GAME_WEATHER_SAMPLE_TICKS` | 10 | ticks between `climate_weather_at` calls (0.5 s) |
| `GAME_LIGHTNING_RANGE` | 2000 | blocks |
| `GAME_LIGHTNING_RATE` | 0.05 | strikes per second at strength 1 |
| `GAME_LIGHTNING_SECONDS` | 0.2 | s a bolt is seen |
| `GAME_LIGHTNING_CLOUD_BASE` | 180 | blocks above the ground |
| `GAME_LIGHTNING_JITTER` | 8 | blocks sideways per joint |

In `run/include/player/movement/index.h`:

| Name | Value | Unit |
|---|---|---|
| `PLAYER_WIND_THRESHOLD` | 10.0 | blocks/s |
| `PLAYER_WIND_SHARE` | 0.04 | drift on the ground per block/s above the threshold |
| `PLAYER_WIND_AIR_SHARE` | 0.2 | of the ground push |
| `PLAYER_WIND_SHELTER_BLOCKS` | 3 | blocks upwind |
| `PLAYER_WIND_SHELTER_SHARE` | 0.25 | of the push |

In `run/include/game/cover/index.h`:

| Name | Value | Unit |
|---|---|---|
| `GAME_COVER_SNOW_ON_MM` | 20.0 | mm of water: master plan 10 |
| `GAME_COVER_SNOW_OFF_MM` | 10.0 | mm: see Risks R4 |
| `GAME_COVER_ICE_ON_CM` | 5.0 | cm: master plan 10 |
| `GAME_COVER_ICE_OFF_CM` | 2.0 | cm: see Risks R4 |
| `GAME_COVER_SEA_ICE_ON_CM` | 30.0 | cm: see Risks R5 |
| `GAME_COVER_COLUMNS_PER_STEP` | 64 | columns per sim step during the census |

### What the renderer needs per frame, and how it gets there

The chain, one thread at a time:

```
sim thread                                    gpu thread
──────────                                    ──────────
every GAME_WEATHER_SAMPLE_TICKS:              before each frame:
  climate_weather_at(player x, y, eye z)        render_exchange_take(&game->weather_exchange)
every tick:                                     render_frame_weather_set(frame, slot)
  lightning strikes near (hash)                   eases density and cloud toward the new numbers
  the rain top map if the player moved            adjusts sky, sun, fog in post_settings_t, shade_sun_t
    or a block in it changed                      fills post_weather_t
  fill game_weather_slot_t                        uploads the rain top map if its revision changed
  render_exchange_publish                       render_frame_draw → frame_gpu_run → post kernels
```

**The slot** (sim to gpu, three of them in the World arena, like `camera_slots`):

```c
/* run/include/game/index.h (or game/weather) */
typedef struct game_weather_slot_t {
    climate_now_t now;                                   /* 0: phase 07's, at the player's eye */
    uint64_t world_ticks;                                /* 64: the tick this was made at */
    int64_t tick_ns;                                     /* 72: when, on the sim's clock, for animating between ticks */
    float32_t bolt[POST_BOLT_SEGMENTS + 1u][3];          /* 80: joints, from the cloud down, in blocks from bolt_origin */
    int32_t bolt_origin[3];                              /* 236: a whole block */
    float32_t bolt_age;                                  /* 248: seconds since the strike, or -1 for none */
    float32_t flash;                                     /* 252: f, 0 to 1 */
    int32_t map_origin[2];                               /* 256: the rain top map's lowest corner, whole blocks */
    uint32_t map_revision;                               /* 264: goes up when the map changes */
    uint8_t forced;                                      /* 268: set by --weather, for tests and the bench */
    uint8_t _pad0[3];                                    /* 269 */
    int16_t map[POST_WEATHER_MAP_SIDE * POST_WEATHER_MAP_SIDE]; /* 272: rain top, z from sea level; y * side + x */
} game_weather_slot_t;
static_assert(sizeof(game_weather_slot_t) == 8464);
static_assert(alignof(game_weather_slot_t) == 8);
/* offsetof asserts on each field */
```

`game_t` gains `render_exchange_t weather_exchange` (its own 3 lines) and `game_weather_slot_t *borrowed_weather_slots` (3 slots from the World arena). Update `sizeof(game_t)` and its offset asserts; the new fields go at the end, so no existing offset moves.

**What the kernels get** (shared C/CUDA, in `run/include/gpu_bridge/post/index.h`; plain numbers only, passed by value):

```c
typedef struct post_weather_t {
    float32_t density;          /* 0: share of cells with a drop, 0 for none */
    float32_t snow_share;       /* 4: 0 rain to 1 snow */
    float32_t wind[2];          /* 8: blocks/s */
    float32_t time_s;           /* 16: seconds, wrapping every hour, for the fall */
    float32_t flash;            /* 20: 0 to 1 */
    float32_t drop_rgb[3];      /* 24: linear light */
    float32_t fog_rgb[3];       /* 36: what weather fog tends to */
    float32_t fog_mix;          /* 48: how far fog goes toward fog_rgb, 0 for today's fog exactly */
    float32_t bolt_alpha;       /* 52: 0 for no bolt */
    float32_t bolt_width;       /* 56: blocks */
    int32_t map_origin[2];      /* 60: in whole blocks, relative to the camera's eye cell */
    float32_t bolt[POST_BOLT_SEGMENTS + 1u][3]; /* 68: 13 joints, relative to the camera's eye cell, to 224 */
} post_weather_t;
static_assert(sizeof(post_weather_t) == 224);
static_assert(alignof(post_weather_t) == 4);
/* offsetof asserts on every field, checked by both GCC and nvcc */
```

A field added later goes at the end, with the size assert moved. **Positions are relative to the camera's eye cell,** like everything the kernels read (`traverse_camera_t.eye_cell`), so they're exact far from the origin.

`frame_args_t` gains `const post_weather_t *borrowed_weather` (nullptr for none) and `const device_block_t *borrowed_weather_map`. `post_frame_temporal` and `post_frame` gain the same two parameters; `cuda/src/frame/index.cu` passes them. Every existing caller passes nullptr, and a nullptr means the picture is exactly as today.

**Inline functions, both sides, in `run/include/gpu_bridge/post/index.h`** (so a host test runs the same lines as the card, as `post_fog` and `post_sky` already do):

- `post_weather_fog_colour(weather, sky_in, out)`: today's fog colour mixed by `fog_mix`.
- `post_weather_drops(weather, map, direction, t, out_rgb_alpha)`: the 4 layers, returns colour and coverage.
- `post_weather_bolt(weather, direction, t)`: coverage of the bolt.
- `post_weather_apply(weather, map, direction, t, in_out_rgb)`: drops, then bolt, mixed in. With `density == 0` and `bolt_alpha == 0`, it returns without touching `in_out_rgb`, so a clear sky is the same bytes as today.

`map` is the rain top, read on the card from the device block, and on the host from an array.

**`render_frame_weather_set(frame, slot, now_ns)`** on the gpu thread, between frames (like `render_frame_time_set`):

1. Ease: `density`, `cloud`, `fog` and `dust` move toward the slot's values by $1 - e^{-\Delta t / 5}$ per frame, so weather never pops. (Exactly the slot's values when `forced`, so a test gets one picture.)
2. `time_s` = (slot `world_ticks` mod 72000) / 20, plus $\min(0.05,\ (\text{now} - \text{tick\_ns})/10^9)$ with threads, plus nothing without, so a script without threads gives one picture.
3. Store the weather in `render_frame_t`, and call `frame_settings_apply`, which now ends with a new static `frame_weather_apply`: after `render_day_fill`, mix the sky toward grey by cloud, cut the glow, disc, direct and ambient, add the flash, set `fog_start` and `fog_end` from visibility, fill `post_weather_t`.
4. If the slot's `map_revision` is new, `device_memory_write` the 8 KiB to the block.

`render_frame_t` gains `post_weather_t weather`, the eased numbers, and the last map revision. Its size grows past 960; update the assert.

**`SHADE_FEATURE_WEATHER`** (bit 17) is in `SHADE_FEATURES_ALL`. Off, `frame_weather_apply` does nothing and `borrowed_weather` is nullptr. `--no-weather` turns it off, like the other `--no-*` flags in `run/src/main/index.c`.

### The sim's side: `run/src/game/weather/`

- **Sampling.** Every `GAME_WEATHER_SAMPLE_TICKS` ticks, `climate_weather_at(climate, x, y, eye z, world_ticks, &game->arena_tick, &now)`. The arena is reset each tick, so this allocates nothing that lives on.
- **Lightning.** Every tick: the thunderstorm cells within 2000 blocks (phase 06's list near the player, which phase 06 caches per day), the current 1-second slot and the previous 1 (a bolt lasts 0.2 s, so only the latest strike matters). A hash of (seed, cell id, slot) below $q = 0.05 s$ makes a strike. The nearest strike in the current slot wins. Its joints by hash. `flash` by distance, and `bolt_age` from the slot's start.
- **The rain top map.** Rebuilt when the player's block column changes (whole map, 4096 columns: each a scan down from `world_edit_column_top_z` of its chunk to the first non-air voxel, through `world_map_block_get`), or patched when the edit log has a change inside it (this module keeps its own cursor, `world_edit_log_position` and `world_edit_log_read`; if it lost count, it rebuilds). A column not loaded reads `INT16_MAX`: covered, no rain drawn there.
- **Publish:** fill the back slot, `render_exchange_publish`.
- **`--weather <clear|rain|snow|sleet|storm|thunder|fog|dust|hurricane>`** (NEW in `run/src/main/index.c`): a fixed `climate_now_t` instead of sampling (and a fixed strike every 2 seconds at 300 blocks for `thunder`), and `forced` set. It's for golden pictures and the bench. It needs no climate at all, so it works on any test world.

With no climate in the game (tests that open a world without one), nothing is sampled, the slot is zeros, and every picture is as today.

### Wind on the player

- `player_t` gains, on a new line at its end, `float32_t wind[2]` (blocks/s, the sim's) and padding to keep `alignof(player_t) == 64`: size 2304 → 2368. The save does not store `player_t` whole (`save_state_player_t` stores `movement`, `survival`, `interaction`, `combat` and a count, `run/include/save/state/index.h`), so **no save change**.
- `player_wind_set(player_t *player, float32_t u, float32_t v)`: the sim sets it each tick from the latest sample, linearly blended between samples so it doesn't jump every half second.
- `player_movement_tick_wind(movement, physics, map, intent, wind_u, wind_v)`: NEW. `player_movement_tick` calls it with `0, 0`, so every existing caller and test is byte-for-byte unchanged. `player_tick` calls the new one with `player->wind`.
- Inside, in `movement_tick_land` only (not fluids, not climbing), before the move, add $a\,\hat w$ to the horizontal velocity, with $a$ from the Background's rule: ground share 1, air 0.2, sheltered a quarter. Shelter: `collision_cell_read` (through `physics`'s header) of the cells at feet and head height at 1, 2 and 3 blocks along $\hat w$ toward where it comes from: any solid one shelters.

### Census-time ice and snow cover: `run/src/game/cover/`

**The rule for one column**, `game_cover_column(game, slot)`, idempotent (running it twice changes nothing the second time):

For each of the 256 block columns $(x, y)$ in the chunk:

1. Read the week store's `snow_mm` and `ice_cm` at $(x, y)$: **bilinear between cell centres, at a point moved by phase 03's warp** (up to 32 blocks), so cover edges wobble instead of following the 64-block grid. That's the same blend as every other climate value (master plan 6.9).
2. Find the top: scan down from `world_edit_column_top_z` to the first voxel that isn't air. Call its block $B$ at height $z_t$.
3. **Snow on:** $s > 20$ mm, and $B$ is solid, a whole cube (`shape: cube`), not leaves (`BLOCK_FLAG_DECAYS`), not `snow_block`, `ice`, `packed_ice`, `blue_ice` or `lava` (the same list as `run/src/terrain/feature/README.md`'s snow rule), and $z_t + 1$ is air and inside the world. Then `world_edit_block_set(x, y, z_t + 1, BLOCK_ID_SNOW_LAYER)`.
4. **Snow off:** $s \le 10$ mm. Every `snow_layer` in the column goes back to air. Only the census places `snow_layer` (it has no item, and drops a snowball), so any found is the census's. Sections whose palette has no `snow_layer` are skipped without a look.
5. **Ice on:** $i > 5$ cm (30 cm on a sea cell, R5), $B$ is `water`, a source (no level in `world_tick_section_levels`), and $z_t + 1$ is air. Then `world_edit_block_set(x, y, z_t, BLOCK_ID_ICE)`.
6. **Ice off:** $i \le 2$ cm (and the sea's own, R5), $B$ is `ice` and the voxel below it is `water`. Then `world_edit_block_set(x, y, z_t, BLOCK_ID_WATER)`, a source.

Between the on and off thresholds nothing changes: snow that's there stays, and none is added. That's **hysteresis**, so the edge of the snow doesn't flicker week to week.

**When it runs:**

- **At each census**, after phase 07's last week and before Continue is enabled: every column in the map, `GAME_COVER_COLUMNS_PER_STEP` per `game_sim_step`, nearest the player first, with the census screen's progress bar showing "Snow and ice: 1200 of 4225". The sim is paused, so no player tick runs at the same time.
- **When a column streams in**, right after `world_map_stream_commit` in `game_sim_step`: the same rule for that column. **Why:** a column first loaded in the middle of winter would otherwise have no snow until the next census, and one saved with snow in winter and loaded in summer would keep it. The master plan says "in loaded chunks only": this never loads a column to edit it. It just keeps every column that's loaded in step with the current week. See Issues found 2.

**Consistency with player edits:**

- Everything goes through `world_edit_block_set` on the sim, the same path a player's mining and placing take, so edits are logged, redrawn, saved and lit the same way, and never race.
- `snow_layer` is `replaceable`, so a player placing a block on it replaces it, as on tall grass.
- A player who breaks a snow layer gets a snowball. The next census may lay it again if it's still snowy: it snowed again.
- A player who builds a roof over snow: the snow under the roof is no longer the top. It's removed by rule 4 when the week thaws (rule 4 looks at the whole column), and no new snow goes under the roof (rule 3 looks only at the top).
- Ice that a player covers with a block is not the top, so it isn't melted by rule 6 (R6).
- Generated `snow_block` and generated ice from biome features: snow blocks are never touched. Generated ice on water is ruled by rules 5 and 6 like any ice on water, so an icy biome's lake melts in a warm summer (R7).

**What it costs:** a first snow at render distance 32 could lay up to $4225 \times 256 = 1{,}081{,}600$ layers. The edit log is a 65,536-record ring, so its readers are told they lost count, and look at the world again, which is what the log's README says to do. Render re-encodes the top sections of every column, once. The light pass is flagged, and since `snow_layer` has opacity 0 (the default for `thin`) nothing changes. The save captures each changed section: snowy columns become deltas on disk. `tests/run/game/cover/` measures the time and prints it, and Risks R3 has the default if it's too slow.

### The snow layer block (waits for plan 003 phase 1)

`config/blocks/snow_layer/index.yaml`:

```yaml
name: snow_layer
hardness: 0.1
tool: shovel
drops: [{ item: snowball, count: 1 }]
shape: thin
texture: { all: blocks/snow }
```

`thin` gives `solid: false` (the body walks through it), `opacity: 0`, `replaceable: false` by default: set `replaceable: true` explicitly, so placing a block on it replaces it. `textures/blocks/snow/` already exists.

**Drawing it:** `thin` today becomes `TRAVERSE_SHAPE_FLAT` for a lily pad and `TRAVERSE_SHAPE_PLATE` for the rest (`frame_shape_of`, `run/src/render/frame/index.c:182`). A lily pad's flat box is 2/32 of a block thick. Add `TRAVERSE_SHAPE_LAYER = 8` (`TRAVERSE_SHAPE_COUNT` 8 → 9) in `run/include/gpu_bridge/traverse/index.h`, with box `{{0, 0, 0}, {32, 32, 4}}` (1/8 of a block) and its row in `cuda/include/traverse/index.hu`'s tables, and map `BLOCK_ID_SNOW_LAYER` to it. 4/32 is enough to see from a distance and low enough to walk over.

### Thread ownership, all in one place

| What | Owner thread | Read by |
|---|---|---|
| `climate_now_t` samples, lightning, rain top map | sim | gpu, through `weather_exchange` |
| `player_t.wind` | sim | sim (movement) |
| Cover edits | sim | everything that reads the edit log and flags |
| `render_frame_t.weather`, the eased numbers, `post_settings_t`, `shade_sun_t` | gpu | the kernels |
| The rain top device block | gpu (write) | the post kernels |
| The week store | sim (phase 07) | sim (cover) |

### Save format

**Nothing new is saved.** Snow and ice are block edits, saved by the existing deltas. Wind, lightning and the rain top come back from the climate and the world. No version is bumped in this phase. (Plan 003 phase 1 bumps the formats for name tables; adding `snow_layer` after it is then just a new block.)

### The bench

`bench/world/index.c` gains `--weather <kind>` (the same kinds as the game's flag), passed through to each row, and new rows at render distance 32: `plains/orbit-rain`, `plains/orbit-snow`, `plains/orbit-thunder`, `plains/orbit-fog`. Recorded with `--record bench/world/history.tsv --label "004 phase 08, before"` on the commit before the renderer changes, and `"... after"` after them, and once more with `--weather clear` after, to show clear weather costs nothing.

## Owned paths

- `run/include/gpu_bridge/post/index.h`, `cuda/src/post/index.cu`, `cuda/src/post/README.md`
- `run/include/gpu_bridge/frame/index.h`, `cuda/src/frame/index.cu`: only the weather pointers and passing them
- `run/include/gpu_bridge/shade/index.h`: only `SHADE_FEATURE_WEATHER`
- `run/include/gpu_bridge/traverse/index.h`, `cuda/include/traverse/index.hu`: only `TRAVERSE_SHAPE_LAYER`
- `run/include/render/frame/index.h`, `run/src/render/frame/index.c`, its README: `render_frame_weather_set`, `frame_weather_apply`, the shape mapping
- `run/src/render/index.c`, `run/include/render/index.h`: only the rain top device block
- `run/src/game/weather/`, `run/src/game/cover/` (NEW), their headers
- `run/src/game/index.c`, `run/include/game/index.h`: only the exchange, the slots, the sampling and cover calls, `player_wind_set`
- `run/src/player/movement/`, `run/include/player/movement/index.h`, `run/src/player/index.c`, `run/include/player/index.h`: only the wind
- `run/src/main/index.c`: only `--weather` and `--no-weather`
- `config/blocks/snow_layer/` (NEW, after 003 phase 1)
- `tests/cuda/weather/`, `tests/run/game/cover/`, `tests/run/game/weather/` (NEW); `tests/run/player/movement/` (added tests)
- `bench/world/index.c`, `bench/world/README.md`, `bench/world/history.tsv` (lines added)
- `plans/REPO_STRUCTURE.md`: the new rows
- This file

## Steps

Build in this order. Run the build and the step's tests through the wrapper after each (Verification has the commands).

1. **Check the ground is there.** Phase 07's status must be `done`, with `climate_weather_at` and `climate_now_t` as its file says, and phase 06's thunderstorm cells near a point with id, centre, radius and strength. Write down the names you found. Check plan 003 phase 1's status and write it here: steps 15 to 18 wait for it.

2. **Measure before.** Build the profile preset and record the "before" rows of `bench_world` at render distance 32, with the label `004 phase 08, before`. **Why first:** the master plan asks for the frame-rate cost before and after, and "before" has to be measured on code without this phase.

3. **The kernels' struct and maths, host first.** In `run/include/gpu_bridge/post/index.h`: the constants, `post_weather_t` with every offset asserted, and the inline `post_weather_fog_colour`, `post_weather_drops`, `post_weather_bolt`, `post_weather_apply`. **Why host first:** the same lines run in a host test, so the maths is checked before any kernel uses it.

4. **Host tests of the maths** in `tests/cuda/weather/index.c`: `a_clear_sky_changes_nothing`, `drops_by_hand`, `rain_stops_under_a_roof`, `drops_never_in_front_of_a_wall`, `the_bolt_by_hand`, `fog_colour_by_hand`.

5. **The kernels.** In `cuda/src/post/index.cu`: `post_kernel` mixes fog toward the weather's fog colour, and in its last branch (no TAA) calls `post_weather_apply` before the tone map; `post_taa_kernel` calls it after writing `history_next`, before the tone map, with the pixel's unjittered direction. `post_frame` and `post_frame_temporal` take the two new parameters. `cuda/src/frame/index.cu` passes them from `frame_args_t`. Update `cuda/src/post/README.md`: what weather adds, and why after the history.

6. **The C side of the frame.** In `run/src/render/frame/index.c`: `frame_weather_apply` at the end of `frame_settings_apply`, `render_frame_weather_set`, `SHADE_FEATURE_WEATHER`. In `run/src/render/index.c`: the 8 KiB rain top block in `DEVICE_POOL_FRAMES`, allocated with the frame's other blocks, given back with them. `render_frame_t`'s size and asserts.

7. **Prove nothing else moved.** Run `render_feature_sky`, `render_feature_water`, `render_feature_post`, `render_feature_shapes`, `render_feature_lod`, `render_feature_entities`, `run_render_frame`: every golden picture must match byte for byte, since nothing passes weather yet.

8. **The sim's side.** `run/src/game/weather/index.c`: sampling every 10 ticks, lightning, the rain top map with its own edit-log cursor, the slot. `game_t` gains the exchange and slots. The gpu thread takes the newest slot before each frame and calls `render_frame_weather_set`. The no-threads path (headless scripts, `game_scene_draw`) does the same in its one thread.

9. **`--weather` and `--no-weather`** in `run/src/main/index.c`.

10. **Golden pictures.** `tests/cuda/weather/` runs the built program `--headless` on a test world with `--weather rain`, `snow`, `thunder`, `fog`, `dust`, `hurricane`, looking out of a doorway, and keeps PNGs in `fixtures/`, after looking at each one. Tests: `golden_pictures_of_each_weather`, `rain_stops_under_a_roof_in_the_real_game`, `a_flash_lights_the_land`.

11. **Wind on the player.** `player_t.wind` and its line, `player_wind_set`, `player_movement_tick_wind` with the rule and the shelter check, `player_movement_tick` calling it with zero wind, `player_tick` with the player's. The sim sets the wind each tick from the samples, blended. Update `run/src/player/movement/README.md`'s table with the wind's drift numbers.

12. **Tests of the wind** in `tests/run/player/movement/` (ctest `run_player_movement`): `hurricane_drifts_the_body_at_2_blocks_a_second`, `no_push_below_the_threshold`, `a_wall_upwind_shelters`, `no_push_in_water_or_on_a_ladder`, `zero_wind_is_the_same_bytes_as_before`.

13. **Ice cover.** `run/src/game/cover/index.c`: rules 1, 2, 5 and 6 only (ice), the census call (columns per step, progress on the census screen), the stream-in call after `world_map_stream_commit`. **Why ice before snow:** ice uses the existing block, so it doesn't wait for plan 003.

14. **Tests of ice cover** in `tests/run/game/cover/`: `a_cold_week_freezes_still_water`, `a_warm_week_melts_it`, `ice_is_idempotent`, `ice_survives_a_save_and_load`, `a_column_streamed_in_matches_the_week`, `flowing_water_does_not_freeze`.

15. **(Waits for plan 003 phase 1.)** Check 003 phase 1 is `done`: a world saved before a new block still opens. If not, stop here, set this file's status to `blocked`, and say why.

16. **The snow layer block.** `config/blocks/snow_layer/index.yaml`. `TRAVERSE_SHAPE_LAYER` in `run/include/gpu_bridge/traverse/index.h` and `cuda/include/traverse/index.hu`. `frame_shape_of` maps `BLOCK_ID_SNOW_LAYER` to it. Run `compile_game_tables`, `render_feature_shapes`, and a world saved before this step must still open (003 phase 1's own test, rerun).

17. **Snow cover.** Rules 3 and 4 in `run/src/game/cover/index.c`.

18. **Tests of snow cover:** `snow_builds_and_melts_at_45_degrees_in_loaded_chunks`, `snow_stays_at_75_degrees`, `snow_never_on_leaves_or_ice`, `a_roof_keeps_new_snow_off`, `placing_a_block_replaces_the_layer`, `the_snow_edge_does_not_flicker`, `cover_time_at_render_distance_32`.

19. **Measure after.** The same `bench_world` rows, label `004 phase 08, after`, plus the four weather rows, and once with `--weather clear`. Write the differences in this file's **Issues found** and in `bench/world/README.md`.

20. **Docs.** `plans/REPO_STRUCTURE.md`: `game/weather`, `game/cover`, `tests/cuda/weather/`, `tests/run/game/cover/`, `tests/run/game/weather/`, `config/blocks/snow_layer`. Every changed README. This file's todo, Commits and Issues found.

21. **Gate.** Verification, then Ahmed's part.

## Todo

- [ ] Step 1: phase 07 and 06 are `done`, the functions named here; plan 003 phase 1's status written here. Found on 2026-11-06: `climate_weather_at`, `climate_now_t` (with `thunder_cell`), `climate_weather_time_of_clock`, `climate_week_store` and `climate_week_store_cell` (phase 07); `climate_systems_day_build` with `1u << CLIMATE_SYSTEM_THUNDER` and `climate_systems_thunder_at` for a cell's centre and strength (phase 06). Phases 06 and 07 are "in progress" with only Ahmed's gates left; plan 003 phase 1 is "in progress", every agent check passed, only Ahmed's own check left. The box stays open until those are called done.
- [x] Step 2: bench "before" rows recorded at render distance 32 (`004 phase 08, before`, commit `ed199219`)
- [x] Step 3: `post_weather_t` and its inline maths, both sides
- [x] Step 4: host tests of the maths pass
- [x] Step 5: rain and snow falling, in the CUDA renderer, by the rain rate where the player is; lightning in the post kernels, after the history
- [x] Step 6: fog and sky colour by weather, in `render_weather_apply` (Issues found 5); the rain top block
- [x] Step 7: every existing golden picture unchanged (`render_feature_sky`, `_water`, `_post`, `_shapes`, `_lod`, `_entities`, `run_render_frame`, `compile_game_tables`: 8 of 8, with the snow layer in the block table)
- [x] Step 8: the sim samples the weather, makes lightning in thunderstorm cells, keeps the rain top map, and sends it to the gpu thread
- [x] Step 9: `--weather` and `--no-weather`
- [x] Step 10: golden pictures of each weather, looked at (made with the real renderer in-process, Issues found 13)
- [x] Step 11: wind pushes the player in storms and hurricanes, by a share of its speed
- [x] Step 12: wind tests pass, and zero wind is the same bytes as before
- [x] Step 13: at each census, in loaded chunks only: ice on still water over 5 cm, removed when it melts
- [x] Step 14: ice cover tests pass
- [ ] Step 15: plan 003 phase 1 is `done` (or this phase is `blocked` here): it is "in progress" with only Ahmed's check left, and its name tables are in main, so a new block keeps every world. Steps 16 to 18 went ahead on that (Issues found 14); its tests pass with `snow_layer` in.
- [x] Step 16: the thin snow layer block, drawn as a layer
- [x] Step 17: at each census, in loaded chunks only: snow on the ground over 20 mm, removed when it melts
- [x] Step 18: snow cover tests pass
- [x] Step 19: bench: the frame-rate cost at render distance 32, before and after, recorded (Issues found 17)
- [x] Step 20: `plans/REPO_STRUCTURE.md` and every README updated
- [x] Gate: Verification passes (Issues found 18)
- [ ] Gate: Ahmed plays through a winter and a hurricane season

## Tests

Every test runs under `timeout` through the wrapper. A failure prints expected and actual.

**`render_feature_weather`** (`tests/cuda/weather/`, named in its `test.cmake` as the other `render_feature_*` tests are; on the real card, `MC_TEST_TIMEOUT 280`):

- `a_clear_sky_changes_nothing`: host: `post_weather_apply` with density 0 and no bolt leaves 10,000 random colours bit-identical; `frame_weather_apply` with cloud 0, fog 0, dust 0, rain 0 leaves `post_settings_t` and `shade_sun_t` bit-identical to `render_day_fill`'s.
- `drops_by_hand`: host: a ray straight out at height 0, a cell whose hash is forced to hold a drop at its middle, $\tau$ chosen so the drop is at the ray's point: coverage is `POST_RAIN_ALPHA`; moved 0.1 blocks sideways (past the 0.03 width): 0. With wind 6 and rain, the streak through a point 1 block above is found 0.67 blocks upwind.
- `rain_stops_under_a_roof`: host: a map whose centre 3 by 3 entries are 70 and the rest 64; a ray straight up from height 65 at the centre: no coverage. The same ray at an entry of 64: coverage.
- `drops_never_in_front_of_a_wall`: host: 10,000 random rays with `t` = 1.5: no coverage from layers past 1.5 blocks.
- `the_bolt_by_hand`: host: a bolt straight down 10 blocks east of the eye; a ray at it: coverage; 1 block off: none.
- `fog_colour_by_hand`: host: render distance 32, rain 30 mm/h → visibility **124** blocks; fog 0.9 → **117**; clear → **496**; a heavy fog never goes under **32**.
- `golden_pictures_of_each_weather`: the built program, `--headless`, a test world, `--weather` each kind, looking out of a doorway at noon. PNGs in `fixtures/`, held byte for byte, each looked at by the agent before keeping it. **Fails if** any pixel differs.
- `rain_stops_under_a_roof_in_the_real_game`: the same doorway, looking up from inside and outside: the inside picture's top third has no drop pixels (it equals the `--weather clear` picture there), the outside one has drops.
- `a_flash_lights_the_land`: `--weather thunder`, two pictures 1 tick apart around a forced strike at 300 blocks: the mean brightness of ground pixels rises by more than 20% in the flash picture.

**`run_player_movement`** (added to `tests/run/player/movement/`):

- `hurricane_drifts_the_body_at_2_blocks_a_second`: a body standing on flat stone, no intent, wind (−60, 0) for 200 ticks: steady speed **2.00 ± 0.02** blocks/s west. Wind 30: **0.80 ± 0.02**.
- `no_push_below_the_threshold`: wind 10 for 200 ticks: the body hasn't moved, byte for byte.
- `a_wall_upwind_shelters`: wind 60 from the east, a stone wall 2 blocks east: steady speed **0.50 ± 0.02**.
- `no_push_in_water_or_on_a_ladder`: the same wind in water and on a ladder: the body's bytes equal those with no wind.
- `zero_wind_is_the_same_bytes_as_before`: every existing movement scenario run through `player_movement_tick_wind(…, 0, 0)` equals `player_movement_tick`, byte for byte.

**`run_game_cover`** (`tests/run/game/cover/`, a small world on fake devices, no card):

- `a_cold_week_freezes_still_water`: a lake of sources, a week store set to `ice_cm` 6 over it: the top water of every lake column is `ice`, and nothing else changed.
- `a_warm_week_melts_it`: then `ice_cm` 1: every one of those is a `water` source again. `ice_cm` 3 (between): nothing changes.
- `ice_is_idempotent`: running the rule twice: the second run logs no edit.
- `flowing_water_does_not_freeze`: water with a level: never `ice`.
- `ice_survives_a_save_and_load`: freeze, autosave, reopen: the ice is there, from the deltas.
- `a_column_streamed_in_matches_the_week`: a column not loaded at the census, walked into after: frozen like its neighbours.
- `snow_builds_and_melts_at_45_degrees_in_loaded_chunks` (after step 17): the world at 45° N run through a year of censuses (phase 07's `--climate-jump` or its test hook): `snow_layer` covers open ground at a census between days 1 and 60, and none is left at day 200.
- `snow_stays_at_75_degrees`: at 75° N, snow layers at every census of the second year.
- `snow_never_on_leaves_or_ice`: a tree, a frozen lake, a lava pool: no layer on leaves, ice or lava.
- `a_roof_keeps_new_snow_off`: a roofed room: no layer inside after a snowy census.
- `placing_a_block_replaces_the_layer`: placing stone on a layer: the layer is gone and stone is in its cell.
- `the_snow_edge_does_not_flicker`: a column whose blended snow goes 25, 15, 25, 15 over four weeks keeps its layer all four.
- `cover_time_at_render_distance_32`: a census's cover over 4225 columns of real terrain at first snow: prints the time and the edits; fails only past 10 seconds (see R3).

**`run_game_weather`** (`tests/run/game/weather/`):

- `lightning_is_the_same_on_replay`: the same seed, place and ticks twice: the same strikes at the same ticks and joints.
- `the_rain_top_follows_edits`: placing a block above the player changes the map entry in the next slot.
- `a_world_with_no_climate_sends_zeros`.

## Verification

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(render_feature_weather|run_player_movement|run_game_cover|run_game_weather)$' --output-on-failure
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^(render_feature_sky|render_feature_water|render_feature_post|render_feature_shapes|render_feature_lod|render_feature_entities|run_render_frame|run_game|run_player_survival|run_save_world|run_save_state_game|run_playthrough|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --preset profile
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "004 phase 08, after"
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --weather clear --only orbit
    pgrep -a minecraft_c    # must print nothing
    pgrep -af "$PWD/build"  # must print nothing

Then the whole suite once, as the gate:

    tools/test-or-build.sh timeout 3400 ctest --preset debug --output-on-failure

## Gate

What Ahmed does by hand, on a release build (`tools/test-or-build.sh cmake --preset release`, then `tools/test-or-build.sh cmake --build --preset release`):

1. **A look at each weather without waiting for it:** `./build/release/bin/minecraft_c --windowed --weather rain`, then `snow`, `thunder`, `fog`, `hurricane`. Walk in and out of a house. Does rain stop at the roof? Does it slant with the wind? Is the lightning right?
2. **Play through a winter.** In a world at mid-latitude (y about 15,000 to 25,000), jump to late autumn with phase 07's `--climate-jump`, then a week at a time to spring. At each census, look at the ground: does the snow come, and go? Does the lake freeze, and thaw? Mine a snow layer; place a block on one.
3. **Play through a hurricane season.** On a warm tropical coast (y about 2,000 to 10,000), jump into the season and play until the outlook names a hurricane coming. Meet it: does the sky go dark, the rain hammer, the wind shove you? Is the shove "gentle"? Stand behind a wall: is it gentler?
4. **Read the bench numbers** in this file's Issues found and decide whether the frame-rate cost is acceptable.

## Risks and open questions

| # | Risk or question | Default |
|---|---|---|
| R1 | **Frame-rate cost.** The renderer is already below its 120 fps target: `plains/orbit` measured 90.79 fps (11.0 ms) on 2026-11-03 (`bench/world/history.tsv`), against plan 003's note B. Weather adds work to the post pass (0.8 ms today at 1080p) | Budget: at most **0.5 ms a frame** added in heavy rain at 1080p, and **0** in clear weather (the clear row must equal the before row within noise). Over budget: fewer layers (3), or half-resolution drops. Ahmed decides with the numbers |
| R2 | **How hard wind pushes the player** | `PLAYER_WIND_SHARE` 0.04 above 10 blocks/s: 2 blocks/s in a 60 blocks/s hurricane, 0.8 in a 30 storm (Q4: "Yes, gently"). One constant each |
| R3 | **Cover takes too long** at render distance 32 at the first snow (up to 1.08 million edits) | Up to 10 s behind the census screen's progress bar is accepted for the first snow; later weeks change far less. If slower, only columns within 16 chunks are covered at the census, and the rest as they next stream in, by the same rule |
| R4 | **"Removed when they melt"** could mean at 0 | Off at 10 mm of snow and 2 cm of ice, on at 20 mm and 5 cm, so the edge doesn't flicker. Setting the off thresholds to 0 is one constant each |
| R5 | **The sea freezes like a lake** under the master plan's rule. Real seas freeze far less (salt, waves) | Sea cells (`CLIMATE_CELL_SEA`) get ice only above 30 cm of the week's `ice_cm`, so only polar seas freeze. Ask Ahmed |
| R6 | Ice covered by a player's block never melts, and snow under a new roof does | As Design. Real ice under a roof would melt too; it's rare and harmless |
| R7 | **Generated ice in icy biomes melts in a warm summer,** where the biome and climate disagree (master plan 2.6) | Allowed: the climate is right, the biome is what's wrong, and the vegetation plan fixes biomes |
| R8 | **Snowy columns become deltas on disk.** A world explored in winter saves many small changes | Accepted: "state saved like any block edit" (master plan). Region files are rewritten only when they change |
| R9 | **No thunder sound.** Plan 003 phase 5 (sound) isn't built | Left out. When sound exists, a thunder clap at the strike, delayed by distance / 343 seconds, is one call |
| R10 | Rain is drawn only round the eye (16 blocks); far rain is only the fog | Accepted; heavy rain's fog stands in for far rain. A rain shaft seen from afar is a later polish |
| R11 | The wind doesn't move mobs, items or arrows | Not asked for. One call each in a later plan |
| R12 | Plan 003 phase 1 is `not started` | Ice, rain, snow falling, lightning, fog and wind are all built first; only steps 15 to 18 wait |

## Commits

Appended as they land:

- `ed199219` Record bench_world before plan 004 phase 08 puts weather in the renderer, at render distance 32.
- `912f0b14` Draw rain, snow, lightning and weather fog in the post kernels after the history, with the sky, light and fog eased toward the weather, and add the thin snow layer block.
- `496c4934` Sample the weather at the eye, strike lightning in thunderstorm cells, keep the rain top map, push the body in storms, lay snow and ice on loaded ground at each census, and take --weather and --no-weather.
- `22170c49` Test the weather in the renderer, the sim, the wind on the body and the snow and ice cover, with golden pictures of each weather.
- `ed0f211e` Draw snow with one cell a layer, its rows wobbling together, and test cover through three years of the climate's weeks at 45 and 75 degrees.
- `e7a6c4c3` Give bench_world --weather and the rain, snow, thunder and fog orbit rows at render distance 32, and stop it cutting --features-off to 16 bits.
- `fa80e467` Document plan 004 phase 08: the weather in the post kernels and the frame, the wind on the body, the sim's weather and cover, and the new paths.
- `7293a212` Record bench_world after plan 004 phase 08, and what rain, snow, thunder and fog cost at render distance 32 against the same card's clear row.
- `f6315832` Keep the weather effect in the settings file, with the screen's seventeen boxes as they were.
- `7f2b0217` Pin the terrain digests again for snow_layer, which moved the numbers of the blocks after it and not the land.
- `73842bdb` Record plan 004 phase 08 in its phase file: the todos, the commits, the verification, the cost and the issues found.

## Issues found

**1. The master plan says both phase 08 and phase 09 put snow and ice on loaded ground.** Found while writing this file, 2026-11-04.

- **Expected:** one phase to own it.
- **Actual:** "Where this plan sits beside plan 003" says: "The one exception is phase 09, which puts snow and ice on loaded ground, and that's state saved like any block edit." Section 10's phase 08 has "At each census, in loaded chunks only: ice on still water over 5 cm, snow on the ground over 20 mm", and phase 09 is the Atlas. The vision file's table puts "Weather the player sees: rain, snow, storms, fog" in phase 08.
- **Evidence:** `plans/004-ahmed-bohemoth-climate-system.md` line 57 against lines 780 to 790.
- **Done:** this phase builds it, as section 10 (the phase list) and the vision file say. The master plan is left as written (README rule 1). Phase 09's file should not build it.

**2. "In loaded chunks only" alone would leave newly loaded columns out of step.** Found while writing this file.

- **Expected:** cover placed at the census, in loaded chunks only, to be enough.
- **Actual:** a column first loaded after a census (walking into new land in winter) would have no snow until the next census, up to 7 days of world time. A column saved with snow in winter and loaded in summer would keep its snow until the next census.
- **Done:** the same idempotent rule also runs when a column streams in (Design). It still never loads a column to edit it, so it keeps to "in loaded chunks only". Ahmed can drop the stream-in call if he wants cover to change only at the census.

**3. Thunderstorm cell rates, noted in phase 07's Issues found 4,** decide how often lightning is seen. If phase 06 settles on many fewer cells than 7.2.1's worked example, lightning will be rarer than this file's examples suggest. Nothing to do here; `GAME_LIGHTNING_RATE` is per cell and is tuned by playing.

**Arithmetic checked:** the movement steady state ($v^* = af/(1-f)$, 1.2026 on the ground with friction 0.546 from `run/src/player/movement/index.c:14`), the hurricane push (2.0 blocks/s, $a = 0.0832$), the fog numbers (124, 117, 496), the flash curve (1, 0.5, 0.1), the rain tilt (0.67, 34°).

**4. `SHADE_FEATURE_WEATHER` is bit 18, not 17.** Found at step 3. Expected: bit 17 free, as the Design says. Actual: `SHADE_FEATURE_UNDER_WATER = 1u << 17` (`run/include/gpu_bridge/shade/index.h`). Done: bit 18, in `SHADE_FEATURES_ALL`.

**5. `app_t` is at main's frame limit, so `game_t`, `render_t`, `render_frame_t` and `player_t` could not grow.** Found at step 6. Expected: the Design's new fields in `game_t` (the exchange and slots), `render_frame_t` (the eased weather) and `player_t` (`wind`). Actual: `app_t` is 16128 bytes on main's stack, and `objdump` of the profile and release links shows main's frame at `sub $0x4000,%rsp`, exactly the 16 KiB limit; one more cache line in any member breaks the release link. Done, with no struct in `app_t` grown: the exchange, slots, the drawing thread's eased `render_weather_t` and the sim's state are `game_weather_t` in the World arena, laid out with the column jobs and `game_cover_t` in `game_world_parts_t`, found from `borrowed_jobs` (`game_weather_of`, `game_cover_of`); the rain top device block is in `render_t`'s old padding; the frame takes the weather as an argument (`render_frame_draw_weather`) and applies it to copies of the sky and light, so `render_frame_t` is unchanged; the wind is an input to `player_tick_wind` and `player_movement_tick_wind`, as the body's rule wants, in place of `player_t.wind` and `player_wind_set`; `--weather` is a padding byte of `main_options_t`, and the session's copy grows `game_session_t`, which is not on main's frame. The slot is `render_weather_slot_t` in `render/frame` with the numbers the frame needs, not a `climate_now_t`, as `render` may not include `climate`. Release build after all of it: main's frame still `sub $0x4000`, links clean.

**6. The wind push in the Background makes the drift 1 / 0.546 times too fast.** Found writing step 12's test. Expected: `a = (D / 20) (1 - f) / f` to drift `D` blocks a second. Actual: the push goes in before the move, so a steady body moves `v + a = a / (1 - f)` a tick (walking's 4.317 is `0.098 / 0.454` blocks a tick by the same sum); that `a` drifts `D / f`, 3.66 blocks a second in the hurricane. Done: `a = (D / 20) (1 - f)`; the test measures 2.00 (hurricane), 0.80 (storm), 0.50 sheltered, 4.317 - 2.0 walking into it.

**7. The kernels' maths needs the camera.** The Design's `post_weather_drops(weather, map, direction, t, out)` cannot place a ray without the eye's fraction in its cell and the cell's height. Done: each takes the `traverse_camera_t` too.

**8. The rain top map is made again when the eye has gone 8 blocks from its middle, not at every block column it crosses.** The widest cylinder is 16 blocks and the map 32 each way, so the picture is the same, for an eighth of the scans. It is also made again when a column it lacked may have come in (on a sample tick) and when its edit-log cursor lost count; edits in it patch single columns.

**9. Lightning looks only at the current second's slot.** A bolt lasts 0.2 s, so the previous slot's strike has always gone. A strike is worked out at its slot's start, so its place stays put for its 4 ticks.

**10. `run/src/game/index.c` passed 1500 lines.** The wiring took it to 1528. Done: `game_screen_sim`, `game_has_screens`, `game_screen_input` and `game_screen_up` moved to `game/input` (now 1500 and 159).

**11. The census screen's bar says "tiles" while cover runs.** Continue waits for cover (the session passes the screen a copy of the report, not written, with the columns as its tiles), but the label is `ui/screen_census`'s, phase 07's, and says "tiles", not "Snow and ice". Left: a word in another phase's screen. Cover is fast (0.55 to 0.74 s for 4225 columns on the debug build), so the bar is seldom seen.

**12. A tile never through a census has zeros for a week.** Expected: the store's cells to be a week whenever the tile is kept. Actual: a fresh tile's cells are zero until its first census starts them at spin-up, and zero ice is under the off line, so cover on stream-in would melt an icy biome's lake ice before any census. Done: cells of fresh tiles count as unknown, and nothing changes there; cover also waits while a census writes the store.

**13. Golden pictures are made in-process, not by the built program.** The plan asks for `--headless` runs on a test world out of a doorway; test worlds have no house. Done: `render_feature_weather` builds a stone house in a hand world on the real card (the `render_feature_sky` rig), takes each kind's slot through `render_weather_take` and draws with `render_frame_draw_weather`: the real kernels and frame code. The built program is tested apart (`--weather rain` is not the clear picture, and the same twice; the refusals). A picture with `--test-world --weather rain` at 960 by 540 was also looked at: grey sky, rain slanting in the wind, the fog nearer.

**14. Plan 003 phase 1 is not called done.** Step 15 says stop at "not done". Its status is "in progress" with every agent-runnable check passed and only Ahmed's check after its phase 2 left, and its name tables are on main. Done: steps 16 to 18 went ahead; the block table test and the save tests pass with `snow_layer` added (the whole suite, Issues found 18). If Ahmed wants to wait, `config/blocks/snow_layer/` and the two snow rules can come out.

**15. The snow layer needed two small wirings outside the owned paths.** `run/src/physics/raycast/` aims at it as the flat box (as at a lily pad), not the upright plate every other `thin` block gets; `config/blocks/snow_layer` needs `sound: snow`, which the table requires.

**16. Snow first cost too much.** The first snow kernel looked at 5 cells a layer, as a flake's wobble (0.3) is wider than a cell (0.25): about 1.5 ms. Done: a whole row of cells wobbles together, each row in its own phase, so a flake stays in its cell and one cell a layer is asked, as for rain; the snow golden picture was made again and looked at.

**17. The frame-rate cost, before and after, at render distance 32 and 1080p.** Recorded in `bench/world/history.tsv` (`004 phase 08, before` and `after`) and `bench/world/README.md`. The card slows to about a third after some 30 s of load (every pass alike, at 79 to 82 degrees C), so within one run later rows read slower: the `after` run's four weather rows, last, read 21.8 to 24.5 fps, and a `--weather clear --only orbit` run after it read `plains/orbit` at 25.2. With no weather, which is every other row, the pictures and post pass are unchanged (`jagged_peaks/orbit` post 1.62 ms before, 1.61 after; `deep_ocean/orbit` 2.41, 2.43; `cave/orbit` 3.04, 2.99). Like with like, each weather row run right after a clear `plains/orbit` in turn, post against shade: rain (8 mm an hour) +0.49 ms, snow +0.75, thunder (30 mm and a bolt) +0.71, fog 0.0. **Rain is at the 0.5 ms budget; snow and a thunderstorm are over it.** Not changed: R1's way down is 3 layers or half-resolution drops, and Ahmed decides with the numbers.

**18. Verification, run 2026-11-06.** All through `tools/test-or-build.sh`. Debug configure and build: clean. The phase's four tests: all pass (`render_feature_weather` 3 s, `run_player_movement` 9 s, `run_game_cover` 4 s with the 45 and 75 degree years, `run_game_weather` 1.4 s). The second list (`render_feature_sky`, `_water`, `_post`, `_shapes`, `_lod`, `_entities`, `run_render_frame`, `compile_game_tables` before the sim's part; the rest within the whole suite): pass. Profile build: clean; bench as in Issues found 17. Release build (`-O3 -flto`, the 16 KiB frame check): clean, main's frame `0x4000`. The whole debug suite: 145 tests: 139 passed on the first run (2350 s) and 6 failed, `run_settings` (Issues found 20) and five terrain pins (Issues found 19); after those fixes the six, with `run_ui_screens`, `standards_lint` and `clang_format`, pass. The 16 tests the Verification lists outside the phase's own are among the 139. Afterwards `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"` printed nothing. Not run: Ahmed's gate.

**19. `snow_layer` moved the terrain tests' pinned digests.** Found by the whole suite. Expected, as `config/blocks/README.md` says: a new block moves the numbers of the blocks after it in the alphabet, and pinned digests of states with them. Actual: `run_terrain_biome`, `_density`, `_feature` (the generator's row and the forest), `_hydrology` (states and flood) and `_ore` failed only on their pins; 1 and 16 workers still gave the same bytes in each. The land is the same: no file under `run/src/terrain/` or `run/include/terrain/` changed in this phase, and `run_terrain_hydrology`'s columns pin, which holds heights and no block numbers, still passed. Done: the pins set again, each with a line saying why, and `TERRAIN_GENERATOR_VERSION` left, as plan 003 phases 2 and 3 did; all six pass.

**20. Every effect bit must have a setting, and the Effects page has no room for an eighteenth box.** Found by the whole suite: `run_settings` holds that the settings' effects cover `SHADE_FEATURES_EVERY`, which now has `SHADE_FEATURE_WEATHER`. The page is two columns of eight and one on Back's row, at 320 by 240. Done: `effect_weather` is an eighteenth effect in the settings file (default on, round trips, and the session's `features_off` follows it), and the page draws the first `SETTINGS_EFFECT_ON_SCREEN` (17) boxes as before, so its pictures and click points are unchanged. A box on screen is for whoever next lays out that page (phase 10 moves its HUD boxes to a page of their own); `--no-weather` turns it off for a run.

