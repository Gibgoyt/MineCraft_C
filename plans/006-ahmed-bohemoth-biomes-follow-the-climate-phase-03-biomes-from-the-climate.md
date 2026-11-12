# 006-ahmed-bohemoth-biomes-follow-the-climate — phase 3: Biomes from the climate

**Status:** in progress: family table, fields and the climate plumbing landed   (updated 2026-11-12)

Read first: `plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-00-vision-and-plan-files.md`, then the master `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md` (sections 0, 1, 2.4, 2.7, 4, 5, 6 and 9). Every number here comes from the master. Where this file adds a number the master does not give, it says so and why.

## Goal

`terrain_biome_choose` picks a biome in two steps: first the **family** from the climate at the column's own height (Ice, Tundra, Boreal, Continental, Temperate, Mediterranean, Steppe, Desert, Savanna, Monsoon, Rainforest; master 2.4), then, **only among that family's members**, today's nearest-box choice over the landform axes (continentalness, erosion, weirdness, height, hydrology's kind) plus two new noise fields, *variety* and *wetness*, that replace noise temperature and humidity. Each of the 53 biome YAMLs under `config/biomes/` says which families it belongs to; `compile/src/game_tables/biomes/` refuses a surface biome without one, and a compile-time test proves every family has a member for every landform box. Over 8 seeds, the share of land whose biome family differs from its climate class is **under 5% in every class** (today 16% to 81%). `bench_biome_vs_class` and an equator-to-pole strip draw it, and the pictures are looked at and recorded. F3 shows the new biome and its family. Snow, ice, sand and tree rules are not changed yet (phase 04), and `bench_terrain` stays at or above 1000 columns a second.

## Where this fits

**The vision.** Ahmed: "run through our climate model first and then decide biome systems", and "climate decides what kind of land, and noise only adds variety inside it" (decision, 2026-11-12). This phase is that sentence in code. It is the core of plan 006: the vegetation layer that plan 004 phase 00 deferred.

**What later phases read from it:**

| Reader | What it reads |
|---|---|
| Phase 04 | The family of each column (sand only in Desert and Steppe; `snowy_beach` only in Ice and Tundra), and each biome's YAML rain middle for tree rates |
| Phase 05 | The final digests become version-2 rows; the survey numbers become the "after" column of plan 004 phase 11's table |
| Later plans (settlements, economy) | The family as the land's kind: farming where Temperate and wet, herding in Steppe |

**What this phase must NOT build:**

- No change to height, density, hydrology, rivers, caves or ores (master section 3). Their digests hold.
- No snowline, ice, sand or tree-rate rule (phase 04). A biome whose `top` is `snow_block` still lays it this phase; the *place* it is chosen changes, the rule does not.
- No new biomes, no new blocks, no grass tint (master section 8).
- No change to the climate's maths beyond phase 02's `det_*`.

## Ahmed's words

- "Snow just spawns absolutely freiking randomly. Look at ~/Pictures/1.png please! temperature is 24 deg C but there is snow!"
- "at world generation time please run through our climate mode first and then decide biome systems!!"
- "biome creation needs to properly follow ... align biome with climate"

The screenshot is x -68, y 149, z 443, 0.35° N, 24.1 °C, standing on `snow_block`: today `snowy_slopes` or `grove` wins wherever noise temperature is -0.15 or below at ground 300 to 500, about 42% of such ground, equator included (master section 0). After this phase that place is in the Rainforest, Monsoon or Savanna family, and no snow biome is a member of those.

---

## Background, explained

### Today's choice: nearest box in 6-D

Each biome has a box `[low, high]` on six axes: noise temperature, humidity, continentalness, erosion, weirdness, and ground height (scaled by `BIOME_HEIGHT_SCALE`). `terrain_biome_choose` (`run/src/terrain/biome/index.c:60-92`) loops over every biome whose `where` matches the column's kind, sums `biome_gap` on each axis (0 inside the box, the distance outside), and keeps the smallest sum, ties to the higher `priority`. **Example:** at temperature -0.2, height 400, `snowy_slopes` (temperature `[-1, -0.15]`, height `[300, 500]`) has gap 0 on both, so it wins against `jungle` whatever the latitude.

