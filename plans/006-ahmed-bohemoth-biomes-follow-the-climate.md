# 006-ahmed-bohemoth-biomes-follow-the-climate — master plan

**Status:** draft for approval (2026-11-12). Plans only: on approval, `plans/README.md` applies. This file is copied verbatim to `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md`, a setup agent creates one phase file per phase in section 9, and **no code is written until Ahmed says go.** Nothing is pushed.

## 0. Context

### Ahmed's words, 2026-11-12

- "Snow just spawns absolutely freiking randomly. Look at ~/Pictures/1.png please! temperature is 24 deg C but there is snow!"
- "at world generation time please run through our climate mode first and then decide biome systems!!"
- "biome creation needs to properly follow ... align biome with climate"
- "EXTREMELY THOROUGHLY EXAMINE EVERYTHING NEEDED FOR THIS PLAN WHILE WE ARE STILL WRITING PLAN FILES! SO THAT WE CAN HAVE A SOLID IMPLEMENTATION!!"
- "we also need a /tp command somehow so I can teleport to higher latitudes/longitudes and when i see a mountain to higher altitudes really really fast so that I can fine tune exactly how terrain/biome generation ties in with our climate system"

### What the screenshot shows

`~/Pictures/1.png`: the player stands at x -68, y 149, z 443, which is 0.35° north of the equator, on day 91 in the wet season. The HUD reads **24.1 °C**, but the slopes and the ground round the player are **snow blocks**.

### Why it happens (read from the code, 2026-11-12)

1. **Biomes never read the climate.** This was by design: plan 004 master section 2.6 says "Biomes stay as they are", and phase 00 rule 3 says "Nothing in the terrain changes." A biome is the nearest box in a 6-D space (`run/src/terrain/biome/index.c:60-92`). Five of those axes are noise fields from `run/src/terrain/fields/` (temperature, humidity, continentalness, erosion, weirdness), and the sixth is hydrology's ground height divided by 128.
2. **The temperature field is plain noise, with no latitude in it.** It has a 4096-block cell and 4 octaves (`fields/index.c:30-36`). At ground height 300 to 500, a noise temperature of -0.15 or below picks `snowy_slopes` or `grove`. That is about 42% of all such ground, equator included.
3. **The snow itself is laid in two places:**
   - `surface_tile_lay` puts the biome's `top` there (`run/src/terrain/surface/index.c:389`, `:169`). For `snowy_slopes` and `grove` that top is `snow_block`.
   - `feature_cold_paint` puts a snow block on every dry top of a biome with `snow > 0`, and ice on its water (`run/src/terrain/feature/index.c:1141-1170`).
4. **The climate system alone would never put permanent snow there.** At the equator, the sea-level yearly mean is 27 °C and cooling with height is 0.012 °C a block. Even the 1250 world top is about 12 °C, and `snow_balance_z` there is 9999 ("never lasts the summer").
5. **Plan 004 phase 11 measured the mismatch** over 8 seeds, pole to pole:
   - Deserts and badlands disagree with the climate class on **81%** of desert and steppe land.
   - Every other class disagrees on 16% to 21% of its land.
   - The Continental class never occurs.

### What exists to build on

