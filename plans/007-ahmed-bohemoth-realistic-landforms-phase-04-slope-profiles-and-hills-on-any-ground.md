# 007-ahmed-bohemoth-realistic-landforms — phase 4: Slope profiles, and hills on any ground

**Status:** built and tested; knobs on plateaus met; the slope rows, floating rock and bench_terrain not met (Issues found 7 to 9)   (updated 2026-11-13)

Read first: `plans/007-ahmed-bohemoth-realistic-landforms-phase-00-vision-and-plan-files.md`, then the master `plans/007-ahmed-bohemoth-realistic-landforms.md` (sections 0, 2.2, 3.2, 3.6, 3.7, 3.9, 3.12, 4, 6), then phase 03's file (what it built, its budget and its Issues found), then phase 01's "Today" table. Every number here comes from the master unless it says otherwise.

## Goal

Mountains are **gentle at their feet and steep near their tops**, and how much depends on the region.

- **The range height** is $(1000 - P)\, S_G(m)$. $S_G$ blends a soft curve ($\approx m^{1.6}$) and a sharp one ($\approx m^3$) by a ruggedness field $G$ (cell 3072, on the landform lattice).
- **The slope ratio** between 90% and 10% of the envelope runs from about 3.7 on rolling ground to about 81 on alpine ground.
- **Summit cliffs and arêtes:** a ridged `peak` field (cell 48) adds them only above 60% of the envelope, reaching 1:3 to 1:6. It replaces `jagged`'s needles on every foothill.
- **Hills** are gentler: at most 30 blocks, on cell 384.
- **Knobs** (Ahmed's "pimples": koppies, inselbergs, domes) stand on **any** ground, including plateau tops. They are 20 to 90 blocks high, dome- or koppie-shaped by $G$, and never on a range.
- **Softness** follows faces, so overhangs go to cliffs, not to plains.

`bench_landform` on 8 seeds meets master section 4's slope rows:

- the histogram;
- of ground steeper than 1:2, at least 70% high up or on faces;
- the top-decile-over-bottom slope ratio at least 5;
- the median peak concavity at least 3;
- at least 25% of knobs on bases of 250 or over.

Floating rock rises at most 25%. Every pin moved is re-pinned with this phase's reason, 1 and 16 workers agree, and `bench_terrain` stays at 1000 or more.

## Where this fits

**The vision.** Phase 03 made the big shapes (plateaus and long ranges). This phase makes them **look like real land at the scale Ahmed walks**: "slopes don't need to be 1:1, they can be 5:1 ... and at the peaks go to 1:5", and "pimples ... can also be on plateus".

**What reads it:**

| Reader | What |
|---|---|
| Everything downstream of the profile | New heights, softness; nothing else changes shape |
| The climate | Heights through the estimate (summits a little different) |
| Phase 05 | $G$ on the lattice; the budget |
| Phase 06 and Ahmed | The slope targets, and the walk from foot to summit |

**What this phase must NOT build:**

- No new plateau or range geometry (phase 03's; only its height line gains $S_G$).
- No basins (phase 05).
- No change to caves, ores, rivers' rules, or the climate.
- No 3D landform term: everything stays 2D in the profile, so the estimate follows (master 3.10).

## Ahmed's words

- "please add a plan that varies the steepness of hills! I noticed all your hills are 1:1"
- "2. these things I refer to as "pimples" does not only need to spawn at altitude 0, it can also be on plateus etc...."
- "3. slopes don't need to be 1:1, they can be 5:1 (e.g. 5 blocks forward for every 1 block up) and at the peaks go to 1:5 please make terrain generation much much more realistic!!"

---

## Background, explained

### Why a power curve makes feet gentle and tops steep

Let $m$ climb steadily from 0 at the foot to 1 at the top, and set $h = H\, m^p$. Then

$$\frac{dh}{dx} = H\,p\,m^{p-1}\,\frac{dm}{dx}$$

With $dm/dx$ about constant, the slope grows like $m^{p-1}$.

- $p = 1$ gives a cone: the same slope everywhere.
- $p = 2.5$ gives a slope at $m = 0.9$ that is $(0.9/0.1)^{1.5} = 27$ times the slope at $m = 0.1$.

That matches Ahmed's 5:1 to 1:5, which is a factor of 25. Real mountains are like this because water and ice wear the low ground into long gentle aprons and leave the high ground steep.

### Splines in place of `pow`

`pow` is libm, and nothing that decides a block may depend on how a libm rounds (`run/src/terrain/ore/README.md`). The repo's monotone spline through knots of $m^p$ is close enough. It is exact at the knots, never overshoots, and is plain arithmetic:

| $m$ | 0 | 0.2 | 0.4 | 0.6 | 0.8 | 0.9 | 1 |
|---|---|---|---|---|---|---|---|
| $S_{\text{soft}}$ ($m^{1.6}$) | 0 | 0.076 | 0.231 | 0.442 | 0.700 | 0.845 | 1 |
| $S_{\text{sharp}}$ ($m^{3}$) | 0 | 0.008 | 0.064 | 0.216 | 0.512 | 0.729 | 1 |

### Ridged noise

$J = 1 - |n|$ for a noise $n$ in $[-1, 1]$. Where $n$ crosses 0, $J$ has a sharp crest: a ridge line. Several octaves make branching arêtes. Multiplied by a weight that is 0 below 60% of the envelope and rises as the cube of $(m - 0.6)/0.4$, it adds rock only near summits.

**Example:** at $m = 0.8$, the weight is $40 \times 0.5^3 = 5$ blocks. At $m = 1$ it is 40 blocks.

### A knob

A knob is a bump standing alone on flat ground. With a smooth field $K$ spread to $[-1, 1]$, only where $K > k_0$ does

$$t = \frac{K - k_0}{1 - k_0} \in [0, 1]$$

become a knob of height $A_K\, S_K(t)$. With $k_0 = 0.55$, the share of ground with some knob is $(1 - 0.55)/2 = 0.225$. Each knob's footprint is the patch round a maximum of $K$, about 120 to 250 blocks across for a cell of 320.

$S_K$ blends two shapes by $G$:

- **dome:** $1 - (1 - t)^2$, convex, round-topped;
- **koppie:** knots 0: 0, 0.3: 0.15, 0.5: 0.75, 0.7: 0.95, 1: 1. Steep sides, flat top, like the Highveld's koppies.

---

## Design

### Directories and files

- **`run/src/terrain/landform/`:** `landform/rugged` on the nodes (cell 3072, 2 octaves, spread to 0..1); `relief/knobs` and `landform/peak` sampled per lattice column (fine fields), the latter only where $m > 0.6$.
- **`run/src/terrain/density/`:** $S_{\text{soft}}$, $S_{\text{sharp}}$, $S_{K,\text{dome}}$ and $S_{K,\text{koppie}}$ splines; the new height line; hills retuned; `jagged` removed; foothills retuned or folded into knobs (below); softness with `face`.
- **READMEs:** density's formula, table and limits rewritten; landform's fields.

### The height line after this phase

$$h = \text{base} + P + (1000 - P)\,S_G(m) + \text{peak} + \text{knob} + \text{hill}$$

| Term | Formula | At most |
|---|---|---|
| $S_G(m)$ | $(1 - G)\,S_{\text{soft}}(m) + G\,S_{\text{sharp}}(m)$ | 1 |
| peak | $40 \cdot \big(\max(0, (m - 0.6)/0.4)\big)^3 \cdot J$, $J = 1 - \lvert n_{48} \rvert$ | 40 |
| knob | $A_K\, S_K(t) \cdot (1 - \text{body}) \cdot \text{hill\_land}(c)$, $A_K = 20 + 70\sigma$ | 90 |
| hill | $\big(6 + 24\,\text{hill\_relief}(e)(0.5 + 0.5G)\big) \cdot \text{hill\_land}(c) \cdot \text{valley}(r) \cdot \text{hills}_{384} + 6\,\text{hill\_sea}(c)\,\text{hills}_{384}$ | 30 (land), 6 (sea) |
| foothill | **Removed.** Its job (rough ground near high ground) is done by knobs and by $S_G$'s low part | 0 |
| jagged | **Removed**, replaced by `peak` | 0 |

$\sigma$ is the knob field's second octave alone, spread to 0..1. `terrain_noise_sample_points_split` gives it for no extra noise, as `broad` comes from continentalness.

**Budget** (master 3.2):

- **top:** $100 + 1000 + 40 + 30 = 1170 \le 1190$ ✓ (knob is 0 where body is 1; on a plateau with no range, $100 + 650 + 90 + 30 = 870$);
- **bottom:** $-110 - 6 = -116$ ✓.

The header's comment and the limits test say so.

**Removing foothills is a choice this file makes; the master did not.** Its 60 blocks on `ridges` (weirdness cell 512, creased) are one of the master's "pimple" sources (0.4). If the slope targets are met without it, it stays removed. If Ahmed misses the rough ground beside ranges, it comes back at 30 blocks with $S_{\text{soft}}$ applied to `peak(r)`. Recorded in Issues found either way.

### Softness

$$\text{softness} = 4 + 22 \cdot (h' + \text{body} - h' \cdot \text{body}), \quad h' = \min(1,\ \text{high} + 0.5\,\text{face} + 0.3\,G\,m)$$

- Overhangs come where faces are (`face` from phase 03) and on rugged high ground.
- Plateau tops ($\text{face} = 0$, body 0, high low) stay at about 4: one surface, walkable.

### What is measured, and the tuning loop

`bench_landform` (phase 01) on 8 seeds after each change; every try recorded in Issues found with its numbers, as plan 003 phase 8 did. Knobs to turn, in order:

1. The $G$ field's spread, to shift the region mix: more rolling or more alpine.
2. The soft and sharp knots (between $p$ 1.4 and 1.8, and between 2.6 and 3.4).
3. `peak`'s 40 and its 0.6 threshold.
4. The knob threshold $k_0$ and $A_K$.
5. The hill amplitude and cell.

Each is one constant. Nothing is tuned against a single seed.

---

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/terrain/landform/`, `run/{src,include}/terrain/density/` (profile, splines, inputs, limits, README)
- `run/include/terrain/index.h` (field count assert), `run/src/terrain/README.md`
- `tests/run/terrain/` (new tests; re-pins with reasons), `tests/run/climate/` (re-pins with reasons), Atlas and render goldens on real terrain
- `bench/landform/README.md`, `bench/terrain/README.md`, `bench/world_map/README.md` (results)

## Steps

1. **Read** phase 03's code and Issues found; today's foothill, hill and jagged terms (`run/src/terrain/density/index.c:269-279`). *Why: what is being replaced.*
2. **$S_{\text{soft}}$, $S_{\text{sharp}}$, $G$ on the lattice;** the height line uses $S_G(m)$; unit tests on a synthetic envelope. *Why: the core of "5:1 to 1:5".*
3. **`peak`** replacing `jagged`, sampled only where $m > 0.6$. *Why: summit cliffs where they belong, and the needles gone from foothills.*
4. **Knobs,** dome and koppie, on any base; **hills** at 384 and 30. **Foothills removed.**
5. **Softness** with `face` and $G\,m$.
6. **Limits test, estimate test,** re-pins with reasons, 1 against 16 workers.
7. **`bench_landform` on 8 seeds;** tune (Design) until master section 4's slope rows are met; record every try.
8. **`bench_world_map`'s floating rock, `bench_terrain`, `bench_lod`.**
9. **Pictures** looked at and described: slope map, profiles through the tallest peak and across a plateau, a knob field on a plateau.
10. READMEs; tick each todo as it lands.

## Todo

- [x] `landform/rugged` $G$ on the lattice; `relief/knobs`, `landform/peak` as fine fields; limits measured
- [x] $S_{\text{soft}}$ and $S_{\text{sharp}}$ splines; height line $(1000 - P)\,S_G(m)$
- [x] `peak` (ridged, cell 48, above $m = 0.6$) replaces `jagged`
- [x] Knobs with dome and koppie shapes, on any base, not on ranges; hills at cell 384 and at most 30; foothills removed (or restored at 30, recorded)
- [x] Softness with `face` and $G\,m$
- [x] Limits test (-116 to 1190) and the estimate-is-the-profile test pass; every moved pin re-pinned with this phase's reason; 1 against 16 workers the same bytes
- [ ] `bench_landform` on 8 seeds: the slope histogram in master section 4's bands
- [ ] Of ground steeper than 1:2, at least 70% in the top 30% of relief or on faces
- [ ] Top-decile-over-bottom-3 slope ratio at least 5
- [ ] Median peak concavity ratio at least 3
- [x] At least 25% of knobs on bases of 250 or over
- [ ] Floating rock per km² at most phase 01's + 25%
- [ ] `bench_terrain` at least 1000 everywhere (with `plateau_top`); `bench_lod` at most 40 ms
- [x] Pictures looked at and described here
- [x] READMEs (`density/`, `landform/`)

## Tests

In `tests/run/terrain/density/index.c` and `tests/run/terrain/landform/index.c`.

- **`the_shaping_curves_go_through_their_knots`.** $S_{\text{soft}}$ and $S_{\text{sharp}}$ equal the table at every knot to $10^{-12}$, are monotone on 10,001 points, and stay inside $[0, 1]$.
- **`feet_are_gentle_and_tops_steep`.** On a synthetic envelope rising linearly from 0 to 1 over 2000 blocks, with $H = 1000$, the gradient at $m = 0.9$ over the gradient at $m = 0.1$ is:
  - 3 to 5 for $G = 0$;
  - 6 to 13 for $G = 0.5$;
  - at least 40 for $G = 1$ (master section 7).

  The gradients at $m = 0.2$, 0.4, 0.6, 0.8 and 0.95 for $G = 0.5$ are within 15% of the master's table (0.18, 0.35, 0.57, 0.83, 1.06).
- **`peaks_add_nothing_below_sixty_percent`.** `peak` is exactly 0 for $m \le 0.6$, on 100,000 columns of 3 seeds, and at most 40 anywhere.
- **`knobs_never_stand_on_a_range_or_in_the_sea`.** Where body is 1 or `hill_land` is 0, knob is exactly 0. A knob with $G = 1$ has a flat top: the middle 30% of its footprint is within 10% of its height.
- **`knobs_stand_on_plateaus_too`.** On 3 seeds, among knobs found by `bench_landform`'s rule, at least 25% have a base of 250 or over.
- **`the_height_limits_add_up`** (existing, extended), **`the_height_estimate_is_the_profile_on_the_lattice`** and **`…agrees_with_the_real_ground_within_the_softness`** (existing). Pass.
- **`plateau_tops_have_one_surface`.** On plateau land with face 0 and no range, softness is at most 6. A 1 by 1 by 751 column there has exactly one solid-to-air change above -100, in at least 99% of 10,000 columns.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_climate_.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_ui_atlas.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1800 ctest --preset release -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_lod
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 2198175988018515243 12636146071196537873 5569068473609503046 1
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 42 20261104 9999999999999 18446744073709551557
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map /tmp/mc_c_maps 2198175988018515243 1 42 20261104
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map /tmp/mc_c_maps 12636146071196537873 5569068473609503046 9999999999999 18446744073709551557
    pgrep -af "$PWD/build"    # must print nothing

`bench_world_map` without `--map-only` gives floating rock; it takes more than 590 s for 8 seeds, so it runs as two calls of 4, as plan 003 phase 8 did.

## Gate

Ahmed, in a new world on his seed:

1. `/tp` to the printed tallest peak's foot (`bench_landform` prints the foot as well as the summit).
2. With `/fly` off, walks and climbs to the top, and says whether it starts gentle (about 5:1) and gets steep (cliffs near the top).
3. `/tp` to "a knob on a base ≥ 250" and looks at a koppie on the plateau.
4. Looks at the slope map and profiles from `bench_landform`.

He names any slope that still feels like a cone. Recorded here.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Sharp ranges ($G$ near 1) leave long flat aprons that read as "nothing there" | The soft curve's share via $G$'s spread; knobs and hills on aprons |
| Concavity ratio misses 3 because the envelope's top is a rounded noise maximum, where the slope falls to 0 at the summit | `peak` adds the summit's steep rock; if still short, `flank`'s top knots move closer to 1 (0.985: 0.96 → 0.99: 0.97) |
| Floating rock rises with softness on faces | `face`'s 0.5 and $G\,m$'s 0.3 are the knobs; at most +25% |
| Removing foothills leaves bare ground beside ranges | Restore at 30 with the soft curve, recorded (Design) |
| `bench_terrain` falls with `peak` and knobs | `peak` only above $m = 0.6$ (a few % of columns); knobs read one field, gate first; measured |
| A 1:5 face over 4-block lattice cells shows as a stair of 20-block steps | That is what a heightfield face is in blocks; softness bends it; looked at on slices |

## Commits

Appended as they land:

- `a3dc9438` Plan 007 phase 04, work in progress: slope shaping S_G(m) by ruggedness on the landform lattice, the ridged summit rock in place of jagged, knobs on any ground, hills at cell 384 and at most 30, foothills gone, softness on faces and rugged ranges; the density and landform tests on the new formula. Pins not yet moved.
- `346ab3cc` Shape plan 007 phase 04's land: a range's height is (1000 - P) S_G(m), soft m^1.6 and sharp m^3 blended by ruggedness G on the landform lattice; the ridged summit rock (cell 48, only above 60% of the envelope) replaces jagged; knobs, dome or koppie by G, 20 to 90 high, on any ground but a range or the sea, on a knob field of cell 640; hills at cell 384 and at most 30; foothills gone; softness from faces and rugged ranges only. Re-pin the density, cave, ore, biome, hydrology, feature, climate cell and systems digests and the Atlas goldens on it, each with this phase's reason, and allow 0.3% bare peaks and six seeds of eight with a long piece of high ground.
- `17d7ccc1` Follow plan 007 phase 04's land in the program and picture tests: run_audio and run_player_interaction dig on the nearest even ground of seed 11, found from the land by tests/run/game/fixtures/ground.h and reached by /tp (the start is now on the edge of grass and sand); run_player_survival's run is also clear of trees, read from the blocks; run_playthrough's bench allows another kind picked up on the way; render_feature_lod_bands holds near colours exactly at E 2 and to 2 in 10,000 at E 8; run_render_frame's start, north_low and down, render_feature_under_water's land, sea and sea_no_air, and render_feature_lod's real_near and real_far made again, each looked at; clang-format on the phase's files.
- `34f42bba` Have run_save_state_game's autosave test capture and settle what opening left until no far column more is built, before the away-and-back edit it times: on plan 007 phase 04's land a worker was still building then (it failed 3 times of 5 and passed on the old land), and print what the step built. Tick plan 007 phase 04's built todos.

## Issues found

Open questions took their defaults: `TERRAIN_GENERATOR_VERSION` stays 1 and row 1 of `FEA_DIGEST_OF_GENERATOR` changes with a line saying why (master Q2, as phase 03); master section 4's targets as written (Q3, no gate held yet). **Worlds made from this branch's land are test worlds.**

1. **What was built** (`run/src/terrain/density/README.md`, `run/src/terrain/landform/README.md`): `h = base + P + (1000 - P) S_G(m) + peak + knob + hill`, with `S_G = (1 - G) soft(m) + G sharp(m)`; `G` from `landform/rugged` (cell 3072, 2 octaves) on the 32-block lattice, blended with the uplift from the same nodes (a node costs 9 octaves, was 7); `peak = 40 w^3 (1 - |n|)` above `m = 0.6` from `landform/peak` (cell 48), sampled only where the envelope passes 0.6; `knob = (20 + 70 sigma) S_K(t) (1 - body) hill_land(c)` from `relief/knobs`, sampled only on land, `sigma` its second octave by `terrain_noise_sample_points_split`; hills `(6 + 24 hill_relief(e)(0.5 + 0.5 G))` on cell 384; `jagged` and foothills (and the `inland`, `relief` and `peak` splines that only they read) removed. The density's noise fields go 6 to 5, the landform's 3 to 6. Budget: the corner test reaches exactly -116 and 1170 (100 + 1000 + 40 + 30), under 1190.

2. **Changed from the plan: the shaping splines have 12 knots, not 7.** With only master 3.6's table the spline's slope at `m = 0.2` and `G = 0.5` came out 0.145 against the master's 0.18 (19% off, the test allows 15%), as Fritsch-Carlson slopes at a knot follow the secants, not the power curve. The table's knots stay (the test holds them to 1e-12); tenths and 0.95 between them, at `m^1.6` and `m^3` to three places, bring every gradient within 4%: 0.173, 0.344, 0.558, 0.825, 1.063 against 0.18, 0.35, 0.57, 0.83, 1.06. The slope at 0.9 over 0.1: 4.43 (G 0), 10.92 (G 0.5), 134 (G 1); test bands 3 to 5, 6 to 13, at least 40.

3. **Changed from the plan: the knob field's cell is 640, not 320.** On 320 the slope map showed every knob as a dark-red ring, 1:2 to 1:5: `t` climbs 0 to 1 within a few tens of blocks of `K` passing 0.55, so a koppie's middle rise of 60% of its height comes over about 10 blocks, not the master's "6:1 for a low dome to 1:1.5 for a koppie". On 640 the rings are thinner and half as many (2537 against 4988 knobs in 32 km on seed 42); the sides are still steep (the koppie's shape is the master's) and the knobs 250 to 500 across, not 120 to 250. Recorded for Ahmed's look at a koppie.

4. **Changed from the plan: softness no longer reads `high` (erosion's high ground).** `plateau_tops_have_one_surface` wants softness at most 6 on a plateau top with no face and no range, but `high = inland(c) relief(e)` is 0.3 to 1 wherever erosion is low, plateau or not (the plan assumed it low there). `high` meant "where foothills are", and they are gone, so `h = min(1, 0.5 face + 0.3 G m)`: overhangs on faces and rugged ranges, one surface on plains and plateau tops. The test: 5138 plateau-top columns, all with softness at most 6 and one surface.

5. **Foothills stay removed** (the plan's choice): nothing in the measures asked for them back (Issue 8). If Ahmed misses rough ground beside ranges, they come back at 30 with `S_soft`, as the Design says.

6. **`knobs_stand_on_plateaus_too` is measured by `bench_landform`, not a ctest.** The knob rule lives in `bench/landform/measure/` (a flood to the highest saddle within 512 blocks), compiled into that program only; the share of knobs on a base of 250 or more is in the table below for every seed.

7. **The tuning loop** (master section 4's slope rows; `bench_landform`'s profile at 32 km, seeds 42 and 1, a debug build with the constants read from the environment for the tries only, never committed). Each line: flat / gentle / moderate / steep / very steep / cliff / sheer % of land (seed 42; seed 1 within 1.5 points), steep-in-top-30% share, top-decile-over-bottom-3, median concavity, knobs on a base of 250 or more:

   | Try | Histogram (seed 42) | Steep high | Decile ratio | Concavity | Knobs >= 250 |
   |---|---|---|---|---|---|
   | The plan's numbers (knob cell 320) | 1.8 / 14.7 / 27.1 / 20.9 / 17.8 / 12.4 / 5.3 | 32.5 / 24.5% | 0.94 / 1.30 | 1.47 / 1.52 | 73 / 43% |
   | Knob cell 640 (kept) | 1.8 / 15.4 / 29.1 / 21.4 / 16.6 / 10.7 / 5.2 | 30.5 / 24.6% | 0.70 / 0.90 | 1.09 / 1.14 | 72 / 44% |
   | `G` middle 0.75 (more alpine) | 1.8 / 14.8 / 27.7 / 21.0 / 17.3 / 12.1 / 5.4 | 32.8 / 24.8% | 0.93 / 1.28 | 1.51 / 1.53 | 73 / 42% |
   | `G` middle 0.25 (more rolling) | 1.8 / 14.7 / 26.6 / 20.8 / 18.3 / 12.7 / 5.3 | 32.2 / 24.2% | 0.95 / 1.31 | 1.44 / 1.53 | 73 / 44% |
   | No summit rock | 1.8 / 14.7 / 27.1 / 20.9 / 17.8 / 12.4 / 5.3 | 32.9 / 24.8% | 0.95 / 1.30 | 1.46 / 1.52 | 73 / 43% |
   | `k0` 0.7 (fewer knobs) | 1.9 / 15.6 / 28.1 / 20.2 / 16.7 / 12.1 / 5.4 | 32.5 / 24.6% | 0.86 / 1.25 | 1.47 / 1.50 | 73 / 43% |
   | Cell 640, `k0` 0.7, `G` 0.75 | 1.9 / 16.2 / 29.8 / 20.6 / 15.8 / 10.6 / 5.2 | 31.3 / 25.0% | 0.68 / 0.86 | 1.12 / 1.17 | 71 / 44% |
   | Hills of 2 octaves; persistence 0.35 | within 0.2 points of the plan's | | | | |

   **None of the phase's five levers moves the slope rows more than a point or two.** To find what does, the same tool took terms away (seed 42 / 1):

   | Taken away | Flat / gentle / moderate % | Sheer % | Steep high |
   |---|---|---|---|
   | Knobs | 2.3 / 18.2 / 31.1 | 5.1 | 32.2% |
   | Hills | 2.8 / 16.1 / 26.3 | 5.3 | 32.6% |
   | Knobs and hills | 3.6 / 20.0 / 30.0 | 5.1 | 32.7% |
   | The valley sinking of `base` (`valley(r)`, ridges cell 512, folded) | 2.3 / 18.3 / 31.9 | 5.1 | 23.6% |
   | Valley, knobs and hills | 4.8 / 25.5 / 34.9 | 4.9 | 20.6% |
   | Plateaus | 2.4 / 19.4 / 32.7 | 0.6 | 12.2% |
   | Plateaus and valley | 3.0 / 24.1 / 38.4 | 0.4 | 16.3% |
   | Plateaus, valley and knobs | 3.9 / 30.2 / 44.4 | 0.3 | 16.1% |

   So: **the sheer class is the plateau faces** (5% of land with them, 0.4% without); **the "flat" class cannot reach 30%** while ranges cover so much of the land (on seed 42's 32 km, 33% of land stands at 500 or more, and a range's flank, 1000 blocks over about 2.4 km, averages 0.4, moderate by definition) and the lowland keeps `base`'s valleys. Even with plateaus, valleys and knobs all gone, flat is 4%. The decile ratio and the concavity are dominated by peaks on plateaus: a knob or a range on a plateau is a "peak" whose rays cross a flat top before the escarpment, so its top half is short and steep and its bottom half long (ratio under 1); taking knobs away *lowers* the median concavity (fewer peaks with any steep top). **For phase 06**: master section 4's slope rows need the range belt narrower or rarer (the flank's 0.40 knot, the `range_land` gate), the valley sinking softened off the rivers, and the concavity measured on range peaks (relief over 500, not on a plateau) if it is to say what 3.6 means by it. None of those is this phase's (master 3.2: `base` is today's; phase 03 owns the range geometry).

8. **`bench_landform` on the 8 seeds** (release, 64 km, two runs of 4 and Ahmed's seed with the first, exit 0 each; a CPU bench on a shared machine, no card clock applies). Min to max over the 8, Ahmed's seed `11107447384583428913` beside, and phase 01's "Today" for comparison:

   | Measure | Target | Phase 01 (today) | This phase | Ahmed's seed | Met |
   |---|---|---|---|---|---|
   | Flat / gentle / moderate / steep / very steep / cliff / sheer, % of land (profile, whole map) | 30–40 / 25–35 / 15–20 / 7–12 / 3–6 / 1.5–3 / 0.3–1 | 1.0–1.2 / 8.8–10.0 / 21.7–23.0 / 24.8–25.4 / 24.4–25.8 / 15.1–16.1 / 1.4–1.6 | 2.0–2.4 / 16.3–19.3 / 30.6–32.7 / 20.5–21.7 / 14.8–16.1 / 8.4–9.8 / 1.8–3.6 | 2.5 / 19.6 / 32.8 / 20.7 / 14.6 / 8.0 / 1.8 | No (every class moved toward its band except sheer, the plateau faces; none is in it but gentle at its edge) |
   | The same from real blocks in the 64 windows | | 1.1–1.8 / 6.5–9.5 / 17.0–22.0 / 26.4–28.3 / 22.4–26.2 / 13.5–17.7 / 2.5–4.3 | 2.7–3.5 / 13.0–16.1 / 26.6–29.6 / 24.8–26.5 / 15.6–17.8 / 8.5–10.6 / 2.0–3.8 | 2.7 / 13.0 / 26.4 / 27.2 / 18.4 / 10.1 / 2.2 | |
   | Profile against blocks, largest class difference | under 5 | 3.7–4.7 | 4.5–5.5 (over 5 on 5 seeds: the real blocks steeper by 5 points in gentle; hills and knobs bent by the 3D noise and features) | 5.9 | No |
   | Of ground steeper than 1:2, share in the top 30% of local relief | at least 70% | 10.9–11.5% | 20.4–27.0% | 22.2% | No |
   | Top decile's median slope over the bottom 3's | at least 5 | 2.45–2.62 | 0.87–0.99 | 0.91 | No (fell: Issue 7, plateau peaks) |
   | Median slope by decile, 0 to 9 (seed 2198175988018515243) | rising | 0.39 … 1.65 | 0.33 0.40 0.45 0.51 0.49 0.51 0.51 0.57 0.43 0.39 | 0.32 0.38 0.43 0.47 0.44 0.46 0.47 0.54 0.42 0.37 | The top two deciles are the gentle plateau tops |
   | Peak concavity, median (peaks found) | at least 3 | 1.39–1.44 | 1.12–1.21 (4733–5117 peaks) | 1.16 (4659) | No (Issue 7) |
   | Knobs on a base of 250 or more | at least 25% | 12.6–15.2% | 25.4–48.6% (10414–11142 knobs) | 26.8% | **Yes, on all 8** (25.4% and 25.5% on two) |
   | Escarpment length (phase 03's row) | at least 30 km | 0 | 30.2–156.2 km | 103.7 km | **Yes, on all 8** (phase 03: 5 to 30): the tops are calm enough now for the plateau rule to see their rims |
   | Plateau pieces of 25 km² (phase 03's row) | at least 2 | 0 | 0; the largest 0.80–1.18 km² | 1.43 km² | No: the rule wants relief at most 80 within 512 blocks, and a knob of up to 90 on the top breaks it. For phase 06: the plateau rule and knobs on plateaus (Ahmed's point 2) cannot both hold as written |
   | Longest range piece at 500 or more | at least 20 km on 6 of 8 | 3.9–6.0 km | 12.7–39.7 km, 20 or more on 5 of 8 | 13.8 km | No (phase 03: 7 of 8): gentle feet put less of each range above 500 |
   | Sea | 25–30% | 25.7–27.4% | 24.7–27.0% | 27.1% | Yes (24.66% on one) |
   | Top | 1100–1190 | 1166–1181 | 1125–1146 | 1128 | Yes |
   | Profile, µs a lattice column | +15% at most | 0.32–0.37 | 0.44–0.53 (phase 03: 0.38–0.43); still 0.1% of `terrain_generate_box` | 0.47 | No by the letter, at 0.1% of a box's cost |

   `/tp` lines on Ahmed's seed: tallest peak `/tp -1840 -17328` (z 1128); a knob on a base of 250 or more `/tp 12624 -12944` (z 1110); largest plateau piece `/tp -9776 5328` (z 686). `bench_landform` does not print a peak's foot (the Gate asks for it); the y profile through the tallest peak shows its foot at the sea about 2 km south.

9. **Benches**, release, shared machine:
   - **`bench_terrain`** (load average 8.2): slowest **jungle 968** (`/tp -32000 -5120`), then mangrove_swamp 1008; jagged_peaks 1233 (977 in phase 03: `jagged` gone), plateau_top 1366, every other place 1343 or more. A second run at load 13.9: jungle 900, mangrove_swamp 1059, jagged_peaks 1114, plateau_top 1294. **Missed by 3.2% at its best, at the jungle**, a feature-heavy place this land moved (the profile is 0.1% of a box, so the cost is in what stands on the ground).
   - **`bench_lod`**: now mean 20.4, 30.6, 11.7, 10.5, 10.5, 10.7 ms at shifts 1 to 6, worst 33.8 and 46.5 at shifts 1 and 2; the 40 ms budget is met on the means, not on shift 2's worst (its own target, 50, is met).
   - **Floating rock** (`bench_world_map`, two runs of 4, exit 0, 0 columns differing): 931 to 2098 per km² (2198175988018515243 1911, 1 1464, 42 2098, 20261104 1257, 12636146071196537873 1104, 5569068473609503046 1380, 9999999999999 1630, 18446744073709551557 931) against phase 01's 1072 to 1383 (+25%: 1729). **Not met on 2 seeds**, the two with the most land at 300 or more (51% and 56%): it follows the plateau land, whose faces phase 03 made soft (0.5 face) and whose sea pockets have walls hundreds of blocks high; phase 03 did not measure it, so how much is that phase's and how much this one's is not known. This phase's softness change (no `high`) can only have taken overhangs away from lowland.

10. **Pictures, looked at** (`bench_landform`'s, release, in the scratchpad, not committed):
    - **Slope map, Ahmed's seed.** Lowland is green and yellow (gentle and moderate) in a fine mottle, with thin orange-red rings: plateau rims and knob sides. The ranges are no longer dark red from foot to top (phase 01): each shows as a narrow dark-red streak along its crest with orange flanks fading to yellow at the feet.
    - **Profiles through the tallest peak** (1128). Along y it rises from the sea over about 2 km at roughly 6:1, then 3:1, steepening past 1:1 in the last 300 blocks to a jagged crest of summit rock; along x a high ridge at about 1000 with arêtes on it. That is the shape the phase is for. Its far side drops in two steps, a plateau escarpment below the range.
    - **Across the largest plateau's edge.** A level top at about 700 with gentle swells of 30 to 50 (hills and knobs), then a two-step escarpment, tier 2's face and tier 1's, down to the sea.
    - **A knob field on a plateau** (relief map round the printed knob, which is itself a bump on a range crest at 1110): the grey tier-2 plateau beside it carries small closed contour rings, knobs 250 to 500 across, standing on the top as Ahmed asked; the lowland round it has the same.
    - **Along the longest range** (13.8 km): plateau tops at 700 broken by three dome-shaped knobs on lower ground and a sea pocket with walls, as phase 03 noted.

11. **Pins moved, each with this phase's line** (old values in comments beside them): `DENSITY_DIGEST_PIN` 0xE3EBEDF560C5F6E1, `CAVE_DIGEST_PIN` 0x5E10440E68CDB35A (unmoved by phase 03; now the ground moves everywhere through the hills' new cell, and caves reach up to it), `ORE_DIGEST_PIN` 0xF20FF176125D18AE (the same), `BIO_DIGEST_PIN` 0x4986AAD77E3B650B, `BIO_COLUMNS_PIN` 0xEDDCD46AB553A0A2, `HYDRO_DIGEST_PIN` 0x7F7DCC0FBB1885CA, `HYDRO_COLUMNS_PIN` 0x7B19743C198ED0F8, `HYDRO_FLOOD_PIN` 0xFE61DC57722A8EC5, `FEA_FOREST_PIN` 0xB1F782D0CD44F880, `FEA_DIGEST_OF_GENERATOR[1]` 0xFDF54F0635ADEE5A, `CELL_DIGEST_PIN` 0x809BAD24EE9E4DA8, `SYS_DIGEST_PIN` 0x70F3E27238A79D91, the Atlas's 52 goldens and its two plain-terrain hashes (0xCF0CDF4A758F698E, 0xE907F05516EF366F); every one of them is the 1-against-16-workers digest of its test, so the same bytes on 1 and 16 workers. `TERRAIN_GENERATOR_VERSION` stays 1.

12. **Other tests changed with reasons:**
    - `run_terrain_biome`'s bare-peak share: at least 0.3% (was 2%): 0.5 to 0.9% on its three seeds, as only a range's steep top is high.
    - `run_terrain_density`'s ranges: six seeds of eight with a piece at 150 or more 4 km long and 5 km across (was every seed): one seed's longest is 2816 blocks in the 16 km looked at, as a range's gentle feet put less of its belt above 150.
    - `run_audio` and `run_player_interaction` dig at seed 11's start, which is now on the edge of grass and sand: the top was grass, the top beside the hole sand, so the hand held dirt and the sound was grass while the report's last block was sand. They now go by `/tp` to the nearest even ground the land gives (`tests/run/game/fixtures/ground.h`: a 9 by 9 square of level ground with the same top, the block under it dropping the same, no log or leaves, read from the very blocks the test world makes, with 12 blocks of air over it so the box did reach above the ground; the first try lacked that and took a box wholly under a wood for even ground).
    - `run_player_survival`'s run is also clear of trees along every block, read from the blocks: the first dry run went into a wood and the body spent 4 of its 5 saturation jumping against a trunk.
    - `run_playthrough`'s bench test: at least one stack more after the bench is picked up (a sapling or stick of another kind came too).
    - `render_feature_lod_bands`: near-pixel colours exactly the reference's at E 2 (the default), and at most 2 in 10,000 at E 8 (63 of 582,668: 48 looking east, 15 north, all at E 8 and 70 degrees, from plan 009 phase 06's shadow rays by level on this land).
    - `run_save_state_game`'s autosave test captures and settles what opening left until no far column more is built before the away-and-back step it times: on this land a worker was still building then (3 failures of 5; main's land passed with the same test).
    - **Pictures made again, each looked at:** `run_render_frame`'s `start.png` (jungle crowns), `north_low.png` (a level jungle floor with trunks and a pool, where the old was a terraced slope), `down.png` (crowns and water from above); `render_feature_under_water`'s `land.png` (a level jungle floor: no terraces now), `sea.png` and `sea_no_air.png` (the sea bed's hills lower and broader); `render_feature_lod`'s `real_near.png` (crowns over haze) and `real_far.png` (a coast with low plateaus and one peak whose feet are gentle and top sharp, where the old showed a broad grey dome).
    - Timing tests (`run_ui_atlas`'s lock wait, `run_climate_warning`'s 20 ms, `run_terrain_feature`'s 300 s under `-j 4`) failed under load and passed alone.

13. **Verification** (debug, after rebasing onto main at ed5bdbb9): the whole suite ran once (175 tests); its land failures are the ones above, each fixed and rerun alone: `run_audio`, `run_player_interaction`, `run_player_survival`, `run_playthrough`, `run_save_state_game` (4 passes in a row after its fix), `render_feature_lod_bands`, `run_render_frame`, `render_feature_under_water`, `render_feature_lod`, `run_ui_atlas`, `run_climate_warning`, `clang_format`, `standards_lint`, all passing; the terrain, climate and Atlas suites pass. Release: `run_terrain_*` pass but `run_terrain_cave_walk`, which needs the release game built (only the bench and terrain targets were, for the disk); `bench_terrain`, `bench_lod`, `bench_landform` and `bench_world_map` as above. The release build was deleted after the numbers.

**For phase 05 and 06:** `G` is on the lattice beside the uplift (`terrain_landform_lattice_points`); the budget's top is 1170 with 20 blocks spare under 1190; the slope rows of master section 4 need work outside this phase's levers (Issue 7); the plateau-piece rule and knobs on plateaus conflict (Issue 8); floating rock follows the plateau faces (Issue 9); `bench_landform` should print a peak's foot for the Gate.

