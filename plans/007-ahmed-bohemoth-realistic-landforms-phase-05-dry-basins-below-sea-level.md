# 007-ahmed-bohemoth-realistic-landforms — phase 5: Dry basins below sea level

**Status:** not started   (updated 2026-11-12)

Read first: `plans/007-ahmed-bohemoth-realistic-landforms-phase-00-vision-and-plan-files.md`, then the master `plans/007-ahmed-bohemoth-realistic-landforms.md` (sections 0 "What is in the way", 3.2, 3.8, 4, 8, 12 Q4). Then:

- plan 004's master 2.1 and 6.6, and its phase 05 Issues found 3 ("The basin heat term can't fire on any cell today");
- `run/src/terrain/hydrology/README.md` in full, especially "What an opened voxel holds" and its three rules.

Every number here comes from the master unless it says otherwise.

## Goal

Some inland lowlands sink **below sea level and stay dry**: plan 004's "places available below sea level, similar to the dead sea".

- **By default:** 1 to 3 per 64 by 64 km map (averaged over 8 seeds), each 2 to 6 km across, with floors from -40 to -100.
- **A closed rim** of dry ground at least `softness + 3` high surrounds each.
- **Inside:** open voxels at or below 0 are **air**, not water. Caves under and beside a basin never put water against air.
- **A new hydrology kind,** `TERRAIN_HYDROLOGY_KIND_BASIN`, marks dry land below 0, and biomes treat it as land.
- **The climate counts basin cells as land,** so plan 004's basin heat (2.5 °C per 100 blocks below sea level, `CLIMATE_BASIN_C_PER_BLOCK`) finally fires. A floor at -100 is 3.7 °C warmer than the sea-level mean there.

The floor stops at -100, so `TERRAIN_DENSITY_HEIGHT_MIN` stays -116 and the lava separation in hydrology stands.

**Proofs:**

- the whole-box "nothing placed here can flow" test passes on basin boxes on 8 seeds;
- pins move only where basins are, each with this phase's reason;
- 1 and 16 workers agree;
- `bench_terrain` holds.

## Where this fits

**The vision.** Plan 004 built the basin heat term and left it waiting: "The terrain makes no such land today. A later geology plan will." Plan 004 phase 05 Issues found 3 named what stands in the way: the climate marks every cell with ground at or below 0 as water. This phase is the geology plan's part that removes that, end to end.

**What reads it:**

| Reader | What |
|---|---|
| The density and hydrology | The per-column surface water level: 0, or "none" in a basin |
| Biomes (plan 006) | `TERRAIN_HYDROLOGY_KIND_BASIN` as land; the climate there (hot, and dry at desert latitudes) picks desert or steppe |
| The climate | The dry mask through the estimate seam; basin cells are land; basin heat fires |
| The HUD and F3 | A hotter temperature on the basin floor |
| `bench_landform` | Basin count and floors (master section 4) |

**What this phase must NOT build:**

- **No salt lake** in the basin, unless Ahmed says yes to master Q4's lake option. A lake inside a dry basin needs its own water level and walls, which is more of this phase's hardest part. Default: bone dry.
- No basin deeper than -100. Hydrology's near-surface flood band reaches $-116 - 26 - 12 = -154$, and aquifer lava starts at -160; a deeper floor would need those rules reworked.
- No change to the climate's maths beyond the water rule.
- No change to plateaus, ranges, slopes or knobs (phases 03 and 04).

## Ahmed's words

- From plan 004 (2026-11-02): "places available below sea level, similar to the dead sea, and these areas might be very hot"
- From plan 004 phase 05 Issues found 3: "A geology plan that makes dry basins needs phase 03's water rule to change too; that's for that plan."

---

## Background, explained

### Why water fills everything below 0 today

`density_block_of` (`run/src/terrain/density/index.c:505-516`) turns every non-stone voxel at or below `MC_WORLD_Z_SEA` (0) into water. Hydrology then:

- cuts rivers;
- marks "Lake" where ground is below 0 inland (`run/src/terrain/hydrology/README.md`, the kind table);
- floods anything caves open near the surface with "water up to `z = 0`" (its "sea's rule").