- **The climate is ready before columns are.** Climate tiles (16 by 16 cells of 64 blocks) are worked out on workers ahead of the player. The radius is render distance × 16 + 5224 blocks, so 5736 at render distance 32. They are precomputed behind the loading bar when a world opens.
- **The column gate already waits for climate.** `game_columns_request` waits for a column's tiles (`run/src/game/index.c:1078-1086`). It does that today only so that columns arrive in order; nothing reads the tiles yet.
- **There is no loop.** The climate reads the ground only through `terrain_height_estimate*`: the 2D splines over continentalness, erosion, ridges and broad. Height and density never read temperature, humidity or biome. So **height, then climate, then biome and surface** is acyclic, as long as biomes never feed back into height.
- **The values a biome needs already exist per cell:** `t_mean` (at sea level), `t_swing`, `t_lag_days`, `rain_mm_week[5]`, `snow_balance_z` and `climate_class`. `climate_normals_at` gives warmest and coldest month and yearly rain at any z.
- **Seasonal snow already follows the climate.** `run/src/game/cover/` lays and removes `snow_layer` from the weekly snow, over 20 mm, at each census and as columns stream in. Terrain `snow_block` is never touched by it, and that is the snow that is wrong.
- **Most of `/tp` can reuse existing parts:**
  - typing: `ui_widget_text_field` and `ui_widget_input_feed`, which handle `- . / ~`, digits and Enter;
  - an overlay that takes the input: the Atlas pattern (`game_atlas_input`, `game_atlas_compose`, the `game_screen_up` Esc guard);
  - setting the position without smearing: `player_movement_start`;
  - waiting for the column, then finding standing room: `survival_place` and `survival_stand_at_spawn`;
  - map, climate and far-field recentring, which already follow the eye;
  - latitude: `climate_latitude_deg`.

### What is in the way

| Obstacle | Where | What this plan does |
|---|---|---|
| Terrain may not include climate: lint ranks are climate 8, terrain 9, and a lower row may never include a higher one | `compile/src/standards_lint/rules_include/index.c:62-73` | **A seam, not a rank change.** Terrain gets a plain-data "climate sample" type of its own. `game` fills it from the climate and hands it in (2.1) |
| Block decisions may not lean on libm rounding, but the climate uses glibc `exp`, `sin`, `cos` and `asin` | `terrain/ore/README.md:87`, `terrain/fields/README.md:68` | The values terrain reads are worked out with our own deterministic math (2.3) |
| Callers that generate terrain with no climate: spawn search (before the climate opens), far field, mob spawn, every terrain test and bench, test worlds | `game/index.c:665`, `render/lod/index.c:180-199`, `mob/spawn/index.c:178` | A defined fallback climate from latitude and height alone, the same pure maths, so every caller works (2.2) |
| The column gate covers the column plus 96 blocks, but features read neighbour chunks up to 16 blocks further | `climate/index.c:210-215` | Widen the read reach to 112 |
| Per-column climate reads would be costly: about 4 µs each, two seq_cst RMWs a cell, and 11 to 19 biome tiles per column | `bench/terrain`: 1134 columns a second against a target of 1000 | One small snapshot of the needed cells per column job, copied once (2.1) |
| Saved worlds store edits over generated land, with no migration | `save/world/README.md:54` | **Bump `TERRAIN_GENERATOR_VERSION` to 2 and delete the old worlds,** as Ahmed decided (section 7) |
| A big jump may leave the world black: plan 004 phase 02 Issues found 14 saw streaming stop for good after a fast flight, cause unknown | Suspect: the early `return` in `game_columns_request` when the nearest tile isn't ready (`game/index.c:1081-1086`), or a tile slot stuck CLAIMED or EVICTING | Phase 01 finds and fixes it, because `/tp` makes exactly that jump |

### Ahmed's decisions, 2026-11-12

| Question | Decision |
|---|---|
| Old worlds (`saves/test_1` to `test_8`) | **Delete them.** The generator version goes to 2 |
| Fast travel | `/tp` with absolute, `~` relative and `lat`; **a `/fly` toggle**; and **an F3 tuning readout** |
| Where the climate sits | **"run through our climate model first and then decide biome systems"**: climate decides what kind of land, and noise only adds variety inside it |

## 1. The rule this plan changes

Plan 004 phase 00 put the layers in this order: seed → terrain → climate → geology / vegetation → settlements → economy → player. Plan 004 had no vegetation layer. **Plan 006 is that layer.** It lifts plan 004's rule 3 ("Nothing in the terrain changes") for the biome, surface and feature stages only. **Height, density, hydrology, caves and ores do not change.** The climate keeps reading only the height estimate, so nothing becomes circular.

