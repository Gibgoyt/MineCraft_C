# 007-ahmed-bohemoth-realistic-landforms — phase 4: Slope profiles, and hills on any ground

**Status:** not started   (updated 2026-11-12)

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

- [ ] `landform/rugged` $G$ on the lattice; `relief/knobs`, `landform/peak` as fine fields; limits measured
- [ ] $S_{\text{soft}}$ and $S_{\text{sharp}}$ splines; height line $(1000 - P)\,S_G(m)$
- [ ] `peak` (ridged, cell 48, above $m = 0.6$) replaces `jagged`
- [ ] Knobs with dome and koppie shapes, on any base, not on ranges; hills at cell 384 and at most 30; foothills removed (or restored at 30, recorded)
- [ ] Softness with `face` and $G\,m$
- [ ] Limits test (-116 to 1190) and the estimate-is-the-profile test pass; every moved pin re-pinned with this phase's reason; 1 against 16 workers the same bytes
- [ ] `bench_landform` on 8 seeds: the slope histogram in master section 4's bands
- [ ] Of ground steeper than 1:2, at least 70% in the top 30% of relief or on faces
- [ ] Top-decile-over-bottom-3 slope ratio at least 5
- [ ] Median peak concavity ratio at least 3
- [ ] At least 25% of knobs on bases of 250 or over
- [ ] Floating rock per km² at most phase 01's + 25%
- [ ] `bench_terrain` at least 1000 everywhere (with `plateau_top`); `bench_lod` at most 40 ms
- [ ] Pictures looked at and described here
- [ ] READMEs (`density/`, `landform/`)

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

## Issues found

None.