The noise temperature is plain 2D noise: a 4096-block cell, 4 octaves, persistence 0.45 (`FIELDS_SHAPE_TEMPERATURE`, `run/src/terrain/fields/index.c:30-36`). It has no latitude in it.

### Families from the climate (master 2.4)

Plan 004 section 6.10's classes, but worked out **per column at its own height** from phase 02's `terrain_climate_at`:

| Family | Rule |
|---|---|
| Ice | warmest month < 0 °C |
| Tundra | warmest 0 to 10 |
| Boreal | coldest ≤ -3, warmest < 22 |
| Continental | coldest ≤ -3, warmest ≥ 22 |
| Temperate | coldest > -3, rain all year |
| Mediterranean | coldest > -3, dry summer |
| Steppe | rain < P_th |
| Desert | rain < P_th / 2 |
| Savanna | coldest ≥ 18, a dry season |
| Monsoon | coldest ≥ 18, monsoon flag |
| Rainforest | coldest ≥ 18, driest month ≥ 60 mm |

The order of tests, and $P_{th}$, are plan 004's (`run/src/climate/season/`); the family function is the same as the class function with the column's z in place of the cell's mean ground. **Example:** a cell whose `t_mean` at sea level is 14 °C with a 10 °C swing: at z 80, warmest is $14 + 10 - 0.96 = 23.0$ and coldest $14 - 10 - 0.96 = 3.0$: Temperate. A peak in that cell at z 1100: warmest $24 - 13.2 = 10.8$, coldest $-9.2$: Boreal. At z 1250: warmest $9.0$: Tundra. "So a peak can be Tundra while its valley is Temperate" (master 2.4).

### Within the family: landform plus variety

Step 2 is today's nearest box, but only over the family's members and only on continentalness, erosion, weirdness, height and kind (the **landform** axes), plus:

- **variety:** a new low-frequency 2D noise, chooses for example forest against birch forest;
- **wetness:** a new noise biased by the column's yearly rain, chooses for example plains against swamp.

They never choose between snow and jungle, because they only ever see one family's members.

**Fallback inside a family:** if no member's landform box fits (Desert at 900 blocks), the nearest member by landform distance wins. Never a biome from another family (master 2.4).

### The mismatch survey

Plan 004 phase 11's survey measured "biome disagrees": a desert or badlands biome in a class that is not Desert or Steppe, or another biome in Desert or Steppe. Results: Desert 81.0%, Steppe 80.9%, Ice 17.6, Tundra 16.4, Rainforest 16.3, Monsoon 16.9, Savanna 16.8, Mediterranean 21.5, Temperate 17.9, Boreal 19.9, Continental 0 (never occurs). **This phase's survey is stricter:** a column disagrees when its biome is not a member of the family its climate class names. Both numbers are printed, so the "before" stays comparable.

---

## Design

### The YAML block

Master 2.4 says each `config/biomes/<name>/index.yaml` gets a new `climate:` block with `families`, optional `t_warm`, `t_cold`, `rain_mm` limits, and `variety`, `wetness` ranges. **The key `climate:` is already taken**: today it holds the noise boxes (`climate: { temperature: [...], humidity: [...], height: [...] }`, for example `config/biomes/snowy_slopes/index.yaml`; `GZ_CLIMATE_FIELDS` in `compile/src/game_tables/biomes/index.c:48`). **Default:** rename today's block to `landform:` (keeping `continentalness`, `erosion`, `weirdness`, `height`, `z`; dropping `temperature` and `humidity`), and make `climate:` the new block. The compiler refuses the old `temperature` / `humidity` keys with a line saying where they went. Recorded in Issues found when done.

```yaml
# config/biomes/taiga/index.yaml, after
name: taiga
where: land
landform: { height: [-1000, 300] }
climate: { families: [boreal], variety: [-1, 0.3], wetness: [0.15, 1], rain_mm: [300, 1200] }
```