So any hollow below 0 is a lake.

### The rule that keeps water from flowing

Hydrology's README: "Nothing placed here can flow: no fluid beside air, no fluid over air, and no water touching lava." Its rules 1 to 3 build stone walls where two neighbouring places would hold different things.

A dry basin puts air below 0 right next to land whose rule says water below 0. **So every place where those two rules meet must be stone.** This phase builds that wall, from the surface down.

### A closed rim, by construction

A **contour** of a smooth field round a region where the field is high is a closed curve. The region $B \ge 0.8$ of a smooth field whose share above 0.8 is about 10% is a set of separate islands, each with a closed boundary. So the ring $0.72 \le B < 0.8$ round each island is closed too.

If every column in that ring has ground at least `softness + 3` above 0, then, because the density never leaves ground missing below `height - softness` (density README), every voxel of the ring from `z = 0` up to `z = 3` is stone. **The basin is sealed at sea level all round.**

### Basin heat, worked

Plan 004 master 6.6:

$$T = T_{\text{sea level}} - \Gamma z + B_{\text{heat}}, \qquad B_{\text{heat}} = 0.025 \cdot \max(0, -z) \text{ on land}$$

At $z = -100$: $-\Gamma z = +1.2$ and $B_{\text{heat}} = +2.5$, so **+3.7 °C** over the sea-level mean. At 28° N (plan 004's worked example 3 uses -150 there), a basin is a hot, dry hollow.

---

## Design

### The field and the depression

- **`landform/basin` $B$:** cell 8192, 2 octaves, persistence 0.5, on the landform lattice (master 3.3), spread to $[-1, 1]$. **1 new noise field.**
- **Depression:**

$$\text{basin} = (\text{base} + 100) \cdot S_B\!\left(\frac{B - 0.8}{0.2}\right)_{+} \cdot \text{inland}(c) \cdot \left(1 - \frac{P}{65}\right)_{+}$$

  - $S_B$ is concave, with a flat floor and steep sides: knots 0: 0, 0.15: 0.35, 0.35: 0.80, 0.6: 0.97, 1: 1.
  - `inland` is today's spline: 0 at the coast, 1 inland.
  - $(1 - P/65)_+$ keeps basins off plateaus (lift under 10% of $P_{\max}$).
  - So the floor is at least $\text{base} - (\text{base} + 100) = -100$.
- **Hills are multiplied by $(1 - S_B)$,** so a basin floor is flat. **Knobs stay,** as buttes in a basin are fine.
- **`lift`** $= P - \text{basin}$, so a basin floor at -80 counts as 80 below its surroundings on the biome height axis.

### The rim

For $0.72 \le B < 0.8$, with the inland gate non-zero and the plateau gate non-zero, $h = \max(h,\ s + 3)$, where $s$ is the column's softness. `max` is a comparison, not libm.

**At $B = 0.8$ exactly** the depression is 0, so the height is continuous across the ring's inner edge. At the outer edge (0.72) the max may add a step up to $s + 3$ over land that was below it. That happens only where the land was below $s + 3$: low ground and lakes next to a basin. **Default:** accept a small levee. If it shows as a wall in pictures, the max is blended in over $B$ from 0.70 to 0.72.

### The surface water level

A per-column value $w$, from the profile batch (no new noise):

- $w = 0$ (the sea's level): every column as today.
- $w = \text{NONE}$ (no water at all): a column with $B \ge 0.8$ whose depression is above 0 after the gates. **The basin.**

It travels in the profile (`terrain_density_profile_t` gains `uint8_t dry` in its padding; still 16 bytes) and through hydrology's column.

**Readers:**

| Reader | Today | With $w$ |
|---|---|---|
| `density_block_of` | non-stone at or below 0 is water | water only if the column's $w$ is 0; else air |
| Hydrology's kinds | ground below 0 inland is Lake | a dry column with ground below 0 is `TERRAIN_HYDROLOGY_KIND_BASIN`; River and Lake never in a dry column |
| Hydrology's sea's rule for opened voxels ("water up to `z = 0`") | everywhere above the band | dry columns: air |
| Hydrology rule 2 (a deeper voxel beside the sea's rule is stone where that rule says water) | beside the sea's rule | also beside any column whose rule at that height differs: a voxel within 2 blocks of a column boundary where one side would hold water and the other air at that height is stone |
| Rivers | cut to `z = 0`, water | a river's carve never reaches a dry column: $B \ge 0.72$ (the ring and inside) sets `reach` to 0, so the river stops at the rim as it stops at high ground today |

### The climate

- **`terrain_height_estimate_area`** gains an optional out-array `uint8_t *dry` (nullptr skips it), the profile's `dry` at each point. **`climate_open_ground`'s ground callback** (`run/src/climate/index.c:83`) passes it through.
- **`climate_cell`'s water rule** (`run/src/climate/cell/index.c:72-78`) becomes: water if $h \le 0$ **and not dry**. Dry cells are land.
- **Heat** (`run/src/climate/heat/index.c:10-11`) already adds $B_{\text{heat}}$ on land below 0, so nothing more is needed there.
- **Climate digests move only where basins are.** Each re-pin names this phase. A test on a synthetic ground with a dry hollow gives plan 004's worked example 3 numbers.

### Share and size

Thresholds are tuned against master section 4 with `bench_landform`: 1 to 3 basins per 64 by 64 km (averaged over 8 seeds), 2 to 6 km across, floors -40 to -100. The knob is the 0.8 threshold (and the ring's 0.72 with it), moved 0.02 at a time, with every try recorded.

---

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/terrain/landform/`, `run/{src,include}/terrain/density/`, `run/{src,include}/terrain/hydrology/`, `run/{src,include}/terrain/biome/` (the new kind's where-mapping only)
- `run/src/terrain/index.c`, `run/include/terrain/index.h` (the estimate's `dry`)
- `run/src/climate/index.c` (the callback), `run/src/climate/cell/index.c` (the water rule), their headers and READMEs: the only climate edits
- `tests/run/terrain/`, `tests/run/climate/` (new tests; re-pins with reasons)
- `bench/landform/` (basin measures if phase 01 left them out), `bench/landform/README.md`

## Steps

1. **Read** hydrology's fill, flood and rule code, and the climate's cell water rule. *Why: the fluid rules are the hard part, and they are spread over three places.*
2. **`landform/basin`, the depression, the flat floor, `lift`.** *Why: the shape first, checked on pictures before water moves.*
3. **The rim's max,** with its test (every ring column at least `softness + 3`).
4. **`dry` in the profile;** `density_block_of` reads it. *Why: the first voxel rule.*
5. **Hydrology:** the basin kind, rivers stopping at $B \ge 0.72$, the sea's rule and rule 2 for dry columns. The whole-box fluid test runs on basin boxes. *Why: water must never touch the basin's air.*
6. **The climate's water rule** through the estimate's `dry`; the synthetic worked-example test.
7. **Re-pins** with reasons; 1 against 16 workers.
8. **`bench_landform`:** basin count and floors on 8 seeds; tune the threshold.
9. **`bench_terrain`.** Pictures of a basin (relief, a slice through it from `bench_world_map`) looked at and described.
10. READMEs; tick each todo as it lands.

## Todo

- [ ] `landform/basin` on the lattice; the depression with $S_B$, the inland and plateau gates; floor at least -100; flat floors (hills × $(1 - S_B)$); `lift` includes the basin
- [ ] The rim: $h \ge s + 3$ on $0.72 \le B < 0.8$, tested on every ring column of 8 seeds over 64 km
- [ ] `dry` in `terrain_density_profile_t`; `density_block_of` gives air in dry columns
- [ ] `TERRAIN_HYDROLOGY_KIND_BASIN`, mapped to land for biomes; rivers' `reach` 0 at $B \ge 0.72$
- [ ] Hydrology's sea's rule and rule 2 extended to dry columns; the whole-box "nothing placed here can flow" test passes on basin boxes on 8 seeds
- [ ] `terrain_height_estimate_area`'s `dry` out-array; the climate's callback passes it; `climate_cell` water rule `h <= 0 && !dry`
- [ ] A climate test on a synthetic dry hollow gives plan 004's worked example 3; at a real basin, F3's temperature at the floor equals the formula
- [ ] `TERRAIN_DENSITY_HEIGHT_MIN` still -116; the limits test passes
- [ ] Every moved pin re-pinned with this phase's reason; 1 against 16 workers the same bytes
- [ ] `bench_landform` on 8 seeds: 1 to 3 basins per map averaged, floors -40 to -100
- [ ] `bench_terrain` at least 1000 everywhere
- [ ] Pictures looked at and described here
- [ ] READMEs (`hydrology/`, `density/`, `landform/`, `climate/cell/`)

## Tests

- **`a_basin_s_rim_is_closed_dry_land`** (`tests/run/terrain/landform/`). On 8 seeds over 64 km at step 4:
  - every column with $0.72 \le B < 0.8$ and non-zero gates has profile height at least softness + 3;
  - for 200 basins (or all found), a flood from the deepest column through columns with ground at or below 0 never leaves $B \ge 0.72$.

  Fails if a ring has a gap.
- **`no_water_touches_a_basin_s_air`** (`tests/run/terrain/hydrology/`, the existing whole-box fluid test run on basin boxes). For each basin found on 3 seeds:
  - boxes of 64 by 64 by 160 (z from -150 to 9) across its rim, with every voxel opened as caves would open them;
  - no fluid voxel beside air, no fluid over air, and no water touching lava.

  Fails on any one.
- **`a_basin_column_is_dry_and_land`.** A dry column's voxels from its ground up to `z = 0` are air; its kind is `BASIN`; biomes choose from the land set.
- **`rivers_stop_at_a_basin_s_rim`.** No `River` column has $B \ge 0.72$ on 8 seeds.
- **`the_climate_counts_a_dry_basin_as_land`** (`tests/run/climate/cell/`). A synthetic ground with a dry hollow to -100, 3 km across, at 28° N, 4000 blocks inland: its cells are land. `climate_normals_at` at the floor gives plan 004's example 3 numbers, adjusted from -150 to -100 (the test states both).
- **`the_height_limits_add_up`** (existing). -116 still holds.
- **`one_and_sixteen_workers_give_the_same_bytes`.** On the new pins.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_climate_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1800 ctest --preset release -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 2198175988018515243 12636146071196537873 5569068473609503046 1
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 42 20261104 9999999999999 18446744073709551557
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed, in a new world:

1. `/tp` to `bench_landform`'s "lowest dry land below 0" line.
2. Stands on the floor: F3 shows z about -80, dry air, and a temperature about 3 °C warmer than the rim's sea-level ground nearby.
3. Climbs the rim and sees the basin from above.
4. Opens the Atlas: contours ring the hollow.

He answers master Q4: how many, how deep, and whether he wants a salt lake later.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Water touches air somewhere at a rim (3D noise, a cave, a river end) | The rim's $s + 3$; rule 2 extended; rivers stopped at 0.72; the whole-box test on 8 seeds; this phase does not close until it passes |
| The levee at the ring's outer edge shows as a wall next to a lake | Blend the max in over 0.70 to 0.72 (Design) |
| Hydrology's fill reads `dry` per lattice column, and a voxel between a dry and a wet lattice column must pick one | `dry` is per lattice column. A voxel takes the nearest lattice column's value, rounding half towards the south-west, a fixed rule. The wall (rule 2) puts stone where the two meet |
| Climate tiles' coarse samples (every 64 blocks) miss a narrow dry basin | Basins are 2 to 6 km; at 64 blocks a cell, at least 30 cells across |
| Basins too rare or too common | The 0.8 threshold, recorded tries |
| A basin under a desert biome looks like a plain desert | Plan 006's climate family makes it hot desert; the hollow and its rim read in the Atlas contours |

## Commits

Appended as they land:

## Issues found

None.
