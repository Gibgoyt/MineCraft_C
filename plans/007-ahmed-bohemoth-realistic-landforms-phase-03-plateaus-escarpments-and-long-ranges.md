# 007-ahmed-bohemoth-realistic-landforms — phase 3: Plateaus, escarpments and long ranges

**Status:** in progress   (updated 2026-11-13)

Read first: `plans/007-ahmed-bohemoth-realistic-landforms-phase-00-vision-and-plan-files.md`, then the master `plans/007-ahmed-bohemoth-realistic-landforms.md` (sections 0, 1, 2.1, 2.3, 3.1 to 3.5, 3.10 to 3.12, 4, 6, 8, 9), then phase 01's "Today" table and gate answers, then `run/src/terrain/density/README.md` in full. Every number here comes from the master unless it says otherwise. Phase 01's gate answers (master Q1 and Q3) override the defaults below; record which applied.

## Goal

The world has **plateaus**:

- 6 to 16 km across by default (master Q1 option B);
- in two tiers, about 300 and about 650 high;
- **escarpments** on their seaward side that rise concavely, from a 5:1 pediment to a 1:2 face just below a rounded rim;
- a gentle back slope inland, so the ground "does not need to come down really fast".

**Ranges** run 15 to 40 km along their lines (range cell 8192). They are 3 to 5 km foot to foot, and stand on the plateaus inside the 1190-block budget.

All of it lives in a new terrain part, `run/src/terrain/landform/`, which samples its macro fields on a world-aligned 32-block lattice.

The profile gains `lift` (the plateau term), and the biome height axis reads `ground - lift`, so a plateau's top is grassland and not bare peaks.

**Proofs:**

- the height estimate is still the profile bit for bit;
- 1 and 16 workers give the same bytes;
- every terrain and climate pin is re-pinned with this phase's reason;
- the generator version is opened (master section 9);
- `bench_terrain` (with a new plateau place), `bench_lod`, `bench_climate` and the render pool stay inside their budgets;
- `bench_landform` on 8 seeds meets master section 4's plateau, escarpment, range and sea rows.

Slopes inside ranges (the shaping curve) and hills and knobs are **phase 04's**. So the ranges here still have the `flank` envelope with today's linear rise, and `jagged` stays until phase 04 replaces it.

## Where this fits

**The vision.** Plan 007 is the landform layer of the stack (phase 00), deferred by plan 004 ("Geology and landforms: plateaus, escarpments, Everest-style massifs, below-sea-level basins (a later plan; the climate is ready for them)"). This phase is its large-scale half: the shapes kilometres across that make the land read as a continent with highlands, not as pimples.

**What reads it:**

| Reader | What |
|---|---|
| The 3D density, hydrology, surface, caves, features, the far field | New heights through `density_profile_batch`, with no change of their own |
| The climate | `terrain_height_estimate*`, so every climate tile follows. The plateau is cooler by $0.012 \times 650 = 7.8$ °C |
| Biomes (today's, then plan 006 phase 03's) | `lift`, through `ground - lift` on the height axis |
| Phase 04 | The landform lattice (ruggedness $G$ lives there), `flank`'s envelope $m$, the budget |
| Phase 05 | The landform lattice (basin $B$), `lift`, the gates |
| Phase 06 and Ahmed | `bench_landform`'s numbers and `/tp` lines |

**What this phase must NOT build:**

- No slope-shaping curve $S_G$, no `peak` field, no change to hills, and no knobs: phase 04.
- No basins and no change to water below sea level: phase 05.
- No change to caves, ores or rivers' rules.
- No climate code change. The climate follows through the estimate.
- No change to plan 006's files. The biome-axis note for plan 006 phase 03 goes in this file's Issues found and in phase 06.
- No raise of the world's top or of `TERRAIN_DENSITY_HEIGHT_MAX` (master 2.3).

## Ahmed's words

- "1. plateus of mountain ranges can span **MASSIVE AREAS!** for example I live in South Africa, there is an escarpment, but a really really high plateu 1000km wide and 600km high, we can have something similar, if a mountain goes up that does not necessarily mean it needs to come down really fast, you are creating pimples, but we want actual mountain ranges"
- "all your hills still are ice cream cones ... over a span of 2km x 2km there is a mountain range!"
- From plan 004: "huge mountains, but sort of plateus in between! Like how mount everest has base-camp"

---

## Background, explained

### Domain warp

