# 006-ahmed-bohemoth-biomes-follow-the-climate — phase 4: Snow, ice, sand and trees from the climate

**Status:** done: steps 1 to 9, the downstream tests and the benches; the debug suite passes but for four render tests that fail on main itself (Issues found 9) (updated 2026-11-13)

Read first: `plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-00-vision-and-plan-files.md`, then the master `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md` (sections 0 "Why it happens", 2.5, 4, 5, 6 and 9). Every number here comes from the master. Where this file adds a number the master does not give, it says so and why.

## Goal

Terrain lays a permanent `snow_block` only where the climate says snow outlasts the summer: at or above the column's `snow_line_z` with a ragged edge (a wander noise of ±24 blocks), or anywhere in the Ice family. Below the snowline, biomes whose `top` is `snow_block` lay their non-snow top (grass, stone, gravel or podzol, per biome). Generated ice forms on water only where the coldest month at the water's height is below -2 °C (permanent ice below -10), replacing each biome's `ice` rate. Desert sand is laid only in the Desert and Steppe families, and a beach is `snowy_beach` only in Ice and Tundra. Every feature row's rate is scaled by $\min(1.5,\ P_{\text{year}} / P_{\text{biome-mid}})$. The far field uses the same sample and rules, so far mountains show the same snowline as near ones. On a seed's 16 km round (0, 0) with the climate on there is **no `snow_block` and no generated ice at any height**, and Ahmed's screenshot place (x -68, y 149) is not snow. Seasonal snow and ice stay the plan 004 phase 08 cover's. `bench_terrain` stays at or above 1000 columns a second and `bench_lod` at or under 40 ms a column.

## Where this fits

**The vision.** This is the phase that answers the screenshot directly: "temperature is 24 deg C but there is snow!" Phase 03 moved *which* biome is chosen; this phase moves *what blocks* a biome lays, so that even a snowy biome near its family's warm edge lays no snow below the line, and so the far field agrees.

**What later phases read from it:**