`rain_mm`'s middle is phase 04's $P_{\text{biome-mid}}$; it is written now so phase 04 has it.

### Membership (master 2.4 table, finished against the pictures)

| Family | Members (today's names) |
|---|---|
| Ice | frozen_peaks, ice_spikes, snowy_plains, frozen_ocean, frozen_river, snowy_beach |
| Tundra | snowy_slopes, snowy_taiga, snowy_plains, grove, jagged_peaks, stony_shore |
| Boreal | taiga, old_growth_spruce_taiga, old_growth_pine_taiga, grove, windswept_forest |
| Continental | forest, birch_forest, plains, meadow |
| Temperate | forest, flower_forest, plains, meadow, birch_forest, swamp, dark_forest |
| Mediterranean | savanna_plateau, plains, sparse forest (wooded_badlands as rare) |
| Steppe | savanna, plains, windswept_savanna |
| Desert | desert, badlands, eroded_badlands |
| Savanna | savanna, savanna_plateau |
| Monsoon | jungle, sparse_jungle, bamboo_jungle |
| Rainforest | jungle, bamboo_jungle, mangrove_swamp |

The master says `old_growth_taiga`; the repo has `old_growth_spruce_taiga` and `old_growth_pine_taiga`, both put in Boreal by default. The master's "sparse forest" has no biome of that name; **default:** `forest` with a low tree rate is left to phase 04's rain scaling, and Mediterranean lists `forest` too. Biomes the table does not name (`cherry_grove`, `old_growth_birch_forest`, `sunflower_plains`, `stony_peaks`, `windswept_hills`, `windswept_gravelly_hills`, `mushroom_fields`, the oceans, `river`, `beach`) get families in step 1, each with a one-line reason in its README row. `where: underground` biomes (`deep_dark`, `dripstone_caves`, `lush_caves`) are exempt: caves don't follow surface climate.

### Oceans

Ocean biomes (`warm_ocean`, `lukewarm_ocean`, `ocean`, `cold_ocean`, `frozen_ocean` and the `deep_*`) choose by the family of the water's own climate: **default** Rainforest, Monsoon, Savanna → warm/lukewarm; Temperate, Mediterranean, Continental, Steppe, Desert → ocean/lukewarm; Boreal, Tundra → cold; Ice → frozen.

### The coverage test

A compile-time check in `compile/src/game_tables/biomes/`: for every family and every landform box of every `where` (land, ocean, deep_ocean, river, beach, stony_shore) the family has at least one member with that `where`. The tool refuses the build, naming the family and the `where`.

### The pictures (master 2.7)

- `bench_biome_map --climate` (`bench/biome_map/`): biomes with the climate snapshot.
- New `bench/biome_vs_class/`: three pictures of the same 16 km at the same scale: biome colours, climate class colours (plan 004 phase 09's palette), a mismatch map (red where family ≠ class); prints the mismatch share per class.
- An equator-to-pole strip at x = 0, 4 km wide, one tall picture.

## Owned paths

This phase may edit these, plus this file:

- `config/biomes/*/index.yaml` (all 53), `config/biomes/README.md`
- `compile/src/game_tables/biomes/` (the reader, the refusals, the coverage test)
- `run/{src,include}/terrain/biome/` (`terrain_biome_choose`, the inputs struct)
- `run/{src,include}/terrain/fields/`: two new fields, variety and wetness; temperature and humidity stay for anything else that reads them
- `tests/run/terrain/biome/`, `tests/run/terrain/fields/`
- `bench/biome_map/`, `bench/biome_vs_class/` (new)

**Wiring outside them:**

- `run/src/game/corner/` (F3): biome and family from the new choice
- Every `run_terrain_*` digest that moves because biomes moved: re-pinned with "plan 006 phase 03: biomes follow the climate" (the generator version itself is phase 05)
- `run/src/mob/spawn/`: nothing, unless a mob's biome list names a biome that can no longer occur where it used to; recorded if so
- `plans/REPO_STRUCTURE.md`: `bench/biome_vs_class`

## Steps

1. **Assign every biome a family** in a table in this file first, using master 2.4 and the extra biomes above, and look at it against plan 004 phase 11's class shares. *Why: the YAMLs are data; agreeing the table first stops 53 edits being made twice.*
2. **The compiler:** `landform:` and the new `climate:`, the refusals, the coverage test. *Why: a biome without a family must not build.*
3. **The 53 YAMLs** and `config/biomes/README.md`. *Why: master 2.4 "the exact membership is data".*
4. **Variety and wetness noise** in `terrain/fields/`. *Why: they replace noise T and H for biomes only.*
5. **`terrain_biome_choose`**: family from `terrain_climate_at` at the column's height, then landform plus variety among the family, fallback inside the family. *Why: the core rule.*
6. **`bench_biome_vs_class` and the strip;** draw for seed `2198175988018515243` and two others; look at them and record what is seen here.
7. **The mismatch survey** over plan 004 phase 11's 8 seeds; tune membership and ranges until every class is under 5%.
8. **F3** shows the new biome and family.
9. **Benches:** `bench_terrain` at or above 1000 columns a second at its slowest place. Re-pin digests with reasons. READMEs. Tick as each lands.

## Todo

From the master (section 9, phase 03):

- [ ] The `climate:` block in all 53 biome YAMLs; the compiler and README; the family coverage test
- [ ] `terrain_biome_choose`: family first, then landform and variety
- [ ] Variety and wetness noise in place of noise temperature and humidity for biomes (the fields stay for anything else that reads them)
- [ ] `bench_biome_vs_class` and the equator-to-pole strip; pictures looked at and recorded
- [ ] The mismatch survey under 5% in every class
- [ ] The F3 readout shows the new biome and family

Expanded:

- [x] The family table for all 53 biomes agreed in this file (step 1), including the biomes master 2.4 does not name and the oceans
- [ ] The `climate:` key clash resolved (default: today's block renamed `landform:`), recorded in Issues found
- [x] `old_growth_taiga` in the master mapped to `old_growth_spruce_taiga` and `old_growth_pine_taiga`
- [ ] Ahmed's place (x -68, y 149) on the screenshot's seed is not a snow biome
- [ ] Every height, density, hydrology, cave and ore digest unchanged; biome/surface/feature digests re-pinned with reasons
- [ ] `bench_terrain` slowest place at or above 1000 columns a second, recorded
- [ ] `standards_lint`, `clang_format` and the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `every_family_has_a_biome_for_every_landform` (compile time) | Each family × each surface `where` | At least one member | A family is empty for some `where` |
| `a_biome_without_a_family_is_refused` (`tests/compile/...`) | A YAML with no `families` | Tool exits non-zero, names the line | The refusal is missing |
| `the_old_temperature_key_is_refused_with_a_hint` | `climate: { temperature: ... }` | Refused, says "moved: landform/variety" | Old files silently accepted |
| `the_family_follows_the_worked_example` (`run_terrain_biome`) | Hand-built sample: sea-level mean 14 °C, swing 10 | z 80 Temperate; z 1100 Boreal; z 1250 Tundra | Lapse or order of tests wrong |
| `the_choice_never_leaves_the_family` | 1 000 000 random inputs | Chosen biome is always a member of the input's family | The fallback crosses families |
| `the_screenshot_place_is_not_snow` | Seed of `~/Pictures/1.png`, x -68, y 149, climate on | Biome is not `snowy_slopes`, `grove` or any Ice/Tundra member | Biomes still read noise T |
| `mismatch_is_under_five_percent_in_every_class` (`bench_biome_vs_class`, release) | Plan 004 phase 11's 8 seeds, pole to pole | Every class < 5% (today 16.3 to 81.0; Continental is 0 land) | Membership or ranges off |
| `one_and_sixteen_workers_choose_the_same_biomes` | 8 by 8 columns on 1 and 16 workers | Same bytes | Order dependence |
| `pole_land_is_ice_or_tundra` | 75° N, all land columns above sea | 100% Ice or Tundra family | Family rule wrong at the pole |
| Bench `bench_terrain` | Slowest place | ≥ 1000 columns/s (1134 before) | |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 7200 build/release/bin/bench_biome_vs_class --seeds 8 --pictures /tmp/mc_c_biome_vs_class
    tools/test-or-build.sh timeout 600 build/release/bin/bench_biome_map --climate /tmp/mc_c_biome_map 2198175988018515243
    tools/test-or-build.sh timeout 600 build/release/bin/bench_terrain
    pgrep -af "$PWD/build"    # must print nothing

(The `bench_biome_vs_class` arguments are this file's proposal; use what the tool ends up taking and correct this section.)

## Gate

Ahmed flies the strip with `/tp lat` steps (0, 15, 30, 45, 60, 75) and checks each looks like its climate, reading the F3 family line at each.

## Risks and open questions

| Risk or question | Default |
|---|---|
| A family has no biome for some landform, giving odd mixes | The compile-time coverage test; landform-only fallback inside the family (master section 6) |
| Biome borders follow the 64-block cell grid | The warp in `terrain_climate_at`, plus variety noise; checked on the pictures (master section 6) |
| Terrain throughput falls below 1000 columns a second | Integer reads; bench before and after (master section 6) |
| The `climate:` key already exists in every YAML | Rename today's block `landform:` (above) |
| Continental never occurs (plan 004 phase 11), so its members are never chosen | Expected until the climate's swing knots change; not this plan's to fix (master section 8) |
| The 5% target is stricter than plan 004's measure | Both measures printed; 5% is on the strict one |
| Mob spawn lists that name biomes now rare in a place | Recorded; no mob change in this plan |
| Q1: snowy peaks at the equator? | No; the climate decides (master Q1) |

## Commits

Appended as they land:

## Issues found

1. **Step 1, the family table for all 53 biomes** (2026-11-12). Master 2.4's members, then the biomes it does not name, each
   with its reason. A family chooses only among its own members, and the coverage test needs every family to have a member
   for each of the six surface `where`s, so the water and shore biomes are spread over every family. Underground biomes
   (`deep_dark`, `dripstone_caves`, `lush_caves`) have no family: caves do not follow the surface's climate.

   | Family | Land | Ocean, deep ocean | River, beach, stony shore |
   |---|---|---|---|
   | Ice | frozen_peaks, ice_spikes, snowy_plains, snowy_slopes | frozen_ocean, deep_frozen_ocean | frozen_river, snowy_beach, stony_shore |
   | Tundra | snowy_slopes, snowy_taiga, snowy_plains, grove, jagged_peaks, windswept_gravelly_hills | cold_ocean, deep_cold_ocean | river, snowy_beach, stony_shore |
   | Boreal | taiga, old_growth_spruce_taiga, old_growth_pine_taiga, grove, windswept_forest, windswept_hills, stony_peaks | cold_ocean, deep_cold_ocean | river, beach, stony_shore |
   | Continental | forest, birch_forest, old_growth_birch_forest, dark_forest, plains, sunflower_plains, meadow, swamp, windswept_hills, stony_peaks | ocean, deep_ocean | river, beach, stony_shore |
   | Temperate | forest, flower_forest, plains, sunflower_plains, meadow, birch_forest, old_growth_birch_forest, swamp, dark_forest, cherry_grove, windswept_forest, mushroom_fields, stony_peaks | ocean, deep_ocean | river, beach, stony_shore |
   | Mediterranean | savanna_plateau, plains, sunflower_plains, forest, meadow, wooded_badlands (rare: variety 0.6 up), mushroom_fields, stony_peaks | ocean or lukewarm_ocean, deep_ocean or deep_lukewarm_ocean | river, beach, stony_shore |
   | Steppe | savanna, plains, windswept_savanna, savanna_plateau, wooded_badlands (rare), stony_peaks | ocean or lukewarm_ocean, deep_ocean or deep_lukewarm_ocean | river, beach, stony_shore |
   | Desert | desert, badlands, eroded_badlands, stony_peaks | ocean or lukewarm_ocean, deep_ocean or deep_lukewarm_ocean | river, beach, stony_shore |
   | Savanna | savanna, savanna_plateau, windswept_savanna, stony_peaks | warm_ocean or lukewarm_ocean, deep_lukewarm_ocean | river, beach, stony_shore |
   | Monsoon | jungle, sparse_jungle, bamboo_jungle, mangrove_swamp | warm_ocean or lukewarm_ocean, deep_lukewarm_ocean | river, beach, stony_shore |
   | Rainforest | jungle, bamboo_jungle, mangrove_swamp, sparse_jungle | warm_ocean or lukewarm_ocean, deep_lukewarm_ocean | river, beach, stony_shore |

   The reasons for what master 2.4 leaves out or adds:

   - `old_growth_taiga` is `old_growth_spruce_taiga` and `old_growth_pine_taiga` (the default), both Boreal.
   - The master's "sparse forest" is `forest` in Mediterranean (the default); phase 04's rain factor thins it.
   - `snowy_slopes` is in Ice as well as Tundra: Ice land at 300 to 500 blocks would otherwise fall back to snowy plains or
     frozen peaks. `snowy_plains` is in both, as the master has it.
   - `stony_shore` is in every family: it is the only `where: stony_shore` biome. The master's Tundra row named it.
   - `windswept_gravelly_hills` (noise temperature -1 to -0.45 before): Tundra, the cold windswept hills.
   - `windswept_hills` (-0.45 to -0.15 before): Boreal and Continental. `windswept_forest`: Boreal and Temperate. The two have
     the same landform box, so `variety` splits them where both are members (forest below 0, hills above).
   - `stony_peaks` (bare rock over 500 blocks, noise temperature over -0.15 before): every family with high ground that is not
     snowy or tropical: Boreal to Savanna. Rainforest and Monsoon keep their jungle up the slopes (a cloud forest; the
     fallback picks the nearest jungle by landform), and Ice and Tundra have their snowy peaks.
   - `cherry_grove`, `flower_forest`: Temperate only. `old_growth_birch_forest`, `sunflower_plains`, `dark_forest`, `swamp`:
     Temperate and Continental (Continental never occurs today, plan 004 phase 11, but needs members); `sunflower_plains` is
     also Mediterranean.
   - `meadow` (300 to 500 blocks, dry) is in Continental, Temperate and Mediterranean, as the high grass of each.
   - `mushroom_fields`: Temperate and Mediterranean, the mild islands; its priority 4 and landform box keep it rare.
   - `savanna_plateau` is in Steppe as well as Mediterranean and Savanna: Steppe land at 150 to 500 blocks.
   - `wooded_badlands` is Mediterranean and Steppe, rare by `variety` 0.6 to 1 (the master's "as rare").
   - `savanna` gets priority 1 and `variety` -0.2 to 1, so that in Steppe, where `plains` has the same box, plains wins only
     at low variety; before, the two met on noise temperature.
   - `mangrove_swamp` is in Monsoon as well as Rainforest, `sparse_jungle` in Rainforest as well as Monsoon: tropical wet
     coasts and the drier edge of each.
   - Oceans as the phase file's default: Rainforest, Monsoon, Savanna take `warm_ocean` (priority 1, `variety` below 0.5)
     and `lukewarm_ocean` (0 up); Temperate, Continental take `ocean`; Mediterranean, Steppe, Desert take `ocean` (variety
     below 0) and `lukewarm_ocean` (0 up), and the same split for the deep ones (`deep_ocean`, `deep_lukewarm_ocean`; there
     is no deep warm ocean); Boreal and Tundra take the cold ones, Ice the frozen ones.
   - Rivers: `frozen_river` in Ice, `river` in the rest. Beaches: `snowy_beach` in Ice and Tundra (phase 04 reads this), `beach`
     in the rest.

   Against plan 004 phase 11's class shares (land): Temperate, Mediterranean, Boreal and the tropical classes hold most of the
   land, and each has a forest, a grassland and a high-ground member; Continental never occurs. Each land biome's `rain_mm` is
   written now for phase 04's middle (`config/biomes/README.md`).