Reading a field at a moved point, $U(x + 700 W_x, y + 700 W_y)$, bends its contour lines. With $W$ a smooth noise of cell 2048, the plateau's rim gets bays and spurs about 1 to 2 km across instead of a smooth blob edge, like the Drakensberg's amphitheatres. The climate's fields use the same trick at ±96 blocks (`run/src/terrain/fields/README.md`, "What a value is").

### A step from a smooth field

A smooth field $L$ changes slowly. Passed through a spline that is flat, then rises sharply over a small range of $L$, then is flat again, it becomes a level, a cliff, and a level. How wide the cliff is depends on how fast $L$ changes across the ground: width $= \Delta L / |\nabla L|$. So a face over $\Delta L = 0.05$ with $|\nabla L| = 4 \times 10^{-4}$ a block is 125 blocks wide (master 3.4's table).

Because the spline never overshoots (Fritsch–Carlson slopes, `run/src/terrain/density/index.c:174-201`), the levels stay level and the heights can be added up by hand.

### The landform lattice

A lattice node is the point $(32i, 32j)$, lined up on the world. Its values are pure functions of $(i, j)$ and the seed. A lattice column (every 4 blocks) takes its macro values by bilinear blend of its 4 nodes:

$$v = v_{00} + t_x (v_{10} - v_{00}),\quad v' = v_{01} + t_x (v_{11} - v_{01}),\quad v = v + t_y (v' - v)$$

with $t_x = (x \bmod 32)/32$, in float64, always $x$ first. That is the density's own rule (`density_write_voxels`), so a column's bits cannot depend on which box, tile or batch it came in.

**Example:** at $x = 40$, $y = 8$: the nodes are $i = 1, 2$ and $j = 0, 1$, with $t_x = 8/32 = 0.25$ and $t_y = 8/32 = 0.25$.

### Which edge faces the sea, without gradients

A plateau blob has an edge all round, and Ahmed's escarpment faces the coast while the plateau runs on inland. $L$ carries $+0.25\,b$ (broad continentalness), so:

- the edge where $L$ crosses the face's knots **near the coast** (low $b$) is where the plateau rises from the coastal plain;
- an edge **far inland** (high $b$) is where the uplift itself fades.

So the stretch reads $b$ at the lattice column, which `density_profile_batch` already samples:

$$k = 1 - 0.8\,\text{smoothstep}(b;\ 0.2,\ 0.5), \qquad L' = 0.53 + (L - 0.53)\,k \ \text{ where } L < 0.53$$

**Example:** $b = 0.35$ gives smoothstep $0.5$, so $k = 0.6$. At $L = 0.48$, $L' = 0.53 - 0.05 \times 0.6 = 0.50$, so the column sits 40% of the way up tier 2's face instead of at its foot.

No node needs its neighbours, which keeps the lattice cheap (Design). The true test, $\nabla L \cdot \nabla b < 0$, is the fallback if the pictures show faces turned inland (Risks).

---

## Design

### Directories and files

- **`run/src/terrain/landform/index.c`, `README.md`; `run/include/terrain/landform/index.h`** (new):
  - the landform's struct, in the arena `terrain_init` lends;
  - its noise fields;
  - `terrain_landform_nodes` (the lattice values for a set of nodes);
  - `terrain_landform_at_columns` (blended values for a batch of lattice columns);
  - the plateau spline and the back-edge stretch.

  Tier, lifetime and threads as `terrain/density`.