| Reader | What it reads |
|---|---|
| Phase 05 | The final land: its digests are the version-2 rows |
| The cover (plan 004 phase 08, `run/src/game/cover/`) | Unchanged. It lays and removes `snow_layer` from the weekly snow over 20 mm and never touches terrain `snow_block`. After this phase, winter white below the snowline is the cover's, and comes and goes |
| Later plans | Tree density from rain is the first "the land decides" signal a settlement plan can read (wood where it's wet) |

**What this phase must NOT build:**

- No change to height, density, hydrology, caves or ores (master section 3).
- No change to the cover (`run/src/game/cover/`): seasonal snow and ice stay plan 004 phase 08's.
- No grass or leaf tint by climate (master section 8).
- No new blocks, no new biomes.
- No change to which biome is chosen (phase 03), apart from fixes found here and recorded.

## Ahmed's words

- "Snow just spawns absolutely freiking randomly. Look at ~/Pictures/1.png please! temperature is 24 deg C but there is snow!"
- "biome creation needs to properly follow ... align biome with climate"

The screenshot: x -68, y 149, z 443, 0.35° N, day 91, 24.1 °C, snow blocks on the slopes. At the equator `snow_line_z` is 9999 (master section 0 point 4: the sea-level mean is 27 °C, cooling is 0.012 °C a block, the 1250 top is about 12 °C), so after this phase no snow block is laid there at any height.

---

## Background, explained

### Every place terrain snow comes from today

Read from the code, 2026-11-12:

1. **The biome's `top`** (`surface_tile_lay`, `run/src/terrain/surface/index.c:389`, and `:169` for patches): `snowy_slopes`, `snowy_taiga`, `snowy_plains`, `grove`, `jagged_peaks`, `frozen_peaks`, `ice_spikes` have `top: snow_block`; `snowy_slopes` also `filler: snow_block`.
2. **A biome's `high:` rule** (not named in the master): `windswept_hills` lays `snow_block` from z 400 and `stony_peaks` from z 800 (`high: { block: snow_block, from: ... }`), fixed heights with no climate in them.
3. **`feature_cold_paint`** (`run/src/terrain/feature/index.c:1141-1170`): a snow block on every dry top where the biome's `snow` rate is above 0, and ice on lake and river water where its `ice` rate is above 0. It runs last.
4. **The far field's freeze** (`render_lod_sample_freeze`, `run/src/render/lod/index.c:137`, used past `RENDER_LOD_FEATURES_SHIFT_MAX`): its own stand-in for cold paint.

All four must obey the snowline, or snow stays where it was. The master's 2.5 names 1 and 3; 2 and 4 are added here so nothing is missed.

### The snowline and its ragged edge

`snow_line_z` is plan 004's `snow_balance_z`: the height above which a year's snow outlasts its summer. It comes per column from phase 02's `terrain_climate_at` (an integer, blocks; `TERRAIN_CLIMATE_NO_SNOW_LINE` when never). The edge is moved by a 2D wander noise $w(x, y) \in [-1, 1]$:

$$\text{snow here} \iff z_{\text{top}} \ge \text{snow\_line\_z} + 24\,w(x, y) \quad \text{or the family is Ice}$$

**Example.** `snow_line_z` 700 at a 45° mountain. A column with top 690 and $w = -0.5$: the line there is 688, so snow. Its neighbour with top 705 and $w = 0.75$: the line is 718, no snow. So the edge is a wobbly band 48 blocks deep, not a flat contour. In integers: `line = snow_line_z + (24 * w_q) / W_ONE` with $w$ quantised.

### Ice by the coldest month

$t_{\text{cold}}(z_{\text{water}}) < -2$ °C: generated ice on the water's top. Below -10 °C: permanent ice (the master's word; **default:** the same `ice` block, the difference is only that the cover never thaws it in summer, which today's cover already does by its own weekly numbers; recorded if a second block is wanted). **Example:** a lake at z 60 where coldest at sea level is -1.5 °C: $-1.5 - 0.72 = -2.22$: ice. The same lake at sea level: -1.5, no generated ice; the cover may still freeze it in a cold week.

### Rain-scaled tree rates

$$\text{rate}' = \text{rate} \cdot \min\!\left(1.5,\ \frac{P_{\text{year}}}{P_{\text{biome-mid}}}\right)$$

$P_{\text{biome-mid}}$ is the middle of the biome's `rain_mm` range written in phase 03. **Example:** `forest` with `rain_mm: [500, 1500]` (mid 1000) and `tree_oak: 10`: at 1800 mm, $\min(1.5, 1.8) = 1.5$, 15 tries; at 400 mm, 4 tries. Integer form: `rate * min(1500, 1000 * P / mid) / 1000`. A biome without `rain_mm` keeps its rate (factor 1), stated in `config/biomes/README.md`.

---

## Design

### Snow

- **Surface** (`run/src/terrain/surface/`): when the chosen block is `snow_block` (from `top`, `filler`, a patch, or `high`), check the snowline rule; below it, use the biome's **`top_below_snowline`** (new YAML field in `surface:`, required for any biome that can lay `snow_block`; the compiler refuses one without it). Defaults per biome, set in step 1: `snowy_plains`/`snowy_taiga`/`grove` → `grass_block` or `podzol`; `snowy_slopes` → `stone` (filler `dirt`); `jagged_peaks`, `frozen_peaks` → `stone`; `ice_spikes` → `snow_block` stays (Ice family only, which always counts as above the line). `high: { block: snow_block, from }` becomes "snow at or above `max(from, the snowline rule)`".
- **`feature_cold_paint`**: the `snow` rate no longer decides alone. A dry top takes a snow block only if the snowline rule holds there.
- **The far field's freeze**: the same rule, from the far field's own sample.

### Ice

`feature_cold_paint`'s two ice branches (lake, column water) check $t_{\text{cold}}(\text{water z}) < -2$ °C from the sample instead of `rates[BIOME_FEATURE_ICE] > 0`. The YAML `ice:` rate is removed from all 11 biomes that have it; the compiler refuses it with a hint.

### Sand and beaches

Desert sand (a biome's `top: sand`, today `desert` and the sand-topped water biomes' beds) is a land top only in Desert and Steppe members; phase 03 already keeps desert out of other families, so this is a check and a test. Beaches stay hydrology's (`surface/index.c:155` leaves its sand and gravel). `snowy_beach` is chosen only where the family is Ice or Tundra; elsewhere `beach`.

### Trees and plants

Every feature row's rate through the rain factor, in the feature stage where rates are read (`biome_features[...]`).

### The far field

`render_lod_sample_terrain` (`run/src/render/lod/index.c:180-199`) already runs surface and features on the same code; it passes phase 02's sample (tiles where ready, fallback past them), so near and far agree.

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/terrain/surface/` (snowline in `surface_tile_lay`, `top_below_snowline`, `high`)
- `run/{src,include}/terrain/feature/` (`feature_cold_paint`, rain-scaled rates)
- `config/biomes/*/index.yaml` (snow and ice fields, `top_below_snowline`), `config/biomes/README.md`
- `compile/src/game_tables/biomes/` (the new field, the refusals)
- `tests/run/terrain/surface/`, `tests/run/terrain/feature/`, new `tests/run/terrain/climate_rules/` if one test file is cleaner

**Wiring outside them:**

- `run/src/render/lod/index.c`: `render_lod_sample_freeze` and the sample
- Terrain digests that move: re-pinned with "plan 006 phase 04: snow, ice, sand and trees follow the climate"
- `bench/terrain/`, `bench/lod/`: numbers recorded

**Not touched:** `run/src/game/cover/`.

## Every biome that lays snow or ice (step 1)

Read from `config/biomes/*/index.yaml` on 2026-11-13. "Frozen" is `snow_block`, `ice`, `packed_ice` or `blue_ice`.
The tool (`compile/src/game_tables/biomes/`) refuses a biome that lays a frozen top, filler, steep or patch and has a
family other than Ice without `top_below_snowline` (and `filler_below_snowline` when the filler or steep is frozen);
`filler_below_snowline` defaults to `filler` when that is not frozen.

| Biome | Families | Frozen blocks it lays | `top_below_snowline` | `filler_below_snowline` | `ice:` / `snow:` rates |
|---|---|---|---|---|---|
| `snowy_plains` | ice, tundra | top `snow_block` | `grass_block` | (`dirt`, its filler) | removed |
| `snowy_taiga` | tundra | top `snow_block` | `podzol` | (`dirt`) | removed |
| `grove` | ice, tundra, boreal | top `snow_block` | `grass_block` | (`dirt`) | removed |
| `snowy_slopes` | ice, tundra, boreal | top and filler `snow_block` | `stone` | `dirt` | removed |
| `jagged_peaks` | ice, tundra | top `snow_block` | `stone` | (`stone`) | removed |
| `frozen_peaks` | ice, tundra | top `snow_block`, filler, steep and a patch `packed_ice`, a patch `ice` | `stone` | `stone` | removed |
| `ice_spikes` | ice | top and filler `snow_block`, a patch `packed_ice` | none: Ice only, always above the line | none | removed |
| `windswept_hills` | boreal, continental | `high: snow_block` from 400 | none needed: below the line the top (`grass_block`) is laid | | none |
| `stony_peaks` | nine warm families | `high: snow_block` from 800 | none needed: below the line the top (`stone`) is laid | | none |
| `snowy_beach` | ice, tundra | none (sand) | | | removed |
| `frozen_ocean`, `deep_frozen_ocean`, `frozen_river` | ice | none (sand beds) | | | removed |

So the 11 biomes with `ice:` were the seven land ones above `windswept_hills` plus `snowy_beach` and the three frozen
waters; all 11 lost both rates. Cold paint's snow and ice and the far field's freeze now read the climate for every
biome alike.

## Steps

1. **List every biome that can lay `snow_block` or ice** (the four sources above) and set each `top_below_snowline`, in a table in this file. *Why: a source missed is snow left where it was.*
2. **The compiler field and refusals.** *Why: a snow biome without a below-snowline top must not build.*
3. **The snowline rule in `surface`**, with the wander noise. Test on hand-built samples. *Why: most of the wrong snow is the biome's top.*
4. **`feature_cold_paint`**: snow by the snowline, ice by the coldest month. *Why: the second source.*
5. **Sand and `snowy_beach`** checks. *Why: master 2.5.*
6. **Rain-scaled rates.** *Why: wet forest dense, dry forest sparse, no new biome.*
7. **The far field**: `render_lod_sample_freeze` and the sample. *Why: far mountains must show the same snowline.*
8. **Tests in master section 5**: equator, pole, 45° mountain, the screenshot place.
9. **Benches**: `bench_terrain` and `bench_lod` before and after, recorded. Re-pin digests with reasons. READMEs. Tick as each lands.

## Todo

From the master (section 9, phase 04):

- [x] The snowline in `surface` and `feature_cold_paint`, with the ragged edge; snow biomes' non-snow tops below it
- [x] Ice on water by coldest month
- [x] Desert sand only in Desert and Steppe; `snowy_beach` only in Ice and Tundra
- [x] Tree and plant rates scaled by rain
- [x] The far field uses the same rules
- [x] Tests in 5: equator, pole, 45° mountain, and Ahmed's x -68, y 149 place
- [x] Benches: `bench_terrain` at or above 1000 columns a second, and `bench_lod`

Expanded:

- [x] The `high: { block: snow_block }` rules of `windswept_hills` (400) and `stony_peaks` (800) obey the snowline
- [x] `render_lod_sample_freeze` (`run/src/render/lod/index.c:137`) obeys the snowline
- [x] `top_below_snowline` set for every snow-laying biome; table recorded here
- [x] The YAML `ice:` rate removed from all 11 biomes; refused by the compiler
- [x] Height, density, hydrology, cave and ore digests unchanged; surface and feature digests re-pinned with reasons
- [x] `bench_lod` at or under 40 ms a column (35 today), recorded
- [x] `standards_lint`, `clang_format`, the whole debug suite pass (but for main's own four, Issues found 9); `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `the_equator_has_no_snow_and_no_ice` (release) | A seed's 16 km round (0, 0), climate on, every column, every height | 0 `snow_block`, 0 generated `ice` | Any source still ignores the snowline |
| `the_screenshot_place_is_not_snow` | Ahmed's seed, x -68, y 149 | Top block is not `snow_block` | Surface or cold paint still lays it |
| `pole_land_is_cold_and_snowy_above_the_line` | 75°, every column above sea | 100% Ice or Tundra; every top at or above `snow_line_z + 24` is snow | The rule is inverted or the family test wrong |
| `a_45_degree_mountain_has_snow_only_above_its_line` | A peak at 45° | No `snow_block` below `snow_line_z - 24`; snow present above `snow_line_z + 24` | Ragged edge too wide, or no snow at all |
| `the_ragged_edge_is_within_twenty_four_blocks` | 100 000 columns near a line | Every snow decision agrees with $z \ge$ line + 24 w; \|w\| ≤ 1 | Wander noise out of range |
| `ice_follows_the_coldest_month` | Hand-built sample: coldest at water -2.22 / -1.5 °C | Ice / no ice | Wrong z or comparison |
| `sand_only_in_desert_and_steppe` | 8 seeds' survey | 0 desert-sand land tops outside those families | Phase 03 leak |
| `snowy_beach_only_in_ice_and_tundra` | Same | 0 elsewhere | Beach rule missing |
| `tree_rates_scale_with_rain` | `forest` mid 1000, P 1800 / 400 | 1.5× / 0.4× tries | Factor wrong or uncapped |
| `the_far_field_agrees_with_near_columns` | 64 columns sampled by both paths | Same top block | The far field skips the sample |
| `one_and_sixteen_workers_lay_the_same_blocks` | 8 by 8 columns | Same bytes | Order dependence |
| Bench `bench_terrain` | Slowest place | ≥ 1000 columns/s | |
| Bench `bench_lod` | Per column | ≤ 40 ms (35 today) | |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1800 ctest --preset release -R '^run_terrain_(surface|feature|climate_rules)$' --output-on-failure
    tools/test-or-build.sh timeout 600 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_lod
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed opens a new world, finds a mountain near the equator with `/tp`, flies up with `/fly`, and sees no snow; then `/tp lat 50` and sees snow only above the snowline. `/time day` lets him compare winter (the cover's seasonal snow) with summer (only the permanent snow above the line).

## Risks and open questions

| Risk or question | Default |
|---|---|
| Equatorial peaks never get snow, which may look odd | That is the climate as tuned (12 °C at 1250). The lapse constant is the knob (master section 6, Q1) |
| The far field and near columns disagree near the keep square's edge | The far field uses tiles when ready and the fallback past them; checked on a long `/tp` flight (master section 6) |
| Terrain throughput falls below 1000 columns a second | Integer rules on the sample already copied; bench before and after |
| "Permanent ice" below -10 °C has no block of its own | Same `ice` block (default); a second block would need a new block, out of scope (master section 8) |
| Rain factor makes some dry biomes almost treeless | Expected; the cap of 1.5 bounds the wet side; looked at in the pictures |
| Q1: snowy peaks at the equator? | No; the climate decides (master Q1) |

## Commits

Appended as they land (as rebased onto main at db0c7d45):

- `7d301a20` Lay permanent snow only where the climate keeps it and ice only where the coldest month freezes the water, and
  scale every feature rate by rain (plan 006 phase 04): steps 1 to 8 (the surface's snowline with `top_below_snowline` /
  `filler_below_snowline`, cold paint and the far field's freeze by the climate, the `ice:` / `snow:` rates refused, every
  feature rate scaled by rain, the `climate_rules` tests). First committed as a WIP commit, reworded.
- `ef0821e4` Re-pin the biome, column and feature digests for plan 006 phase 04, keep the wide jungle tree's crown to leaves
  within `FEATURE_LEAF_REACH` of its trunk, ...: `BIO_DIGEST_PIN`, `BIO_COLUMNS_PIN`, `FEA_FOREST_PIN` and
  `FEA_DIGEST_OF_GENERATOR[1]` re-pinned with reasons; issues 1 and 2 below. First a WIP commit, reworded.
- `d9d96262` Record in plan 006 phase 04's file the snow and ice table, the commits so far, ...
- `6d724705` Follow plan 006 phase 04's land in the tests that played the default world: bench_cave_walk stands its body
  through the 100 ticks before the first key, ...: issues 3, 4 and 6 to 8 below.
- this commit: the tests, the benches, the rebase and the verification recorded; the todos ticked.

## Issues found

1. **The wide jungle tree's crown crumbled in the game.** Its blob (5 across, 3 tall, round a 2 by 2 trunk) reached
   7 steps from the trunk at its rim, past the 6 the game's leaf decay allows (`run/src/world/tick/`,
   `TICK_LEAF_REACH`). In a dense jungle the neighbours' logs held those leaves; once the rain factor thinned the
   jungles near the default spawn, a 786-tick game run lost 231 jungle leaves to decay. The blob now leaves out any
   leaf more than `FEATURE_LEAF_REACH` (6) steps from the trunk (`feature_wide_crown_steps`); the same run then
   dropped none. Recorded in `run/src/terrain/feature/README.md`.
2. **Tests that counted on the biome's numbers alone.** `a_forest_has_about_the_trees_its_file_asks_for` now asks
   for the mean of each column's rain-scaled rate (4.80 a chunk where the file says 8; 4.12 stand);
   `lily_pads_grow_on_a_swamps_standing_water` also looks at the swamps' own places, since a swamp drier than its
   range's middle grows a third of its pads and the 600 low chunks held 6 swamps (7 pads found there); and sugar
   cane may stand by water that froze after it grew (ice by the coldest month now reaches biomes with cane).
3. **`run_terrain_cave_walk`: the tool's body took its first step in the air (fixed in `bench/cave_walk/`).** A trace of
   both bodies tick by tick (`player_tick_wind` in the game, `walk_along` in the tool, thrown away after) showed the same
   keys and yaw on the same ticks, but the first step 0.098 blocks in the game and 0.020 in the tool, and 0.15 / 0.037 on the
   second. The game's body stands through the 100 ticks before the script's first key, so it is on the ground when it sets
   off; the tool's was started by `player_movement_start` and moved on its first tick, before any tick had found the ground,
   so it took the air's push (a fifth of the ground's) and was a tick behind from there. The tool now ticks its body with no
   keys for `WALK_START_TICK` ticks first, as the game does. The walk near spawn (674 ticks, ending at -133.5, 409.6, 20)
   then agrees within the test's bounds; no wind was involved (the weather's wind there was 0, and under 10 blocks a second
   pushes nothing). So it was the one-tick offset the open question named, and on the tool's side, not the game's.