```
seed
 └─ fields (c, e, ridges, broad; T and H no longer used by biomes)
     └─ density profile and height estimate         (unchanged)
         ├─ hydrology: rivers, beaches, sea bed      (unchanged)
         └─ climate cells: t_mean, swing, rain, class, snowline   (unchanged inputs)
             └─ biome = climate family, then landform and variety noise   ← new
                 └─ surface: top, filler, snowline, sand               ← new rules
                     └─ features: trees, plants, snow and ice by climate   ← new rules
                         └─ cover (plan 004 phase 08): seasonal snow and ice   (unchanged)
```

## 2. Design

### 2.1 The climate sample: a seam, not a dependency

**`run/include/terrain/climate_sample/index.h`** is new, in the terrain row. It includes nothing from `climate`.

```c
/* The climate a column job's terrain may read: plain numbers per 64-block cell, copied once by game from the climate's tiles,
 * or worked out from latitude and height alone where there is no climate (2.2). Read only; never owns anything. */
typedef struct terrain_climate_cell_t {
    int32_t t_mean_cc;        /* 0: yearly mean at sea level, centi-°C, from our own maths (2.3) */
    int32_t t_swing_cc;       /* 4: half the yearly swing, centi-°C, signed by hemisphere */
    int32_t rain_mm_year;     /* 8: yearly rain and snow as water, mm */
    int32_t snow_line_z;      /* 12: snow_balance_z, blocks; TERRAIN_CLIMATE_NO_SNOW_LINE when never */
    int32_t dry_month_mm;     /* 16: the driest month's rain, mm, for rainforest against savanna */
    uint8_t climate_class;    /* 20: CLIMATE_CLASS_* number, copied as a number */
    uint8_t flags;            /* 21: sea, monsoon, warm or cold current coast */
    uint8_t _pad0[2];         /* 22 */
} terrain_climate_cell_t;
static_assert(sizeof(terrain_climate_cell_t) == 24);

typedef struct terrain_climate_sample_t {
    int64_t origin_x, origin_y;                                   /* 0, 8: the south-west cell's corner, blocks */
    terrain_climate_cell_t cells[TERRAIN_CLIMATE_SIDE][TERRAIN_CLIMATE_SIDE]; /* 16: 6 by 6 cells round the column, 864 bytes */
    uint8_t from_climate;                                         /* 880: 1 from tiles, 0 from the fallback */
    uint8_t _pad0[7];
} terrain_climate_sample_t;
```

**Size:** 6 by 6 cells covers the column, plus 16 blocks of neighbour chunks for features, plus the 32-block warp, plus the bilinear blend, with room to spare.

**Who fills it:**
- **Column jobs:** `game_column_job_run` fills its own sample from `climate_cell_get` before it calls terrain. It does so after the column gate has said the tiles are ready, and copies once per job.
- **The far field:** its sampler fills one per level column.
- **Mob spawn:** reads through a small per-step sample at the player.
- Terrain only ever sees `const terrain_climate_sample_t *`.

**Reading inside terrain:** `terrain_climate_at(sample, x, y, z)` gives a column's values:
- It blends bilinearly between cell centres, using our own warp (2.3) so that borders wobble.
- It applies the lapse to the column's own height for temperatures, so a peak is colder than the valley under it.
- Its results are `t_mean_c(z)`, `t_warm_c(z)` and `t_cold_c(z)` (warmest and coldest month), yearly rain, the snowline, the class of the nearest cell, and flags.

**The climate side** adds no new reads beyond `climate_cell_get`. It needs one new field for the driest month's rain: one of the cell's 12 spare bytes, or the value worked out from its rain harmonics when the sample is filled.

### 2.2 The fallback climate, so every caller works

Where there is no climate:
- test worlds;
- terrain tests and benches;
- the spawn search before the climate opens;
- the far field past the climate's keep square.