- **`run/src/terrain/density/index.c`:** `density_profile_batch` calls the landform for its batch; `terrain_density_profile_from` takes the new inputs; the new splines (`flank`); the range's height on $P$; `lift`.
- **`run/include/terrain/density/index.h`:** `terrain_density_profile_t` becomes `{height, softness, lift, _pad}`, 16 bytes; `terrain_density_inputs_t` gains `plateau`, `stretch`, `face`; `terrain_density_t` gains `flank` and the landform pointer; asserts updated.
- **`run/src/terrain/index.c`:** `terrain_init` makes the landform after the fields and before the density; deinit in reverse; the estimate returns `lift` too.
- **`run/src/terrain/biome/index.c`:** the height axis reads `ground - lift`; `terrain_biome_inputs_t` gains `lift` (from hydrology's column, which reads the profile).
- **`run/src/terrain/hydrology/`:** carries `lift` through `terrain_hydrology_column_t` to biomes. No rule changes.

### The landform's fields

| Name | Cell | Octaves | Persistence | Limit | On |
|---|---|---|---|---|---|
| `landform/uplift` | 24576 (Q1 B) | 3 | 0.5 | measured, about 2× its SD | lattice nodes, at the warped point |
| `landform/edge_x`, `landform/edge_y` | 2048 | 2 | 0.5 | measured | lattice nodes; ±700 blocks |

Only $U$ (at its warped point) lives on the nodes. The $b$ term is added per lattice column, from the $b$ that `density_profile_batch` already samples for `range_land`: $L = U_{\text{blended}} + 0.25\,(b - 0.1)$, clamped to $[-1, 1]$. That costs nothing more, and keeps the coast-following part of $L$ at full resolution.

Limits are read off measured raw values, as every field's are today. `raw_noise_measures_what_the_limits_were_chosen_for` gains rows. **3 new noise fields:** 30 + 3 = 33 of 64. Phase 04 adds `landform/rugged`, `relief/knobs` and `landform/peak`; phase 05 adds `landform/basin`.

**Per node:** $U$ at the point warped by $W_x$ and $W_y$: 3 + 2 + 2 = **7 octaves**. Phases 04 and 05 add $G$ (2) and $B$ (2), for 11.

**Per chunk box.** A chunk's box is 16-aligned, so its 5 by 5 lattice columns (at $x_0$ to $x_0 + 16$) lie inside one 32-block cell: **4 nodes**, 28 octaves, about **1.1 octaves a lattice column** against today's 30 to 35. With phases 04 and 05, 44 octaves, about 1.8.

**Scattered single points** (the estimate's 4-corner calls) cost up to 4 nodes each: 28 octaves a call. The climate's tile build absorbs that (cold tile 30.7 ms against 100), measured by `bench_climate`.

**If the cost is higher than this** (it is measured, step 3), the fallbacks in order are:

1. a per-worker cache of the last 256 nodes (pure values, so no bit can change);
2. the uplift at 2 octaves instead of 3;
3. direct sampling of $U$ only where `range_land`-style gates say a plateau could be (skip the deep sea).

Every number goes in Issues found.

**If the pictures show faces turned inland:**

- `bench_landform` gains the measure "share of escarpment length whose foot is nearer the sea than its rim", with a target of at least 75% (master section 4).
- Below that, the fallback is gradients by differences on a ring of nodes round the box: 12 nodes, about 3 times the cost, timed before it is chosen.

### The plateau spline

Master 3.4's knots, with $P_{\max} = 650$:

    L:       -1   0.00  0.10  0.16  0.19  0.21  0.40  0.48  0.53  0.56  1
    plateau:  0   0     0.04  0.14  0.40  0.45  0.48  0.55  0.95  1.00  1.00

**The back-edge stretch** is applied before the spline: $L' = 0.53 + (L - 0.53)\,k$ where $L < 0.53$, else $L' = L$, with $k$ from $b$ (Background). The seaward face keeps $k = 1$, and an edge far inland goes to $k = 0.2$.

**`face`** $= \min(1, \text{plateau}'(L') \cdot k \cdot 4\times10^{-4} \cdot 650 / 2)$: the plateau's slope over 2, capped at 1, using the typical $|\nabla L|$ of master 3.4 (a constant, so no gradient is needed). It goes into softness (master 3.2) and into `bench_landform`'s escarpment test. $\text{plateau}'$ is the spline's derivative, which `terrain_density_spline_slope_at` gives: a new public function next to `terrain_density_spline_at`, the Hermite derivative, with no libm.

### Ranges

| Item | Today | This phase |
|---|---|---|
| `DENSITY_SHAPE_RANGE` cell | 4096 | 8192 (persistence 0.45, 3 octaves kept) |
| `DENSITY_SHAPE_MASSIF` cell | 2048 | 4096 |
| `belt` | 0: 0, 0.52: 0, 0.68: 0.32, 0.82: 0.68, 0.94: 1, 1: 1 | replaced by `flank`: 0: 0, 0.40: 0, 0.55: 0.06, 0.70: 0.20, 0.82: 0.42, 0.90: 0.64, 0.95: 0.82, 0.985: 0.96, 1: 1 |
| `crest` depth | always 0.5 to 1 | $1 - 0.5\,m\,(1 - \text{crest}(1 - \|crest\|))$ with $m$ = body: side valleys only high up |
| Height | $1030 \cdot \text{rise}$ | $(1000 - P)\cdot m$, $m = \text{body} \cdot \text{crest\_share}$; phase 04 puts $S_G(m)$ here |
| `range_land` | on $b$ | unchanged |

Changing a field's cell changes its values: the field's name and seed stay, its shape changes. The density README's table is updated.

**Budget, in this phase.** The phase 04 terms are not here yet, and jagged stays at 14 on `body`:

- **top on a range:** $100 + P + (1000 - P) + 46 + 14 = 1160 \le 1190$ ✓;
- **plateau without a range:** $100 + 650 + 46 = 796$ ✓;
- **bottom:** unchanged at -116 ✓.

`TERRAIN_DENSITY_HEIGHT_MAX` and `MIN` are unchanged. The header comment is rewritten with this sum.

**Foothills** ($60 \cdot (1 - \text{body}) \cdot \text{high} \cdot \text{peak}(r)$) stay in this phase. Phase 04 retunes or replaces them with knobs. **Softness:** `high` becomes $\min(1, \text{high} + 0.5\,\text{face})$.

### The profile and biomes

- `terrain_density_profile_from` returns `lift` $= P$ (phase 05 subtracts the basin).
- `terrain_density_profile_area` and `_points` take an optional `float32_t *lift` (nullptr skips it).
- `terrain_height_estimate_profile` returns `lift` blended like height. `terrain_height_estimate` and `_area` are unchanged in signature.
- **Biomes:** `terrain_biome_distance` reads `(ground - lift) * BIOME_HEIGHT_SCALE` (`run/src/terrain/biome/index.c:68-74`).
- **Surface rules that read absolute height** (`high:` bands in `windswept_hills` and `stony_peaks`) are **left as they are**: plan 006 phase 04 replaces them with the climate snowline. Recorded as a known look: until then, a plateau at 650 with `windswept_hills` above 400 gets its rock band. Measured on the pictures; if it looks wrong, those two `from:` values read `ground - lift` too, as a one-line follow-up.

### Benches

- **`bench/terrain/`** gains a place `plateau_top`: the first column the search meets with `lift` at least 550 and `body` 0. The gate covers it like the others.
- **`bench_lod`:** 40 ms a column at most.
- **`bench_climate`:** cold tile under 100 ms.
- **Render pool:** sections a column with something other than one block all through, at the tallest plateau-with-range place, from the same measurement plan 003 phase 8 Issues found 27 made, recorded against the 32-section budget and the 96 for near columns.

### Version and pins (master section 9)

**At this phase's first land-changing commit:**

- `TERRAIN_GENERATOR_VERSION` goes 1 to 2, if plan 006 has not already;
- `FEA_DIGEST_OF_GENERATOR` gains row 2, with the comment "open: plan 007 phase 03 (and plan 006 phases 03 to 04 / plan 007 phases 04 to 05 as they land); closed by the joint final check".

**If Ahmed has said no to master Q2's default:** follow plan 006's pattern. Re-pin, and bump only in the final phase. Record which.

**Pins that move, each re-pinned with "plan 007 phase 03: plateaus, escarpments and long ranges":**

| Pin | Where |
|---|---|
| `DENSITY_DIGEST_PIN` | `tests/run/terrain/density/index.c:65` |
| Hydrology, biome, cave, ore, surface, feature digests | `tests/run/terrain/*/` |
| `FEA_DIGEST_OF_GENERATOR[2]` | `tests/run/terrain/feature/index.c:49-61` |
| Climate digests that read the estimate (`run_climate_cell` and those after it) | `tests/run/climate/*` |
| Atlas goldens on real terrain | `tests/run/ui/atlas/fixtures/goldens.txt` |
| Render goldens on real terrain (`run_render_frame`, `render_feature_lod`'s `real_near`, `real_far`) | as plan 003 phase 8 Issues found 45 did; each picture looked at and its eye moved if it shows nothing |

---

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/terrain/landform/` (new)
- `run/{src,include}/terrain/density/` (profile, splines, inputs, README)
- `run/src/terrain/index.c`, `run/include/terrain/index.h`, `run/src/terrain/README.md` (init order, estimate's `lift`, the version, the asserts on table bytes and field counts)
- `run/{src,include}/terrain/biome/` (the height axis only), `run/{src,include}/terrain/hydrology/` (carrying `lift` only)
- `tests/run/terrain/` (new tests; re-pins with reasons), `tests/run/climate/` (re-pins with reasons), `tests/run/ui/atlas/fixtures/goldens.txt`, the render goldens
- `bench/terrain/` (the new place), `bench/landform/README.md`, `bench/world_map/README.md` (results)

**Wiring outside them:** `plans/REPO_STRUCTURE.md` (the new part); `compile/src/standards_lint/` only if a new directory needs registering.

## Steps

1. **Read** the density README, `density_profile_batch`, `terrain_density_profile_from`, the estimate, hydrology's column and biome distance; phase 01's numbers. *Why: every reader of the profile must see the new fields, and nothing else may change.*
2. **The landform part** with its fields, the 32-block nodes, the blend and the plateau spline, with unit tests on synthetic $L$ (a hook that replaces $U$ with a ramp). *Why: the step's shape is checked before it touches the world.*
3. **The back-edge stretch from $b$;** time the lattice's cost per chunk box, single-threaded, against phase 01's profile numbers. *Why: the cost is the main risk of this phase.*
4. **Wire into `density_profile_batch`:** plateau into height; `face` into softness; `lift` out. Ranges at the new cells with `flank`, crest scaled by $m$, and the $(1000 - P)$ height. *Why: one place, so the estimate follows.*
5. **The budget's limits test** (every corner and 8 seeds' real columns), and the estimate-is-the-profile test with `lift`.
6. **Biomes read `ground - lift`;** look at `bench_biome_map` pictures of a plateau.
7. **Open the version; re-pin** everything that moved, each with the reason; 1 against 16 workers.
8. **Benches:** `bench_terrain` with `plateau_top`, `bench_lod`, `bench_climate`, the render pool.
9. **`bench_landform` on 8 seeds;** tune the plateau thresholds, the $b$ weight and `flank`'s 0.40 knot against master section 4. Every number tried is recorded in Issues found, with its result, as plan 003 phase 8 did.
10. **Pictures looked at and described:** relief, plateaus, profiles, strip; `bench_world_map` and `bench_world_map_atlas` (contours from phase 02 if built).
11. READMEs; tick each todo as it lands.

## Todo

- [x] `run/src/terrain/landform/` with `landform/uplift`, `landform/edge_x`, `landform/edge_y`, their limits measured, the 32-block lattice and blend, and its README
- [x] The plateau spline (11 knots) and `terrain_density_spline_slope_at`; unit tests on a synthetic ramp give master 3.4's table within 1 block
- [ ] The back-edge stretch from $b$; the lattice's cost per chunk box measured (numbers in Issues found); `bench_landform`'s escarpment-facing share at least 75%, else the gradient fallback measured
- [x] `density_profile_batch` and `terrain_density_profile_from`: plateau, `face` into softness, `lift`; ranges at cell 8192 with `flank`, massif 4096, crest scaled by $m$, height $(1000 - P)\,m$
- [x] `terrain_density_profile_t` 16 bytes with `lift`; the estimate returns it; the bit-for-bit test passes for `lift` too
- [x] Limits test: every spline corner sums inside -116 to 1190; 8 seeds' real columns over 64 km inside
- [ ] Biome height axis reads `ground - lift`; `bench_biome_map` picture of a plateau looked at
- [ ] Version opened (or plan 006's pattern, per Q2); every moved pin re-pinned with this phase's reason; 1 against 16 workers the same bytes
- [ ] `bench_terrain` with `plateau_top`: every place at 1000 or more
- [ ] `bench_lod` at most 40 ms a column; `bench_climate` cold tile under 100 ms; render pool sections recorded
- [ ] `bench_landform` on 8 seeds: plateau pieces, escarpment length, range length, sea 25 to 30%, height bands, all in master section 4 (or as Ahmed moved them)
- [ ] `rivers_hold_water_and_reach_the_sea_and_the_coast_has_beaches` still at 90% or more; `run_game_spawn` passes
- [ ] Pictures looked at and described here
- [ ] READMEs (`terrain/`, `landform/`, `density/`, `biome/`), `plans/REPO_STRUCTURE.md`
- [ ] The note for plan 006 phase 03 (its height axis means height above `lift`) written in Issues found

## Tests

In `tests/run/terrain/density/index.c` and a new `tests/run/terrain/landform/index.c`.

- **`the_plateau_spline_is_the_master_s_table`.** On a synthetic ramp $L = 4 \times 10^{-4} x$ (hook), the heights at the knots' $x$ are 0, 0, 26, 91, 260, 293, 312, 358, 618, 650 within 1 block.
  - The face from $L = 0.16$ to 0.19 lies between $x = 400$ and 475.
  - Its steepest gradient is between 2.0 and 3.5.
  - Fails if knots are wrong or the spline overshoots (a height over 650 or a dip on a level).
- **`the_back_edge_is_five_times_wider`.** A synthetic ramp of $L$ falling inland while $b$ rises inland. The tier-2 edge's 10%-to-90% width is between 4 and 6 times that of the seaward edge's.
- **`landform_values_do_not_depend_on_the_box`.** 1000 random lattice columns, each as a single point, inside a 16-block box, inside a 300-block box and in the estimate area: every `plateau`, `lift` and height is bit-equal. Fails if the blend reads a different node by box, or the order is not $x$ then $y$.
- **`the_height_limits_add_up`** (existing, extended). Every corner of every spline's range summed: at most 1190 and at least -116. 8 seeds, 64 km at step 16, real columns: never outside.
- **`the_height_estimate_is_the_profile_on_the_lattice`** (existing). Passes, and the same for `lift`.
- **`the_height_estimate_agrees_with_the_real_ground_within_the_softness`** (existing). Passes on the new land.
- **`plateau_tops_choose_lowland_biomes`.** On 3 seeds, columns with `lift` ≥ 550, slope under 0.1 and no range: the biomes are from today's lowland set (plains, forest, savanna, taiga and their kin), not `stony_peaks`, `jagged_peaks`, `frozen_peaks` or `snowy_slopes`, in at least 95% of them. Today's climate-free biomes decide this until plan 006 phase 03.
- **`one_and_sixteen_workers_give_the_same_bytes`** (each terrain test's existing check). Passes on the new pins.
- **`raw_noise_measures_what_the_limits_were_chosen_for`** (existing, extended). The 3 new fields' standard deviations against their limits.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_climate_.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_ui_atlas.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_game_spawn$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1800 ctest --preset release -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_lod
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 2198175988018515243 12636146071196537873 5569068473609503046 1
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 42 20261104 9999999999999 18446744073709551557
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map --map-only /tmp/mc_c_maps 2198175988018515243
    pgrep -af "$PWD/build"    # must print nothing

The release run of the terrain tests must print the same digests as debug: `-O3 -flto` must move no bit.

## Gate

Ahmed, in a new world on his seed:

1. `/tp` to `bench_landform`'s "largest plateau piece" line.
2. `/fly` to its seaward rim and along the escarpment for a few km.
3. Lands on the rim and walks inland a few hundred blocks.
4. `/tp top` on the printed tallest range.
5. Opens the Atlas: contours bunch at the face and spread over the top (if phase 02 is in).
6. Reads F3's temperature on the plateau against the coast: about 7.8 °C cooler at 650.

He says whether the plateau is big enough (master Q1), whether the face is steep enough and the back slope gentle enough, and whether the ranges now run on. His words are recorded here. **Worlds made while the version is open are test worlds** (master section 9).

## Risks and open questions

| Risk or question | Default |
|---|---|
| The landform lattice pushes `bench_terrain` under 1000 | Measured in step 3; the fallbacks in Design, in order; every number in Issues found |
| Faces turned inland where $b$ misjudges which edge faces the sea (a plateau near a long inlet) | The escarpment-facing share (target 75%); the gradient fallback if under |
| Plateaus near the coast squeeze the sea or stand in it as walls (plan 003's first try at ranges did) | The $b$ term pulls them inland; the sea row is held at 25 to 30%; the pictures are read for walls in the sea |
| Rivers' "90% joined" drops because plateaus cut them | Measured; plateaus sit where `reach` already fades. If it drops, the $b$ weight rises |
| Too much land above 550 (tier 2 more than 12% of land) | Raise the tier-2 knots (0.48 to 0.53) by 0.05 at a time; record each try |
| The 8192 range cell makes ranges too rare | `flank`'s 0.40 knot is the share knob; "reaching 500" and range length are held together |
| `windswept_hills` and `stony_peaks` rock bands at absolute 400 and 800 show on plateaus | Known look until plan 006 phase 04; a one-line follow-up if Ahmed dislikes it |
| A spawn in a plateau region: `game_spawn_find` wants ground ≤ 120 within 32 km | `run_game_spawn`'s seeds must pass; coastal plains remain |
| Floating rock on faces with softness up to 26 | Measured with `bench_world_map`; at most +25% (master section 4); else `face`'s weight in softness drops from 0.5 |

## Commits

Appended as they land:

## Issues found

None.