4. **Downstream tests the new land moved, each made to read the land or to ask for what it means** (after the rebase onto
   ed5bdbb9, the full debug suite at -j 2 had 9 failing, below; all fixed in `6d724705`):
   - `run_playthrough`'s stairs: the tree it finds is a mangrove of 14 logs 874 blocks off (the search, unchanged, takes the
     first fit tree of the nearest square). There the second stair went onto the first, and the first's drop lay under the
     second, out of reach: the hand ended with 2 stairs, not 3. `stairs.script` now steps in for 5 ticks after mining it and
     comes back by a new `@feet` line, which the test turns into the same `/tp` to the place in the trunk's block that ends
     `@tree` (the look is kept, so the bench's half is aimed at as before). A first try that walked back with S for 5 ticks
     ended a little off and mined mud in place of the bench, which is why it is a `/tp`.
   - `run_profiler`'s aimed-at overlay was in every one of 181 frames: the walking body at the test world's start
     (-87.5, 440.5, 11) stands under a jungle crown, so the look straight up met leaves. The script now mines looking down
     first, then types `/tp top` and looks straight up: over the top of its column there is only sky.
   - `run_ui_hud`'s dead-player band: the pixel at (1, 8) was a tree's shade (0x21180f before, 0x0f1c09 after the respawn),
     which the old test, red under twice blue, read as the band. It now asks for the band's own colour: 3/4 of 0xA00000 over
     a quarter of the world, red at least 0x70, green and blue at most 0x48.
   - `run_render_frame`'s real threads: the 2.5 s flight now ends 34 blocks over open sea at 118.8, 581.7, where level 0's own
     hits were 6786 of 57600 (11.8 %, under the eighth asked for). A picture of something is now a pixel whose ray met a block
     in level 0, whose colour ends on a surface, or where the far field drew one: 46104 of 57600.
   - `render_feature_lod_bands`' near guarantee: 0 hits and 21 colours differed, all at "north, over it" (2 at E 2, 19 at E 8,
     fov 70): leaves 14.3 to 15.1 blocks off with the pixel ending on ground 25 to 42 blocks behind in the reference and on sky
     in the bands. What shows through a near see-through surface from past 16 blocks is past `d_1`, so the colour clause now
     leaves out a near pixel whose reference ends past 16 (its hit is still held to the bit). That the far field at E 8 drew
     sky where level 0 had ground 25 blocks off is a question for plan 009, recorded in `tests/cuda/lod/bands/README.md`.
   - Goldens made again with `MC_GOLDEN_WRITE=1` and looked at: `run_render_frame`'s `start.png` (a jungle crown and trunk at
     the left, the slope's coarse dirt, the sea and haze ahead, a bush at the lower right), `north_low.png` (dense jungle
     leaves close round the eye, terraced grass and dirt rising behind, the sea at the right edge), `down.png` (looking down
     on a jungle canopy, round crowns and trunks, a pale beach at the left); `render_feature_lod`'s `real_near.png` (mostly
     sky and haze, a jungle tree and leaves on the slope at the left, the sea at the lower right) and `real_far.png` (a wide
     view of hills, stone ranges with pale tops, a lake in the foreground; no snow block is laid there now, the pale tops are
     the cover's seasonal snow and stone, as plan 009 phase 04 noted); `render_feature_under_water`'s `land.png` (a jungle
     trunk ahead, leaves over it, grass terraces), `sea.png` and `sea_no_air.png` (the blue sea bed with kelp and sea grass,
     the second with the air bubbles gone). Nothing looks broken: no holes, no wrong colours, the sky and water as before.
5. **`run_climate_warning`'s `it_takes_under_20_ms_on_any_preset` is load flake.** It failed in both full runs at -j 2 and
   passed alone each time (6.4 s and 7.1 s). Likewise `run_ui_atlas`'s `the_sim_never_waits_long_on_the_lock` failed once at
   -j 2 (a 2 ms bound) and passed alone. Neither reads the land.
6. **Benches** (release build of `bench_terrain` and `bench_lod` only, through the lock, load average 1.7 before and 1.4
   after; CPU only, no card): `bench_terrain`, 16 workers, chunk columns a second: plains 2128, forest 1461, **jungle 1005**
   (the slowest), dark_forest 1179, mangrove_swamp 1535, desert 1904, snowy_taiga 1789, jagged_peaks 1516, deep_ocean 2385,
   plateau_top 1238: **target 1000 met, by 0.5 %**, exit 0. Climate sample from the tiles 10.6 µs, `terrain_climate_at`
   0.176 µs. `bench_lod`, one column on one thread: shift 1 mean 17.0 ms (worst 24.4), shift 2 mean 26.0 (worst 37.4),
   shifts 3 to 6 10.3 to 11.3 ms: **every level at or under 40 ms**, exit 0, the same tops as before. `build/release` was
   deleted afterwards for the disk (19 GB free).
7. **Rebase.** Onto main at ed5bdbb9 (plan 009 phases 05 and 06) with no conflict, then, by the rewording's `git rebase -i
   main`, onto db0c7d45 (plan 009 phase 07, plan 008 phase 06), again with no conflict. The goldens made on ed5bdbb9 still
   match on db0c7d45.
8. **The WIP commits were reworded** (the same content) into `7d301a20` and `ef0821e4`.
9. **Four render tests fail on main at db0c7d45 itself, not from this phase:** `render_feature_sky` and
   `render_feature_water` (time out after `everything_starts_on_...` fails), `render_feature_shapes` and
   `render_feature_post`, each on `frame->scene.features == SHADE_FEATURES_EVERY` (or `_PLAY`): 1507327 against 458751, the
   difference being `1u << 20`, plan 008 phase 06's `SHADE_FEATURE_LOD_TURN_CACHE`, which the game now asks for beside
   `SHADE_FEATURES_EVERY`. That phase's verification ran the LOD and frame tests but not these four. Left for its owner.

## Verification after the rebase (2026-11-13)

- `cmake --build --preset debug`: clean.
- The whole debug suite at -j 2 on ed5bdbb9: 173 of 176, the three being `run_climate_warning` and `run_ui_atlas` (load,
  both passed alone) and `clang_format` (fixed, then passed with `standards_lint`).
- The whole debug suite at -j 2 on db0c7d45: 173 of 177, the four being main's own (Issues found 9). `run_playthrough`,
  `run_profiler`, `run_render_frame`, `run_terrain_cave_walk`, `run_ui_hud`, `render_feature_lod`,
  `render_feature_lod_bands`, `render_feature_under_water` and `run_climate_warning` passed in it.
- `bench_terrain` and `bench_lod` as in Issues found 6. Not run: the release terrain tests (`run_terrain_(surface|feature|
  climate_rules)` in release), for the disk; the debug ones passed.
- `pgrep -af "$PWD/build"` and `pgrep -a Xvfb` print nothing.