There the sample is filled by `terrain_climate_fallback(seed, x, y)`. It is the same temperature formula as plan 004 section 6.6, with fixed values:
- $\kappa = 0.5$, no current;
- rain from the latitude belts of 6.7 alone, with moisture $M = 0.6$ and orography $O = 1$;
- the class and snowline worked out from those, with the same deterministic maths.

It is a pure function, and it is defined everywhere.

**Order at world open:** the spawn search moves to **after** the climate is prepared, so that a new world's first columns use real climate. Test worlds and benches keep the fallback, which is labelled in their output.

### 2.3 Deterministic maths for anything that decides a block

**Terrain decides blocks, so it must not depend on how glibc rounds.** Plan 004's climate cells use libm. So:

- **A new `run/src/math/det/` part,** with no libm:
  - `det_exp`, `det_sin`, `det_cos`, `det_asin`, `det_atan2` in float64;
  - range-reduced polynomials with fixed coefficients;
  - each tested against libm to 1e-12 over its range, and pinned by digest.
- **The cell fields the sample copies are worked out with `det_*`:** `t_mean`, `t_swing`, yearly rain, the driest month, `snow_balance_z` and the class. This happens in the climate's own parts (`heat`, `rain`, `season`, `harmonic`, `cell`), so the climate and the terrain agree exactly. Climate digests change once, each with a recorded reason.
- **The sample stores integers** (centi-°C, mm, blocks). So terrain compares integers, and a block never flips on a last-bit difference.

### 2.4 Choosing a biome from the climate

**Two steps, in `terrain_biome_choose`:**

1. **Family, from the climate at the column's height.** These are the plan 004 section 6.10 classes, worked out per column with its own height, so a peak can be Tundra while its valley is Temperate:

| Family | Climate | Biomes in it (today's names) |
|---|---|---|
| Ice | warmest month < 0 °C | frozen_peaks, ice_spikes, snowy_plains, frozen_ocean, frozen_river, snowy_beach |
| Tundra | warmest 0 to 10 | snowy_slopes, snowy_taiga, snowy_plains, grove, jagged_peaks, stony_shore |
| Boreal | coldest ≤ -3, warmest < 22 | taiga, old_growth_taiga, grove, windswept_forest |
| Continental | coldest ≤ -3, warmest ≥ 22 | forest, birch_forest, plains, meadow |
| Temperate | coldest > -3, rain all year | forest, flower_forest, plains, meadow, birch_forest, swamp, dark_forest |
| Mediterranean | coldest > -3, dry summer | savanna_plateau, plains, sparse forest (wooded_badlands as rare) |
| Steppe | rain < P_th | savanna, plains, windswept_savanna |
| Desert | rain < P_th / 2 | desert, badlands, eroded_badlands |
| Savanna | coldest ≥ 18, a dry season | savanna, savanna_plateau |
| Monsoon | coldest ≥ 18, monsoon flag | jungle, sparse_jungle, bamboo_jungle |
| Rainforest | coldest ≥ 18, driest month ≥ 60 mm | jungle, bamboo_jungle, mangrove_swamp |

The exact membership is data, in each biome's YAML (below). It is finished in phase 03 against the pictures.

2. **Within the family, today's nearest-box choice.** It uses only continentalness, erosion, weirdness, height and hydrology's kind, which are the **landform** axes. Two new variety fields take the place of noise temperature and humidity: a low-frequency "variety" noise, and a "wetness" noise from local rain. They choose between, for example, forest and birch forest. They never choose between snow and jungle.

**Biome YAML.** Each `config/biomes/<name>/index.yaml` gets a new `climate:` block:
- `families: [tundra, boreal]`;
- optional hard limits: `t_warm: [min, max]`, `t_cold: [min, max]`, `rain_mm: [min, max]` in °C and mm, checked after the family;
- `variety: [min, max]` and `wetness: [min, max]`, which replace the noise `temperature` and `humidity` boxes.

`compile/src/game_tables/biomes/` reads it and refuses a biome without a family. `config/biomes/README.md` explains each field.

**Fallback inside a family:** if no biome of the family fits the landform, for example Desert at 900 blocks high, the nearest family member wins by landform distance alone. It is never a biome from another family. A test proves every family has a member for every landform box.

### 2.5 Snow, ice, sand and trees from the climate

**Permanent snow** (terrain `snow_block`): only where the climate says snow outlasts the summer.
- At or above `snow_line_z` at that column, with a ragged edge: a wander noise of ±24 blocks, so it isn't a flat line.
- Also anywhere in the Ice family.
- `feature_cold_paint` and every biome's `top: snow_block` obey this. Below the snowline, those biomes lay their non-snow top: grass, stone, gravel or podzol, per biome.
- **So no snow block lies where today's yearly mean is warm.** At the equator `snow_line_z` is 9999, so there is none at any height.
- Seasonal snow stays plan 004 phase 08's `snow_layer`, laid by the cover from the weekly state. Winter snow still comes and goes.

**Ice on water:** only where the coldest month is below -2 °C at the water's height (permanent ice: below -10). This replaces each biome's `ice` rate. Seasonal ice stays the cover's.

**Sand:** desert sand only in the Desert and Steppe families. Beaches are still hydrology's. A beach is `snowy_beach` only in Ice and Tundra.

**Trees and plants:** each feature row's `rate` is scaled by a rain factor, $\min(1.5,\ P_{\text{year}} / P_{\text{biome-mid}})$, where the middle comes from the biome's YAML. Wet forest is dense and dry forest sparse, with no new biome needed.

**The far field** uses the same sample and the same rules, so far mountains show the same snowline as near ones.

### 2.6 Teleport, fly and the tuning readout

**Opening the command box:** `T` or `/` opens a one-line box at the bottom of the screen. It is an overlay in the Atlas pattern, and the game keeps running. Enter runs the command, and Esc closes the box. **Up and Down recall the last 16 commands,** kept for the session.

| Command | Does |
|---|---|
| `/tp X Y Z` | To block X Y, feet at Z |
| `/tp ~DX ~DY ~DZ` | Relative; any mix, such as `/tp ~ ~ ~300` (300 up) or `/tp ~2000 ~ ~` |
| `/tp X Y` | To X Y, standing on the ground there once it loads |
| `/tp lat DEG` | y from latitude ($y = \text{DEG}/90 \cdot Y_{\text{pole}}$, north positive, -90 to 90); x and height kept, standing on the ground |
| `/tp lat DEG X` | The same, with x given |
| `/tp top` | Straight up to stand on the highest ground of this column |
| `/fly` | Fly mode on or off |
| `/time day N` and `/time hour H` | Day of year and clock, for checking seasons: snow in winter, the snowline in summer |
| `/help` | Lists these |

**Longitude:** x has no climate meaning, because the world is infinite east and west. So `lat` takes an optional x, and nothing pretends x is longitude. **The box shows the latitude of the target** before Enter: for example `/tp lat 60` shows "y 25532 · 60.0° N".

**Bad input:** a bad command shows one red line in the box and does nothing. Coordinates are clamped to the world's height range and to |x|, |y| within the save's limits.

**How a teleport runs, without dying or a black world:**
1. Main sends the target through a new latch on `player_t` (`teleport_word` and `teleport_target`). `player_t` is asserted at 2304 bytes with one spare byte, so the layout is rearranged and the assert updated.
2. The sim takes the latch at the start of its step:
   - It calls `player_movement_start(target)`, which sets the position, zeroes velocity and fall distance, and stops drawing from smearing.
   - It sets `survival->arriving = 1`: no fall damage, and the body held still until the column under it is loaded. This reuses `survival_place`'s wait.
   - For `/tp X Y` and `lat`, it then stands the body on the top solid block with headroom (`survival_stand_at_spawn`). For `X Y Z` it places the feet exactly at Z, and if Z is inside rock it climbs to the first space with headroom.
3. The map, the climate tiles and the far field recentre on their own, because they follow the eye.
4. A new fill kind, `GAME_FILL_JUMP`, logs how fast the world comes back after the jump.
5. `arriving` is saved with survival, so an autosave mid-jump reloads safely.

**The black-world bug comes first in phase 01.** Before any teleport ships, a test jumps 30 km north and 40 km east, three times. It requires the world round the player to be drawn within 15 s each time (`GAME_FILL_JUMP`). It is investigated from the suspects above, and fixed and recorded wherever it is.

**`/fly`:**
- A new movement mode, `PLAYER_MOVEMENT_MODE_FLY`: no gravity, no fall damage, no hunger drain.
- Space goes up and Shift down. The mouse wheel sets speed, from 10 to 1000 blocks a second, as the test-world camera does, and Ctrl doubles it.
- Columns that aren't loaded hold the body at their edge, as walking does. It is saved with movement.
- The HUD shows "Flying · 120 b/s".

**The F3 tuning readout,** under the existing lines, read where the player stands. The sim works it out every 20 ticks, as the corner climate line is:

```
biome savanna  family Savanna  class Savanna (climate)
yearly 24.6 °C  warm 27.1  cold 22.0 at z 88   today 25.3
rain 640 mm/yr  dry month 4 mm   snowline none
```

At a test world it says `(fallback)` instead of `(climate)`.

### 2.7 Maps for tuning, before and after

- **`bench_biome_map --climate`** draws biomes with the climate snapshot.
- **A new `bench_biome_vs_class`** draws three pictures of the same 16 km at the same scale:
  - biome colours;
  - climate class colours (plan 004 phase 09's palette);
  - a mismatch map: red where the biome's family isn't the class.

  It prints the mismatch share per class, the same survey as plan 004 phase 11.
- A strip from the equator to the pole, at x = 0, 4 km wide, as one tall picture: the snowline appears where it should.

## 3. What does not change

- Height, density, hydrology, rivers, caves and ores. Their digests hold, apart from block-id moves if new blocks are added (none are planned).
- The climate's inputs, and every climate output apart from the `det_*` change in 2.3.
- Seasonal snow and ice: the plan 004 phase 08 cover.
- The weekly census, the Atlas and the HUD climate line.

## 4. Costs and limits

| What | Budget | How it's held |
|---|---|---|
| Filling a sample in a column job | under 20 µs | 36 cell copies, one tile read each; a bench |
| `terrain_climate_at` per call | under 0.2 µs | integers and one warp; a bench |
| Terrain throughput | `bench_terrain` stays at or above 1000 columns a second at its slowest place (1134 today) | measured before and after, recorded |
| The far field | 35 ms a column today, no worse than 40 | `bench_lod` |
| Determinism | 1 and 16 workers give the same bytes; climate-on and the same climate replayed give the same bytes | tests in each phase |

## 5. Tests the plan relies on

- **Equator:** with the climate on, a seed's 16 km round (0, 0) has **no `snow_block` and no generated ice** at any height. It reproduces `~/Pictures/1.png`'s place on that seed (x -68, y 149) and asserts the top there isn't snow.
- **Pole:** at 75° every column above the sea is Ice or Tundra family, and snow blocks lie above the snowline.
- **45° mountain:** snow blocks lie only above that column's `snow_line_z`, and none below it apart from the cover's `snow_layer`.
- **Mismatch survey:** over 8 seeds, the share of land whose biome family differs from its climate class is **under 5%** in every class. Today it is 16% to 81%.
- **Family coverage:** every family has a biome for every landform box (compile-time).
- **Teleport:** jumps there and back land safely, with no fall damage, the column loaded, standing room and the same health. `/tp ~ ~ ~300` keeps x and y. `lat 0`, `45` and `-90` give the right y. A bad command changes nothing. A save mid-jump reloads standing.
- **Black world:** three far jumps redraw within 15 s each (2.6).
- **Fly:** no gravity, no fall damage on landing after `/fly` off, saved and reloaded in fly.
- **det maths:** against libm to 1e-12, pinned.

## 6. Risks

| Risk | What's done about it |
|---|---|
| Terrain throughput falls below 1000 columns a second | A snapshot per job, integer reads, and benches in phases 02 and 03 |
| A family has no biome for some landform, giving odd mixes | The compile-time coverage test; landform-only fallback inside the family |
| Biome borders follow the 64-block cell grid | The warp in `terrain_climate_at`, plus variety noise; checked on the pictures |
| The black world after a jump has a deeper cause | Phase 01 doesn't close until three far jumps redraw |
| `det_*` changes climate digests and plan 004's pinned constants | Each move recorded with its reason; `run_climate_constants` updated with the table in plan 004 phase 11 |
| The far field and near columns disagree near the keep square's edge | The far field uses tiles when ready and the fallback past them, and the fallback matches the climate at κ 0.5. Checked on a long `/tp` flight |
| Equatorial peaks never get snow, which may look odd | That is the climate as tuned (12 °C at 1250). If Ahmed wants snowy equatorial peaks, the lapse constant is the knob, recorded as Q1 |

## 7. Old worlds

As Ahmed decided:
- `TERRAIN_GENERATOR_VERSION` goes from 1 to 2.
- `FEA_DIGEST_OF_GENERATOR` and every terrain pin gets its version-2 row.
- Worlds `saves/test_1` to `saves/test_8` are deleted in phase 05. Before deleting, the agent lists them with sizes in the phase file. **Ahmed may keep any he names before then.**
- Older worlds still show on the list as made by an older generator, as today.

## 8. Out of scope

- Grass and leaf colour tint by climate. The renderer has no biome tint yet; it would be its own plan.
- Geology (plateaus, basins).
- New biomes and new blocks.
- Changing the climate's own maths beyond 2.3.
- Settlements.

## 9. Phases

Each phase ends somewhere worth stopping, and becomes one phase file with the template's Goal, Todo, Verification, Commits and Issues found. Every phase file also gets plan 004's richer parts:
- read first;
- must NOT build;
- owned paths;
- steps with a *Why*;
- tests with numbers;
- the gate;
- risks and open questions.

Every heavy command goes through `tools/test-or-build.sh`.

### Phase 00: Plan files

- [ ] Copy this file byte for byte to `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md`
- [ ] Create phase files 01 to 05 from the template, every box unticked, each linking back to phase 00
- [ ] Phase 00's own file restates section 1's layer order and that 006 lifts plan 004's rule 3 for biome, surface and features only. It points to plan 004 phase 11's mismatch numbers as the "before"

### Phase 01: Teleport, fly and the tuning readout

The tools come first, so Ahmed can tune everything after with them.

- [ ] Find and fix the black world after a big jump (plan 004 phase 02 Issues found 14), with the three-jump test
- [ ] `GAME_FILL_JUMP` in `run/src/game/fill/`
- [ ] The command box overlay (`run/src/ui/command/`, `run/src/game/command/`): T and `/`, history, Esc, a red error line
- [ ] The parser: every command in 2.6, a latitude preview, and the clamping
- [ ] The teleport latch on `player_t`, its sim handler, `arriving` in survival (saved), and standing on the ground
- [ ] `PLAYER_MOVEMENT_MODE_FLY` and `/fly`, with speed on the wheel and the HUD line
- [ ] `/time day` and `/time hour`
- [ ] The F3 tuning lines. Until phase 03 they show climate values and today's biome, which is useful as the "before"
- [ ] Tests in 5 (teleport, black world, fly), including a headless script that types `/tp` commands as keys
- [ ] READMEs, and `plans/REPO_STRUCTURE.md` for the new directories

**Gate:** Ahmed types `/tp lat 60`, `/tp ~ ~ ~400`, `/fly`, and back to `/tp 0 0`, and reads F3.

### Phase 02: The climate sample and deterministic maths

- [ ] `run/src/math/det/` with tests against libm and a pin
- [ ] The climate cell fields that terrain reads move to `det_*`; climate digests are re-pinned with reasons
- [ ] The driest month's rain per cell (a spare byte, or from harmonics)
- [ ] `terrain/climate_sample/`: the types, `terrain_climate_at`, and `terrain_climate_fallback`
- [ ] `game` fills the sample in column jobs; the far field and mob spawn fill theirs
- [ ] The read reach goes from 96 to 112; the spawn search moves after the climate is prepared
- [ ] `terrain_generate_box`, the far field sampler and `terrain_biome_column_at` take the sample. **The biome rules don't change yet,** so every terrain digest holds
- [ ] Benches: filling a sample, and `terrain_climate_at`

**Gate:** none for Ahmed; the agent shows the F3 values match plan 004's worked examples at three places.

### Phase 03: Biomes from the climate

- [ ] The `climate:` block in all 53 biome YAMLs; the compiler and README; the family coverage test
- [ ] `terrain_biome_choose`: family first, then landform and variety
- [ ] Variety and wetness noise in place of noise temperature and humidity for biomes (the fields stay for anything else that reads them)
- [ ] `bench_biome_vs_class` and the equator-to-pole strip; pictures looked at and recorded
- [ ] The mismatch survey under 5% in every class
- [ ] The F3 readout shows the new biome and family

**Gate:** Ahmed flies the strip with `/tp lat` steps (0, 15, 30, 45, 60, 75) and checks each looks like its climate.

### Phase 04: Snow, ice, sand and trees from the climate

- [ ] The snowline in `surface` and `feature_cold_paint`, with the ragged edge; snow biomes' non-snow tops below it
- [ ] Ice on water by coldest month
- [ ] Desert sand only in Desert and Steppe; `snowy_beach` only in Ice and Tundra
- [ ] Tree and plant rates scaled by rain
- [ ] The far field uses the same rules
- [ ] Tests in 5: equator, pole, 45° mountain, and Ahmed's x -68, y 149 place
- [ ] Benches: `bench_terrain` at or above 1000 columns a second, and `bench_lod`

**Gate:** Ahmed opens a new world, finds a mountain near the equator with `/tp`, flies up with `/fly`, and sees no snow; then `/tp lat 50` and sees snow only above the snowline.

### Phase 05: Version, old worlds and the final check

- [ ] `TERRAIN_GENERATOR_VERSION` 2; version-2 rows for every terrain pin, each with its reason
- [ ] List `saves/test_1` to `test_8` with sizes in the phase file, then delete them (unless Ahmed names one to keep)
- [ ] The full debug and release suites; a release build; the 1 against 16 worker check on the new pins
- [ ] Update plan 004's phase 11 table "after" column from the new survey, in plan 006's phase file (plan 004's files are left as they are)
- [ ] Ahmed's gate notes

## 10. Still open for Ahmed

Each has a default, and none blocks approval.

| # | Question | Default if not answered |
|---|---|---|
| Q1 | Snowy peaks at the equator? With cooling 12 °C per 1000 blocks, even 1250 is about 12 °C, so none | No; the climate decides. Change the lapse constant if wanted |
| Q2 | Keys for the command box | T and `/` |
| Q3 | Fly speed range | 10 to 1000 blocks a second, on the wheel |
| Q4 | Should `/tp` also work in test worlds (no climate)? | Yes, using the fallback climate |
| Q5 | Keep any of `saves/test_1` to `test_8`? | Delete all, as said |

## Verification of this plan

    ls plans/006-*
    grep -L "## Issues found" plans/006-*phase-*.md   # prints nothing
    git status --short                               # only plans/006-* new
